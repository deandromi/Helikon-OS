#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Format the disk Helikon Disk Mode exposes as a single HFS+ volume, above
# 128 GiB, from a Mac.
#
# This script is the host half of docs/disk-mode.md §11. It only runs on macOS,
# it refuses to touch the disk it booted from, it refuses to run without a
# backup file you name, and it asks you to type the volume name before it writes
# anything. `--dry-run` prints every command without running it.
#
# What it does, in order:
#   1. checks the target is a real block device and not the boot disk;
#   2. requires a non-empty backup record (a file listing the verified backup);
#   3. asks for confirmation, or accepts --yes;
#   4. unmounts the disk, writes an Apple Partition Map with one HFS+ volume
#      that fills the whole disk, and ejects it.
#
# What it does not do: back up your library, decide that the backup is good, or
# touch a device that is not the one you named.
#
# The iPod side must be running the Disk Mode image:
#   make -C firmware BOARD=6g DISKMODE=1
# and the host must see exactly one external disk for it. Verify that with
# `diskutil list` before you pass a device here.

set -euo pipefail

DEVICE=""
BACKUP=""
VOLUME="IPOD"
ASSUME_YES=0
DRY_RUN=0

usage() {
    cat <<'USAGE'
Usage: disk-mode-format.sh --device /dev/diskN --backup FILE [options]

  --device /dev/diskN   the whole disk Helikon Disk Mode exposes (not a slice)
  --backup FILE         non-empty file recording the verified backup
  --volume NAME         HFS+ volume name (default: IPOD)
  --yes                 skip the typed confirmation
  --dry-run             print the commands, change nothing
  --help                this text

Examples:
  diskutil list
  ./tools/disk-mode-format.sh --device /dev/disk4 --backup ~/ipod-backup.txt --dry-run
  ./tools/disk-mode-format.sh --device /dev/disk4 --backup ~/ipod-backup.txt
USAGE
}

die() {
    printf 'refused: %s\n' "$1" >&2
    exit 1
}

run() {
    if [ "$DRY_RUN" -eq 1 ]; then
        printf '+ %s\n' "$*"
    else
        printf '> %s\n' "$*"
        "$@"
    fi
}

while [ "$#" -gt 0 ]; do
    case "$1" in
    --device)
        [ "$#" -ge 2 ] || die "--device needs a value"
        DEVICE="$2"
        shift 2
        ;;
    --backup)
        [ "$#" -ge 2 ] || die "--backup needs a value"
        BACKUP="$2"
        shift 2
        ;;
    --volume)
        [ "$#" -ge 2 ] || die "--volume needs a value"
        VOLUME="$2"
        shift 2
        ;;
    --yes)
        ASSUME_YES=1
        shift
        ;;
    --dry-run)
        DRY_RUN=1
        shift
        ;;
    --help | -h)
        usage
        exit 0
        ;;
    *)
        die "unknown argument: $1"
        ;;
    esac
done

[ -n "$DEVICE" ] || die "--device is required"
[ -n "$BACKUP" ] || die "--backup is required: this script will not format without one"

[ "$(uname -s)" = "Darwin" ] || die "this script is macOS-only (diskutil)"
command -v diskutil >/dev/null 2>&1 || die "diskutil is missing"

# A whole disk is /dev/diskN with nothing after the number. A slice
# (/dev/disk4s1), a partition or a typo is refused here, before diskutil sees it.
case "$DEVICE" in
/dev/disk[0-9] | /dev/disk[0-9][0-9] | /dev/disk[0-9][0-9][0-9]) ;;
*) die "--device must be a whole disk such as /dev/disk4, not ${DEVICE}" ;;
esac

[ -b "$DEVICE" ] || die "${DEVICE} is not a block device (is Disk Mode running and connected?)"
[ -f "$BACKUP" ] || die "backup record ${BACKUP} does not exist"
[ -s "$BACKUP" ] || die "backup record ${BACKUP} is empty: record the verified backup first"

# Never touch the disk the Mac booted from.
BOOT_DISK="$(diskutil info / | awk -F: '/Part of Whole/ { gsub(/ /, "", $2); print $2 }')"
if [ -n "$BOOT_DISK" ] && [ "/dev/$BOOT_DISK" = "$DEVICE" ]; then
    die "${DEVICE} is the disk macOS booted from"
fi

INFO="$(diskutil info "$DEVICE")"
SIZE_LINE="$(printf '%s\n' "$INFO" | awk -F: '/Disk Size/ { print $2; exit }')"
[ -n "$SIZE_LINE" ] || die "cannot read the size of ${DEVICE}"
printf 'Device: %s\n' "$DEVICE"
printf '%s\n' "$INFO" | awk -F: '/Device \/ Media Name|Disk Size|Virtual|Removable/ { print "  " $1 ":" $2 }'
printf 'Volume name: %s\n' "$VOLUME"
printf 'Backup record: %s\n' "$BACKUP"

if [ "$ASSUME_YES" -ne 1 ] && [ "$DRY_RUN" -eq 1 ]; then
    printf '\n(dry run: the confirmation prompt is skipped)\n'
elif [ "$ASSUME_YES" -ne 1 ]; then
    printf '\nThis ERASES %s and writes one HFS+ volume using the whole disk.\n' "$DEVICE"
    printf 'Type exactly: FORMAT %s\n> ' "$VOLUME"
    read -r answer
    [ "$answer" = "FORMAT $VOLUME" ] || die "confirmation did not match"
fi

run diskutil unmountDisk force "$DEVICE"
# APM, one Journaled HFS+ volume, 0b = all remaining space. -noEFI keeps the
# map to what an iPod expects.
run diskutil partitionDisk "$DEVICE" APM "Journaled HFS+" "$VOLUME" 0b
run diskutil eject "$DEVICE"

printf '\nDone. Next:\n'
printf '  1. Unplug the iPod; Disk Mode should say EJECTED - SAFE.\n'
printf '  2. Verify on a Mac after a read-only mount, or from the iPod: fsck_hfs -fn /dev/diskXsY\n'
printf '  3. Install the normal Helikon firmware through DFU and record the run in docs/device-matrix.md.\n'
printf 'Record the device, capacity, fsck output and hashes; nothing here is evidence until it is written down.\n'
