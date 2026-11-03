/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Tree-level HFS+ B-tree insertion.
 *
 * The node-level mechanics live in hfs_btree_write.c. This file owns the
 * header-node map, root-to-leaf traversal, leaf split, parent insertion and
 * recursive split/root creation. Fork growth is not implemented: new nodes
 * must already exist in the catalog/extents fork and be free in the map.
 */
#include "hfs_btree_tree.h"

#include "hfs_btree_write.h"
#include "hfs_fork_io.h"

#include <string.h>

#define HFS_BTREE_MAX_DEPTH 32u
#define HFS_BTREE_HEADER_DEPTH 0u
#define HFS_BTREE_HEADER_ROOT 2u
#define HFS_BTREE_HEADER_LEAF_RECORDS 6u
#define HFS_BTREE_HEADER_FIRST_LEAF 10u
#define HFS_BTREE_HEADER_LAST_LEAF 14u
#define HFS_BTREE_HEADER_TOTAL 22u
#define HFS_BTREE_HEADER_FREE 26u
#define HFS_BTREE_KEY_LENGTH 2u
#define HFS_BTREE_CHILD_SIZE 4u

struct tree_header {
    uint8_t node[HFS_MAX_NODE_SIZE];
    uint8_t *header;
    size_t header_length;
    uint8_t *map;
    size_t map_length;
    uint32_t total_nodes;
    uint32_t free_nodes;
    uint32_t root_node;
    uint32_t leaf_records;
    uint32_t first_leaf;
    uint32_t last_leaf;
    uint16_t depth;
    int dirty;
};

struct insert_path {
    uint32_t node;
    uint16_t child;
};

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int read_node(struct hfs_btree *btree, uint32_t node, uint8_t *buffer)
{
    if (node >= btree->total_nodes)
        return HFS_ERR_FORMAT;
    return hfs_fork_read(btree->volume, btree->fork,
                         (uint64_t)node * btree->node_size, buffer,
                         btree->node_size);
}

static int stage_node(struct hfs_btree *btree, struct hfs_txn *tx,
                      uint32_t node, const uint8_t *buffer)
{
    if (btree->node_size < btree->volume->block_size ||
        btree->node_size % btree->volume->block_size != 0)
        return HFS_ERR_UNSUPPORTED;
    return hfs_fork_stage(tx, btree->volume, btree->fork,
                          (uint64_t)node * btree->node_size, buffer,
                          btree->node_size);
}

static int load_header(struct hfs_btree *btree, struct tree_header *header)
{
    memset(header, 0, sizeof(*header));
    if (btree->node_size > sizeof(header->node))
        return HFS_ERR_UNSUPPORTED;
    int rc = read_node(btree, 0, header->node);
    if (rc != HFS_OK)
        return rc;

    const uint8_t *record;
    size_t length;
    rc = hfs_btree_node_record(header->node, btree->node_size, 0, &record,
                               &length);
    if (rc != HFS_OK || length < 106)
        return HFS_ERR_FORMAT;
    header->header = (uint8_t *)record;
    header->header_length = length;

    rc = hfs_btree_node_record(header->node, btree->node_size, 2, &record,
                               &length);
    if (rc != HFS_OK || length == 0)
        return HFS_ERR_FORMAT;
    header->map = (uint8_t *)record;
    header->map_length = length;

    header->depth = be16(header->header + HFS_BTREE_HEADER_DEPTH);
    header->root_node = be32(header->header + HFS_BTREE_HEADER_ROOT);
    header->leaf_records = be32(header->header + HFS_BTREE_HEADER_LEAF_RECORDS);
    header->first_leaf = be32(header->header + HFS_BTREE_HEADER_FIRST_LEAF);
    header->last_leaf = be32(header->header + HFS_BTREE_HEADER_LAST_LEAF);
    header->total_nodes = be32(header->header + HFS_BTREE_HEADER_TOTAL);
    header->free_nodes = be32(header->header + HFS_BTREE_HEADER_FREE);

    if (header->depth == 0 || header->depth > HFS_BTREE_MAX_DEPTH ||
        header->total_nodes == 0 || header->total_nodes > btree->total_nodes ||
        header->total_nodes > header->map_length * 8u ||
        header->root_node >= header->total_nodes ||
        header->first_leaf >= header->total_nodes ||
        header->last_leaf >= header->total_nodes ||
        header->free_nodes > header->total_nodes)
        return HFS_ERR_FORMAT;
    return HFS_OK;
}

static int store_header(struct hfs_btree *btree, struct hfs_txn *tx,
                        struct tree_header *header)
{
    put16(header->header + HFS_BTREE_HEADER_DEPTH, header->depth);
    put32(header->header + HFS_BTREE_HEADER_ROOT, header->root_node);
    put32(header->header + HFS_BTREE_HEADER_LEAF_RECORDS,
          header->leaf_records);
    put32(header->header + HFS_BTREE_HEADER_FIRST_LEAF, header->first_leaf);
    put32(header->header + HFS_BTREE_HEADER_LAST_LEAF, header->last_leaf);
    put32(header->header + HFS_BTREE_HEADER_FREE, header->free_nodes);
    header->dirty = 0;

    btree->depth = header->depth;
    btree->root_node = header->root_node;
    btree->leaf_records = header->leaf_records;
    btree->free_nodes = header->free_nodes;
    btree->first_leaf = header->first_leaf;
    btree->last_leaf = header->last_leaf;

    return stage_node(btree, tx, 0, header->node);
}

static int map_get(const struct tree_header *header, uint32_t node)
{
    return (header->map[node / 8u] & (uint8_t)(0x80u >> (node % 8u))) != 0;
}

static void map_set(struct tree_header *header, uint32_t node, int used)
{
    uint8_t mask = (uint8_t)(0x80u >> (node % 8u));
    if (used)
        header->map[node / 8u] |= mask;
    else
        header->map[node / 8u] &= (uint8_t)~mask;
    header->dirty = 1;
}

static int alloc_node(struct tree_header *header, uint32_t *node)
{
    if (header->free_nodes == 0)
        return HFS_ERR_NO_SPACE;
    for (uint32_t i = 0; i < header->total_nodes; ++i) {
        if (!map_get(header, i)) {
            map_set(header, i, 1);
            --header->free_nodes;
            *node = i;
            return HFS_OK;
        }
    }
    return HFS_ERR_NO_SPACE;
}

static int first_key(const uint8_t *node, uint16_t node_size,
                     const uint8_t **key, size_t *key_length)
{
    const uint8_t *record;
    size_t length;
    int rc = hfs_btree_node_record(node, node_size, 0, &record, &length);
    if (rc != HFS_OK)
        return rc;
    if (length < HFS_BTREE_KEY_LENGTH)
        return HFS_ERR_FORMAT;
    uint16_t key_len = be16(record);
    if ((size_t)key_len + HFS_BTREE_KEY_LENGTH > length)
        return HFS_ERR_FORMAT;
    *key = record + HFS_BTREE_KEY_LENGTH;
    *key_length = key_len;
    return HFS_OK;
}

static size_t build_index(uint8_t *buffer, const uint8_t *key,
                          size_t key_length, uint32_t child)
{
    put16(buffer, (uint16_t)key_length);
    memcpy(buffer + HFS_BTREE_KEY_LENGTH, key, key_length);
    put32(buffer + HFS_BTREE_KEY_LENGTH + key_length, child);
    return HFS_BTREE_KEY_LENGTH + key_length + HFS_BTREE_CHILD_SIZE;
}

static int compare_record(const uint8_t *record, size_t length,
                          const uint8_t *key, size_t key_length,
                          hfs_btree_key_compare_fn compare, void *context,
                          int *result, uint16_t *record_key_length)
{
    if (length < HFS_BTREE_KEY_LENGTH)
        return HFS_ERR_FORMAT;
    uint16_t stored = be16(record);
    if ((size_t)stored + HFS_BTREE_KEY_LENGTH > length)
        return HFS_ERR_FORMAT;
    *record_key_length = stored;
    *result = compare(context, record + HFS_BTREE_KEY_LENGTH, stored,
                      key, key_length);
    return HFS_OK;
}

static int insert_parent(struct hfs_btree *btree, struct tree_header *header,
                         struct hfs_txn *tx, const struct insert_path *path,
                         unsigned int level, uint32_t right_node,
                         const uint8_t *key, size_t key_length,
                         hfs_btree_key_compare_fn compare, void *context)
{
    uint32_t parent_node = path[level - 1u].node;
    uint16_t child = path[level - 1u].child;
    uint8_t parent[HFS_MAX_NODE_SIZE];
    uint8_t right[HFS_MAX_NODE_SIZE];
    uint8_t index_record[HFS_MAX_NODE_SIZE];

    int rc = read_node(btree, parent_node, parent);
    if (rc != HFS_OK)
        return rc;
    if (key_length + HFS_BTREE_KEY_LENGTH + HFS_BTREE_CHILD_SIZE >
        sizeof(index_record))
        return HFS_ERR_NO_SPACE;
    size_t record_length = build_index(index_record, key, key_length,
                                       right_node);

    rc = hfs_btree_node_insert(parent, btree->node_size,
                               (uint16_t)(child + 1u), index_record,
                               record_length);
    if (rc == HFS_OK) {
        rc = stage_node(btree, tx, parent_node, parent);
        return rc;
    }
    if (rc != HFS_ERR_NO_SPACE)
        return rc;

    uint16_t count = be16(parent + 10);
    if (count < 2)
        return HFS_ERR_NO_SPACE;
    uint16_t split = (uint16_t)(count / 2u);
    uint32_t new_right;
    rc = alloc_node(header, &new_right);
    if (rc != HFS_OK)
        return rc;

    const uint8_t *separator;
    size_t separator_length;
    rc = hfs_btree_node_split(parent, right, btree->node_size, split,
                              parent_node, new_right, &separator,
                              &separator_length);
    if (rc != HFS_OK)
        return rc;

    int cmp;
    uint16_t stored;
    const uint8_t *right_key;
    size_t right_key_length;
    rc = first_key(right, btree->node_size, &right_key, &right_key_length);
    if (rc != HFS_OK)
        return rc;
    cmp = compare(context, key, key_length, right_key, right_key_length);
    uint8_t *target = cmp < 0 ? parent : right;
    uint16_t target_count = be16(target + 10);
    uint16_t position = 0;
    for (; position < target_count; ++position) {
        const uint8_t *record;
        size_t length;
        rc = hfs_btree_node_record(target, btree->node_size, position,
                                   &record, &length);
        if (rc != HFS_OK)
            return rc;
        rc = compare_record(record, length, key, key_length, compare, context,
                            &cmp, &stored);
        (void)stored;
        if (rc != HFS_OK)
            return rc;
        if (cmp > 0)
            break;
    }
    if (target == parent && position == 0 && level > 1)
        return HFS_ERR_UNSUPPORTED;
    rc = hfs_btree_node_insert(target, btree->node_size, position,
                               index_record, record_length);
    if (rc != HFS_OK)
        return rc;

    rc = stage_node(btree, tx, parent_node, parent);
    if (rc != HFS_OK)
        return rc;
    rc = stage_node(btree, tx, new_right, right);
    if (rc != HFS_OK)
        return rc;

    uint32_t next_index = be32(right);
    if (next_index != 0) {
        uint8_t next[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, next_index, next);
        if (rc != HFS_OK)
            return rc;
        put32(next + 4, new_right);
        rc = stage_node(btree, tx, next_index, next);
        if (rc != HFS_OK)
            return rc;
    }

    rc = first_key(right, btree->node_size, &separator, &separator_length);
    if (rc != HFS_OK)
        return rc;

    if (level == 1) {
        uint32_t new_root;
        rc = alloc_node(header, &new_root);
        if (rc != HFS_OK)
            return rc;
        uint8_t root[HFS_MAX_NODE_SIZE];
        memset(root, 0, btree->node_size);
        root[8] = 0;
        root[9] = (uint8_t)(parent[9] + 1u);
        put16(root + 10, 0);
        put16(root + btree->node_size - 2u, 14u);

        const uint8_t *left_key;
        size_t left_key_length;
        rc = first_key(parent, btree->node_size, &left_key,
                       &left_key_length);
        if (rc != HFS_OK)
            return rc;
        size_t left_record = build_index(index_record, left_key,
                                         left_key_length, parent_node);
        rc = hfs_btree_node_insert(root, btree->node_size, 0, index_record,
                                   left_record);
        if (rc != HFS_OK)
            return rc;
        size_t right_record = build_index(index_record, separator,
                                          separator_length, new_right);
        rc = hfs_btree_node_insert(root, btree->node_size, 1, index_record,
                                   right_record);
        if (rc != HFS_OK)
            return rc;

        header->root_node = new_root;
        ++header->depth;
        return stage_node(btree, tx, new_root, root);
    }

    return insert_parent(btree, header, tx, path, level - 1u, new_right,
                         separator, separator_length, compare, context);
}

int hfs_btree_insert(struct hfs_btree *btree, struct hfs_txn *tx,
                     const void *record, size_t length,
                     hfs_btree_key_compare_fn compare, void *compare_context)
{
    if (btree == NULL || tx == NULL || record == NULL || compare == NULL ||
        length < HFS_BTREE_KEY_LENGTH)
        return HFS_ERR_ARGUMENT;
    const uint8_t *new_record = record;
    uint16_t new_key_length = be16(new_record);
    if ((size_t)new_key_length + HFS_BTREE_KEY_LENGTH > length)
        return HFS_ERR_ARGUMENT;
    const uint8_t *new_key = new_record + HFS_BTREE_KEY_LENGTH;

    struct tree_header header;
    int rc = load_header(btree, &header);
    if (rc != HFS_OK)
        return rc;

    struct insert_path path[HFS_BTREE_MAX_DEPTH];
    uint32_t node_number = header.root_node;
    unsigned int level = 0;
    uint8_t node[HFS_MAX_NODE_SIZE];

    while (1) {
        rc = read_node(btree, node_number, node);
        if (rc != HFS_OK)
            return rc;
        if (node[8] == 0xff)
            break;
        if (node[8] != 0 || level >= HFS_BTREE_MAX_DEPTH)
            return HFS_ERR_FORMAT;
        uint16_t count = be16(node + 10);
        if (count == 0)
            return HFS_ERR_FORMAT;
        uint16_t child = 0;
        for (uint16_t i = 0; i < count; ++i) {
            const uint8_t *existing;
            size_t existing_length;
            rc = hfs_btree_node_record(node, btree->node_size, i, &existing,
                                       &existing_length);
            if (rc != HFS_OK)
                return rc;
            int cmp;
            uint16_t stored;
            rc = compare_record(existing, existing_length, new_key,
                                new_key_length, compare, compare_context,
                                &cmp, &stored);
            if (rc != HFS_OK)
                return rc;
            if (cmp <= 0)
                child = i;
            else
                break;
        }
        const uint8_t *child_record;
        size_t child_length;
        rc = hfs_btree_node_record(node, btree->node_size, child,
                                   &child_record, &child_length);
        if (rc != HFS_OK)
            return rc;
        uint16_t stored = be16(child_record);
        if ((size_t)stored + HFS_BTREE_KEY_LENGTH + HFS_BTREE_CHILD_SIZE >
            child_length)
            return HFS_ERR_FORMAT;
        path[level].node = node_number;
        path[level].child = child;
        ++level;
        node_number = be32(child_record + HFS_BTREE_KEY_LENGTH + stored);
    }

    uint16_t count = be16(node + 10);
    uint16_t position = 0;
    for (; position < count; ++position) {
        const uint8_t *existing;
        size_t existing_length;
        rc = hfs_btree_node_record(node, btree->node_size, position, &existing,
                                   &existing_length);
        if (rc != HFS_OK)
            return rc;
        int cmp;
        uint16_t stored;
        rc = compare_record(existing, existing_length, new_key,
                            new_key_length, compare, compare_context, &cmp,
                            &stored);
        (void)stored;
        if (rc != HFS_OK)
            return rc;
        if (cmp == 0)
            return HFS_ERR_BUSY;
        if (cmp > 0)
            break;
    }

    if (position == 0 && level > 0)
        return HFS_ERR_UNSUPPORTED;
    rc = hfs_btree_node_insert(node, btree->node_size, position, record,
                               length);
    if (rc == HFS_OK) {
        rc = stage_node(btree, tx, node_number, node);
        if (rc != HFS_OK)
            return rc;
        ++header.leaf_records;
        return store_header(btree, tx, &header);
    }
    if (rc != HFS_ERR_NO_SPACE || count < 2)
        return rc == HFS_ERR_NO_SPACE ? HFS_ERR_NO_SPACE : rc;

    uint32_t right_node;
    rc = alloc_node(&header, &right_node);
    if (rc != HFS_OK)
        return rc;

    uint8_t right[HFS_MAX_NODE_SIZE];
    uint16_t split = (uint16_t)(count / 2u);
    const uint8_t *separator;
    size_t separator_length;
    rc = hfs_btree_node_split(node, right, btree->node_size, split,
                              node_number, right_node, &separator,
                              &separator_length);
    if (rc != HFS_OK)
        return rc;

    const uint8_t *right_key;
    size_t right_key_length;
    rc = first_key(right, btree->node_size, &right_key, &right_key_length);
    if (rc != HFS_OK)
        return rc;
    int cmp = compare(compare_context, new_key, new_key_length, right_key,
                      right_key_length);
    uint8_t *target = cmp < 0 ? node : right;
    uint16_t target_count = be16(target + 10);
    uint16_t insert_at = 0;
    for (; insert_at < target_count; ++insert_at) {
        const uint8_t *existing;
        size_t existing_length;
        rc = hfs_btree_node_record(target, btree->node_size, insert_at,
                                   &existing, &existing_length);
        if (rc != HFS_OK)
            return rc;
        int compare_result;
        uint16_t stored;
        rc = compare_record(existing, existing_length, new_key,
                            new_key_length, compare, compare_context,
                            &compare_result, &stored);
        (void)stored;
        if (rc != HFS_OK)
            return rc;
        if (compare_result > 0)
            break;
    }
    rc = hfs_btree_node_insert(target, btree->node_size, insert_at, record,
                               length);
    if (rc != HFS_OK)
        return rc;

    rc = stage_node(btree, tx, node_number, node);
    if (rc != HFS_OK)
        return rc;
    rc = stage_node(btree, tx, right_node, right);
    if (rc != HFS_OK)
        return rc;

    uint32_t next_leaf = be32(right);
    if (next_leaf != 0) {
        uint8_t next[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, next_leaf, next);
        if (rc != HFS_OK)
            return rc;
        put32(next + 4, right_node);
        rc = stage_node(btree, tx, next_leaf, next);
        if (rc != HFS_OK)
            return rc;
    } else {
        header.last_leaf = right_node;
    }

    ++header.leaf_records;
    rc = first_key(right, btree->node_size, &separator, &separator_length);
    if (rc != HFS_OK)
        return rc;

    if (level == 0) {
        uint32_t new_root;
        rc = alloc_node(&header, &new_root);
        if (rc != HFS_OK)
            return rc;
        uint8_t root[HFS_MAX_NODE_SIZE];
        memset(root, 0, btree->node_size);
        root[8] = 0;
        root[9] = (uint8_t)(node[9] + 1u);
        put16(root + 10, 0);
        put16(root + btree->node_size - 2u, 14u);

        const uint8_t *left_key;
        size_t left_key_length;
        rc = first_key(node, btree->node_size, &left_key, &left_key_length);
        if (rc != HFS_OK)
            return rc;
        uint8_t index_record[HFS_MAX_NODE_SIZE];
        size_t left_record = build_index(index_record, left_key,
                                         left_key_length, node_number);
        rc = hfs_btree_node_insert(root, btree->node_size, 0, index_record,
                                   left_record);
        if (rc != HFS_OK)
            return rc;
        size_t right_record = build_index(index_record, separator,
                                          separator_length, right_node);
        rc = hfs_btree_node_insert(root, btree->node_size, 1, index_record,
                                   right_record);
        if (rc != HFS_OK)
            return rc;
        header.root_node = new_root;
        ++header.depth;
        rc = stage_node(btree, tx, new_root, root);
        if (rc != HFS_OK)
            return rc;
    } else {
        rc = insert_parent(btree, &header, tx, path, level, right_node,
                           separator, separator_length, compare,
                           compare_context);
        if (rc != HFS_OK)
            return rc;
    }

    return store_header(btree, tx, &header);
}

static int update_parent_key(struct hfs_btree *btree,
                             struct tree_header *header, struct hfs_txn *tx,
                             const struct insert_path *path,
                             unsigned int level, const uint8_t *new_key,
                             size_t new_key_length)
{
    if (level == 0)
        return HFS_OK;
    uint32_t parent_node = path[level - 1u].node;
    uint16_t child = path[level - 1u].child;
    uint8_t parent[HFS_MAX_NODE_SIZE];
    uint8_t index_record[HFS_MAX_NODE_SIZE];

    int rc = read_node(btree, parent_node, parent);
    if (rc != HFS_OK)
        return rc;

    const uint8_t *old_record;
    size_t old_length;
    rc = hfs_btree_node_record(parent, btree->node_size, child, &old_record,
                               &old_length);
    if (rc != HFS_OK || old_length < HFS_BTREE_KEY_LENGTH)
        return HFS_ERR_FORMAT;
    uint16_t old_key_length = be16(old_record);
    if ((size_t)old_key_length + HFS_BTREE_KEY_LENGTH +
        HFS_BTREE_CHILD_SIZE > old_length)
        return HFS_ERR_FORMAT;
    uint32_t child_node = be32(old_record + HFS_BTREE_KEY_LENGTH +
                               old_key_length);

    if (new_key_length + HFS_BTREE_KEY_LENGTH + HFS_BTREE_CHILD_SIZE >
        sizeof(index_record))
        return HFS_ERR_NO_SPACE;
    size_t new_length = build_index(index_record, new_key, new_key_length,
                                    child_node);

    rc = hfs_btree_node_delete(parent, btree->node_size, child);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_btree_node_insert(parent, btree->node_size, child, index_record,
                               new_length);
    if (rc != HFS_OK)
        return rc;

    rc = stage_node(btree, tx, parent_node, parent);
    if (rc != HFS_OK)
        return rc;

    if (child == 0 && level > 1) {
        const uint8_t *parent_key;
        size_t parent_key_length;
        rc = first_key(parent, btree->node_size, &parent_key,
                       &parent_key_length);
        if (rc != HFS_OK)
            return rc;
        return update_parent_key(btree, header, tx, path, level - 1u,
                                 parent_key, parent_key_length);
    }
    return HFS_OK;
}

int hfs_btree_delete(struct hfs_btree *btree, struct hfs_txn *tx,
                     const uint8_t *key, size_t key_length,
                     hfs_btree_key_compare_fn compare, void *compare_context)
{
    if (btree == NULL || tx == NULL || key == NULL || compare == NULL)
        return HFS_ERR_ARGUMENT;

    struct tree_header header;
    int rc = load_header(btree, &header);
    if (rc != HFS_OK)
        return rc;

    struct insert_path path[HFS_BTREE_MAX_DEPTH];
    uint32_t node_number = header.root_node;
    unsigned int level = 0;
    uint8_t node[HFS_MAX_NODE_SIZE];

    while (1) {
        rc = read_node(btree, node_number, node);
        if (rc != HFS_OK)
            return rc;
        if (node[8] == 0xff)
            break;
        if (node[8] != 0 || level >= HFS_BTREE_MAX_DEPTH)
            return HFS_ERR_FORMAT;
        uint16_t count = be16(node + 10);
        if (count == 0)
            return HFS_ERR_FORMAT;
        uint16_t child = 0;
        for (uint16_t i = 0; i < count; ++i) {
            const uint8_t *existing;
            size_t existing_length;
            rc = hfs_btree_node_record(node, btree->node_size, i, &existing,
                                       &existing_length);
            if (rc != HFS_OK)
                return rc;
            int cmp;
            uint16_t stored;
            rc = compare_record(existing, existing_length, key, key_length,
                                compare, compare_context, &cmp, &stored);
            if (rc != HFS_OK)
                return rc;
            if (cmp <= 0)
                child = i;
            else
                break;
        }
        const uint8_t *child_record;
        size_t child_length;
        rc = hfs_btree_node_record(node, btree->node_size, child,
                                   &child_record, &child_length);
        if (rc != HFS_OK)
            return rc;
        uint16_t stored = be16(child_record);
        if ((size_t)stored + HFS_BTREE_KEY_LENGTH + HFS_BTREE_CHILD_SIZE >
            child_length)
            return HFS_ERR_FORMAT;
        path[level].node = node_number;
        path[level].child = child;
        ++level;
        node_number = be32(child_record + HFS_BTREE_KEY_LENGTH + stored);
    }

    uint16_t count = be16(node + 10);
    uint16_t position = 0;
    int found = 0;
    for (; position < count; ++position) {
        const uint8_t *existing;
        size_t existing_length;
        rc = hfs_btree_node_record(node, btree->node_size, position, &existing,
                                   &existing_length);
        if (rc != HFS_OK)
            return rc;
        int cmp;
        uint16_t stored;
        rc = compare_record(existing, existing_length, key, key_length,
                            compare, compare_context, &cmp, &stored);
        (void)stored;
        if (rc != HFS_OK)
            return rc;
        if (cmp == 0) {
            found = 1;
            break;
        }
    }
    if (!found)
        return HFS_ERR_NOT_FOUND;
    rc = hfs_btree_node_delete(node, btree->node_size, position);
    if (rc != HFS_OK)
        return rc;
    --header.leaf_records;

    if (be16(node + 10) > 0) {
        if (position == 0 && level > 0) {
            const uint8_t *new_key;
            size_t new_key_length;
            rc = first_key(node, btree->node_size, &new_key,
                           &new_key_length);
            if (rc != HFS_OK)
                return rc;
            rc = update_parent_key(btree, &header, tx, path, level, new_key,
                                   new_key_length);
            if (rc != HFS_OK)
                return rc;
        }
        rc = stage_node(btree, tx, node_number, node);
        if (rc != HFS_OK)
            return rc;
        return store_header(btree, tx, &header);
    }
    if (level == 0) {
        rc = stage_node(btree, tx, node_number, node);
        if (rc != HFS_OK)
            return rc;
        return store_header(btree, tx, &header);
    }

    uint16_t child = path[level - 1u].child;

    uint8_t parent[HFS_MAX_NODE_SIZE];
    rc = read_node(btree, path[level - 1u].node, parent);
    if (rc != HFS_OK)
        return rc;
    uint16_t parent_count = be16(parent + 10);
    if (parent_count <= 1)
        return HFS_ERR_UNSUPPORTED;

    uint32_t previous = be32(node + 4);
    uint32_t next = be32(node);
    if (previous != 0) {
        uint8_t prev_node[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, previous, prev_node);
        if (rc != HFS_OK)
            return rc;
        put32(prev_node, next);
        rc = stage_node(btree, tx, previous, prev_node);
        if (rc != HFS_OK)
            return rc;
    } else {
        header.first_leaf = next;
    }
    if (next != 0) {
        uint8_t next_node[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, next, next_node);
        if (rc != HFS_OK)
            return rc;
        put32(next_node + 4, previous);
        rc = stage_node(btree, tx, next, next_node);
        if (rc != HFS_OK)
            return rc;
    } else {
        header.last_leaf = previous;
    }

    rc = hfs_btree_node_delete(parent, btree->node_size, child);
    if (rc != HFS_OK)
        return rc;
    if (be16(parent + 10) == 0)
        return HFS_ERR_UNSUPPORTED;
    rc = stage_node(btree, tx, path[level - 1u].node, parent);
    if (rc != HFS_OK)
        return rc;
    if (child == 0 && level > 1) {
        const uint8_t *parent_key;
        size_t parent_key_length;
        rc = first_key(parent, btree->node_size, &parent_key,
                       &parent_key_length);
        if (rc != HFS_OK)
            return rc;
        rc = update_parent_key(btree, &header, tx, path, level - 1u,
                               parent_key, parent_key_length);
        if (rc != HFS_OK)
            return rc;
    }

    map_set(&header, node_number, 0);
    ++header.free_nodes;
    return store_header(btree, tx, &header);
}
