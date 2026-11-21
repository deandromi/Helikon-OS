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

check_finder_zero() {
    python3 - "$1" <<'PY'
import subprocess
import sys

path = sys.argv[1]
for flag, name in (("-t", "type"), ("-c", "creator")):
    value = subprocess.check_output(["GetFileInfo", flag, path],
                                    text=True).strip()
    if value != '"\\0\\0\\0\\0"':
        raise SystemExit(f"Finder {name} is {value!r}, expected zero")
print("macOS Finder type/creator zero")
PY
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
FILE_MODE=$(stat -f '%Sp' "$MOUNT/Helikon Folder/Helikon Test")
FOLDER_MODE=$(stat -f '%Sp' "$MOUNT/Helikon Folder")
[ "$FILE_MODE" = "-rw-r--r--" ] || fail "file mode is $FILE_MODE"
[ "$FOLDER_MODE" = "drwxr-xr-x" ] || fail "folder mode is $FOLDER_MODE"
FILE_YEAR=$(stat -f '%Sm' -t '%Y' "$MOUNT/Helikon Folder/Helikon Test")
[ "$FILE_YEAR" = "$(date +%Y)" ] || fail "file timestamp year is $FILE_YEAR"
python3 - "$MOUNT/Helikon Folder/Helikon Test" <<'PY'
import sys

data = open(sys.argv[1], "rb").read()
if len(data) != 4096 or any(byte != 0x5A for byte in data):
    raise SystemExit("content mismatch after macOS mount")
print("macOS mount verified: 4096 bytes of 0x5a")
PY
check_finder_zero "$MOUNT/Helikon Folder/Helikon Test"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

"$HERE/hfs_image_test" "$IMAGE" rename
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after rename"
MOUNTED=1
[ -f "$MOUNT/Helikon Folder Renamed/Helikon Test Renamed" ] ||
    fail "macOS did not see the renamed file"
[ ! -e "$MOUNT/Helikon Folder/Helikon Test" ] ||
    fail "old path survived rename"
SIZE=$(stat -f%z "$MOUNT/Helikon Folder Renamed/Helikon Test Renamed")
[ "$SIZE" -eq 4096 ] || fail "renamed file size is $SIZE"
FILE_MODE=$(stat -f '%Sp' "$MOUNT/Helikon Folder Renamed/Helikon Test Renamed")
FOLDER_MODE=$(stat -f '%Sp' "$MOUNT/Helikon Folder Renamed")
[ "$FILE_MODE" = "-rw-r--r--" ] || fail "renamed file mode is $FILE_MODE"
[ "$FOLDER_MODE" = "drwxr-xr-x" ] || fail "renamed folder mode is $FOLDER_MODE"
FILE_YEAR=$(stat -f '%Sm' -t '%Y' \
    "$MOUNT/Helikon Folder Renamed/Helikon Test Renamed")
[ "$FILE_YEAR" = "$(date +%Y)" ] || fail "renamed timestamp year is $FILE_YEAR"
check_finder_zero "$MOUNT/Helikon Folder Renamed/Helikon Test Renamed"
echo "macOS rename verified"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

"$HERE/hfs_image_test" "$IMAGE" truncate
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after truncate"
MOUNTED=1
[ -f "$MOUNT/Helikon Folder Renamed/Helikon Test Renamed" ] ||
    fail "macOS lost the truncated file"
SIZE=$(stat -f%z "$MOUNT/Helikon Folder Renamed/Helikon Test Renamed")
[ "$SIZE" -eq 0 ] || fail "truncated file size is $SIZE"
FILE_YEAR=$(stat -f '%Sm' -t '%Y' \
    "$MOUNT/Helikon Folder Renamed/Helikon Test Renamed")
[ "$FILE_YEAR" = "$(date +%Y)" ] || fail "truncate timestamp year is $FILE_YEAR"
echo "macOS truncate verified"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

"$HERE/hfs_image_test" "$IMAGE" delete
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after delete"
MOUNTED=1
[ ! -e "$MOUNT/Helikon Folder Renamed" ] ||
    fail "renamed folder survived delete"
echo "macOS delete verified"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

"$HERE/hfs_image_test" "$IMAGE" overflow
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after the overflow append"
MOUNTED=1
[ -f "$MOUNT/Overflow File" ] || fail "macOS did not see the overflow file"
SIZE=$(stat -f%z "$MOUNT/Overflow File")
[ "$SIZE" -eq 327680 ] || fail "overflow file size is $SIZE, expected 327680"
python3 "$HERE/check-overflow.py" "$MOUNT/Overflow File"
[ -f "$MOUNT/Overflow Direct" ] || fail "macOS did not see the direct overflow file"
DIRECT=$(stat -f%z "$MOUNT/Overflow Direct")
[ "$DIRECT" -eq 36864 ] || fail "direct overflow size is $DIRECT, expected 36864"
echo "macOS overflow append verified"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

# An in-place overwrite of the same fragmented file: one window straddles the
# inline/overflow boundary, one sits inside the second overflow record.
"$HERE/hfs_image_test" "$IMAGE" overwrite
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after the overwrite"
MOUNTED=1
[ -f "$MOUNT/Overflow File" ] || fail "the overwrite lost the overflow file"
SIZE=$(stat -f%z "$MOUNT/Overflow File")
[ "$SIZE" -eq 327680 ] || fail "overwritten file size is $SIZE, expected 327680"
python3 "$HERE/check-overflow.py" "$MOUNT/Overflow File" overwritten
echo "macOS in-place overwrite verified"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

# A sized truncate of a file that owns an overflow extent: five blocks survive,
# four return to the bitmap and macOS has to see the exact byte count.
"$HERE/hfs_image_test" "$IMAGE" truncate-size
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after the sized truncate"
MOUNTED=1
[ -f "$MOUNT/Overflow Direct" ] || fail "sized truncate lost the direct file"
SIZE=$(stat -f%z "$MOUNT/Overflow Direct")
[ "$SIZE" -eq 16484 ] || fail "truncated size is $SIZE, expected 16484"
python3 - "$MOUNT/Overflow Direct" <<'CHECKSIZE'
import sys

block = 4096
size = 4 * block + 100
data = open(sys.argv[1], "rb").read()
if len(data) != size:
    raise SystemExit(f"truncated size mismatch: {len(data)}")
for index, byte in enumerate(data):
    expected = 0x60 + index // block
    if byte != expected:
        raise SystemExit(f"truncated content mismatch at {index}")
print(f"macOS mount verified: truncated to {size} bytes")
CHECKSIZE
echo "macOS sized truncate verified"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

# A write past the end: the gap it leaves must read back as zeros and the bytes
# must land at the requested offset.
"$HERE/hfs_image_test" "$IMAGE" grow
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after the grow"
MOUNTED=1
[ -f "$MOUNT/Overflow Direct" ] || fail "grow lost the direct file"
SIZE=$(stat -f%z "$MOUNT/Overflow Direct")
[ "$SIZE" -eq 17384 ] || fail "grown size is $SIZE, expected 17384"
python3 - "$MOUNT/Overflow Direct" <<'CHECKGROW'
import sys

block = 4096
truncated = 4 * block + 100
offset = truncated + 400
size = offset + 500
data = open(sys.argv[1], "rb").read()
if len(data) != size:
    raise SystemExit(f"grown size mismatch: {len(data)}")
for index, byte in enumerate(data):
    if index < truncated:
        expected = 0x60 + index // block
    elif index < offset:
        expected = 0
    else:
        expected = 0xA5
    if byte != expected:
        raise SystemExit(f"grown content mismatch at {index}")
print(f"macOS mount verified: {size} bytes with a zero gap and the written range")
CHECKGROW
echo "macOS grow verified"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

"$HERE/hfs_image_test" "$IMAGE" overflow-cleanup
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after overflow cleanup"
MOUNTED=1
[ ! -e "$MOUNT/Overflow File" ] || fail "overflow file survived cleanup"
[ ! -e "$MOUNT/Overflow Direct" ] || fail "direct overflow file survived cleanup"
[ ! -e "$MOUNT/Hole 00" ] || fail "hole file survived cleanup"
echo "macOS overflow cleanup verified"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

"$HERE/hfs_image_test" "$IMAGE" unicode
fsck_image "$IMAGE"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount after Unicode corpus"
MOUNTED=1
python3 - "$MOUNT" <<'PY'
import os
import sys
import unicodedata

expected = {
    "Café",
    "中文",
    "😀",
    "한글",
    "Á",
    "Ω",
    "豈",
    "slash:name",
    "colon:name",
    "a" * 255,
}
actual = {
    unicodedata.normalize("NFC", name)
    for name in os.listdir(sys.argv[1])
    if not name.startswith(".")
}
if actual != expected:
    missing = expected - actual
    extra = actual - expected
    raise SystemExit(f"Unicode corpus mismatch missing={missing!r} extra={extra!r}")
print("macOS Unicode corpus verified")
PY
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

echo "PASS: disposable image create/append/rename/truncate/delete/overflow/overwrite/truncate-to-size/grow/Unicode, fsck_hfs clean"
