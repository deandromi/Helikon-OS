#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Host integration test for the write-diagnostic core. Creates one outer HFS+
# image containing a disposable inner HFS+ image file, runs the same core as
# the bootloader, and checks the outer metadata and inner image with fsck_hfs.
# macOS only.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: write-diagnostic integration test is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-write-diag.XXXXXX")
OUTER="$TMP/outer.dmg"
INNER="$TMP/inner.dmg"
EXTRACTED="$TMP/extracted.dmg"
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

make -C "$HERE" write_diag_host >/dev/null

hdiutil create -size 32m -fs HFS+J -volname HelikonOuter -layout NONE \
    "$OUTER" >"$TMP/outer.log" 2>&1 ||
    fail "could not create the outer image"
hdiutil create -size 16m -fs HFS+J -volname HelikonInner -layout NONE \
    "$INNER" >"$TMP/inner.log" 2>&1 ||
    fail "could not create the inner image"

mkdir -p "$MOUNT"
hdiutil attach -nobrowse -mountpoint "$MOUNT" "$OUTER" \
    >"$TMP/mount.log" 2>&1 || fail "could not mount the outer image"
MOUNTED=1
cp "$INNER" "$MOUNT/helikon-test.img"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""
fsck_image "$OUTER"

"$HERE/write_diag_host" "$OUTER" ||
    fail "write diagnostic core failed"
fsck_image "$OUTER"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$OUTER" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount the outer image"
MOUNTED=1
cp "$MOUNT/helikon-test.img" "$EXTRACTED"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""
fsck_image "$EXTRACTED"

# The inner image must be reusable: a second run creates and deletes its own
# folder/file, then the image is extracted and checked again.
"$HERE/write_diag_host" "$OUTER" ||
    fail "second write diagnostic run failed"
fsck_image "$OUTER"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$OUTER" \
    >"$TMP/mount.log" 2>&1 || fail "could not remount the outer image"
MOUNTED=1
cp "$MOUNT/helikon-test.img" "$EXTRACTED"
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""
fsck_image "$EXTRACTED"

mkdir -p "$MOUNT"
hdiutil attach -nobrowse -mountpoint "$MOUNT" "$EXTRACTED" \
    >"$TMP/mount.log" 2>&1 || fail "could not mount the inner image"
MOUNTED=1
if [ -e "$MOUNT/diag" ]; then
    hdiutil detach "$MOUNT" >/dev/null 2>&1
    MOUNTED=""
    fail "the diagnostic left its test folder behind"
fi
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

echo "PASS: write diagnostic core, outer metadata and inner image are clean"
