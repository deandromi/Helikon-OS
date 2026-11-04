#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build the Helikon-OS read-only firmware-loading bootloader test image for the
# iPod Classic 6G.
#
# This is a DFU-only, development test image. It verifies a signed manifest and
# image, selects a boot-state slot, and executes a tiny signed Thumb payload
# that returns a marker. Production firmware handoff is disabled. It never
# writes to the disk.
#
# The test signing key is generated once under build/ and is never committed.
# Rebuilding reuses it, so the signed manifest on the device stays valid.
#
# Usage:
#   ./tools/build-loader-bootloader.sh
#   DONOR_DIR=/path/to/hfsplus-rockboxed-ipod ./tools/build-loader-bootloader.sh
#
# The donor checkout is read-only: the build uses `git archive` at the pinned
# commit, so local donor changes are never used.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DONOR_URL=${DONOR_URL:-https://github.com/deandromi/hfsplus-rockboxed-ipod.git}
DONOR_COMMIT=${DONOR_COMMIT:-55603bd4bc2524ded900fa17feae99a223b04349}
BUILD_DIR=${BUILD_DIR:-"$ROOT/build/loader-bootloader"}
SRC_DIR="$BUILD_DIR/src"
OBJ_DIR="$SRC_DIR/build"
OUT_DIR="$BUILD_DIR/out"
KEY_FILE=${HELIKON_TEST_KEY:-"$BUILD_DIR/test-key.pem"}
IMAGE="$OUT_DIR/bootloader-ipod6g-loader-test.ipod"
TEST_KEY_ID=0x48454c01

JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}

if ! python3 -c 'import cryptography' >/dev/null 2>&1; then
    echo "error: Python 'cryptography' is required for Ed25519 signing" >&2
    exit 1
fi

if ! command -v arm-elf-eabi-gcc >/dev/null 2>&1; then
    if [ -x "$HOME/.rockbox-toolchain/bin/arm-elf-eabi-gcc" ]; then
        PATH="$HOME/.rockbox-toolchain/bin:$PATH"
        export PATH
    else
        echo "error: arm-elf-eabi-gcc 9.5.0 (Rockbox toolchain) not found" >&2
        echo "       expected at \$HOME/.rockbox-toolchain/bin or on PATH" >&2
        exit 1
    fi
fi

if [ -n "${DONOR_DIR:-}" ]; then
    DONOR=$DONOR_DIR
else
    DONOR="$BUILD_DIR/donor"
    if [ ! -d "$DONOR/.git" ]; then
        echo "Cloning donor into $DONOR ..."
        mkdir -p "$BUILD_DIR"
        git clone "$DONOR_URL" "$DONOR"
    fi
fi

if ! git -C "$DONOR" cat-file -e "$DONOR_COMMIT^{commit}" 2>/dev/null; then
    echo "error: donor commit $DONOR_COMMIT is not present in $DONOR" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"
rm -rf "$SRC_DIR" "$OBJ_DIR" "$OUT_DIR"
mkdir -p "$SRC_DIR" "$OBJ_DIR" "$OUT_DIR"

echo "Preparing the test key, payload and signed manifest ..."
python3 - "$KEY_FILE" "$OUT_DIR" "$TEST_KEY_ID" <<'PY'
import hashlib
import os
import struct
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

key_path, out_dir, key_id_text = sys.argv[1], sys.argv[2], sys.argv[3]
key_id = int(key_id_text, 0)

if os.path.exists(key_path):
    with open(key_path, "rb") as handle:
        key = serialization.load_pem_private_key(handle.read(), password=None)
else:
    key = Ed25519PrivateKey.generate()
    os.makedirs(os.path.dirname(os.path.abspath(key_path)), exist_ok=True)
    pem = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )
    with open(key_path, "wb") as handle:
        handle.write(pem)
    os.chmod(key_path, 0o600)

public_key = key.public_key().public_bytes(
    serialization.Encoding.Raw,
    serialization.PublicFormat.Raw,
)

# ARM payload loaded at 0x08000000:
#   e59f0000  ldr r0, [pc, #0]   ; literal at 0x08000008
#   e12fff1e  bx lr
#   4f4c4548  "HELO" little-endian
payload = bytes.fromhex("00009fe51eff2fe148454c4f")

manifest = bytearray(256)
manifest[0:8] = b"HELIKON1"
struct.pack_into("<I", manifest, 8, 1)       # manifest version
struct.pack_into("<I", manifest, 12, 6)      # iPod Classic 6G
struct.pack_into("<I", manifest, 16, 1)      # test image version
struct.pack_into("<I", manifest, 20, 0x08000000)
struct.pack_into("<I", manifest, 24, len(payload))
struct.pack_into("<I", manifest, 28, key_id)
manifest[32:64] = hashlib.sha256(payload).digest()
struct.pack_into("<I", manifest, 64, 1)      # development-only
manifest[192:256] = key.sign(bytes(manifest[:192]))

# A matching GOOD boot-state slot, so the loader exercises the selector.
bootstate = bytearray(512)
slot = bootstate
slot[0:4] = b"HBST"
struct.pack_into("<I", slot, 4, 1)           # boot-state version
struct.pack_into("<I", slot, 8, 1)           # sequence
struct.pack_into("<I", slot, 12, 6)          # board
struct.pack_into("<I", slot, 16, 1)          # image version
struct.pack_into("<I", slot, 20, 0x08000000)
struct.pack_into("<I", slot, 24, len(payload))
slot[28:60] = hashlib.sha256(payload).digest()
slot[60] = 2                                 # GOOD
slot[64:96] = hashlib.sha256(slot[:64]).digest()

key.public_key().verify(bytes(manifest[192:]), bytes(manifest[:192]))
assert hashlib.sha256(payload).digest() == manifest[32:64]
assert slot[0:4] == b"HBST"
assert slot[60] == 2

with open(os.path.join(out_dir, "firmware.img"), "wb") as handle:
    handle.write(payload)
with open(os.path.join(out_dir, "firmware.manifest"), "wb") as handle:
    handle.write(manifest)
with open(os.path.join(out_dir, "bootstate"), "wb") as handle:
    handle.write(bootstate)
with open(os.path.join(out_dir, "test-public-key.txt"), "w") as handle:
    handle.write("key id: 0x%08x\npublic key: %s\n"
                 % (key_id, public_key.hex()))

print("test key id: 0x%08x" % key_id)
print("public key:  %s" % public_key.hex())
print("payload:     %d bytes, sha256 %s"
      % (len(payload), hashlib.sha256(payload).hexdigest()))
PY

echo "Exporting donor $DONOR_COMMIT ..."
git -C "$DONOR" archive "$DONOR_COMMIT" | tar -x -C "$SRC_DIR"

HELIKON_DIR="$SRC_DIR/bootloader/helikon"
mkdir -p "$HELIKON_DIR"
cp "$ROOT/bootloader/sha256.c" "$ROOT/bootloader/sha256.h" \
   "$ROOT/bootloader/manifest.c" "$ROOT/bootloader/manifest.h" \
   "$ROOT/bootloader/bootstate.c" "$ROOT/bootloader/bootstate.h" \
   "$ROOT/bootloader/firmware_loader.c" "$ROOT/bootloader/firmware_loader.h" \
   "$ROOT/bootloader/hfs_loader.c" "$ROOT/bootloader/hfs_loader.h" \
   "$ROOT/bootloader/loader_error.c" "$ROOT/bootloader/loader_error.h" \
   "$ROOT/bootloader/ed25519.c" "$ROOT/bootloader/ed25519.h" \
   "$ROOT/bootloader/keys.c" "$ROOT/bootloader/keys.h" \
   "$ROOT/bootloader/splash.c" "$ROOT/bootloader/splash.h" \
   "$ROOT/bootloader/hardware/helikon_loader_hw.c" \
   "$ROOT/bootloader/hardware/helikon_build_keys.h" "$HELIKON_DIR/"
cp "$ROOT"/firmware/hfs/hfs_*.c "$ROOT"/firmware/hfs/hfs_*.h "$HELIKON_DIR/"

python3 - "$OUT_DIR/test-public-key.txt" "$HELIKON_DIR/helikon_build_keys.c" <<'PY'
import sys

key_text, output = sys.argv[1], sys.argv[2]
values = dict(line.split(": ", 1) for line in
              open(key_text, encoding="utf-8").read().splitlines())
key_id = int(values["key id"], 0)
public_key = bytes.fromhex(values["public key"])
assert len(public_key) == 32
body = ",\n        ".join("0x%02x" % byte for byte in public_key)
with open(output, "w", encoding="utf-8") as handle:
    handle.write(
        '/* SPDX-License-Identifier: GPL-2.0-or-later */\n'
        '/* Generated test key table. The private key is not in this file. */\n'
        '#include "keys.h"\n\n'
        'static const struct helikon_public_key helikon_build_keys[] = {\n'
        '    { 0x%08xu, { %s } },\n'
        '};\n\n'
        'const struct helikon_key_table helikon_build_key_table = {\n'
        '    helikon_build_keys, 1\n'
        '};\n' % (key_id, body)
    )
PY

# The Rockbox bootloader formatter supports %lu but not long-long.
if grep -n '%ll' "$ROOT/bootloader/hardware/helikon_loader_hw.c"; then
    echo "error: bootloader formatter does not support long-long formatting" >&2
    exit 1
fi

echo "Applying the donor-side integration patch ..."
(
    cd "$SRC_DIR"
    patch -p1 -N < "$ROOT/bootloader/hardware/donor-loader.patch"
)

echo "Configuring the Thumb bootloader build ..."
cd "$OBJ_DIR"
../tools/configure --target=ipod6g --type=b --thumb

echo "Building with $JOBS jobs ..."
make -j"$JOBS"

MAP="$OBJ_DIR/bootloader.map"
python3 - "$MAP" <<'PY'
import re
import sys

text = open(sys.argv[1], encoding="utf-8").read()
move_area = re.search(r"^MOVE_AREA\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)",
                      text, re.MULTILINE)
move_end = re.search(r"^\s+(0x[0-9a-fA-F]+)\s+_moveend\s*=",
                     text, re.MULTILINE)
if move_area is None or move_end is None:
    print("error: MOVE_AREA or _moveend not found in bootloader.map",
          file=sys.stderr)
    sys.exit(1)

origin = int(move_area.group(1), 16)
length = int(move_area.group(2), 16)
end = origin + length
moveend = int(move_end.group(1), 16)
headroom = end - moveend
print(f"MOVE_AREA: 0x{origin:08x}..0x{end:08x} (0x{length:x} bytes)")
print(f"_moveend:  0x{moveend:08x}")
print(f"headroom:  {headroom} bytes")
if moveend > end:
    print("error: bootloader exceeds MOVE_AREA", file=sys.stderr)
    sys.exit(1)
PY

if ! strings -a "$OBJ_DIR/bootloader-ipod6g.ipod" |
        grep -q 'HELIKON-OS LOADER TEST'; then
    echo "error: built image does not contain the loader test marker" >&2
    exit 1
fi

cp "$OBJ_DIR/bootloader-ipod6g.ipod" "$IMAGE"

python3 - "$OUT_DIR" <<'PY'
import hashlib
import os
import sys

out_dir = sys.argv[1]
names = [
    "bootloader-ipod6g-loader-test.ipod",
    "firmware.img",
    "firmware.manifest",
    "bootstate",
]
lines = []
for name in names:
    with open(os.path.join(out_dir, name), "rb") as handle:
        lines.append("%s  %s" % (hashlib.sha256(handle.read()).hexdigest(),
                                 name))
with open(os.path.join(out_dir, "SHA256SUMS"), "w",
          encoding="utf-8") as handle:
    handle.write("\n".join(lines) + "\n")
PY
HASH=$(awk 'NR == 1 { print $1 }' "$OUT_DIR/SHA256SUMS")

echo
echo "image:       $IMAGE"
echo "size:        $(wc -c < "$IMAGE" | tr -d ' ') bytes"
echo "sha256:      $HASH"
echo "test key:    $KEY_FILE (not committed)"
echo "device files: $OUT_DIR/firmware.img"
echo "              $OUT_DIR/firmware.manifest"
echo "              $OUT_DIR/bootstate"
echo
echo "Copy the three device files to /.helikon/ on the iPod, then install"
echo "the image through DFU only. See bootloader/LOADER_TEST.md."
echo

if [ "$(uname -s)" = "Darwin" ]; then
    echo "Checking the generated manifest/image/bootstate through the HFS+ core ..."
    "$ROOT/tools/check-loader-artifact.sh" "$OUT_DIR"
fi

if [ "$(uname -s)" = "Darwin" ] && command -v clang >/dev/null 2>&1; then
    echo "Validating the image with mks5lboot ..."
    make -C "$SRC_DIR/utils/mks5lboot" -j"$JOBS" CC=clang \
        LDOPTS="-framework IOKit -framework CoreFoundation" >/dev/null
    "$SRC_DIR/utils/mks5lboot/mks5lboot" --mkdfu-inst "$IMAGE" \
        "$OUT_DIR/loader-test.dfu" | tail -12
fi
