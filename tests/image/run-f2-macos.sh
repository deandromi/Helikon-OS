#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Disposable-image integration test for F2/T-055 bootstate promotion.
#
# Creates its own 16 MiB journaled HFS+ image. The bootstate starts TRYING;
# f2_host promotes it to GOOD through hfs_txn. fsck_hfs is the external oracle
# and the real loader host core must then select GOOD. macOS only.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: F2 bootstate test is macOS only"
    exit 0
fi

if ! python3 -c 'import cryptography' >/dev/null 2>&1; then
    echo "SKIP: Python 'cryptography' is required for the Ed25519 fixture"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-f2.XXXXXX")
IMAGE="$TMP/f2.dmg"
MOUNT="$TMP/mnt"
DEV=""
MOUNTED=""

cleanup() {
    if [ -n "$MOUNTED" ]; then
        hdiutil detach "$MOUNT" >/dev/null 2>&1 || true
    fi
    if [ -n "$DEV" ]; then
        hdiutil detach "$DEV" >/dev/null 2>&1 || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

fsck_image() {
    DEV=$(hdiutil attach -nomount -nobrowse "$1" 2>"$TMP/attach.log" |
          awk 'NR == 1 { print $1 }')
    [ -n "$DEV" ] || {
        cat "$TMP/attach.log" >&2 || true
        fail "could not attach $1"
    }
    if ! OUT=$(fsck_hfs -fn "$DEV" 2>&1); then
        echo "$OUT" >&2
        hdiutil detach "$DEV" >/dev/null 2>&1 || true
        DEV=""
        fail "fsck_hfs reported an error"
    fi
    echo "$OUT" | grep -q "appears to be OK" ||
        fail "fsck_hfs did not report a clean volume"
    if echo "$OUT" | grep -qE '\*\* (Repairing|Rebuilding|Fixing)'; then
        fail "fsck_hfs attempted a repair"
    fi
    hdiutil detach "$DEV" >/dev/null 2>&1
    DEV=""
}

mount_image() {
    mkdir -p "$MOUNT"
    hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
        >"$TMP/mount.log" 2>&1 || fail "could not mount the disposable image"
    MOUNTED=1
}

unmount_image() {
    hdiutil detach "$MOUNT" >/dev/null 2>&1
    MOUNTED=""
}

make -C "$HERE" f2_host loader_host >/dev/null

hdiutil create -size 16m -fs HFS+J -volname HelikonF2 -layout NONE \
    "$IMAGE" >"$TMP/create.log" 2>&1 ||
    fail "could not create the disposable HFS+ image"

mount_image
mkdir "$MOUNT/.helikon"
python3 - "$MOUNT/.helikon" "$TMP/public.txt" <<'PY'
import hashlib
import struct
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

base, public_path = sys.argv[1], sys.argv[2]
image = bytes((i * 5 + 3) & 0xff for i in range(64))
key = Ed25519PrivateKey.generate()
public_key = key.public_key().public_bytes(
    serialization.Encoding.Raw, serialization.PublicFormat.Raw)
open(public_path, "w").write(public_key.hex())

manifest = bytearray(256)
manifest[0:8] = b"HELIKON1"
struct.pack_into("<I", manifest, 8, 1)       # manifest version
struct.pack_into("<I", manifest, 12, 6)      # iPod 6G
struct.pack_into("<I", manifest, 16, 1)      # image version
struct.pack_into("<I", manifest, 20, 0x08000000)
struct.pack_into("<I", manifest, 24, len(image))
struct.pack_into("<I", manifest, 28, 7)      # key ID
manifest[32:64] = hashlib.sha256(image).digest()
struct.pack_into("<I", manifest, 64, 0)      # not development-only
manifest[192:256] = key.sign(bytes(manifest[:192]))

bootstate = bytearray(512)
slot = bootstate[0:256]
slot[0:4] = b"HBST"
struct.pack_into("<I", slot, 4, 1)           # bootstate version
struct.pack_into("<I", slot, 8, 1)           # TRYING sequence
struct.pack_into("<I", slot, 12, 6)          # board
struct.pack_into("<I", slot, 16, 1)          # image version
struct.pack_into("<I", slot, 20, 0x08000000)
struct.pack_into("<I", slot, 24, len(image))
slot[28:60] = hashlib.sha256(image).digest()
slot[60] = 1                                 # TRYING
slot[64:96] = hashlib.sha256(slot[0:64]).digest()
bootstate[0:256] = slot

open(base + "/firmware.img", "wb").write(image)
open(base + "/firmware.manifest", "wb").write(manifest)
open(base + "/bootstate", "wb").write(bootstate)
PY
sync
MANIFEST_BEFORE=$(shasum -a 256 "$MOUNT/.helikon/firmware.manifest" |
                  awk '{ print $1 }')
IMAGE_BEFORE=$(shasum -a 256 "$MOUNT/.helikon/firmware.img" |
               awk '{ print $1 }')
unmount_image
fsck_image "$IMAGE"

OUT=$("$HERE/f2_host" "$IMAGE" promote) ||
    fail "f2_host did not promote the TRYING slot"
echo "$OUT"
echo "$OUT" | grep -q '^promoted sequence 2, readback GOOD$' ||
    fail "unexpected promotion result"
fsck_image "$IMAGE"

OUT=$("$HERE/f2_host" "$IMAGE" promote) ||
    fail "the second promotion run failed"
echo "$OUT"
echo "$OUT" | grep -q '^already GOOD sequence 2$' ||
    fail "the second run was not a no-op"

mount_image
MANIFEST_AFTER=$(shasum -a 256 "$MOUNT/.helikon/firmware.manifest" |
                 awk '{ print $1 }')
IMAGE_AFTER=$(shasum -a 256 "$MOUNT/.helikon/firmware.img" |
              awk '{ print $1 }')
[ "$MANIFEST_BEFORE" = "$MANIFEST_AFTER" ] ||
    fail "the manifest changed during promotion"
[ "$IMAGE_BEFORE" = "$IMAGE_AFTER" ] ||
    fail "the firmware image changed during promotion"
python3 - "$MOUNT/.helikon/bootstate" <<'PY'
import hashlib
import struct
import sys

data = open(sys.argv[1], "rb").read()
if len(data) != 512:
    raise SystemExit("bootstate is not 512 bytes")
slot = data[0:256]
if slot[0:4] != b"HBST" or struct.unpack_from("<I", slot, 4)[0] != 1:
    raise SystemExit("bootstate header invalid")
if slot[60] != 2:
    raise SystemExit("slot state is not GOOD")
if struct.unpack_from("<I", slot, 8)[0] != 2:
    raise SystemExit("sequence is not 2")
if hashlib.sha256(slot[0:64]).digest() != slot[64:96]:
    raise SystemExit("slot checksum invalid")
print("bootstate slot: GOOD sequence 2, checksum valid")
PY
unmount_image
fsck_image "$IMAGE"

PUBLIC=$(cat "$TMP/public.txt")
OUT=$("$HERE/loader_host" "$IMAGE" "$PUBLIC" 7) ||
    fail "the loader rejected the promoted image"
echo "$OUT"
echo "$OUT" | grep -q '^loader: OK$' || fail "loader did not report OK"
echo "$OUT" | grep -q '^bootstate: GOOD$' ||
    fail "loader did not select the GOOD bootstate"

echo "PASS: F2 bootstate promotion, readback, fsck and loader GOOD"
