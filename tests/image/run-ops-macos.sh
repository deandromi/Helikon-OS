#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Randomized write round-trip on a disposable image.
#
#   ./run-ops-macos.sh [SEEDS] [OPS]
#
# Creates its own 16 MiB journaled HFS+ image under a temporary directory, then
# for each seed runs `hfs_ops_test` (random create/append/truncate/rename/delete
# with the read core as the model oracle), `hfs-check` over the whole volume and
# `fsck_hfs -fn`. Every seed starts from a freshly formatted image, so one
# failure cannot hide behind another.
#
# macOS only: hdiutil and fsck_hfs are the outside oracles. The program itself is
# plain C and runs anywhere; this driver is what makes the result evidence.
# It never accepts an existing image or a device path.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: randomized image round-trip is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
# Two seeds by default so the target stays quick; the soak the harness was
# written for is `./run-ops-macos.sh 8 300`, which today is not green — see
# docs/testing.md §5.3.4 for the open finding it reproduces.
SEEDS=${1:-2}
OPS=${2:-120}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-ops-test.XXXXXX")
IMAGE="$TMP/ops.dmg"
DEV=""

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

make -C "$HERE" hfs_ops_test hfs-check >/dev/null

seed=1
while [ "$seed" -le "$SEEDS" ]; do
    rm -f "$IMAGE"
    if ! hdiutil create -size 16m -fs HFS+J -volname HelikonOps \
            -layout NONE "$IMAGE" >"$TMP/create.log" 2>&1; then
        cat "$TMP/create.log" >&2
        fail "could not create the disposable HFS+ image"
    fi
    printf '== seed %s, %s ops\n' "$seed" "$OPS"
    "$HERE/hfs_ops_test" "$IMAGE" "$seed" "$OPS" ||
        fail "seed $seed failed; reproduce with hfs_ops_test IMAGE $seed $OPS"
    check_structure "$IMAGE"
    check_fsck "$IMAGE"
    seed=$((seed + 1))
done

echo "PASS: $SEEDS seeds x $OPS operations, structural check clean, fsck_hfs clean"
