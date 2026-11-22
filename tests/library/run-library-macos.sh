#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Storage-path test for the music library, macOS only.
#
# Creates a disposable 16 MiB journaled HFS+ image with hdiutil, puts a
# HelikonDB catalog at /.helikon/music.hdb, unmounts it, checks the volume with
# fsck_hfs and then runs the host test that mounts the image read-only through
# the VFS and reads the catalog. It never accepts an existing image or device
# path, never touches a real iPod and makes no device claim.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "SKIP: the library image test is macOS only"
    exit 0
fi

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/helikon-library-test.XXXXXX")
IMAGE="$TMP/library.dmg"
MOUNT="$TMP/mnt"
HDB="$TMP/music.hdb"
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

echo "building the host test"
# Always from scratch: the target does not track CFLAGS, so a `SAN=1` run must
# not silently reuse the plain binary.
make -C "$HERE" clean >/dev/null 2>&1 || true
make -C "$HERE" all SAN="${SAN:-0}" >/dev/null || fail "could not build the host test"

echo "building the fixture catalog"
python3 "$ROOT/tools/hdb_build.py" fixture --out "$HDB" >/dev/null ||
    fail "could not build the fixture catalog"

echo "creating a disposable 16 MiB journaled HFS+ image"
hdiutil create -size 16m -fs HFS+J -volname HelikonLibrary -layout NONE \
    "$IMAGE" >"$TMP/create.log" 2>&1 ||
    { cat "$TMP/create.log" >&2 || true
      fail "could not create the disposable image"; }

mkdir -p "$MOUNT"
hdiutil attach -nobrowse -mountpoint "$MOUNT" "$IMAGE" >"$TMP/attach.log" 2>&1 ||
    { cat "$TMP/attach.log" >&2 || true
      fail "could not mount the disposable image"; }
MOUNTED=1

mkdir -p "$MOUNT/.helikon"
cp "$HDB" "$MOUNT/.helikon/music.hdb"
# One audio file that exists only on this volume: the storage-path decode test
# reads it through the VFS and compares the PCM with a resident-buffer decode.
mkdir -p "$MOUNT/Music"
cp "$ROOT/tests/codec/fixtures/mp3_cbr_stereo16.mp3" "$MOUNT/Music/first.mp3"
[ -s "$MOUNT/Music/first.mp3" ] || fail "could not copy the audio fixture"

# The six paths the fixture catalog names, each a real file. A playlist is
# resolved against the catalog and then played from the volume, so the two have
# to agree: a catalog entry with no file is exactly the bug this would hide.
FIXTURE=$ROOT/tests/codec/fixtures/mp3_cbr_stereo16.mp3
for rel in \
    "Music/The Ember Set/Night Harbour/01 First Light.m4a" \
    "Music/The Ember Set/Night Harbour/02 Second Light.m4a" \
    "Music/Lighthouse/Static Bloom/01 Signal.m4a" \
    "Music/Lighthouse/Static Bloom/02 Noise.m4a" \
    "Music/Various Artists/Mixtape/01 Ember.m4a" \
    "Music/Various Artists/Mixtape/02 Tide.m4a"
do
    mkdir -p "$MOUNT/$(dirname "$rel")"
    cp "$FIXTURE" "$MOUNT/$rel"
    [ -s "$MOUNT/$rel" ] || fail "could not copy $rel"
done

# One playlist with every resolution case in it: a `..` entry, an absolute
# entry, an entry the catalog does not have, and one that escapes the root.
mkdir -p "$MOUNT/Playlists"
cat > "$MOUNT/Playlists/first.m3u" <<'PLAYLIST'
#EXTM3U
#EXTINF:220,Tide
../Music/Various Artists/Mixtape/02 Tide.m4a
/Music/Lighthouse/Static Bloom/01 Signal.m4a
../Music/The Ember Set/Night Harbour/01 First Light.m4a
../Music/Nowhere/Gone.m4a
../../escape.m4a
PLAYLIST
[ -s "$MOUNT/Playlists/first.m3u" ] || fail "could not write the playlist"

# A playlist whose only entry the catalog does not have: the resolver must report
# it and the source must not start a track it cannot play.
cat > "$MOUNT/Playlists/stale.m3u" <<'PLAYLIST'
#EXTM3U
../Music/Nowhere/Gone.m4a
PLAYLIST
[ -s "$MOUNT/Playlists/stale.m3u" ] || fail "could not write the stale playlist"
sync
hdiutil detach "$MOUNT" >"$TMP/detach.log" 2>&1 ||
    { cat "$TMP/detach.log" >&2 || true
      fail "could not unmount the disposable image"; }
MOUNTED=""

echo "checking the volume with fsck_hfs"
DEV=$(hdiutil attach -nomount -nobrowse "$IMAGE" 2>"$TMP/attach2.log" |
      awk 'NR == 1 { print $1 }')
[ -n "$DEV" ] || { cat "$TMP/attach2.log" >&2 || true
                   fail "could not attach the image"; }
OUT=$(fsck_hfs -fn "$DEV" 2>&1) || { echo "$OUT" >&2
                                     fail "fsck_hfs reported an error"; }
echo "$OUT"
echo "$OUT" | grep -q "appears to be OK" ||
    fail "fsck_hfs did not report a clean volume"
echo "$OUT" | grep -qE '\*\* (Repairing|Rebuilding|Fixing)' &&
    fail "fsck_hfs attempted a repair"
hdiutil detach "$DEV" >/dev/null 2>&1
DEV=""

echo "running the host test against the image"
"$HERE/test_library" "$IMAGE"

echo "PASS: the library catalog and one audio file read from a real HFS+ volume"
