#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the HFS+ FastUnicodeCompare and normalization tables.

The HFS+ on-disk catalog is ordered with Apple's FastUnicodeCompare. Its
folding table is a snapshot of Unicode 2.1.9 lower-case mappings with three
HFS+ rules:

* characters with a canonical decomposition are folded only after
  decomposition, so the precomposed form is left unchanged;
* a small set of characters that Apple's published table does not fold is
  left unchanged (see NO_FOLD below);
* direction/format controls in the ranges below and U+FEFF are ignorable and
  fold to zero. U+0000 folds to U+FFFF so it cannot be confused with the
  end-of-string sentinel.

The second and third tables implement the HFS+ normalization variant: fully
expanded canonical decompositions (except U+2000-U+2FFF, U+F900-U+FAFF and
U+2F800-U+2FAFF, which HFS+ leaves composed) and canonical combining classes
for reordering combining marks. Hangul syllables are decomposed
algorithmically by the runtime, not listed here.

The tables are generated from UnicodeData-2.1.9.txt, published by the Unicode
Consortium under the Unicode License. Regenerate with:

    tools/gen-hfs-unicode-table.py \
        --unicode-data /path/to/UnicodeData-2.1.9.txt \
        --output firmware/hfs/hfs_unicode_data.c
"""

import argparse
import hashlib
import pathlib
import sys

UNICODE_DATA_SHA256 = (
    "3dcee8b6b68151956fb799e4445c4e8948c0f4257d241b193ec3881b08c48137"
)

# Characters present in UnicodeData 2.1.9 that Apple's table leaves unfolded.
NO_FOLD = {
    0x01A6,  # LATIN LETTER YR
    0x04D4,  # CYRILLIC CAPITAL LETTER A WITH BREVE
    0x04D8,  # CYRILLIC CAPITAL LETTER SCHWA
    0x04E0,  # CYRILLIC CAPITAL LETTER ABKHASIAN DZE
    0x04E8,  # CYRILLIC CAPITAL LETTER BARRED O
}
NO_FOLD.update(range(0x24B6, 0x24D0))  # circled Latin capitals

IGNORED = (
    set(range(0x200C, 0x2010))
    | set(range(0x202A, 0x202F))
    | set(range(0x206A, 0x2070))
    | {0xFEFF}
)

# HFS+ leaves these ranges composed even when Unicode gives them a canonical
# decomposition (TN1150).
HFS_EXCLUDED = (
    (0x2000, 0x2FFF),
    (0xF900, 0xFAFF),
    (0x2F800, 0x2FAFF),
)

HANGUL_FIRST = 0xAC00
HANGUL_LAST = 0xD7A3
HANGUL_SBASE = 0xAC00
HANGUL_LBASE = 0x1100
HANGUL_VBASE = 0x1161
HANGUL_TBASE = 0x11A7
HANGUL_VCOUNT = 21
HANGUL_TCOUNT = 28
HANGUL_NCOUNT = HANGUL_VCOUNT * HANGUL_TCOUNT


def is_excluded(code_point):
    return any(first <= code_point <= last
               for first, last in HFS_EXCLUDED)


def load_unicode_data(path):
    raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != UNICODE_DATA_SHA256:
        sys.exit(
            f"{path}: SHA-256 {digest} does not match the pinned "
            f"UnicodeData-2.1.9.txt ({UNICODE_DATA_SHA256})"
        )

    lower = {}
    canonical = set()
    decompositions = {}
    combining = {}
    for line in raw.decode("utf-8").splitlines():
        fields = line.split(";")
        if len(fields) < 15:
            continue
        code_point = int(fields[0], 16)
        if fields[3] and int(fields[3]) != 0:
            combining[code_point] = int(fields[3])
        if fields[13]:
            lower[code_point] = int(fields[13], 16)
        decomposition = fields[5]
        if not decomposition or decomposition.startswith("<"):
            continue
        canonical.add(code_point)
        decompositions[code_point] = [int(part, 16)
                                      for part in decomposition.split()]
    return lower, canonical, decompositions, combining


def build_fold(lower, canonical):
    fold = list(range(0x10000))
    fold[0] = 0xFFFF
    for code_point, replacement in lower.items():
        if code_point in canonical or code_point in NO_FOLD:
            continue
        fold[code_point] = replacement
    for code_point in IGNORED:
        fold[code_point] = 0
    return fold


def compact(fold):
    high = [0] * 256
    subtables = []
    for high_byte in range(256):
        block = fold[high_byte * 256:(high_byte + 1) * 256]
        if all(value == high_byte * 256 + index
               for index, value in enumerate(block)):
            continue
        high[high_byte] = (len(subtables) + 1) * 256
        subtables.append(block)
    return high, subtables


def hangul_decomposition(code_point):
    index = code_point - HANGUL_SBASE
    lead = HANGUL_LBASE + index // HANGUL_NCOUNT
    vowel = HANGUL_VBASE + (index % HANGUL_NCOUNT) // HANGUL_TCOUNT
    trail = HANGUL_TBASE + index % HANGUL_TCOUNT
    result = [lead, vowel]
    if trail != HANGUL_TBASE:
        result.append(trail)
    return result


def expand(code_point, decompositions, stack=()):
    if code_point in stack:
        sys.exit(f"decomposition cycle at U+{code_point:04X}")
    if HANGUL_FIRST <= code_point <= HANGUL_LAST:
        return hangul_decomposition(code_point)
    if (code_point in decompositions and not is_excluded(code_point)):
        result = []
        for part in decompositions[code_point]:
            result.extend(expand(part, decompositions, stack + (code_point,)))
        return result
    return [code_point]


def build_decompositions(decompositions):
    table = []
    for code_point in sorted(decompositions):
        if is_excluded(code_point):
            continue
        if HANGUL_FIRST <= code_point <= HANGUL_LAST:
            continue
        units = expand(code_point, decompositions)
        if len(units) > 4:
            sys.exit(f"decomposition too long at U+{code_point:04X}: {units}")
        table.append((code_point, units))
    return table


def build_ccc_ranges(combining):
    ranges = []
    for code_point in sorted(combining):
        value = combining[code_point]
        if ranges and code_point == ranges[-1][1] + 1 and value == ranges[-1][2]:
            ranges[-1][1] = code_point
        else:
            ranges.append([code_point, code_point, value])
    return ranges


def emit(high, subtables, decompositions, ccc_ranges, output):
    fold = high + [value for block in subtables for value in block]
    lines = [
        "/* SPDX-License-Identifier: GPL-2.0-or-later */",
        "/*",
        " * Generated by tools/gen-hfs-unicode-table.py. Do not edit by hand.",
        " *",
        " * Case-folding and normalization data for HFS+, generated from",
        " * UnicodeData-2.1.9.txt (Unicode Consortium, Unicode License).",
        " * Fold table: the first 256 entries are high-byte offsets in uint16",
        " * units; a zero offset means the block folds to itself. Subtables map",
        " * the low byte to its folded value, with zero marking an ignorable.",
        " * Decompositions are fully expanded except for the HFS+ exception",
        " * ranges; Hangul syllables are handled algorithmically at runtime.",
        " */",
        "#include \"hfs_unicode.h\"",
        "",
        "const uint16_t hfs_unicode_fold_table[%d] = {" % len(fold),
    ]
    for index in range(0, len(fold), 8):
        chunk = fold[index:index + 8]
        lines.append("    " + ", ".join("0x%04x" % value for value in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("const struct hfs_unicode_decomposition "
                 "hfs_unicode_decompositions[%d] = {" % len(decompositions))
    for code_point, units in decompositions:
        values = ", ".join("0x%04x" % unit for unit in units)
        lines.append("    { 0x%04x, %d, { %s } }," %
                     (code_point, len(units), values))
    lines.append("};")
    lines.append("")
    lines.append("const size_t hfs_unicode_decomposition_count = %d;"
                 % len(decompositions))
    lines.append("")
    lines.append("const struct hfs_unicode_ccc_range "
                 "hfs_unicode_ccc_ranges[%d] = {" % len(ccc_ranges))
    for first, last, value in ccc_ranges:
        lines.append("    { 0x%04x, 0x%04x, %d }," % (first, last, value))
    lines.append("};")
    lines.append("")
    lines.append("const size_t hfs_unicode_ccc_range_count = %d;"
                 % len(ccc_ranges))
    lines.append("")
    output.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--unicode-data", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    lower, canonical, raw_decompositions, combining = load_unicode_data(
        args.unicode_data)
    fold = build_fold(lower, canonical)
    high, subtables = compact(fold)
    decompositions = build_decompositions(raw_decompositions)
    ccc_ranges = build_ccc_ranges(combining)
    emit(high, subtables, decompositions, ccc_ranges, args.output)
    print(f"wrote {args.output} ({256 + 256 * len(subtables)} fold entries, "
          f"{len(decompositions)} decompositions, "
          f"{len(ccc_ranges)} CCC ranges)")


if __name__ == "__main__":
    main()
