#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Send the prebuilt Helikon-OS write-diagnostic DFU installer to a 6G in DFU.
# This never uses --single. The inner test image must already be on the iPod.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DFU="$ROOT/bootloader/artifacts/bootloader-ipod6g-write-diag.dfu"
EXPECTED=b7385057e10d50a750afbb229543a1969862d89b4b82e174f7308efed00ab513
MKS=${MKS5LBOOT:-}

if [ ! -f "$DFU" ]; then
    echo "error: missing $DFU" >&2
    exit 1
fi

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
echo "  1. helikon-test.img is at the iPod root (16 MiB, HFS+J)."
echo "  2. The iPod was ejected cleanly and is now in DFU mode."
echo "  3. No '--single' option is used."
echo
printf "Press Enter to send the installer, or Control-C to stop: "
read -r _ || true
echo

"$MKS" --dfuscan
echo
"$MKS" --dfusend "$DFU"
echo
echo "Sent. The iPod should reboot into HELIKON-OS WRITE DIAG."
echo "Recovery: tools/uninstall-bootloader.sh"
