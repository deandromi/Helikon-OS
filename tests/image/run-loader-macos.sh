#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Host integration test for the read-only HFS+ firmware loader. Creates a
# disposable HFS+ image, writes a signed-manifest fixture through macOS, then
# verifies it through the HFS+ read core. macOS only.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: loader integration test is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-loader.XXXXXX")
IMAGE="$TMP/loader.dmg"
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

make -C "$HERE" loader_host >/dev/null

hdiutil create -size 16m -fs HFS+J -volname HelikonLoader -layout NONE \
    "$IMAGE" >"$TMP/create.log" 2>&1 ||
    fail "could not create the disposable HFS+ image"

mkdir -p "$MOUNT"
hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not mount the disposable image"
MOUNTED=1
mkdir "$MOUNT/.helikon"
python3 - "$MOUNT/.helikon" <<'PY'
import hashlib
import struct
import sys

base = sys.argv[1]
image = bytes((i * 3 + 1) & 0xff for i in range(64))

manifest = bytearray(256)
manifest[0:8] = b"HELIKON1"
struct.pack_into("<I", manifest, 8, 1)       # manifest version
struct.pack_into("<I", manifest, 12, 6)      # iPod 6G
struct.pack_into("<I", manifest, 16, 1)      # image version
struct.pack_into("<I", manifest, 20, 0x08000000)
struct.pack_into("<I", manifest, 24, len(image))
struct.pack_into("<I", manifest, 28, 7)      # key ID
manifest[32:64] = hashlib.sha256(image).digest()
manifest[192:256] = b"\xa5" * 64

bootstate = bytearray(512)
slot = bootstate
slot[0:4] = b"HBST"
struct.pack_into("<I", slot, 4, 1)           # bootstate version
struct.pack_into("<I", slot, 8, 1)           # sequence
struct.pack_into("<I", slot, 12, 6)          # board
struct.pack_into("<I", slot, 16, 1)          # image version
struct.pack_into("<I", slot, 20, 0x08000000)
struct.pack_into("<I", slot, 24, len(image))
slot[28:60] = hashlib.sha256(image).digest()
slot[60] = 2                                 # GOOD
slot[64:96] = hashlib.sha256(slot[0:64]).digest()

open(base + "/firmware.img", "wb").write(image)
open(base + "/firmware.manifest", "wb").write(manifest)
open(base + "/bootstate", "wb").write(bootstate)
PY
sync
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""
fsck_image "$IMAGE"

OUT=$("$HERE/loader_host" "$IMAGE") ||
    fail "the loader rejected a valid fixture"
echo "$OUT"
echo "$OUT" | grep -q '^loader: OK$' || fail "loader did not report OK"
echo "$OUT" | grep -q '^bootstate: GOOD$' ||
    fail "loader did not select the GOOD bootstate"

echo "PASS: HFS+ firmware loader reads manifest/image/bootstate"
