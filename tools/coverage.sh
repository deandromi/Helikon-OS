#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Line coverage for the host suites.
#
#   ./tools/coverage.sh
#
# Builds every host suite with clang's source-based coverage, runs it, merges the
# profiles and reports how much of `firmware/` and `bootloader/` the tests
# actually execute. This is a map of where the untested code is, not a gate: a
# number below 100 is normal, and the interesting part is the list of files with
# most of their lines never reached, because that is where a reader's bugs live.
#
# Needs clang plus llvm-cov and llvm-profdata (Xcode command line tools). Writes
# `build/coverage/{lcov.info,coverage.txt}` and one log per suite. The suites are
# left built with coverage instrumentation, so run `make -C tests/<suite> clean`
# before the next normal build.
#
# No source in the repository is touched and nothing here runs on a device.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/coverage"
# `library` needs an HFS+ image, so it joins only when one is supplied:
#   COVERAGE_IMAGE=/tmp/vol.img ./tools/coverage.sh
SUITES="hfsplus firmware bootloader vfs codec musicdb player ui usb image"
[ -n "${COVERAGE_IMAGE:-}" ] && SUITES="$SUITES library"
CLANG="clang -fprofile-instr-generate -fcoverage-mapping"

LLVM_COV=$(xcrun -f llvm-cov 2>/dev/null || true)
LLVM_PROFDATA=$(xcrun -f llvm-profdata 2>/dev/null || true)
if [ -z "$LLVM_COV" ] || [ -z "$LLVM_PROFDATA" ]; then
    echo "coverage: llvm-cov and llvm-profdata are required" >&2
    echo "coverage: install the Xcode command line tools (xcode-select --install)" >&2
    exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

for suite in $SUITES; do
    dir="$ROOT/tests/$suite"
    [ -d "$dir" ] || continue
    printf '== %s\n' "$suite"
    make -C "$dir" clean >/dev/null 2>&1 || true
    extra=""
    [ "$suite" = "library" ] && extra="IMAGE=$COVERAGE_IMAGE"
    if ! LLVM_PROFILE_FILE="$OUT/$suite-%p.profraw" \
        make -C "$dir" test CC="$CLANG" HOSTCC="$CLANG" $extra \
        >"$OUT/$suite.log" 2>&1; then
        echo "coverage: $suite failed; see $OUT/$suite.log" >&2
        tail -5 "$OUT/$suite.log" >&2
        exit 1
    fi
done

# Every executable a suite left behind is an instrumented binary. The suites
# build their tests in place, so the ones that are not sources are the ones that
# carry the counters. Both CC and HOSTCC carry the flags: tests/firmware builds
# its objects with HOSTCC and would otherwise link uninstrumented code.
: > "$OUT/binaries.txt"
for suite in $SUITES; do
    dir="$ROOT/tests/$suite"
    [ -d "$dir" ] || continue
    find "$dir" -maxdepth 1 -type f -perm -u+x \
        ! -name '*.c' ! -name '*.h' ! -name '*.sh' ! -name '*.py' \
        ! -name 'Makefile' >> "$OUT/binaries.txt"
done
if [ ! -s "$OUT/binaries.txt" ]; then
    echo "coverage: no instrumented binaries found" >&2
    exit 2
fi

printf '== merging %u profile(s)\n' "$(ls "$OUT"/*.profraw | wc -l | tr -d ' ')"
# shellcheck disable=SC2086
"$LLVM_PROFDATA" merge -sparse "$OUT"/*.profraw -o "$OUT/all.profdata"

: > "$OUT/lcov.info"
while read -r binary; do
    "$LLVM_COV" export -format=lcov "$binary" -instr-profile="$OUT/all.profdata" \
        >> "$OUT/lcov.info" 2>/dev/null || true
done < "$OUT/binaries.txt"

python3 - "$OUT/lcov.info" "$ROOT" > "$OUT/coverage.txt" <<'PY'
import sys

lcov_path, root = sys.argv[1], sys.argv[2]
hits = {}          # source -> {line: max count}
for line in open(lcov_path, encoding='utf-8', errors='replace'):
    line = line.strip()
    if line.startswith('SF:'):
        source = line[3:]
    elif line.startswith('DA:'):
        fields = line[3:].split(',')
        if len(fields) < 2:
            continue
        number, count = int(fields[0]), int(fields[1])
        per_line = hits.setdefault(source, {})
        # A line covered by any one suite counts: the report is about whether
        # something reaches the code at all.
        if count > per_line.get(number, 0):
            per_line[number] = count
        elif number not in per_line:
            per_line[number] = 0

rows = []
for source, per_line in hits.items():
    name = source.replace(root + '/', '')
    # Only the shipped code: the suites' own test files are not the subject, and
    # `tests/bootloader/` would otherwise pass for `bootloader/`.
    if not (name.startswith('firmware/') or name.startswith('bootloader/')):
        continue
    total = len(per_line)
    if total == 0:
        # A source credited to the binary with no line records at all.
        continue
    covered = sum(1 for count in per_line.values() if count > 0)
    rows.append((total - covered, name, total, covered))

rows.sort(reverse=True)
print('%6s %6s %7s  %s' % ('lines', 'hit', 'cover', 'file'))
print('%6s %6s %7s  %s' % ('-' * 6, '-' * 6, '-' * 7, '-' * 40))
for missed, name, total, covered in rows:
    print('%6d %6d %6.1f%%  %s' % (total, covered, 100.0 * covered / total, name))

total = sum(row[2] for row in rows)
covered = sum(row[3] for row in rows)
print()
print('total: %d lines, %d hit, %.1f%% covered' %
      (total, covered, 100.0 * covered / total if total else 0.0))
print('%d lines in firmware/ and bootloader/ are never executed by any host suite' %
      (total - covered))
PY

cat "$OUT/coverage.txt"
