/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_CATALOG_WRITE_H
#define HELIKON_HFS_CATALOG_WRITE_H

#include "hfs_btree_tree.h"
#include "hfs_catalog.h"
#include "hfs_txn.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Native catalog create/delete for a file.
 *
 * `name` is UTF-16BE code units, `units` is 1..255, and is normalized to the
 * HFS+ canonical decomposed form before use; composed and decomposed inputs
 * address the same catalog key. The caller supplies the catalog key
 * comparator, normally hfs_catalog_key_compare(). The parent folder's valence,
 * file count and next CNID are updated through the transaction.
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

/*
 * Append at EOF using inline extents plus extents-overflow records. Data blocks
 * are staged before metadata; the transaction block budget bounds one call.
 */
int hfs_catalog_append_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint64_t offset,
                            const void *data, size_t length,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context);

/* Truncate a file to size zero and free its inline and overflow extents. */
int hfs_catalog_truncate_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                              uint32_t parent, const uint8_t *name,
                              size_t units,
                              hfs_btree_key_compare_fn compare,
                              void *compare_context);

#endif
