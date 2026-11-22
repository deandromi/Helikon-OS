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
CRASHED="$TMP/crashed.dmg"
DOUBLE="$TMP/double.dmg"
DEV=""
MAX_CUTS=${MAX_CUTS:-200}
# How many cut points of each named scenario also get a second fault during the
# replay itself. The double-fault pass costs an attach, a replay and an fsck per
# point, so it is sampled rather than run at every cut.
DOUBLE_CUTS=${DOUBLE_CUTS:-4}
DOUBLE_SCENARIOS=${DOUBLE_SCENARIOS:-create overwrite grow}

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
        overwrite)
            "$HERE/hfs_crash_test" "$PREP" setup create >/dev/null
            "$HERE/hfs_crash_test" "$PREP" setup append >/dev/null
            ;;
        truncate-size)
            "$HERE/hfs_crash_test" "$PREP" setup create >/dev/null
            "$HERE/hfs_crash_test" "$PREP" setup append >/dev/null
            ;;
        grow)
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
    FAILMODE=${2:-write}
    echo "== scenario $SCENARIO ($FAILMODE) =="
    prepare_scenario "$SCENARIO"
    CUT=1
    while [ "$CUT" -le "$MAX_CUTS" ]; do
        cp "$PREP" "$WORK"
        if [ "$FAILMODE" = write ]; then
            CRASH=$("$HERE/hfs_crash_test" "$WORK" crash "$SCENARIO" "$CUT") ||
                fail "write injection at $SCENARIO cut $CUT failed"
        else
            CRASH=$("$HERE/hfs_crash_test" "$WORK" crash "$SCENARIO" "$CUT" "$FAILMODE") ||
                fail "$FAILMODE injection at $SCENARIO cut $CUT failed"
        fi
        echo "$SCENARIO cut $CUT: $CRASH"
        # Keep the crashed image: the double-fault pass replays from this state,
        # not from the already recovered one.
        cp "$WORK" "$CRASHED"

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
        elif [ "$SCENARIO" = grow ]; then
            echo "$RECOVER" |
                grep -qE '^recovered present=1 size=(0|2500)$' ||
                fail "unexpected grow recovery at cut $CUT"
        elif [ "$SCENARIO" = truncate-size ]; then
            echo "$RECOVER" |
                grep -qE '^recovered present=1 size=(2000|4096)$' ||
                fail "unexpected sized-truncate recovery at cut $CUT"
        elif [ "$SCENARIO" = overwrite ]; then
            echo "$RECOVER" |
                grep -qE '^recovered old=[01] new=[01]$' ||
                fail "unexpected overwrite recovery state at cut $CUT"
            echo "$RECOVER" |
                grep -qE 'old=1 new=0|old=0 new=1' ||
                fail "overwrite recovery is neither old nor new at cut $CUT"
        else
            echo "$RECOVER" |
                grep -qE '^recovered present=[01]( size=(0|4096))?$' ||
                fail "unexpected recovery state at $SCENARIO cut $CUT"
        fi
        fsck_image "$WORK"

        # Replay must be idempotent: opening the recovered image again may not
        # change the tree, and the journal must still be clean.
        TWICE=$("$HERE/hfs_crash_test" "$WORK" replay-twice "$SCENARIO") ||
            fail "second recovery at $SCENARIO cut $CUT failed"
        echo "$TWICE" | grep -q '^idempotent=1$' ||
            fail "recovery is not idempotent at $SCENARIO cut $CUT"

        # A second fault, this time during the replay itself. The image is left
        # half-replayed on purpose; the next replay has to converge on one of the
        # two legal states, and fsck has to agree.
        case " $DOUBLE_SCENARIOS " in
        *" $SCENARIO "*)
            if [ "$FAILMODE" = write ] && [ "$CUT" -le "$DOUBLE_CUTS" ]; then
                for RCUT in 1 2; do
                    cp "$CRASHED" "$DOUBLE"
                    "$HERE/hfs_crash_test" "$DOUBLE" replay-cut "$SCENARIO" \
                        "$RCUT" >/dev/null ||
                        fail "replay fault $RCUT at $SCENARIO cut $CUT failed"
                    DOUBLE_STATE=$("$HERE/hfs_crash_test" "$DOUBLE" recover \
                        "$SCENARIO") ||
                        fail "recovery after replay fault $RCUT at $SCENARIO cut $CUT failed"
                    echo "$SCENARIO cut $CUT: replay-fault $RCUT -> $DOUBLE_STATE"
                    fsck_image "$DOUBLE"
                    rm -f "$DOUBLE"
                done
            fi
            ;;
        esac
        rm -f "$CRASHED"

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

# SCENARIOS="create grow" narrows the run to those scenarios; the default is
# every scenario in every failure mode.
run() {
    if [ -n "${SCENARIOS:-}" ] &&
        ! echo " $SCENARIOS " | grep -q " $1 "; then
        return 0
    fi
    run_scenario "$@"
}

run create
run append
run truncate
run delete
run rename
run overwrite
run truncate-size
run grow
run create flush
run append flush
run truncate flush
run delete flush
run rename flush
run overwrite flush
run truncate-size flush
run grow flush
run create partial
run append partial
run truncate partial
run delete partial
run rename partial
run overwrite partial
run truncate-size partial
run grow partial
echo "PASS: create/append/truncate/truncate-to-size/grow/delete/rename/overwrite write, flush and partial matrices are fsck-clean, recovery is idempotent and a fault during replay still converges"
