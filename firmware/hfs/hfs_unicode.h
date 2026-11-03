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
 * Apple's HFS+ FastUnicodeCompare over UTF-16BE code units. It is
 * case-insensitive and ignores the format controls and U+FEFF listed in the
 * generated table. It does not decompose names; callers must store HFS+
 * canonical (decomposed) names before relying on composed input.
 */
int hfs_fast_unicode_compare(const uint8_t *a, size_t a_units,
                             const uint8_t *b, size_t b_units);

/* Generated case-folding table; see tools/gen-hfs-unicode-table.py. */
extern const uint16_t hfs_unicode_fold_table[];

/* Generated ranges of code points with canonical decompositions. */
extern const uint16_t hfs_unicode_decomposed_ranges[][2];
extern const size_t hfs_unicode_decomposed_range_count;

/*
 * Check that a name is safe to insert before the HFS+ decomposer exists:
 * non-empty, at most 255 UTF-16 units, no NUL, no unpaired surrogates, at
 * least one non-ignorable unit, and no code point that needs canonical
 * decomposition. Returns HFS_OK or HFS_ERR_NAME_INVALID.
 */
int hfs_unicode_name_is_canonical(const uint8_t *name, size_t units);

#endif
