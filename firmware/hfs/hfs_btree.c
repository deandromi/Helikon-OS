/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic HFS+ B-tree header and node validation.
 *
 * Derived from the read-only HFS+ parser in HFSPlus Rockboxed iPod
 * (firmware/hfsplus/hfsplus_ro.c, GPL-2.0-or-later). Catalog-specific rules
 * (key compare type, record shapes) belong in the catalog module, not here.
 */
#include "hfs_btree.h"

#include <string.h>

#define HFS_BTREE_NODE_DESCRIPTOR_SIZE 14u
#define HFS_BTREE_HEADER_RECORDS 3u
#define HFS_BTREE_HEADER_RECORD_SIZE 106u
#define HFS_BTREE_MAX_DEPTH 32u
#define HFS_BTREE_MAX_KEY_SLACK 4u

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int power2(uint32_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

static int is_header_node(const uint8_t *node, uint16_t node_size)
{
    if (node_size < HFS_BTREE_NODE_DESCRIPTOR_SIZE + 2u)
        return HFS_ERR_FORMAT;
    if (node[8] != 1 || node[9] != 0)
        return HFS_ERR_FORMAT;
    if (be32(node) != 0 || be32(node + 4) != 0)
        return HFS_ERR_FORMAT;
    if (be16(node + 10) != HFS_BTREE_HEADER_RECORDS)
        return HFS_ERR_FORMAT;
    return HFS_OK;
}

int hfs_btree_node_record(const uint8_t *node, uint16_t node_size,
                          uint16_t index, const uint8_t **record,
                          size_t *length)
{
    if (node == NULL || record == NULL || length == NULL ||
        node_size < HFS_BTREE_NODE_DESCRIPTOR_SIZE + 2u)
        return HFS_ERR_ARGUMENT;

    uint16_t count = be16(node + 10);
    if (count > (node_size - HFS_BTREE_NODE_DESCRIPTOR_SIZE) / 2u - 1u)
        return HFS_ERR_FORMAT;

    unsigned int table = node_size - 2u * (count + 1u);
    unsigned int previous = HFS_BTREE_NODE_DESCRIPTOR_SIZE;
    for (unsigned int i = 0; i <= count; ++i) {
        unsigned int offset = be16(node + node_size - 2u * (i + 1u));
        if (offset < previous || offset > table || offset % 2 != 0 ||
            (i == 0 && offset != HFS_BTREE_NODE_DESCRIPTOR_SIZE) ||
            (i != 0 && offset == previous))
            return HFS_ERR_FORMAT;
        previous = offset;
    }

    if (index >= count)
        return HFS_ERR_FORMAT;

    unsigned int begin = be16(node + node_size - 2u * (index + 1u));
    unsigned int end = be16(node + node_size - 2u * (index + 2u));
    *record = node + begin;
    *length = end - begin;
    return HFS_OK;
}

int hfs_btree_open(struct hfs_btree *btree, struct hfs_volume *volume,
                   const struct hfs_fork *fork, void *scratch,
                   size_t scratch_size)
{
    uint8_t prefix[120];
    const uint8_t *header = NULL;
    size_t header_length = 0;

    if (btree == NULL || volume == NULL || fork == NULL || scratch == NULL)
        return HFS_ERR_ARGUMENT;
    if (fork->size < 120)
        return HFS_ERR_FORMAT;

    memset(btree, 0, sizeof(*btree));
    int rc = hfs_fork_read(volume, fork, 0, prefix, sizeof(prefix));
    if (rc != HFS_OK)
        return rc;

    uint16_t node_size = be16(prefix + 32);
    if (!power2(node_size) || node_size < 512 ||
        node_size > HFS_MAX_NODE_SIZE)
        return HFS_ERR_FORMAT;
    if (node_size > scratch_size)
        return HFS_ERR_BUFFER;

    rc = hfs_fork_read(volume, fork, 0, scratch, node_size);
    if (rc != HFS_OK)
        return rc;

    rc = is_header_node(scratch, node_size);
    if (rc != HFS_OK)
        return rc;

    rc = hfs_btree_node_record(scratch, node_size, 0, &header,
                               &header_length);
    if (rc != HFS_OK)
        return rc;
    if (header_length < HFS_BTREE_HEADER_RECORD_SIZE)
        return HFS_ERR_FORMAT;

    uint16_t depth = be16(header);
    uint32_t root = be32(header + 2);
    uint32_t leaf_records = be32(header + 6);
    uint32_t first_leaf = be32(header + 10);
    uint32_t last_leaf = be32(header + 14);
    uint16_t max_key_length = be16(header + 20);
    uint32_t total_nodes = be32(header + 22);
    uint32_t free_nodes = be32(header + 26);

    /*
     * Catalog keys can be up to node_size + 4 bytes (516 for a 512-byte
     * node), so the key-length bound is deliberately slightly above node size.
     */
    if (depth == 0 || depth > HFS_BTREE_MAX_DEPTH || root == 0 ||
        root >= total_nodes || leaf_records == 0 || first_leaf == 0 ||
        last_leaf == 0 || first_leaf >= total_nodes ||
        last_leaf >= total_nodes || free_nodes > total_nodes ||
        max_key_length < 2 ||
        max_key_length > node_size + HFS_BTREE_MAX_KEY_SLACK ||
        fork->size != (uint64_t)total_nodes * node_size)
        return HFS_ERR_FORMAT;

    btree->volume = volume;
    btree->fork = fork;
    btree->scratch = scratch;
    btree->scratch_size = scratch_size;
    btree->node_size = node_size;
    btree->depth = depth;
    btree->max_key_length = max_key_length;
    btree->root_node = root;
    btree->total_nodes = total_nodes;
    btree->free_nodes = free_nodes;
    btree->leaf_records = leaf_records;
    btree->first_leaf = first_leaf;
    btree->last_leaf = last_leaf;
    btree->btree_type = header[36];
    btree->key_compare = header[37];
    return HFS_OK;
}

int hfs_btree_read_node(struct hfs_btree *btree, uint32_t node,
                        const uint8_t **node_data)
{
    if (btree == NULL || node_data == NULL)
        return HFS_ERR_ARGUMENT;
    if (btree->scratch_size < btree->node_size)
        return HFS_ERR_BUFFER;
    if (node >= btree->total_nodes)
        return HFS_ERR_FORMAT;

    int rc = hfs_fork_read(btree->volume, btree->fork,
                           (uint64_t)node * btree->node_size,
                           btree->scratch, btree->node_size);
    if (rc != HFS_OK)
        return rc;

    *node_data = btree->scratch;
    return HFS_OK;
}
