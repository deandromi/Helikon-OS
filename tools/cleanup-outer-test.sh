#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Safely inspect or remove a Helikon-OS outer test folder left by an interrupted
# run. The folder must contain the exact marker and only the known test files.
# Run without --yes for a dry run.

set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 /Volumes/<iPod> [--yes]" >&2
    exit 2
fi

VOL=$1
YES=${2:-}
DIR="$VOL/helikon-outer-test"

if [ ! -d "$DIR" ]; then
    echo "no $DIR"
    exit 0
fi

fail() {
    echo "refusing: $*" >&2
    exit 1
}

echo "contents of $DIR:"
for path in "$DIR"/* "$DIR"/.[!.]* "$DIR"/..?*; do
    [ -e "$path" ] || continue
    [ -L "$path" ] && fail "symlink: $path"
    [ -f "$path" ] || fail "not a regular file: $path"
    printf '  %s (%s bytes)\n' "$(basename "$path")" "$(stat -f%z "$path")"
done

[ -f "$DIR/marker" ] || fail "missing marker; this may be user data"
[ "$(cat "$DIR/marker")" = "HELIKON-OUTER-TEST" ] ||
    fail "marker content does not match HELIKON-OUTER-TEST"

for path in "$DIR"/* "$DIR"/.[!.]* "$DIR"/..?*; do
    [ -e "$path" ] || continue
    case "$(basename "$path")" in
        marker|test|renamed) ;;
        *) fail "unexpected entry: $path" ;;
    esac
done

if [ "$YES" != "--yes" ]; then
    echo
    echo "Dry run only. This is the Helikon test folder."
    echo "Re-run with --yes to remove marker/test/renamed and the folder."
    exit 0
fi

rm -f "$DIR/marker" "$DIR/test" "$DIR/renamed"
rmdir "$DIR"
sync
echo "removed $DIR"
