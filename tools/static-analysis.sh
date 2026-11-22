#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Static analysis over the firmware and bootloader sources.
#
#   ./tools/static-analysis.sh            # report, exit 0 unless --strict
#   ./tools/static-analysis.sh --strict   # exit 1 on the first finding
#   ./tools/static-analysis.sh hfs        # only sources whose path matches `hfs`
#
# Each translation unit goes through `clang --analyze`: the path-sensitive
# checker, which is the one that finds a null dereference on the fifth branch and
# a leak on an error path. This is not a linter; the compilers already run with
# -Wall -Wextra -Werror -Wconversion -Wshadow in every host target.
#
# Firmware sources are analysed the way the device builds them: freestanding,
# `-nostdinc`, and the compiler's own `stddef.h`/`stdint.h`. That matters, because
# the firmware's `string.h` shim declares the `mem*`/`str*` names only in a
# freestanding build — analysing it as hosted code finds a dozen "undeclared
# function" errors that the real build does not have. `bootloader/` is analysed
# hosted, which is how its host tests compile it.
#
# Vendored sources (libmad) and the donor-facing hardware glue are skipped: the
# first is not ours to change, the second is register poking whose values only a
# device can confirm.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

strict=no
filter=""
for arg in "$@"; do
    case "$arg" in
    --strict) strict=yes ;;
    -*) echo "static-analysis: unknown option $arg" >&2; exit 2 ;;
    *) filter=$arg ;;
    esac
done

if ! command -v clang >/dev/null 2>&1; then
    echo "static-analysis: clang is required" >&2
    exit 2
fi

OUT="$ROOT/build/static-analysis"
rm -rf "$OUT"
mkdir -p "$OUT"

# Freestanding: the firmware's own headers first, then the compiler's builtin
# ones for stddef.h/stdint.h. No libc.
FREESTANDING="-ffreestanding -nostdinc -isystem $(clang -print-file-name=include)"
INCLUDES="-Ifirmware/lib -Ifirmware -Ifirmware/hfs -Ifirmware/block \
-Ifirmware/boot -Ifirmware/diag -Ifirmware/db -Ifirmware/vfs -Ifirmware/player \
-Ifirmware/ui -Ifirmware/codec -Ifirmware/codec/flac -Ifirmware/codec/mp3 \
-Ifirmware/usb -Ifirmware/hal/6g -Ifirmware/board/6g -Ifirmware/library \
-Ifbootloader"

# One line per tree: the globs to analyse.
sources() {
    find firmware/hfs firmware/block firmware/boot firmware/diag firmware/db \
        firmware/vfs firmware/player firmware/ui firmware/usb firmware/codec \
        firmware/library bootloader -name '*.c' \
        ! -path 'firmware/codec/libmad/*' \
        ! -path 'bootloader/hardware/*' 2>/dev/null | sort
}

count=0
findings=0
status=0

skipped=""

for source in $(sources); do
    case "$source" in
    *"$filter"*) ;;
    *) continue ;;
    esac
    flags=$FREESTANDING
    case "$source" in
    # Donor-facing: it includes a `config.h` that only exists in the donor tree.
    bootloader/splash_color.c)
        skipped="$skipped $source"
        continue
        ;;
    # The bootloader glue is hosted in its own tests, so it is analysed that way:
    # with libc and without the firmware's freestanding shim.
    bootloader/*) flags="" ;;
    esac
    count=$((count + 1))
    log="$OUT/$(echo "$source" | tr '/' '_').log"
    # shellcheck disable=SC2086
    clang --analyze -Xanalyzer -analyzer-output=text \
        -std=c99 $flags $INCLUDES \
        -Wno-unused-function "$source" >"$log" 2>&1 || true
    # `clang --analyze` exits 0 whether or not it found something, so the log is
    # the signal. A compile error counts too: a file the analyzer cannot parse is
    # a file nothing checked.
    if grep -q ' warning: \| error: ' "$log"; then
        findings=$((findings + 1))
        printf 'FINDING %s\n' "$source"
        sed 's/^/        /' "$log" | head -20
        [ "$strict" = yes ] && status=1
    fi
done

if [ "$count" = 0 ]; then
    echo "static-analysis: no source matched '${filter}'" >&2
    exit 2
fi

printf 'static-analysis: %d translation unit(s), %d with findings\n' \
    "$count" "$findings"
[ -n "$skipped" ] && printf 'static-analysis: skipped (donor-facing):%s\n' \
    "$skipped"
[ "$findings" != 0 ] && printf 'static-analysis: logs in build/static-analysis/\n'
exit "$status"
