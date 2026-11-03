#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Disposable-image integration test for the native HFS+ writer.
#
# This script creates its own 16 MiB journaled HFS+ image under a temporary
# directory. It never accepts an existing image or device path. It is macOS
# only because it uses hdiutil and fsck_hfs as the external oracle.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: disposable-image test is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-image-test.XXXXXX")
IMAGE="$TMP/helikon-test.dmg"
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
    echo "$OUT"
    echo "$OUT" | grep -q "appears to be OK" ||
        fail "fsck_hfs did not report a clean volume"
    if echo "$OUT" | grep -qE '\*\* (Repairing|Rebuilding|Fixing)'; then
        fail "fsck_hfs attempted a repair"
    fi
    hdiutil detach "$DEV" >/dev/null 2>&1
    DEV=""
}

make -C "$HERE" hfs_image_test >/dev/null

if ! hdiutil create -size 16m -fs HFS+J -volname HelikonTest -layout NONE \
        "$IMAGE" >"$TMP/create.log" 2>&1; then
    cat "$TMP/create.log" >&2
    fail "could not create the disposable HFS+ image"
fi
echo "created disposable 16 MiB journaled HFS+ image"

"$HERE/hfs_image_test" "$IMAGE" create
fsck_image "$IMAGE"

mkdir -p "$MOUNT"
if ! hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
        >"$TMP/mount.log" 2>&1; then
    cat "$TMP/mount.log" >&2
    fail "could not mount the disposable image"
fi
MOUNTED=1
[ -f "$MOUNT/Helikon Folder/Helikon Test" ] || fail "macOS did not see the created file"
SIZE=$(stat -f%z "$MOUNT/Helikon Folder/Helikon Test")
[ "$SIZE" -eq 4096 ] || fail "macOS saw size $SIZE, expected 4096"
python3 - "$MOUNT/Helikon Folder/Helikon Test" <<'PY'
import sys

data = open(sys.argv[1], "rb").read()
if len(data) != 4096 or any(byte != 0x5A for byte in data):
    raise SystemExit("content mismatch after macOS mount")
print("macOS mount verified: 4096 bytes of 0x5a")
PY
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

"$HERE/hfs_image_test" "$IMAGE" delete
fsck_image "$IMAGE"

echo "PASS: disposable image create/append/delete, fsck_hfs clean"
