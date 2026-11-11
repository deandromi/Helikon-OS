#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# collect-ipod-evidence.sh - read-only Apple Disk Mode baseline capture for the
# iPod Classic 6G. It records USB descriptors, diskutil views, partition-map
# information, HFS+ geometry and an optional read-only fsck_hfs check.
#
# This script never writes to the iPod. It refuses non-Darwin hosts, internal
# disks, virtual/synthesized disks and the boot disk. The optional block-0 read
# requires --yes plus an interactive confirmation and reads exactly 512 bytes
# from /dev/rdiskN into the output directory.
#
# Run ./tools/collect-ipod-evidence.sh --help for usage.

set -euo pipefail

PROG=$(basename "$0")
ORIG_ARGS="$*"

OUT=""
DO_FSCK=0
DO_BLOCK0=0
ASSUME_YES=0

FSCK_STATUS="not requested"
BLOCK0_STATUS="not requested"
FAILED=0

usage() {
    cat <<'EOF'
collect-ipod-evidence.sh - read-only Apple Disk Mode evidence capture

Usage:
  ./tools/collect-ipod-evidence.sh [options] [OUTPUT_DIR]

Options:
  --fsck          Interactively run fsck_hfs -n (read-only) on each HFS+
                  volume. All volumes must be unmounted first. The script
                  refuses if any volume is mounted.
  --read-block0   Read one 512-byte block from the raw disk into
                  OUTPUT_DIR/block0.bin. Requires --yes and unmounted volumes.
  --yes           Confirm the optional block-0 read. No other effect.
  -h, --help      Show this help and exit.

OUTPUT_DIR defaults to ./ipod-evidence-YYYY-MM-DD_HHMMSS. The directory must
not already exist.

Safety:
  - Read-only. No command writes to the iPod.
  - No of=/dev/... path is accepted or generated.
  - No format, erase, restore, repair or repartition.
  - fsck_hfs is run with -n only, never in repair mode.
  - The optional block-0 read is:
      sudo dd if=/dev/rdiskN of=OUTPUT_DIR/block0.bin bs=512 count=1

The iPod must be in Apple Disk Mode (Select + Play) and connected by USB.
EOF
}

usage_error() {
    printf '%s: %s\n' "$PROG" "$*" >&2
    printf 'Try %s --help.\n' "$PROG" >&2
    exit 2
}

die() {
    printf '%s: %s\n' "$PROG" "$*" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --fsck)
            DO_FSCK=1
            ;;
        --read-block0)
            DO_BLOCK0=1
            ;;
        --yes)
            ASSUME_YES=1
            ;;
        --)
            shift
            break
            ;;
        -*)
            usage_error "unknown option: $1"
            ;;
        *)
            if [ -n "$OUT" ]; then
                usage_error "only one OUTPUT_DIR is allowed"
            fi
            OUT=$1
            ;;
    esac
    shift
done

while [ "$#" -gt 0 ]; do
    if [ -n "$OUT" ]; then
        usage_error "only one OUTPUT_DIR is allowed"
    fi
    OUT=$1
    shift
done

if [ "$ASSUME_YES" -eq 1 ] && [ "$DO_BLOCK0" -eq 0 ]; then
    usage_error "--yes is only valid with --read-block0"
fi

if [ "$DO_BLOCK0" -eq 1 ] && [ "$ASSUME_YES" -ne 1 ]; then
    usage_error "--read-block0 requires an explicit --yes"
fi

# ---------------------------------------------------------------------------
# Host and tool checks
# ---------------------------------------------------------------------------

if [ "$(uname -s)" != "Darwin" ]; then
    die "this collector runs only on macOS (Darwin)"
fi

for cmd in diskutil system_profiler ioreg fsck_hfs shasum sed; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        die "required command not found: $cmd"
    fi
done

if [ "$DO_BLOCK0" -eq 1 ]; then
    for cmd in sudo od; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            die "required command not found for --read-block0: $cmd"
        fi
    done
fi

if [ -z "$OUT" ]; then
    OUT="./ipod-evidence-$(date +%Y-%m-%d_%H%M%S)"
fi

case "$OUT" in
    /dev|/dev/*)
        die "OUTPUT_DIR must not be under /dev: $OUT"
        ;;
esac

if [ -e "$OUT" ]; then
    die "output directory already exists: $OUT"
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

field() {
    local info=$1 key=$2
    printf '%s\n' "$info" |
        awk -F': *' -v k="$key" '$0 ~ ("^[[:space:]]*" k ":") { print $2; exit }'
}

redact() {
    # Text-only redaction. Keep model, capacity, vendor/product IDs and
    # firmware versions; replace serial-number values.
    sed -E \
        -e 's/(USB Serial Number|Serial Number)([^:]*):[[:space:]]*.*/\1\2: [REDACTED]/' \
        -e 's/(^|[^[:alnum:]_])Serial:[[:space:]]*.*/\1Serial: [REDACTED]/' \
        -e 's/("(USB )?Serial Number"[[:space:]]*=[[:space:]]*).*/\1"[REDACTED]"/' \
        -e 's/("iSerial( Number)?"[[:space:]]*=[[:space:]]*).*/\1"[REDACTED]"/'
}

capture() {
    local out_file=$1
    shift
    printf '%s\n' "$*" >> "$COMMANDS_LOG"
    if ! "$@" 2>&1 | redact > "$out_file"; then
        printf 'warning: command failed: %s\n' "$*" >&2
    fi
}

list_candidates() {
    printf 'Whole disks reported by diskutil:\n'
    printf '  %-14s %-10s %-12s %s\n' "DISK" "LOCATION" "SIZE" "MEDIA NAME"
    diskutil list |
        awk '/^\/dev\/disk[0-9]+ \(/ { print $1 }' |
        while IFS= read -r d; do
            local info media size loc
            info=$(diskutil info "$d" 2>/dev/null || true)
            media=$(field "$info" "Device / Media Name")
            size=$(field "$info" "Disk Size" | awk '{ print $1 " " $2 }')
            loc=$(field "$info" "Device Location")
            printf '  %-14s %-10s %-12s %s\n' \
                "$d" "${loc:-unknown}" "${size:-unknown}" "${media:-unknown}"
        done
}

mounted_volumes() {
    # Print every mounted whole disk or slice with a trailing space.
    if diskutil info "$DISK" 2>/dev/null |
        grep -qE '^[[:space:]]*Mounted:[[:space:]]*Yes'; then
        printf '%s ' "$DISK"
    fi
    # shellcheck disable=SC2086
    for id in $VOLUMES; do
        if diskutil info "/dev/$id" 2>/dev/null |
            grep -qE '^[[:space:]]*Mounted:[[:space:]]*Yes'; then
            printf '/dev/%s ' "$id"
        fi
    done
    return 0
}

fsck_targets() {
    # The 6G normally has slices. If there are none and the whole disk itself is
    # HFS+, check the whole disk.
    if [ -n "$VOLUMES" ]; then
        printf '%s\n' "$VOLUMES"
        return 0
    fi
    if diskutil info "$DISK" 2>/dev/null |
        grep -qE '^[[:space:]]*File System Personality:[[:space:]]*HFS\+'; then
        printf '%s\n' "$DISK_ID"
    fi
    return 0
}

# ---------------------------------------------------------------------------
# Interactive optional steps
# ---------------------------------------------------------------------------

run_fsck() {
    local answer mounted target found fsck_rc_problem vol

    printf '\n-- fsck_hfs -n (read-only) --\n'
    printf 'All volumes of %s must be unmounted first.\n' "$DISK"
    printf 'If Finder has the iPod mounted, run: diskutil unmountDisk %s\n' "$DISK"
    printf 'Continue with fsck_hfs -n? [y/N] '
    if ! IFS= read -r answer; then
        FSCK_STATUS="not run: no interactive answer"
        return 1
    fi
    case "$answer" in
        y|Y|yes|YES|Yes) ;;
        *)
            FSCK_STATUS="skipped by user"
            printf 'fsck skipped.\n'
            return 0
            ;;
    esac

    mounted=$(mounted_volumes)
    mounted=${mounted% }
    if [ -n "$mounted" ]; then
        FSCK_STATUS="refused: mounted volumes:$mounted"
        printf '%s: refusing fsck_hfs: mounted volumes:%s\n' "$PROG" "$mounted" >&2
        printf 'Run: diskutil unmountDisk %s\n' "$DISK" >&2
        return 1
    fi

    : > "$OUT/fsck-summary.txt"
    found=0
    fsck_rc_problem=0

    while IFS= read -r target; do
        [ -n "$target" ] || continue
        vol="/dev/$target"
        if diskutil info "$vol" 2>/dev/null |
            grep -qE '^[[:space:]]*(File System Personality:[[:space:]]*HFS\+|Type \(Bundle\):[[:space:]]*hfs)'; then
            # Re-check immediately before the check: do not run fsck_hfs on a
            # volume that macOS mounted after the first gate.
            mounted=$(mounted_volumes)
            mounted=${mounted% }
            if [ -n "$mounted" ]; then
                FSCK_STATUS="refused: mounted volumes:$mounted"
                printf '%s: refusing fsck_hfs: mounted volumes:%s\n' \
                    "$PROG" "$mounted" >&2
                return 1
            fi
            found=1
            printf 'fsck_hfs -n %s\n' "$vol" >> "$COMMANDS_LOG"
            if fsck_hfs -n "$vol" > "$OUT/fsck-$target.txt" 2>&1; then
                printf '%s: exit 0 (clean check)\n' "$vol" \
                    >> "$OUT/fsck-summary.txt"
            else
                fsck_rc_problem=1
                printf '%s: nonzero exit (review output)\n' "$vol" \
                    >> "$OUT/fsck-summary.txt"
            fi
        else
            printf '%s: skipped (not HFS+)\n' "$vol" \
                >> "$OUT/fsck-summary.txt"
        fi
    done <<EOF
$(fsck_targets)
EOF

    if [ "$found" -eq 0 ]; then
        printf 'No HFS+ volumes found; fsck_hfs not run.\n' \
            >> "$OUT/fsck-summary.txt"
        FSCK_STATUS="no HFS+ volumes found; fsck_hfs not run"
    elif [ "$fsck_rc_problem" -eq 1 ]; then
        FSCK_STATUS="ran; one or more volumes returned nonzero (review fsck-summary.txt)"
    else
        FSCK_STATUS="ran (fsck_hfs -n); see fsck-summary.txt"
    fi
    cat "$OUT/fsck-summary.txt"
}

run_block0() {
    local answer mounted size

    RDISK="/dev/r${DISK#/dev/}"
    BLOCK0="$OUT/block0.bin"

    case "$RDISK" in
        /dev/rdisk[0-9]*) ;;
        *)
            BLOCK0_STATUS="refused: bad raw device name: $RDISK"
            printf '%s: %s\n' "$PROG" "$BLOCK0_STATUS" >&2
            return 1
            ;;
    esac

    case "$BLOCK0" in
        /dev|/dev/*)
            BLOCK0_STATUS="refused: output path under /dev: $BLOCK0"
            printf '%s: %s\n' "$PROG" "$BLOCK0_STATUS" >&2
            return 1
            ;;
    esac

    printf '\n-- optional read-only block 0 --\n'
    printf 'WARNING: dd reads a raw disk. A wrong output path can destroy a disk.\n'
    printf 'This script pins the output to: %s\n' "$BLOCK0"
    printf 'It reads 512 bytes from %s and does not write to the device.\n' "$RDISK"
    printf 'All volumes must be unmounted first.\n'

    if [ -t 0 ]; then
        printf 'Type READ-BLOCK0 to continue: '
        if ! IFS= read -r answer; then
            BLOCK0_STATUS="not run: no interactive answer"
            return 1
        fi
        if [ "$answer" != "READ-BLOCK0" ]; then
            BLOCK0_STATUS="cancelled by user"
            printf 'block-0 read cancelled.\n'
            return 1
        fi
    else
        printf 'Non-interactive --yes confirmation accepted.\n'
    fi

    mounted=$(mounted_volumes)
    mounted=${mounted% }
    if [ -n "$mounted" ]; then
        BLOCK0_STATUS="refused: mounted volumes:$mounted"
        printf '%s: refusing block-0 read: mounted volumes:%s\n' \
            "$PROG" "$mounted" >&2
        printf 'Run: diskutil unmountDisk %s\n' "$DISK" >&2
        return 1
    fi

    # Create the file as the invoking user so sudo dd keeps user ownership and
    # the later od/shasum reads do not depend on the user's umask.
    : > "$BLOCK0"

    # Final gate immediately before the raw read.
    mounted=$(mounted_volumes)
    mounted=${mounted% }
    if [ -n "$mounted" ]; then
        rm -f "$BLOCK0"
        BLOCK0_STATUS="refused: mounted volumes:$mounted"
        printf '%s: refusing block-0 read: mounted volumes:%s\n' \
            "$PROG" "$mounted" >&2
        return 1
    fi

    printf 'sudo dd if=%s of=%s bs=512 count=1\n' "$RDISK" "$BLOCK0" \
        >> "$COMMANDS_LOG"
    if ! sudo dd if="$RDISK" of="$BLOCK0" bs=512 count=1; then
        rm -f "$BLOCK0"
        BLOCK0_STATUS="failed: dd returned nonzero"
        return 1
    fi

    size=$(stat -f%z "$BLOCK0")
    if [ "$size" -ne 512 ]; then
        rm -f "$BLOCK0"
        BLOCK0_STATUS="failed: block0.bin is $size bytes, expected 512"
        printf '%s: %s\n' "$PROG" "$BLOCK0_STATUS" >&2
        return 1
    fi

    if ! od -A x -t x1 -v "$BLOCK0" > "$OUT/block0-hexdump.txt"; then
        BLOCK0_STATUS="read 512 bytes; hexdump failed"
        return 1
    fi

    BLOCK0_STATUS="read 512 bytes (block0.bin, block0-hexdump.txt)"
    printf 'Read 512 bytes from %s.\n' "$RDISK"
}

# ---------------------------------------------------------------------------
# README and hashes
# ---------------------------------------------------------------------------

write_readme() {
    local targets
    {
        cat <<EOF
Helikon-OS - Apple Disk Mode baseline evidence
==============================================

Date (UTC):     $(date -u '+%Y-%m-%dT%H:%M:%SZ')
Host:           macOS $(sw_vers -productVersion) ($(sw_vers -buildVersion))
Disk:           $DISK
Collector:      $0
Arguments:      ${ORIG_ARGS:-none}
Output:         $OUT

No-write statement
------------------
This capture is read-only. It did not format, erase, restore, repair,
repartition or write to the iPod. No command uses an of=/dev/... path.
EOF

        if [ "$DO_BLOCK0" -eq 1 ]; then
            cat <<EOF
The optional 512-byte device read was requested:
  sudo dd if=$RDISK of=$BLOCK0 bs=512 count=1
It writes only to block0.bin in this directory.
EOF
        else
            printf 'No block-0 read was requested.\n'
        fi

        cat <<EOF

Commands and status
-------------------
  diskutil list
  diskutil list $DISK
  diskutil info $DISK
EOF

        # shellcheck disable=SC2086
        for id in $VOLUMES; do
            printf '  diskutil info /dev/%s\n' "$id"
        done

        cat <<EOF
  system_profiler SPUSBDataType
  ioreg -p IOUSB -l -w 0
  sw_vers
  uname -a
EOF

        if [ "$DO_FSCK" -eq 1 ]; then
            targets=$(fsck_targets)
            if [ -n "$targets" ]; then
                while IFS= read -r target; do
                    [ -n "$target" ] || continue
                    printf '  fsck_hfs -n /dev/%s\n' "$target"
                done <<EOF2
$targets
EOF2
            else
                printf '  fsck_hfs -n (no HFS+ volume found)\n'
            fi
        else
            printf '  fsck_hfs -n (not requested; use --fsck after unmounting)\n'
        fi

        if [ "$DO_BLOCK0" -eq 1 ]; then
            printf '  sudo dd if=%s of=%s bs=512 count=1\n' "$RDISK" "$BLOCK0"
        else
            printf '  sudo dd (block 0 not requested)\n'
        fi

        cat <<EOF

Run status
----------
  fsck:    $FSCK_STATUS
  block 0: $BLOCK0_STATUS

Redaction note
--------------
Every text capture is filtered through sed. It replaces the value of
"Serial Number", "USB Serial Number", "Serial:", "\"Serial Number\" =",
"\"USB Serial Number\" =" and "\"iSerial Number\" =" with [REDACTED].
Model, capacity, vendor ID, product ID, USB version and firmware version
are kept. Volume names, the host name in uname -a, USB location IDs and
block0.bin (binary) are not redacted; review them before sharing.

This directory is evidence, not a backup. Do not commit it. Never commit
serial numbers, UDIDs, Wi-Fi credentials, library contents or disk images.
EOF
    } > "$OUT/README.txt"
}

write_sha256sums() {
    (
        cd "$OUT"
        for f in *; do
            if [ "$f" = "SHA256SUMS" ]; then
                continue
            fi
            if [ -f "$f" ]; then
                shasum -a 256 "$f"
            fi
        done > SHA256SUMS
    )
}

print_summary() {
    printf '\nEvidence directory: %s\n' "$OUT"
    printf 'Disk: %s\n' "$DISK"
    printf 'Files: %s\n' "$(find "$OUT" -type f | wc -l | tr -d ' ')"
    printf 'fsck: %s\n' "$FSCK_STATUS"
    printf 'block 0: %s\n' "$BLOCK0_STATUS"
    printf 'SHA256SUMS: %s\n' "$OUT/SHA256SUMS"
    printf 'No write was made to %s.\n' "$DISK"
    if [ "$FAILED" -ne 0 ]; then
        printf 'One or more requested optional steps did not run; see README.txt.\n' >&2
    fi
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

printf '\n--- diskutil list ---\n'
diskutil list
printf '\n'
list_candidates
printf '\nIdentify the iPod by its size and product name. Disconnect other external disks.\n'
printf 'Enter the iPod whole disk (example /dev/disk4), or q to quit: '

if ! IFS= read -r DISK; then
    die "no disk entered"
fi

DISK=$(printf '%s' "$DISK" |
    sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')

case "$DISK" in
    q|Q|quit|QUIT)
        printf 'cancelled.\n'
        exit 0
        ;;
esac

[ -n "$DISK" ] || die "no disk entered"

case "$DISK" in
    disk[0-9]*) DISK="/dev/$DISK" ;;
esac

if ! printf '%s\n' "$DISK" | grep -Eq '^/dev/disk[0-9]+$'; then
    die "expected a whole disk such as /dev/diskN; got: $DISK"
fi

INFO=$(diskutil info "$DISK" 2>/dev/null) || die "diskutil info failed for $DISK"

WHOLE=$(field "$INFO" "Whole")
[ "$WHOLE" = "Yes" ] || die "$DISK is not a whole disk (Whole: ${WHOLE:-unknown})"

NODE=$(field "$INFO" "Device Node")
[ "$NODE" = "$DISK" ] || die "device node mismatch for $DISK: ${NODE:-unknown}"

LOC=$(field "$INFO" "Device Location")
[ "$LOC" = "External" ] || die "refusing $DISK: Device Location is ${LOC:-unknown}, not External"

VIRTUAL=$(field "$INFO" "Virtual")
if [ "$VIRTUAL" = "Yes" ]; then
    die "refusing $DISK: virtual/synthesized disk"
fi

BOOT_INFO=$(diskutil info / 2>/dev/null || true)
BOOT=$(field "$BOOT_INFO" "Part of Whole")
BOOT=${BOOT#/dev/}
DISK_ID=${DISK#/dev/}
if [ -n "$BOOT" ] && [ "$DISK_ID" = "$BOOT" ]; then
    die "refusing the boot disk: $DISK"
fi

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd -P)
case "$OUT" in
    /dev|/dev/*)
        die "OUTPUT_DIR must not be under /dev: $OUT"
        ;;
esac

COMMANDS_LOG="$OUT/commands.log"
RDISK="/dev/r${DISK#/dev/}"
BLOCK0="$OUT/block0.bin"

{
    printf '# Helikon-OS Apple Disk Mode evidence capture\n'
    printf '# Date (UTC): %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf '# Host: macOS %s (%s)\n' \
        "$(sw_vers -productVersion)" "$(sw_vers -buildVersion)"
    printf '# Disk: %s\n' "$DISK"
    printf '# Arguments: %s\n' "${ORIG_ARGS:-none}"
    printf '# Text output is filtered through the redaction sed in README.txt.\n'
    printf '\n'
} > "$COMMANDS_LOG"

capture "$OUT/diskutil-list.txt" diskutil list
capture "$OUT/diskutil-list-$DISK_ID.txt" diskutil list "$DISK"
capture "$OUT/diskutil-info-$DISK_ID.txt" diskutil info "$DISK"

VOLUMES=$(diskutil list "$DISK" |
    awk '/^[[:space:]]*[0-9]+:/ { print $NF }' |
    grep -E '^disk[0-9]+(s[0-9]+)+$' |
    sort -u || true)

if [ -n "$VOLUMES" ]; then
    # shellcheck disable=SC2086
    for id in $VOLUMES; do
        capture "$OUT/diskutil-info-$id.txt" diskutil info "/dev/$id"
    done
else
    printf 'warning: no volume slices found under %s\n' "$DISK" >&2
fi

capture "$OUT/system_profiler-SPUSBDataType.txt" system_profiler SPUSBDataType
capture "$OUT/ioreg-IOUSB.txt" ioreg -p IOUSB -l -w 0
capture "$OUT/sw_vers.txt" sw_vers
capture "$OUT/uname-a.txt" uname -a

{
    printf 'Helikon-OS Apple Disk Mode geometry summary\n'
    printf 'Date (UTC): %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf 'Disk: %s\n' "$DISK"
    printf '\n[whole disk]\n'
    grep -E '^[[:space:]]*(Device / Media Name|Disk Size|Device Block Size|Content \(IOContent\)|Device Location|Removable Media|Ejectable|Virtual|Whole):' \
        "$OUT/diskutil-info-$DISK_ID.txt" || true
    # shellcheck disable=SC2086
    for id in $VOLUMES; do
        printf '\n[%s]\n' "$id"
        grep -E '^[[:space:]]*(Volume Name|Mounted|Partition Type|File System Personality|Type \(Bundle\)|Disk Size|Device Block Size|Volume Total Space|Volume Free Space|Allocation Block Size|Volume Read-Only):' \
            "$OUT/diskutil-info-$id.txt" || true
    done
} > "$OUT/geometry-summary.txt"

if [ "$DO_FSCK" -eq 1 ]; then
    if ! run_fsck; then
        FAILED=1
    fi
fi

if [ "$DO_BLOCK0" -eq 1 ]; then
    if ! run_block0; then
        FAILED=1
    fi
fi

write_readme
write_sha256sums
print_summary

exit "$FAILED"
