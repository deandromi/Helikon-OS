#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Host integration test for the outer-volume write diagnostic. Creates a
# disposable outer HFS+ image, runs the same core as the bootloader, and checks
# the volume with fsck_hfs and macOS. macOS only.
#
# This never runs against a device or a library.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: outer write diagnostic integration test is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-outer-diag.XXXXXX")
IMAGE="$TMP/outer.dmg"
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
    sync
    hdiutil detach "$MOUNT" >/dev/null 2>&1
    MOUNTED=""
}

repair_image() {
    DEV=$(hdiutil attach -nomount -nobrowse "$1" 2>"$TMP/attach.log" |
          awk 'NR == 1 { print $1 }')
    [ -n "$DEV" ] || fail "could not attach $1 for journal replay"
    fsck_hfs -fy "$DEV" >"$TMP/repair.log" 2>&1 ||
        fail "fsck_hfs could not replay the dirty journal"
    hdiutil detach "$DEV" >/dev/null 2>&1
    DEV=""
}

make -C "$HERE" outer_diag_host >/dev/null

hdiutil create -size 32m -fs HFS+J -volname HelikonOuterTest -layout NONE \
    "$IMAGE" >"$TMP/create.log" 2>&1 ||
    fail "could not create the disposable outer image"

mount_image
printf 'library sentinel\n' >"$MOUNT/sentinel.txt"
printf 'HELIKON-OUTER-WRITE-OK\n' >"$MOUNT/helikon-outer-ok.txt"
unmount_image
fsck_image "$IMAGE"

# A committed-but-unreplayed journal is dirty. The diagnostic must refuse it
# without writing; macOS fsck then replays the harmless journal entry.
"$HERE/outer_diag_host" "$IMAGE" dirty >"$TMP/dirty.out" 2>"$TMP/dirty.err" ||
    fail "could not create the dirty-journal fixture"
if "$HERE/outer_diag_host" "$IMAGE" >"$TMP/run0.out" 2>"$TMP/run0.err"; then
    cat "$TMP/run0.out" >&2
    fail "the diagnostic accepted a dirty journal"
fi
grep -q 'outer volume:   FAIL' "$TMP/run0.out" ||
    fail "dirty journal did not fail the volume probe"
grep -q '^writes:         0$' "$TMP/run0.out" ||
    fail "dirty journal caused writes"
repair_image "$IMAGE"
fsck_image "$IMAGE"

# Normal run: the complete sequence must pass and restore root counts.
if ! "$HERE/outer_diag_host" "$IMAGE" >"$TMP/run1.out" 2>"$TMP/run1.err"; then
    cat "$TMP/run1.out" "$TMP/run1.err" >&2
    fail "the outer diagnostic sequence failed"
fi
grep -q 'opt-in:         PASS' "$TMP/run1.out" ||
    fail "opt-in was not accepted"
grep -q 'restored:       PASS' "$TMP/run1.out" ||
    fail "root file/folder counts were not restored"
grep -qE '^writes:         [1-9]' "$TMP/run1.out" ||
    fail "no writes were counted"
fsck_image "$IMAGE"

mount_image
[ ! -e "$MOUNT/helikon-outer-test" ] ||
    fail "the test folder survived a successful run"
[ "$(cat "$MOUNT/sentinel.txt")" = "library sentinel" ] ||
    fail "the sentinel file changed"
[ -f "$MOUNT/helikon-outer-ok.txt" ] ||
    fail "the opt-in file was removed"
unmount_image

# Missing opt-in: fail closed before any write.
mount_image
rm -f "$MOUNT/helikon-outer-ok.txt"
unmount_image
if "$HERE/outer_diag_host" "$IMAGE" >"$TMP/run2.out" 2>"$TMP/run2.err"; then
    cat "$TMP/run2.out" >&2
    fail "the diagnostic ran without the opt-in file"
fi
grep -q 'opt-in:         FAIL' "$TMP/run2.out" ||
    fail "missing opt-in did not fail"
grep -q '^writes:         0$' "$TMP/run2.out" ||
    fail "missing opt-in still wrote to the volume"
fsck_image "$IMAGE"

# A pre-existing folder without the marker is user data and must not be
# deleted. The opt-in file is restored for this check.
mount_image
printf 'HELIKON-OUTER-WRITE-OK\n' >"$MOUNT/helikon-outer-ok.txt"
mkdir "$MOUNT/helikon-outer-test"
printf 'do not delete\n' >"$MOUNT/helikon-outer-test/userdata"
unmount_image
if "$HERE/outer_diag_host" "$IMAGE" >"$TMP/run3.out" 2>"$TMP/run3.err"; then
    cat "$TMP/run3.out" >&2
    fail "the diagnostic accepted an unmarked test folder"
fi
grep -q 'cleanup:        FAIL' "$TMP/run3.out" ||
    fail "unmarked folder did not fail cleanup"
grep -q '^writes:         0$' "$TMP/run3.out" ||
    fail "unmarked folder caused writes"
fsck_image "$IMAGE"

mount_image
[ "$(cat "$MOUNT/helikon-outer-test/userdata")" = "do not delete" ] ||
    fail "the unmarked folder was modified"
rm -rf "$MOUNT/helikon-outer-test"

# Even a marked folder is refused. Cleanup is deliberately non-destructive;
# the operator removes a leftover on macOS after inspecting it. This also
# avoids deleting macOS-created files with extended attributes, which the
# current writer does not manage.
mkdir "$MOUNT/helikon-outer-test"
printf 'HELIKON-OUTER-TEST\n' >"$MOUNT/helikon-outer-test/marker"
dd if=/dev/zero of="$MOUNT/helikon-outer-test/test" bs=4096 count=1 \
    >/dev/null 2>&1
unmount_image
if "$HERE/outer_diag_host" "$IMAGE" >"$TMP/run4.out" 2>"$TMP/run4.err"; then
    cat "$TMP/run4.out" >&2
    fail "the diagnostic deleted a pre-existing marked folder"
fi
grep -q 'cleanup:        FAIL' "$TMP/run4.out" ||
    fail "marked leftover did not fail cleanup"
grep -q '^writes:         0$' "$TMP/run4.out" ||
    fail "marked leftover caused writes"
fsck_image "$IMAGE"

mount_image
[ "$(cat "$MOUNT/helikon-outer-test/marker")" = "HELIKON-OUTER-TEST" ] ||
    fail "the marked folder was modified"
[ "$(stat -f%z "$MOUNT/helikon-outer-test/test")" -eq 4096 ] ||
    fail "the marked test file was modified"
rm -rf "$MOUNT/helikon-outer-test"
unmount_image
fsck_image "$IMAGE"

# A second run proves a clean volume is reusable.
if ! "$HERE/outer_diag_host" "$IMAGE" >"$TMP/run5.out" 2>"$TMP/run5.err"; then
    cat "$TMP/run5.out" "$TMP/run5.err" >&2
    fail "the second outer diagnostic run failed"
fi
grep -q 'restored:       PASS' "$TMP/run5.out" ||
    fail "the second run did not restore root counts"
fsck_image "$IMAGE"

mount_image
[ ! -e "$MOUNT/helikon-outer-test" ] ||
    fail "the test folder survived the second run"
[ "$(cat "$MOUNT/sentinel.txt")" = "library sentinel" ] ||
    fail "the sentinel file changed after the second run"
unmount_image

# A 4 KiB journal header forces whole-sector volume-header and bitmap stages.
# Patch a disposable image's clean journal header, then run the same sequence.
IMAGE4K="$TMP/outer4k.dmg"
hdiutil create -size 32m -fs HFS+J -volname HelikonOuter4K -layout NONE \
    "$IMAGE4K" >"$TMP/create4k.log" 2>&1 ||
    fail "could not create the 4 KiB journal image"
python3 - "$IMAGE4K" <<'PY'
import struct
import sys

path = sys.argv[1]
data = bytearray(open(path, "rb").read())
header = data[1024:1536]
block_size = struct.unpack(">I", header[40:44])[0]
journal_info = struct.unpack(">I", header[12:16])[0] * block_size
journal_offset = struct.unpack(">Q", data[journal_info + 36:
                                        journal_info + 44])[0]
journal = data[journal_offset:journal_offset + 44]
little = journal[4:8] == bytes.fromhex("78563412")
value32 = "<I" if little else ">I"
value64 = "<Q" if little else ">Q"
struct.pack_into(value64, data, journal_offset + 8, 4096)
struct.pack_into(value64, data, journal_offset + 16, 4096)
struct.pack_into(value32, data, journal_offset + 40, 4096)
struct.pack_into(value32, data, journal_offset + 36, 0)
checksum = 0
for byte in data[journal_offset:journal_offset + 44]:
    checksum = ((checksum << 8) & 0xffffffff) ^ ((checksum + byte) & 0xffffffff)
struct.pack_into(value32, data, journal_offset + 36, (~checksum) & 0xffffffff)
open(path, "wb").write(data)
PY
fsck_image "$IMAGE4K"

mkdir -p "$MOUNT"
hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE4K" \
    >"$TMP/mount4k.log" 2>&1 || fail "could not mount the 4 KiB journal image"
MOUNTED=1
printf 'HELIKON-OUTER-WRITE-OK\n' >"$MOUNT/helikon-outer-ok.txt"
printf '4k sentinel\n' >"$MOUNT/sentinel.txt"
unmount_image
fsck_image "$IMAGE4K"

if ! "$HERE/outer_diag_host" "$IMAGE4K" >"$TMP/run4k.out" 2>"$TMP/run4k.err"; then
    cat "$TMP/run4k.out" "$TMP/run4k.err" >&2
    fail "the 4 KiB journal-sector sequence failed"
fi
grep -q 'restored:       PASS' "$TMP/run4k.out" ||
    fail "the 4 KiB journal run did not restore root counts"
fsck_image "$IMAGE4K"

mkdir -p "$MOUNT"
hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE4K" \
    >"$TMP/mount4k.log" 2>&1 || fail "could not remount the 4 KiB journal image"
MOUNTED=1
[ ! -e "$MOUNT/helikon-outer-test" ] ||
    fail "the 4 KiB journal run left the test folder"
[ "$(cat "$MOUNT/sentinel.txt")" = "4k sentinel" ] ||
    fail "the 4 KiB journal run changed the sentinel"
unmount_image

echo "PASS: outer diagnostic, clean journal, 512/4 KiB sectors, opt-in and non-destructive cleanup"
