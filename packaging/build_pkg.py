# =========================================================
# Builds "Pocket Pal Installer.pkg" on any computer (no Mac tools needed).
#
# A Mac .pkg is a "xar" archive. Inside it we put:
#   Distribution          the installer's table of contents
#   PocketPal.pkg/        one component package with:
#       PackageInfo       info about the component
#       Scripts           gzipped cpio archive holding the app + postinstall
# The postinstall script copies Pocket Pal.app into /Applications.
# =========================================================
import gzip, hashlib, io, os, struct, sys, time, zlib
from xml.sax.saxutils import escape

# usage: build_pkg.py <path to Pocket Pal.app> <output .pkg>
APP_DIR = sys.argv[1] if len(sys.argv) > 1 else "app/Pocket Pal.app"
OUT = sys.argv[2] if len(sys.argv) > 2 else "Pocket Pal Installer.pkg"
IDENT = "com.herestudios.pocketpal"
VERSION = "1.0"

POSTINSTALL = r'''#!/bin/bash
# Pocket Pal postinstall: runs as root after you click Install
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="/Applications/Pocket Pal.app"
# close any running copy first (old versions ran through osascript)
/usr/bin/pkill -f "Pocket Pal.app/Contents/Resources/main.js" 2>/dev/null
/usr/bin/pkill -f "Pocket Pal.app/Contents/MacOS/PocketPal" 2>/dev/null
sleep 1
rm -rf "$DEST"
/usr/bin/ditto "$HERE/Pocket Pal.app" "$DEST"
chmod -R a+rX "$DEST"
chmod 755 "$DEST/Contents/MacOS/PocketPal"
/usr/bin/xattr -dr com.apple.quarantine "$DEST" 2>/dev/null
# tell Finder and the Dock the app exists
/usr/bin/touch "$DEST"
# open the app for the person who is logged in
USER_NAME="$(/usr/bin/stat -f%Su /dev/console)"
if [ -n "$USER_NAME" ] && [ "$USER_NAME" != "root" ]; then
  # a fresh install uses the game inside the app; the app re-checks GitHub for anything newer
  HOME_DIR="$(/usr/bin/dscl . -read "/Users/$USER_NAME" NFSHomeDirectory 2>/dev/null | /usr/bin/awk '{print $2}')"
  [ -n "$HOME_DIR" ] && rm -rf "$HOME_DIR/Library/Application Support/PocketPal/game"
  USER_ID="$(/usr/bin/id -u "$USER_NAME")"
  /bin/launchctl asuser "$USER_ID" /usr/bin/sudo -u "$USER_NAME" /usr/bin/open "$DEST" >/dev/null 2>&1 || true
fi
exit 0
'''

# ---------------------------------------------------------
# cpio "odc" writer (the format macOS Installer reads for Scripts)
# ---------------------------------------------------------
def cpio_entry(name, mode, data=b"", ino=1):
    name_b = name.encode() + b"\0"
    hdr = "070707%06o%06o%06o%06o%06o%06o%06o%011o%06o%011o" % (
        0, ino, mode, 0, 0, 1, 0, int(time.time()), len(name_b), len(data))
    return hdr.encode() + name_b + data

def build_scripts():
    entries, ino = [], 1
    def add(name, mode, data=b""):
        nonlocal ino
        entries.append(cpio_entry(name, mode, data, ino)); ino += 1
    add(".", 0o040755)
    add("./postinstall", 0o100755, POSTINSTALL.encode())
    # walk the app bundle; executables keep their run permission
    base = os.path.dirname(APP_DIR)
    for root, dirs, files in os.walk(APP_DIR):
        dirs.sort()
        rel = os.path.relpath(root, base)
        add("./" + rel, 0o040755)
        for f in sorted(files):
            path = os.path.join(root, f)
            mode = 0o100755 if "/MacOS/" in path + "/" and root.endswith("MacOS") else 0o100644
            with open(path, "rb") as fh:
                add("./" + os.path.join(rel, f), mode, fh.read())
    entries.append(cpio_entry("TRAILER!!!", 0, b"", 0))
    raw = b"".join(entries)
    return gzip.compress(raw, 9), raw

# ---------------------------------------------------------
# xar writer
# ---------------------------------------------------------
class Xar:
    def __init__(self):
        self.heap = bytearray(b"\0" * 20)  # first 20 bytes = sha1 of the TOC
        self.next_id = 1

    def file(self, name, data):
        off = len(self.heap); self.heap += data
        sha = hashlib.sha1(data).hexdigest()
        fid = self.next_id; self.next_id += 1
        return (f'<file id="{fid}"><data><length>{len(data)}</length><offset>{off}</offset><size>{len(data)}</size>'
                f'<encoding style="application/octet-stream"/>'
                f'<extracted-checksum style="sha1">{sha}</extracted-checksum>'
                f'<archived-checksum style="sha1">{sha}</archived-checksum></data>'
                f'<mode>0644</mode><uid>0</uid><gid>0</gid><user>root</user><group>wheel</group>'
                f'<type>file</type><name>{escape(name)}</name></file>')

    def folder(self, name, children):
        fid = self.next_id; self.next_id += 1
        return (f'<file id="{fid}"><mode>0755</mode><uid>0</uid><gid>0</gid><user>root</user><group>wheel</group>'
                f'<type>directory</type><name>{escape(name)}</name>{"".join(children)}</file>')

    def write(self, path, files_xml):
        stamp = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime())
        toc = ('<?xml version="1.0" encoding="UTF-8"?>\n<xar><toc>'
               '<checksum style="sha1"><offset>0</offset><size>20</size></checksum>'
               f'<creation-time>{stamp}</creation-time>{"".join(files_xml)}</toc></xar>').encode()
        ztoc = zlib.compress(toc, 9)
        self.heap[0:20] = hashlib.sha1(ztoc).digest()
        header = struct.pack(">IHHQQI", 0x78617221, 28, 1, len(ztoc), len(toc), 1)
        with open(path, "wb") as f:
            f.write(header + ztoc + bytes(self.heap))

# ---------------------------------------------------------
# build it
# ---------------------------------------------------------
scripts_gz, scripts_raw = build_scripts()
kb = max(1, len(scripts_raw) // 1024)

package_info = f'''<?xml version="1.0" encoding="utf-8"?>
<pkg-info format-version="2" identifier="{IDENT}" version="{VERSION}" install-location="/" auth="root" postinstall-action="none">
  <payload numberOfFiles="0" installKBytes="0"/>
  <scripts><postinstall file="./postinstall"/></scripts>
</pkg-info>
'''.encode()

distribution = f'''<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Pocket Pal</title>
  <organization>com.herestudios</organization>
  <options customize="never" require-scripts="true" hostArchitectures="x86_64,arm64"/>
  <domains enable_localSystem="true"/>
  <os-version min="11.0"/>
  <choices-outline>
    <line choice="default"><line choice="{IDENT}"/></line>
  </choices-outline>
  <choice id="default"/>
  <choice id="{IDENT}" visible="false"><pkg-ref id="{IDENT}"/></choice>
  <pkg-ref id="{IDENT}" version="{VERSION}" onConclusion="none" installKBytes="{kb}">#PocketPal.pkg</pkg-ref>
</installer-gui-script>
'''.encode()

x = Xar()
files = [
    x.file("Distribution", distribution),
    x.folder("PocketPal.pkg", [
        x.file("PackageInfo", package_info),
        x.file("Scripts", scripts_gz),
    ]),
]
x.write(OUT, files)
print(f"built {OUT}: {os.path.getsize(OUT)} bytes, scripts {len(scripts_raw)} bytes raw")
