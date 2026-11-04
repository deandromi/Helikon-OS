#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Create the opt-in file for the outer-volume write diagnostic. Run this only
# after the full backup has been verified.

set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /Volumes/<iPod>" >&2
    exit 2
fi

VOL=$1
if [ ! -d "$VOL" ]; then
    echo "error: $VOL is not a mounted directory" >&2
    exit 1
fi
if [ ! -f "$VOL/.rockpod-rw" ] && [ ! -d "$VOL/.rockbox" ]; then
    echo "warning: neither .rockpod-rw nor .rockbox was found on $VOL" >&2
    echo "         check that this is the iPod volume" >&2
fi

printf 'HELIKON-OUTER-WRITE-OK\n' > "$VOL/helikon-outer-ok.txt"
sync
echo "opt-in: $VOL/helikon-outer-ok.txt"
echo "Remove it after the test if you do not want to allow another run."
