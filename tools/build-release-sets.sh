#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build the three firmware sets a release ships, one directory each.
#
#   ./tools/build-release-sets.sh [OUTPUT_DIR]
#
# Writes OUTPUT_DIR/{launcher,music-hub,disk-mode}/ with the image, manifest,
# bootstate, test key and per-set SHA256SUMS that `make image` produces.
#
# Environment:
#   CROSS               toolchain prefix                (default arm-elf-eabi-)
#   TOOLCHAIN_PATH      prepended to PATH for the build (default empty)
#   HDB                 demo catalog for the Music hub  (default
#                       build/helikondb/first100/music.hdb)
#   LAUNCHER_VERSION    image version word, launcher    (default 21)
#   MUSIC_VERSION       image version word, Music hub   (default 22)
#   DISKMODE_VERSION    image version word, Disk Mode   (default 23)
#
# Two mistakes this script exists to catch. `UI=1` alone does not build the
# launcher: the home-screen entry is behind HELIKON_DIAG_LAUNCHER, so the linker
# garbage-collects ui/launcher_run.o and signs a plain boot image — an image
# that is 4 KB smaller and still exits 0. And `HDB=` has to reach the Music-hub
# build, or the catalog blob is dropped the same way. Both are checked by
# symbol, not by exit status.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${1:-"$ROOT/build/release-sets"}
CROSS=${CROSS:-arm-elf-eabi-}
HDB=${HDB:-"$ROOT/build/helikondb/first100/music.hdb"}
LAUNCHER_VERSION=${LAUNCHER_VERSION:-21}
MUSIC_VERSION=${MUSIC_VERSION:-22}
DISKMODE_VERSION=${DISKMODE_VERSION:-23}

if [ -n "${TOOLCHAIN_PATH:-}" ]; then
    PATH="$TOOLCHAIN_PATH:$PATH"
    export PATH
fi

if ! command -v "${CROSS}gcc" >/dev/null 2>&1; then
    echo "build-release-sets: ${CROSS}gcc not found in PATH" >&2
    echo "build-release-sets: set TOOLCHAIN_PATH or CROSS" >&2
    exit 2
fi

if [ ! -f "$HDB" ]; then
    echo "build-release-sets: missing catalog $HDB" >&2
    echo "build-release-sets: build it with tools/hdb_build.py or set HDB" >&2
    exit 2
fi

need_symbol() {
    elf=$1
    symbol=$2
    if ! "${CROSS}nm" "$elf" | grep -q " $symbol\$"; then
        echo "build-release-sets: $elf has no $symbol" >&2
        echo "build-release-sets: the linker dropped it; check the build flags" >&2
        exit 1
    fi
}

copy_set() {
    build=$1
    name=$2
    mkdir -p "$OUT/$name"
    cp "$build"/out/* "$OUT/$name/"
    echo "  $name: $(wc -c < "$OUT/$name/firmware.img" | tr -d ' ') bytes"
}

rm -rf "$OUT"
mkdir -p "$OUT"

echo "== launcher (version $LAUNCHER_VERSION)"
rm -rf "$ROOT/build/firmware/6g-launcher"
make -C "$ROOT/firmware" BOARD=6g VERSION="$LAUNCHER_VERSION" \
    BOOTSTATE_STATE=good UI=1 EXTRA_CFLAGS=-DHELIKON_DIAG_LAUNCHER \
    BUILD=../build/firmware/6g-launcher image >/dev/null
need_symbol "$ROOT/build/firmware/6g-launcher/firmware.elf" helikon_launcher_run
copy_set "$ROOT/build/firmware/6g-launcher" launcher

echo "== music hub (version $MUSIC_VERSION)"
rm -rf "$ROOT/build/firmware/6g-music"
make -C "$ROOT/firmware" BOARD=6g VERSION="$MUSIC_VERSION" \
    BOOTSTATE_STATE=good UI=1 HDB="$HDB" \
    EXTRA_CFLAGS=-DHELIKON_DIAG_LAUNCHER \
    BUILD=../build/firmware/6g-music image >/dev/null
need_symbol "$ROOT/build/firmware/6g-music/firmware.elf" helikon_hdb_start
copy_set "$ROOT/build/firmware/6g-music" music-hub

echo "== disk mode (version $DISKMODE_VERSION)"
rm -rf "$ROOT/build/firmware/6g-diskmode"
make -C "$ROOT/firmware" BOARD=6g DISKMODE=1 VERSION="$DISKMODE_VERSION" \
    BOOTSTATE_STATE=good image >/dev/null
need_symbol "$ROOT/build/firmware/6g-diskmode/firmware.elf" helikon_disk_mode_start
copy_set "$ROOT/build/firmware/6g-diskmode" disk-mode

echo "build-release-sets: $OUT"
