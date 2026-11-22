#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Host test for tools/disk-mode-format.sh: the >128 GiB format path, without a
# device.
#
# It creates a sparse disk image larger than 128 GiB, attaches it, and runs the
# real runbook against it. Then it checks that the result is an Apple Partition
# Map with one HFS+ volume that actually fills the disk, that fsck_hfs agrees,
# and that every guard refuses what it is supposed to refuse.
#
# This proves the host half of "format the iPod above 128 GiB": the tool, the
# partition map and the filesystem. It proves nothing about the 6G, its USB
# stack or the Disk Mode image, because there is no iPod in this loop.
#
# macOS only, and it writes only inside its own temporary directory.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: the format test is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
FORMAT="$ROOT/tools/disk-mode-format.sh"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-format-test.XXXXXX")
IMAGE="$TMP/disk.sparseimage"
BACKUP="$TMP/backup.txt"

# 140 GiB, so the volume has to cross the 128 GiB line that the whole exercise
# is about.
SIZE=${SIZE:-140g}

DEV=""
DEV2=""

cleanup() {
    for disk in "$DEV" "$DEV2"; do
        [ -n "$disk" ] && hdiutil detach "$disk" >/dev/null 2>&1 || true
    done
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

attach_image() {
    OUT=$(hdiutil attach -nomount "$IMAGE" 2>"$TMP/attach.log") ||
        fail "could not attach $IMAGE"
    printf '%s\n' "$OUT" | awk 'NR == 1 { print $1 }'
}

echo "== a blank ${SIZE} image =="
hdiutil create -size "$SIZE" -type SPARSE "$IMAGE" >/dev/null 2>&1 ||
    fail "could not create the image"
printf 'verified backup record for the format test\n' > "$BACKUP"

DEV=$(attach_image)
[ -n "$DEV" ] || fail "no device for the image"
echo "attached $DEV"

echo "== the guards refuse =="
if "$FORMAT" --device "${DEV}s1" --backup "$BACKUP" --yes >/dev/null 2>&1; then
    fail "a slice was accepted as --device"
fi
if "$FORMAT" --device "$DEV" --yes >/dev/null 2>&1; then
    fail "the script ran without --backup"
fi
: > "$TMP/empty.txt"
if "$FORMAT" --device "$DEV" --backup "$TMP/empty.txt" --yes >/dev/null 2>&1; then
    fail "the script ran with an empty backup record"
fi
if "$FORMAT" --device /dev/does-not-exist --backup "$BACKUP" --yes \
    >/dev/null 2>&1; then
    fail "the script ran on a path that is not a block device"
fi

echo "== --dry-run changes nothing =="
"$FORMAT" --device "$DEV" --backup "$BACKUP" --yes --dry-run >"$TMP/dry.log" 2>&1 ||
    fail "the dry run failed"
grep -q '^+ ' "$TMP/dry.log" || fail "the dry run printed no commands"
BEFORE=$(diskutil list "$DEV" | grep -c 'Apple_HFS' || true)
[ "$BEFORE" -eq 0 ] || fail "the dry run already wrote an HFS+ volume"
hdiutil detach "$DEV" >/dev/null 2>&1
DEV=""

echo "== the real format =="
DEV=$(attach_image)
"$FORMAT" --device "$DEV" --backup "$BACKUP" --yes >"$TMP/format.log" 2>&1 ||
    fail "the format run failed"
grep -q 'Apple_partition_scheme' "$TMP/format.log" ||
    fail "no Apple partition map in the output"
grep -q 'Apple_HFS' "$TMP/format.log" || fail "no HFS+ volume in the output"
grep -q 'eject' "$TMP/format.log" || fail "the script did not eject"
DEV=""

echo "== what it wrote =="
DEV2=$(attach_image)
LIST=$(diskutil list "$DEV2")
printf '%s\n' "$LIST"
VOL=$(printf '%s\n' "$LIST" | awk '/Apple_HFS/ { print $NF; exit }')
[ -n "$VOL" ] || fail "no HFS+ volume on the formatted disk"
SLICE=$(printf '%s\n' "$LIST" |
    awk '/Apple_HFS/ { for (i = 1; i <= NF; i++) if ($i ~ /^disk[0-9]+s[0-9]+$/) print $i }')
[ -n "$SLICE" ] || fail "no slice for the HFS+ volume"

# 128 GiB is 137,438,953,472 bytes. diskutil prints the size twice; the
# parenthesised form is the byte count, which is the one worth comparing.
BYTES=$(diskutil info "/dev/$SLICE" |
    awk -F: '/Disk Size/ { print $2 }' |
    awk -F'(' 'NF > 1 { print $2 }' |
    awk '{ print $1 }')
echo "volume /dev/$SLICE is $BYTES bytes"
[ -n "$BYTES" ] || fail "cannot read the volume size"
[ "$BYTES" -gt 137438953472 ] ||
    fail "the volume is not larger than 128 GiB ($BYTES bytes)"

echo "== fsck_hfs =="
OUT=$(fsck_hfs -fn "/dev/$SLICE" 2>&1) || fail "fsck_hfs reported an error"
printf '%s\n' "$OUT" | grep -q 'OK' || fail "fsck_hfs did not report a clean volume"
printf '%s\n' "$OUT" | grep -qi 'repair' && fail "fsck_hfs would repair something"
echo "$OUT"

# The volume is deliberately not mounted: macOS writes .fseventsd and .Trashes
# on a first mount, and this test would then be describing a volume it changed.
printf '%s\n' "$OUT" | grep -q 'The volume name is' ||
    fail "fsck_hfs did not name the volume"

echo "PASS: a blank ${SIZE} disk formats to one APM HFS+ volume above 128 GiB, fsck-clean"
