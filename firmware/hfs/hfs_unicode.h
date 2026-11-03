/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_UNICODE_H
#define HELIKON_HFS_UNICODE_H

#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Decode an HFS+ UTF-16BE name into UTF-8. This is an exact-byte decode, not a
 * case-folding or normalization policy. Invalid surrogates and NUL are rejected.
 */
int hfs_utf16be_to_utf8(const uint8_t *source, size_t units, char *dest,
                        size_t dest_size);

/*
 * Lossy decode for display of an existing on-disk name that contains NUL or
 * unpaired surrogates (macOS private names). NUL becomes U+2400 and invalid
 * surrogates become U+FFFD. Never returns HFS_ERR_NAME_INVALID for the name
 * content; only buffer/argument errors.
 */
int hfs_utf16be_to_utf8_escaped(const uint8_t *source, size_t units,
                                char *dest, size_t dest_size);

/*
 * Apple's HFS+ FastUnicodeCompare over UTF-16BE code units. It is
 * case-insensitive and ignores the format controls and U+FEFF listed in the
 * generated table. It does not decompose names; callers must store HFS+
 * canonical (decomposed) names before relying on composed input.
 */
int hfs_fast_unicode_compare(const uint8_t *a, size_t a_units,
                             const uint8_t *b, size_t b_units);

/* Generated case-folding table; see tools/gen-hfs-unicode-table.py. */
extern const uint16_t hfs_unicode_fold_table[];

/* One fully expanded canonical decomposition (HFS+ exception ranges excluded). */
struct hfs_unicode_decomposition {
    uint16_t code_point;
    uint8_t length;
    uint16_t units[4];
};

/* Canonical combining class range for canonical ordering. */
struct hfs_unicode_ccc_range {
    uint16_t first;
    uint16_t last;
    uint8_t ccc;
};

extern const struct hfs_unicode_decomposition hfs_unicode_decompositions[];
extern const size_t hfs_unicode_decomposition_count;
extern const struct hfs_unicode_ccc_range hfs_unicode_ccc_ranges[];
extern const size_t hfs_unicode_ccc_range_count;

/*
 * Convert a UTF-16BE name to the HFS+ canonical form: fully decomposed
 * (Unicode 2.1.9 canonical decompositions, HFS+ exception ranges left
 * composed, Hangul decomposed algorithmically) and canonically ordered.
 * Rejects NUL, unpaired surrogates, names with no non-ignorable unit, and
 * results over 255 units. `dest_capacity` is in bytes.
 */
int hfs_unicode_normalize_name(const uint8_t *name, size_t units,
                               uint8_t *dest, size_t dest_capacity,
                               size_t *dest_units);

#endif
