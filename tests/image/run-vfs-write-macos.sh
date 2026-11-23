#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The write VFS against a real, macOS-formatted, journaled volume.
#
# Creates its own 16 MiB HFS+J image, lets `vfs_write_host` create, write,
# truncate, mkdir, rename and unlink through `vfs_rw` — every call one journaled
# transaction — then asks the two outside oracles whether the result is a sound
# filesystem:
#
#   1. `hfs-check`, the read core's structural check over the whole volume.
#   2. `fsck_hfs -fn`, Apple's own idea of correct.
#
# and finally mounts the image read-only and confirms macOS sees the same tree
# the firmware says it left behind: a 4096-byte file, a folder, and a renamed
# file with its content.
#
# macOS only: hdiutil, fsck_hfs and mount are the outside oracles. The program is
# plain C and runs anywhere; this driver is what makes the result evidence. It
# never accepts an existing image or a device path, and it never repairs
# anything: a failure is reported, not patched.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: the write VFS image run is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-vfs-write.XXXXXX")
IMAGE="$TMP/vfs-write.dmg"
DEV=""
MNT="$TMP/mnt"

cleanup() {
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

check_structure() {
    OUT=$("$HERE/hfs-check" "$1" 2>&1) || {
        echo "$OUT" >&2
        fail "the structural check found a problem"
    }
    echo "$OUT" | grep -q "  clean$" ||
        fail "the structural check did not report a clean volume"
}

check_fsck() {
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
    echo "$OUT" | grep -q "appears to be OK" || {
        echo "$OUT" >&2
        hdiutil detach "$DEV" >/dev/null 2>&1 || true
        DEV=""
        fail "fsck_hfs did not report a clean volume"
    }
    if echo "$OUT" | grep -qE '\*\* (Repairing|Rebuilding|Fixing)'; then
        echo "$OUT" >&2
        hdiutil detach "$DEV" >/dev/null 2>&1 || true
        DEV=""
        fail "fsck_hfs attempted a repair"
    fi
    hdiutil detach "$DEV" >/dev/null 2>&1
    DEV=""
}

# What macOS has to agree with: the sizes and the content the firmware wrote.
check_macos_view() {
    mkdir -p "$MNT"
    DEV=$(hdiutil attach -readonly -nobrowse -mountpoint "$MNT" "$1" \
              2>"$TMP/mount.log" | awk 'NR == 1 { print $1 }')
    [ -n "$DEV" ] || {
        cat "$TMP/mount.log" >&2 || true
        fail "could not mount $1 read-only"
    }
    [ -f "$MNT/helikon-vfs.txt" ] || fail "macOS does not see /helikon-vfs.txt"
    SIZE=$(stat -f %z "$MNT/helikon-vfs.txt")
    [ "$SIZE" = "4096" ] || fail "/helikon-vfs.txt is $SIZE bytes, not 4096"
    [ -d "$MNT/helikon-dir" ] || fail "macOS does not see /helikon-dir"
    [ -f "$MNT/helikon-dir/moved.txt" ] ||
        fail "macOS does not see /helikon-dir/moved.txt"
    CONTENT=$(cat "$MNT/helikon-dir/moved.txt")
    [ "$CONTENT" = "moved by the write VFS" ] ||
        fail "/helikon-dir/moved.txt holds '$CONTENT'"
    [ -e "$MNT/helikon-dir/remove.txt" ] &&
        fail "/helikon-dir/remove.txt survived the unlink" || true
    [ -e "$MNT/helikon-dir/source.txt" ] &&
        fail "/helikon-dir/source.txt survived the rename" || true
    hdiutil detach "$DEV" >/dev/null 2>&1
    DEV=""
    rmdir "$MNT" 2>/dev/null || true
}

make -C "$HERE" vfs_write_host hfs-check >/dev/null

if ! hdiutil create -size 16m -fs HFS+J -volname HelikonVfsWrite -layout NONE \
        "$IMAGE" >"$TMP/create.log" 2>&1; then
    cat "$TMP/create.log" >&2
    fail "could not create the disposable HFS+ image"
fi

"$HERE/vfs_write_host" "$IMAGE" ||
    fail "the write VFS run failed (the image is in $TMP, but it is removed on exit)"

check_structure "$IMAGE"
check_fsck "$IMAGE"
check_macos_view "$IMAGE"

echo "PASS: write VFS on a journaled macOS volume: structural check clean, fsck_hfs clean, macOS sees the tree"
