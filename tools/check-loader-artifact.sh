#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Verify the exact loader-test artifact files through the host HFS+ read core.
# Creates a disposable HFS+ image, installs the generated manifest/image/boot
# state, and runs tests/image/loader_host with the generated public key.
# macOS only. Never touches a real device.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: loader artifact check is macOS only"
    exit 0
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR=${1:-"$ROOT/build/loader-bootloader/out"}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-loader-artifact.XXXXXX")
IMAGE="$TMP/artifact.dmg"
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

for name in firmware.img firmware.manifest bootstate test-public-key.txt; do
    [ -f "$OUT_DIR/$name" ] || fail "missing $OUT_DIR/$name"
done

make -C "$ROOT/tests/image" loader_host >/dev/null

KEY_ID=$(sed -n 's/^key id: //p' "$OUT_DIR/test-public-key.txt")
PUBLIC=$(sed -n 's/^public key: //p' "$OUT_DIR/test-public-key.txt")
[ -n "$KEY_ID" ] && [ -n "$PUBLIC" ] || fail "cannot parse the test public key"

hdiutil create -size 16m -fs HFS+J -volname HelikonLoaderArtifact \
    -layout NONE "$IMAGE" >"$TMP/create.log" 2>&1 ||
    fail "could not create the disposable HFS+ image"

mkdir -p "$MOUNT"
hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not mount the disposable image"
MOUNTED=1
mkdir "$MOUNT/.helikon"
cp "$OUT_DIR/firmware.img" "$OUT_DIR/firmware.manifest" \
   "$OUT_DIR/bootstate" "$MOUNT/.helikon/"
sync
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""

DEV=$(hdiutil attach -nomount -nobrowse "$IMAGE" 2>"$TMP/attach.log" |
      awk 'NR == 1 { print $1 }')
[ -n "$DEV" ] || fail "could not attach the disposable image"
if ! OUT=$(fsck_hfs -fn "$DEV" 2>&1); then
    echo "$OUT" >&2
    fail "fsck_hfs reported an error"
fi
echo "$OUT" | grep -q "appears to be OK" ||
    fail "fsck_hfs did not report a clean volume"
if echo "$OUT" | grep -qE '\*\* (Repairing|Rebuilding|Fixing)'; then
    fail "fsck_hfs attempted a repair"
fi
hdiutil detach "$DEV" >/dev/null 2>&1
DEV=""

OUT=$("$ROOT/tests/image/loader_host" "$IMAGE" "$PUBLIC" "$KEY_ID" 1) ||
    fail "the loader rejected the generated artifact"
echo "$OUT"
echo "$OUT" | grep -q '^loader: OK$' || fail "loader did not report OK"
echo "$OUT" | grep -q '^bootstate: GOOD$' ||
    fail "loader did not select the GOOD boot state"

echo "PASS: generated loader artifact verifies through the HFS+ core"
