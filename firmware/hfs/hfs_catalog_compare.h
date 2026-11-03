/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_CATALOG_COMPARE_H
#define HELIKON_HFS_CATALOG_COMPARE_H

#include "hfs_btree_tree.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Compare two HFS+ catalog keys as passed to the B-tree (parent ID, name
 * length in UTF-16 units, then the UTF-16BE name). Parent IDs order first;
 * names use Apple's FastUnicodeCompare.
 */
int hfs_catalog_key_compare(void *context, const uint8_t *a, size_t a_length,
                            const uint8_t *b, size_t b_length);

/* Compare two UTF-16BE names using Apple's FastUnicodeCompare. */
int hfs_catalog_name_compare(const uint8_t *a, size_t a_units,
                             const uint8_t *b, size_t b_units);

#endif
