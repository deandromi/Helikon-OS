#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Journal crash-injection test for the native HFS+ writer.
#
# Creates one clean 16 MiB journaled HFS+ image, then for each cut point copies
# it, injects a write failure during a create transaction, reopens the copy,
# replays the journal and runs fsck_hfs. The loop stops when a cut point is
# beyond the transaction's write count (the commit succeeds).
#
# The script accepts no image or device argument and writes only its own
# temporary copies. macOS only.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: crash-injection test is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-crash-test.XXXXXX")
BASE="$TMP/base.dmg"
WORK="$TMP/work.dmg"
DEV=""
MAX_CUTS=${MAX_CUTS:-200}

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

make -C "$HERE" hfs_crash_test >/dev/null

if ! hdiutil create -size 16m -fs HFS+J -volname HelikonCrash -layout NONE \
        "$BASE" >"$TMP/create.log" 2>&1; then
    cat "$TMP/create.log" >&2
    fail "could not create the base HFS+ image"
fi
fsck_image "$BASE"
echo "base image is clean"

CUT=1
while [ "$CUT" -le "$MAX_CUTS" ]; do
    cp "$BASE" "$WORK"
    CRASH=$("$HERE/hfs_crash_test" "$WORK" crash "$CUT") ||
        fail "crash injection at cut $CUT failed"
    echo "cut $CUT: $CRASH"

    RECOVER=$("$HERE/hfs_crash_test" "$WORK" recover) ||
        fail "recovery at cut $CUT failed"
    echo "cut $CUT: $RECOVER"
    echo "$RECOVER" | grep -qE '^recovered present=[01]( size=0)?$' ||
        fail "unexpected recovery state at cut $CUT"
    fsck_image "$WORK"

    if echo "$CRASH" | grep -q '^commit-ok'; then
        echo "PASS: crash injection covered $((CUT - 1)) cuts; commit succeeded at cut $CUT"
        rm -f "$WORK"
        exit 0
    fi

    rm -f "$WORK"
    CUT=$((CUT + 1))
done

fail "commit did not succeed within $MAX_CUTS cuts; increase MAX_CUTS"
