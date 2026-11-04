#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Verify the exact F2 bootstate artifact through the host HFS+ core.
# Installs the generated image/manifest/bootstate into a disposable HFS+ image,
# confirms the loader sees DEFAULT while the slot is TRYING, promotes it to
# GOOD with the firmware-side promoter and rewrite path, then requires the real
# loader host core to select GOOD. macOS only. Never touches a device.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: F2 artifact check is macOS only"
    exit 0
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT_DIR=${1:-"$ROOT/build/firmware/6g/out"}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-f2-artifact.XXXXXX")
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

for name in firmware.img firmware.manifest bootstate test-public-key.txt; do
    [ -f "$OUT_DIR/$name" ] || fail "missing $OUT_DIR/$name"
done

make -C "$ROOT/tests/image" f2_host loader_host >/dev/null

KEY_ID=$(sed -n 's/^key id: //p' "$OUT_DIR/test-public-key.txt")
PUBLIC=$(sed -n 's/^public key: //p' "$OUT_DIR/test-public-key.txt")
[ -n "$KEY_ID" ] && [ -n "$PUBLIC" ] || fail "cannot parse the test public key"

hdiutil create -size 16m -fs HFS+J -volname HelikonF2Artifact \
    -layout NONE "$IMAGE" >"$TMP/create.log" 2>&1 ||
    fail "could not create the disposable HFS+ image"

mkdir -p "$MOUNT"
hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not mount the disposable image"
MOUNTED=1
mkdir "$MOUNT/.helikon"
cp "$OUT_DIR/firmware.img" "$OUT_DIR/firmware.manifest" \
   "$OUT_DIR/bootstate" "$MOUNT/.helikon/"
MANIFEST_BEFORE=$(shasum -a 256 "$MOUNT/.helikon/firmware.manifest" |
                  awk '{ print $1 }')
IMAGE_BEFORE=$(shasum -a 256 "$MOUNT/.helikon/firmware.img" |
               awk '{ print $1 }')
sync
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""
fsck_image "$IMAGE"

OUT=$("$ROOT/tests/image/loader_host" "$IMAGE" "$PUBLIC" "$KEY_ID" 1) ||
    fail "the loader rejected the generated artifact before promotion"
echo "$OUT"
echo "$OUT" | grep -q '^loader: OK$' || fail "loader did not report OK"
echo "$OUT" | grep -q '^bootstate: DEFAULT$' ||
    fail "the loader did not report DEFAULT for the TRYING slot"

OUT=$("$ROOT/tests/image/f2_host" "$IMAGE" promote) ||
    fail "the F2 promoter failed"
echo "$OUT"
echo "$OUT" | grep -q '^promoted sequence ' ||
    fail "the F2 promoter did not report a promotion"
fsck_image "$IMAGE"

OUT=$("$ROOT/tests/image/loader_host" "$IMAGE" "$PUBLIC" "$KEY_ID" 1) ||
    fail "the loader rejected the promoted artifact"
echo "$OUT"
echo "$OUT" | grep -q '^loader: OK$' || fail "loader did not report OK"
echo "$OUT" | grep -q '^bootstate: GOOD$' ||
    fail "the loader did not select the promoted GOOD slot"

hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" \
    >"$TMP/mount.log" 2>&1 || fail "could not mount the promoted image"
MOUNTED=1
MANIFEST_AFTER=$(shasum -a 256 "$MOUNT/.helikon/firmware.manifest" |
                 awk '{ print $1 }')
IMAGE_AFTER=$(shasum -a 256 "$MOUNT/.helikon/firmware.img" |
              awk '{ print $1 }')
[ "$MANIFEST_BEFORE" = "$MANIFEST_AFTER" ] ||
    fail "the manifest changed during promotion"
[ "$IMAGE_BEFORE" = "$IMAGE_AFTER" ] ||
    fail "the firmware image changed during promotion"
python3 - "$MOUNT/.helikon/bootstate" <<'PY'
import hashlib
import sys

data = open(sys.argv[1], "rb").read()
if len(data) != 512:
    raise SystemExit("bootstate is not 512 bytes")
slot = data[0:256]
if slot[0:4] != b"HBST":
    raise SystemExit("bootstate magic is invalid")
if slot[60] != 2:
    raise SystemExit("slot state is not GOOD")
if hashlib.sha256(slot[0:64]).digest() != slot[64:96]:
    raise SystemExit("slot checksum invalid")
print("bootstate slot: GOOD, checksum valid")
PY
hdiutil detach "$MOUNT" >/dev/null 2>&1
MOUNTED=""
fsck_image "$IMAGE"

echo "PASS: F2 artifact promotes through hfs_txn and the loader selects GOOD"
