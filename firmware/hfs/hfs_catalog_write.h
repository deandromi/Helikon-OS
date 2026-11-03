/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_CATALOG_WRITE_H
#define HELIKON_HFS_CATALOG_WRITE_H

#include "hfs_btree_tree.h"
#include "hfs_catalog.h"
#include "hfs_txn.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal native catalog create/delete for an empty file.
 *
 * `name` is UTF-16BE code units, `units` is 1..255. The caller supplies the
 * catalog key comparator; this module does not implement FastUnicodeCompare.
 * The parent folder's valence is not updated yet, and data forks are empty.
 */
int hfs_catalog_create_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint16_t mode,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context, uint32_t *cnid);

int hfs_catalog_delete_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint32_t cnid,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context);

/* Append to an empty file using one inline extent. */
int hfs_catalog_append_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint64_t offset,
                            const void *data, size_t length,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context);

/* Truncate a file with one inline extent to size zero. */
int hfs_catalog_truncate_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                              uint32_t parent, const uint8_t *name,
                              size_t units,
                              hfs_btree_key_compare_fn compare,
                              void *compare_context);

#endif
