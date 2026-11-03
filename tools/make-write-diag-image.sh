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

if [ -e "$OUT" ] || [ -e "$OUT.dmg" ]; then
    echo "error: $OUT or $OUT.dmg already exists; remove it or choose another path" >&2
    exit 1
fi

# hdiutil appends .dmg when the requested name does not end in .dmg. Create
# under a .dmg name and rename so the host file has the exact name required on
# the iPod.
TMP="$OUT.dmg"
case "$OUT" in
    *.dmg) TMP="$OUT" ;;
esac
hdiutil create -size "$SIZE" -fs HFS+J -volname HelikonInner -layout NONE \
    "$TMP" >/dev/null
if [ "$TMP" != "$OUT" ]; then
    mv "$TMP" "$OUT"
fi

echo "created $OUT ($SIZE, journaled HFS+)"
echo "copy it to the iPod root as helikon-test.img, then eject cleanly."
echo "the iPod volume must have at least $SIZE free and must not be dirty."
