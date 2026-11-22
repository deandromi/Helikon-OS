#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build the Helikon-OS USB Disk Mode diagnostic image for the iPod Classic 6G.
#
# The hardware bring-up (S5L8702 clocking, PMU, ATA, LCD, buttons) comes from a
# pinned hfsplus-rockboxed-ipod checkout. This separate, DFU-only image hands
# the raw disk to a host as a writable USB mass-storage LUN, so a player whose
# firmware cannot boot can still be formatted, including above 128 GiB. It
# never mounts a filesystem and never loads firmware; it is not the production
# bootloader and it is not part of a release.
#
# Usage:
#   ./tools/build-diagnostic-bootloader.sh
#   DONOR_DIR=/path/to/hfsplus-rockboxed-ipod ./tools/build-diagnostic-bootloader.sh
#
# The donor checkout is read-only: the build uses `git archive` at the pinned
# commit, so local donor changes are never used.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DONOR_URL=${DONOR_URL:-https://github.com/deandromi/hfsplus-rockboxed-ipod.git}
DONOR_COMMIT=${DONOR_COMMIT:-55603bd4bc2524ded900fa17feae99a223b04349}
BUILD_DIR=${BUILD_DIR:-"$ROOT/build/usb-disk-bootloader"}
SRC_DIR="$BUILD_DIR/src"
OBJ_DIR="$SRC_DIR/build"
OUT_DIR="$ROOT/bootloader/artifacts"
IMAGE="$OUT_DIR/bootloader-ipod6g-usb-disk.ipod"
DFU="$OUT_DIR/bootloader-ipod6g-usb-disk.dfu"
UNINSTALL="$OUT_DIR/bootloader-ipod6g-uninstall.dfu"

JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}

if ! command -v arm-elf-eabi-gcc >/dev/null 2>&1; then
    if [ -x "$HOME/.rockbox-toolchain/bin/arm-elf-eabi-gcc" ]; then
        PATH="$HOME/.rockbox-toolchain/bin:$PATH"
        export PATH
    else
        echo "error: arm-elf-eabi-gcc 9.5.0 (Rockbox toolchain) not found" >&2
        echo "       expected at \$HOME/.rockbox-toolchain/bin or on PATH" >&2
        exit 1
    fi
fi

if [ -n "${DONOR_DIR:-}" ]; then
    DONOR=$DONOR_DIR
else
    DONOR="$BUILD_DIR/donor"
    if [ ! -d "$DONOR/.git" ]; then
        echo "Cloning donor into $DONOR ..."
        mkdir -p "$BUILD_DIR"
        git clone "$DONOR_URL" "$DONOR"
    fi
fi

if ! git -C "$DONOR" cat-file -e "$DONOR_COMMIT^{commit}" 2>/dev/null; then
    echo "error: donor commit $DONOR_COMMIT is not present in $DONOR" >&2
    exit 1
fi

rm -rf "$SRC_DIR" "$OBJ_DIR"
mkdir -p "$SRC_DIR" "$OBJ_DIR" "$OUT_DIR"

echo "Exporting donor $DONOR_COMMIT ..."
git -C "$DONOR" archive "$DONOR_COMMIT" | tar -x -C "$SRC_DIR"

HELIKON_DIR="$SRC_DIR/bootloader/helikon"
mkdir -p "$HELIKON_DIR"
# The Disk Mode image reuses the firmware's own USB stack and block layer: the
# same files the host suites cover. `usb.c` is the controller driver; the seam
# it needs is built in the hardware glue below.
cp "$ROOT/bootloader/hardware/helikon_usb_disk_hw.c" "$HELIKON_DIR/"
cp "$ROOT"/firmware/usb/usb_descriptors.c "$ROOT"/firmware/usb/usb_descriptors.h \
   "$ROOT"/firmware/usb/usb_control.c "$ROOT"/firmware/usb/usb_control.h \
   "$ROOT"/firmware/usb/usb_msc.c "$ROOT"/firmware/usb/usb_msc.h \
   "$ROOT"/firmware/usb/disk_mode.c "$ROOT"/firmware/usb/disk_mode.h \
   "$ROOT"/firmware/usb/usb_events.h \
   "$ROOT"/firmware/hal/6g/usb.c "$ROOT"/firmware/hal/6g/usb.h \
   "$ROOT"/firmware/hal/6g/usb_hw.h \
   "$ROOT"/firmware/block/block.c "$ROOT"/firmware/block/block.h \
   "$ROOT/firmware/lib/error.h" "$HELIKON_DIR/"

# The Rockbox bootloader formatter supports %lu but not long-long. The USB disk
# screen prints no numbers, but keep the guard so a future edit cannot add one.
if grep -n '%ll' "$ROOT/bootloader/hardware/helikon_usb_disk_hw.c"; then
    echo "error: bootloader formatter does not support long-long formatting" >&2
    exit 1
fi

echo "Applying the donor-side integration patch ..."
(
    cd "$SRC_DIR"
    patch -p1 -N < "$ROOT/bootloader/hardware/donor-usb-disk.patch"
)

echo "Configuring the Thumb bootloader build ..."
cd "$OBJ_DIR"
../tools/configure --target=ipod6g --type=b --thumb

echo "Building with $JOBS jobs ..."
make -j"$JOBS"

MAP="$OBJ_DIR/bootloader.map"
python3 - "$MAP" <<'PY'
import re
import sys

text = open(sys.argv[1], encoding="utf-8").read()
move_area = re.search(r"^MOVE_AREA\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)",
                      text, re.MULTILINE)
move_end = re.search(r"^\s+(0x[0-9a-fA-F]+)\s+_moveend\s*=",
                     text, re.MULTILINE)
if move_area is None or move_end is None:
    print("error: MOVE_AREA or _moveend not found in bootloader.map",
          file=sys.stderr)
    sys.exit(1)

origin = int(move_area.group(1), 16)
length = int(move_area.group(2), 16)
end = origin + length
moveend = int(move_end.group(1), 16)
headroom = end - moveend
print(f"MOVE_AREA: 0x{origin:08x}..0x{end:08x} (0x{length:x} bytes)")
print(f"_moveend:  0x{moveend:08x}")
print(f"headroom:  {headroom} bytes")
if moveend > end:
    print("error: bootloader exceeds MOVE_AREA", file=sys.stderr)
    sys.exit(1)
PY

if ! strings -a "$OBJ_DIR/bootloader-ipod6g.ipod" |
        grep -q 'HELIKON DISK MODE (BOOTLOADER)'; then
    echo "error: built image does not contain the Disk Mode marker" >&2
    exit 1
fi
if ! strings -a "$OBJ_DIR/bootloader-ipod6g.ipod" |
        grep -q 'Helikon Disk Mode'; then
    echo "error: built image does not contain the USB product string" >&2
    exit 1
fi

cp "$OBJ_DIR/bootloader-ipod6g.ipod" "$IMAGE"

if command -v shasum >/dev/null 2>&1; then
    HASH=$(shasum -a 256 "$IMAGE" | awk '{print $1}')
else
    HASH=$(sha256sum "$IMAGE" | awk '{print $1}')
fi
printf '%s  bootloader-ipod6g-usb-disk.ipod\n' "$HASH" > "$OUT_DIR/SHA256SUMS.usb-disk"

echo
echo "image:  $IMAGE"
echo "size:   $(wc -c < "$IMAGE" | tr -d ' ') bytes"
echo "sha256: $HASH"

if [ "$(uname -s)" = "Darwin" ] && command -v clang >/dev/null 2>&1; then
    echo
    echo "Building the DFU installer with mks5lboot ..."
    make -C "$SRC_DIR/utils/mks5lboot" -j"$JOBS" CC=clang \
        LDOPTS="-framework IOKit -framework CoreFoundation" >/dev/null
    "$SRC_DIR/utils/mks5lboot/mks5lboot" --mkdfu-inst "$IMAGE" "$DFU" |
        tail -12
    if command -v shasum >/dev/null 2>&1; then
        DFU_HASH=$(shasum -a 256 "$DFU" | awk '{print $1}')
    else
        DFU_HASH=$(sha256sum "$DFU" | awk '{print $1}')
    fi
    printf '%s  bootloader-ipod6g-usb-disk.dfu\n' "$DFU_HASH" \
        >> "$OUT_DIR/SHA256SUMS.usb-disk"
    echo "dfu:    $DFU"
    echo "dfu size: $(wc -c < "$DFU" | tr -d ' ') bytes"
    echo "dfu sha256: $DFU_HASH"

    "$SRC_DIR/utils/mks5lboot/mks5lboot" --mkdfu-uninst ipod6g "$UNINSTALL" |
        tail -8
    if command -v shasum >/dev/null 2>&1; then
        UNINSTALL_HASH=$(shasum -a 256 "$UNINSTALL" | awk '{print $1}')
    else
        UNINSTALL_HASH=$(sha256sum "$UNINSTALL" | awk '{print $1}')
    fi
    printf '%s  bootloader-ipod6g-uninstall.dfu\n' "$UNINSTALL_HASH" \
        > "$OUT_DIR/SHA256SUMS.uninstall"
    echo "uninstall dfu: $UNINSTALL"
    echo "uninstall sha256: $UNINSTALL_HASH"
fi
