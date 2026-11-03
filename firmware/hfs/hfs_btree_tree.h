/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_BTREE_TREE_H
#define HELIKON_HFS_BTREE_TREE_H

#include "hfs_btree.h"
#include "hfs_txn.h"

#include <stddef.h>
#include <stdint.h>

typedef int (*hfs_btree_key_compare_fn)(void *context,
                                        const uint8_t *a, size_t a_length,
                                        const uint8_t *b, size_t b_length);

/*
 * Insert one record into a B-tree. The record is [key_length(2)][key][data].
 * New nodes come from the header node's free-node map; the catalog fork must
 * already contain those nodes (fork growth is not implemented). All modified
 * nodes and the header node are staged into `tx`.
 */
int hfs_btree_insert(struct hfs_btree *btree, struct hfs_txn *tx,
                     const void *record, size_t length,
                     hfs_btree_key_compare_fn compare, void *compare_context);

/*
 * Delete one record by key. Removing the first record of a multi-record
 * non-root node returns HFS_ERR_UNSUPPORTED because the parent separator would
 * need a key update that is not implemented yet. An empty non-root leaf is
 * unlinked and freed; empty parents/root collapse are not implemented.
 */
int hfs_btree_delete(struct hfs_btree *btree, struct hfs_txn *tx,
                     const uint8_t *key, size_t key_length,
                     hfs_btree_key_compare_fn compare, void *compare_context);

#endif
