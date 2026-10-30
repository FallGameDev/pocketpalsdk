#!/bin/bash
# Builds "Pocket Pal Installer.pkg" from this repo (works on Linux or Mac).
#   1. game/pet.html      (python3 src/build.py)
#   2. the Mac app binary (native/build.sh, needs clang + lld 18)
#   3. the .pkg            (this file)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$ROOT/src/build.py"
bash "$ROOT/native/build.sh"
OUT="$ROOT/build"
rm -rf "$OUT" && mkdir -p "$OUT"
cp -R "$ROOT/packaging/app/Pocket Pal.app" "$OUT/"
APP="$OUT/Pocket Pal.app/Contents"
cp "$ROOT/native/PocketPal" "$APP/MacOS/PocketPal"
cp "$ROOT/game/pet.html" "$APP/Resources/pet.html"
# keep the app's version number in step with the game
V=$(python3 -c "import json;print(json.load(open('$ROOT/game/version.json'))['version'])")
sed -i.bak "s#<key>CFBundleShortVersionString</key><string>[^<]*</string>#<key>CFBundleShortVersionString</key><string>$V.0</string>#; s#<key>CFBundleVersion</key><string>[^<]*</string>#<key>CFBundleVersion</key><string>$V</string>#" "$APP/Info.plist" && rm -f "$APP/Info.plist.bak"
sed -i.bak "s#^VERSION = .*#VERSION = \"$V.0\"#" "$ROOT/packaging/build_pkg.py" && rm -f "$ROOT/packaging/build_pkg.py.bak"
python3 "$ROOT/packaging/build_pkg.py" "$OUT/Pocket Pal.app" "$OUT/Pocket Pal Installer.pkg"
