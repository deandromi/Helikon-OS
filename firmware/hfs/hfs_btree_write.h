/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_BTREE_WRITE_H
#define HELIKON_HFS_BTREE_WRITE_H

#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Node-level B-tree mutation. These helpers insert or delete one record in an
 * already-validated node and maintain the reverse offset table. Tree-level
 * split/merge, free-node allocation and parent updates are separate.
 *
 * Records must have an even length (HFS+ keys and records are 16-bit aligned).
 */
int hfs_btree_node_free_space(const uint8_t *node, uint16_t node_size,
                              size_t *free_bytes);

int hfs_btree_node_insert(uint8_t *node, uint16_t node_size, uint16_t index,
                          const void *record, size_t length);

int hfs_btree_node_delete(uint8_t *node, uint16_t node_size, uint16_t index);

/*
 * Split a node at `split_index` (0 < split_index < record count). The left
 * node keeps [0, split_index), the right node gets [split_index, count).
 * The right node receives the left node's fLink and a bLink to `left_node`;
 * the left node's fLink is set to `right_node`. The separator key points into
 * the right node and is valid until the right buffer is overwritten.
 */
int hfs_btree_node_split(uint8_t *left, uint8_t *right, uint16_t node_size,
                         uint16_t split_index, uint32_t left_node,
                         uint32_t right_node, const uint8_t **separator,
                         size_t *separator_length);

#endif
