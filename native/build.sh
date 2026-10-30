#!/bin/bash
# Builds the Pocket Pal Mac app binary (Apple Silicon + Intel) on Linux.
# Uses clang + lld with tiny hand-written .tbd stubs instead of the macOS SDK.
set -e
cd "$(dirname "$0")"
LD=/usr/lib/llvm-18/bin/ld64.lld
for A in arm64 x86_64; do
  clang -target $A-apple-macos11.0 -O2 -fno-stack-protector -fno-builtin -Wall -Wno-unused-parameter -c main.c -o main_$A.o
  # ad-hoc signed so Apple Silicon Macs will run it
  $LD -arch $A -platform_version macos 11.0 14.0 -o PocketPal_$A main_$A.o \
      sdk/libSystem.tbd sdk/libobjc.tbd sdk/Foundation.tbd sdk/AppKit.tbd sdk/WebKit.tbd -adhoc_codesign
done
# glue both into one "universal" binary (same thing Apple's lipo tool does)
python3 - <<'EOF'
import struct
slices = [open(f'PocketPal_{a}', 'rb').read() for a in ('arm64', 'x86_64')]
align = 14                                   # 16 KB page alignment
out, offset, headers = b'', 1 << align, []
for data in slices:
    cputype, subtype = struct.unpack('<ii', data[4:12])
    headers.append(struct.pack('>iiIII', cputype, subtype & 0xffffff, offset, len(data), align))
    offset += (len(data) + (1 << align) - 1) // (1 << align) * (1 << align)
fat = struct.pack('>II', 0xcafebabe, len(slices)) + b''.join(headers)
fat += b'\0' * ((1 << align) - len(fat))
for data in slices:
    fat += data + b'\0' * ((-len(data)) % (1 << align))
open('PocketPal', 'wb').write(fat)
print('universal binary', len(fat), 'bytes')
EOF
chmod +x PocketPal
