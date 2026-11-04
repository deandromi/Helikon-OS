#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Send the prebuilt Helikon-OS outer-volume write-diagnostic DFU installer to a
# 6G in DFU. This never uses --single. The opt-in file must be at the iPod root
# and the full backup must have been verified first.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DFU="$ROOT/bootloader/artifacts/bootloader-ipod6g-outer-diag.dfu"
EXPECTED=dd027696b1710985fd9979904d2be8b29dcee48727819b95f26a5929b75ece0a
MKS=${MKS5LBOOT:-}

if [ ! -f "$DFU" ]; then
    echo "error: missing $DFU" >&2
    exit 1
fi

if [ -z "$MKS" ]; then
    for candidate in \
        "$ROOT/build/outer-diagnostic-bootloader/src/utils/mks5lboot/mks5lboot" \
        "$ROOT/build/write-diagnostic-bootloader/src/utils/mks5lboot/mks5lboot" \
        "$ROOT/build/diagnostic-bootloader/src/utils/mks5lboot/mks5lboot"
    do
        if [ -x "$candidate" ]; then
            MKS=$candidate
            break
        fi
    done
fi

if [ -z "$MKS" ] || [ ! -x "$MKS" ]; then
    echo "error: mks5lboot not found." >&2
    echo "Set MKS5LBOOT=/path/to/mks5lboot or build it from the donor checkout." >&2
    exit 1
fi

if command -v shasum >/dev/null 2>&1; then
    ACTUAL=$(shasum -a 256 "$DFU" | awk '{print $1}')
else
    ACTUAL=$(sha256sum "$DFU" | awk '{print $1}')
fi
if [ "$ACTUAL" != "$EXPECTED" ]; then
    echo "error: DFU hash mismatch" >&2
    echo "expected: $EXPECTED" >&2
    echo "actual:   $ACTUAL" >&2
    exit 1
fi

echo "DFU:    $DFU"
echo "sha256: $ACTUAL"
echo
echo "Before continuing:"
echo "  1. The full iPod backup was verified by reading it back."
echo "  2. helikon-outer-ok.txt is at the iPod root with the magic string."
echo "  3. The iPod journal is clean and the volume was ejected cleanly."
echo "  4. /helikon-outer-test does not already exist."
echo "  5. The iPod is in DFU mode."
echo "  6. No '--single' option is used."
echo
printf "Press Enter to send the installer, or Control-C to stop: "
read -r _ || true
echo

"$MKS" --dfuscan
echo
"$MKS" --dfusend "$DFU"
echo
echo "Sent. The iPod should reboot into HELIKON-OS OUTER DIAG."
echo "Recovery: tools/uninstall-bootloader.sh"
