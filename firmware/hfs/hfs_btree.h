/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_BTREE_H
#define HELIKON_HFS_BTREE_H

#include "hfs_volume.h"

struct hfs_btree {
    struct hfs_volume *volume;
    const struct hfs_fork *fork;
    uint8_t *scratch;
    size_t scratch_size;
    uint16_t node_size;
    uint16_t depth;
    uint16_t max_key_length;
    uint32_t root_node;
    uint32_t total_nodes;
    uint32_t free_nodes;
    uint32_t leaf_records;
    uint32_t first_leaf;
    uint32_t last_leaf;
    uint8_t btree_type;
    uint8_t key_compare;
};

/*
 * Open an inline HFS+ B-tree. The caller owns the scratch buffer and must keep
 * both the volume and fork alive for the lifetime of the handle.
 */
int hfs_btree_open(struct hfs_btree *btree, struct hfs_volume *volume,
                   const struct hfs_fork *fork, void *scratch,
                   size_t scratch_size);

int hfs_btree_read_node(struct hfs_btree *btree, uint32_t node,
                        const uint8_t **node_data);

/*
 * Validate a node's offset table and return one record. This is the shared
 * structural check used by the header and by catalog/leaf iteration.
 */
int hfs_btree_node_record(const uint8_t *node, uint16_t node_size,
                          uint16_t index, const uint8_t **record,
                          size_t *length);

#endif
