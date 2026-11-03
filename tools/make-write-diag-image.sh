#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Create the disposable inner HFS+ image used by the separate write
# diagnostic. Copy the result to the iPod root as helikon-test.img.
#
# This script never writes an iPod. It only creates a host file.

set -eu

if [ "$(uname -s)" != Darwin ]; then
    echo "error: this script needs macOS hdiutil" >&2
    exit 1
fi

OUT=${1:-helikon-test.img}
SIZE=${SIZE:-16m}

if [ -e "$OUT" ]; then
    echo "error: $OUT already exists; remove it or choose another path" >&2
    exit 1
fi

hdiutil create -size "$SIZE" -fs HFS+J -volname HelikonInner -layout NONE \
    "$OUT" >/dev/null

echo "created $OUT ($SIZE, journaled HFS+)"
echo "copy it to the iPod root as helikon-test.img, then eject cleanly."
echo "the iPod volume must have at least $SIZE free and must not be dirty."
