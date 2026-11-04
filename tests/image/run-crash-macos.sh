#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Journal crash-injection test for the native HFS+ writer.
#
# Creates one clean 16 MiB journaled HFS+ image, prepares a scenario, then for
# each cut point copies it, injects a write failure, reopens the copy, replays
# the journal and runs fsck_hfs. The loop stops when a cut point is beyond the
# transaction's write count (the commit succeeds).
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
PREP="$TMP/prepared.dmg"
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

prepare_scenario() {
    SCENARIO=$1
    cp "$BASE" "$PREP"
    case "$SCENARIO" in
        create)
            ;;
        append)
            "$HERE/hfs_crash_test" "$PREP" setup create >/dev/null
            ;;
        truncate)
            "$HERE/hfs_crash_test" "$PREP" setup create >/dev/null
            "$HERE/hfs_crash_test" "$PREP" setup append >/dev/null
            ;;
        delete)
            "$HERE/hfs_crash_test" "$PREP" setup create >/dev/null
            ;;
        rename)
            "$HERE/hfs_crash_test" "$PREP" setup create >/dev/null
            ;;
        *)
            fail "unknown scenario $SCENARIO"
            ;;
    esac
    fsck_image "$PREP"
}

run_scenario() {
    SCENARIO=$1
    echo "== scenario $SCENARIO =="
    prepare_scenario "$SCENARIO"
    CUT=1
    while [ "$CUT" -le "$MAX_CUTS" ]; do
        cp "$PREP" "$WORK"
        CRASH=$("$HERE/hfs_crash_test" "$WORK" crash "$SCENARIO" "$CUT") ||
            fail "crash injection at $SCENARIO cut $CUT failed"
        echo "$SCENARIO cut $CUT: $CRASH"

        RECOVER=$("$HERE/hfs_crash_test" "$WORK" recover "$SCENARIO") ||
            fail "recovery at $SCENARIO cut $CUT failed"
        echo "$SCENARIO cut $CUT: $RECOVER"
        if [ "$SCENARIO" = rename ]; then
            echo "$RECOVER" |
                grep -qE '^recovered old=[01] new=[01]$' ||
                fail "unexpected rename recovery state at cut $CUT"
            echo "$RECOVER" |
                grep -qE 'old=1 new=0|old=0 new=1' ||
                fail "rename recovery is not exactly one name at cut $CUT"
        else
            echo "$RECOVER" |
                grep -qE '^recovered present=[01]( size=(0|4096))?$' ||
                fail "unexpected recovery state at $SCENARIO cut $CUT"
        fi
        fsck_image "$WORK"

        if echo "$CRASH" | grep -q '^commit-ok'; then
            echo "PASS $SCENARIO: covered $((CUT - 1)) cuts; commit at cut $CUT"
            rm -f "$WORK"
            return 0
        fi

        rm -f "$WORK"
        CUT=$((CUT + 1))
    done
    fail "$SCENARIO commit did not succeed within $MAX_CUTS cuts"
}

run_scenario create
run_scenario append
run_scenario truncate
run_scenario delete
run_scenario rename

echo "PASS: create/append/truncate/delete/rename crash matrices are fsck-clean"
