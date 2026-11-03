#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Remove the installed Helikon-OS bootloader from a 6G in DFU mode.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DFU="$ROOT/bootloader/artifacts/bootloader-ipod6g-uninstall.dfu"
EXPECTED=203e836412b71d3a16b3b57a5bf7ef034a0f5dcae1ca193685b8bfee33868a3a
MKS=${MKS5LBOOT:-}

if [ -z "$MKS" ]; then
    for candidate in \
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
    echo "Set MKS5LBOOT=/path/to/mks5lboot." >&2
    exit 1
fi

if command -v shasum >/dev/null 2>&1; then
    ACTUAL=$(shasum -a 256 "$DFU" | awk '{print $1}')
else
    ACTUAL=$(sha256sum "$DFU" | awk '{print $1}')
fi
if [ "$ACTUAL" != "$EXPECTED" ]; then
    echo "error: uninstaller hash mismatch" >&2
    exit 1
fi

echo "Uninstaller: $DFU"
printf "Put the iPod in DFU, then press Enter to remove the bootloader: "
read -r _ || true
"$MKS" --dfuscan
"$MKS" --dfusend "$DFU"
echo "Uninstall sent."
