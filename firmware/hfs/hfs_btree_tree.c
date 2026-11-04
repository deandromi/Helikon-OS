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

#include "hfs_bitmap.h"
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

static void put64(uint8_t *p, uint64_t value)
{
    put32(p, (uint32_t)(value >> 32));
    put32(p + 4, (uint32_t)value);
}

static int read_node(struct hfs_btree *btree, struct hfs_txn *tx,
                     uint32_t node, uint8_t *buffer)
{
    if (node >= btree->total_nodes)
        return HFS_ERR_FORMAT;
    int rc = hfs_fork_read(btree->volume, btree->fork,
                           (uint64_t)node * btree->node_size, buffer,
                           btree->node_size);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_fork_overlay(tx, btree->volume, btree->fork,
                          (uint64_t)node * btree->node_size, buffer,
                          btree->node_size);
    return rc < 0 ? rc : HFS_OK;
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

static int load_header(struct hfs_btree *btree, struct hfs_txn *tx, struct tree_header *header)
{
    memset(header, 0, sizeof(*header));
    if (btree->node_size > sizeof(header->node))
        return HFS_ERR_UNSUPPORTED;
    int rc = read_node(btree, tx, 0, header->node);
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
    put32(header->header + HFS_BTREE_HEADER_TOTAL, header->total_nodes);
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

static uint32_t fork_offset(const struct hfs_btree *btree)
{
    if (btree->fork == &btree->volume->catalog)
        return 272u;
    if (btree->fork == &btree->volume->extents)
        return 192u;
    return 0;
}

static int update_volume_fork(struct hfs_btree *btree, struct hfs_txn *tx,
                              uint32_t new_block)
{
    struct hfs_fork *fork = (struct hfs_fork *)btree->fork;
    uint32_t offset = fork_offset(btree);
    if (offset == 0 || btree->node_size != btree->volume->block_size)
        return HFS_ERR_UNSUPPORTED;

    int placed = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        if (fork->extents[i].count == 0)
            continue;
        if (fork->extents[i].start + fork->extents[i].count == new_block) {
            ++fork->extents[i].count;
            placed = 1;
            break;
        }
    }
    if (!placed) {
        for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
            if (fork->extents[i].count == 0) {
                fork->extents[i].start = new_block;
                fork->extents[i].count = 1;
                placed = 1;
                break;
            }
        }
    }
    if (!placed)
        return HFS_ERR_UNSUPPORTED;

    fork->size += btree->node_size;
    ++fork->blocks;

    uint32_t sector_size = tx->journal->sector_size;
    if (sector_size < 512u || sector_size > 4096u)
        return HFS_ERR_UNSUPPORTED;

    static uint8_t header[4096];
    for (unsigned int alt = 0; alt < 2u; ++alt) {
        uint64_t base = alt == 0 ? 1024u :
                        btree->volume->length - 1024u;
        if (base > btree->volume->length ||
            sector_size > btree->volume->length - base % sector_size)
            return HFS_ERR_FORMAT;
        uint64_t block_start = base - base % sector_size;
        if (btree->volume->read(btree->volume->context,
                                btree->volume->base + block_start, header,
                                sector_size))
            return HFS_ERR_IO;
        int rc = hfs_txn_overlay(tx, block_start, header, sector_size);
        if (rc < 0)
            return rc;
        uint8_t *dest = header + (base - block_start) + offset;
        put64(dest, fork->size);
        put32(dest + 12, fork->blocks);
        for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
            put32(dest + 16 + i * 8, fork->extents[i].start);
            put32(dest + 20 + i * 8, fork->extents[i].count);
        }
        rc = hfs_txn_stage_replace(tx, block_start / sector_size, header,
                                   sector_size);
        if (rc != HFS_OK)
            return rc;
    }
    return HFS_OK;
}

static int grow_locked(struct hfs_btree *btree, struct hfs_txn *tx,
                       struct tree_header *header)
{
    if (btree->node_size != btree->volume->block_size ||
        header->total_nodes >= header->map_length * 8u ||
        btree->fork == NULL)
        return HFS_ERR_UNSUPPORTED;

    struct hfs_bitmap bitmap;
    int rc = hfs_bitmap_open(&bitmap, btree->volume);
    if (rc != HFS_OK)
        return rc;
    uint32_t block = 0;
    rc = hfs_bitmap_alloc(&bitmap, tx, 0, 1, &block);
    if (rc != HFS_OK)
        return rc;

    rc = update_volume_fork(btree, tx, block);
    if (rc != HFS_OK)
        return rc;

    uint32_t node = header->total_nodes;
    if (node / 8u >= header->map_length)
        return HFS_ERR_UNSUPPORTED;
    map_set(header, node, 0);
    ++header->total_nodes;
    ++header->free_nodes;
    ++btree->total_nodes;
    ++btree->free_nodes;
    return HFS_OK;
}

static int alloc_node(struct hfs_btree *btree, struct hfs_txn *tx,
                      struct tree_header *header, uint32_t *node)
{
    if (header->free_nodes == 0) {
        int rc = grow_locked(btree, tx, header);
        if (rc != HFS_OK)
            return rc;
    }
    for (uint32_t i = 0; i < header->total_nodes; ++i) {
        if (!map_get(header, i)) {
            map_set(header, i, 1);
            --header->free_nodes;
            --btree->free_nodes;
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

static int insert_parent_impl(struct hfs_btree *btree,
                              struct tree_header *header,
                              struct hfs_txn *tx,
                              const struct insert_path *path,
                              unsigned int level, uint32_t right_node,
                              const uint8_t *key, size_t key_length,
                              hfs_btree_key_compare_fn compare,
                              void *context);

static int insert_parent(struct hfs_btree *btree, struct tree_header *header,
                         struct hfs_txn *tx, const struct insert_path *path,
                         unsigned int level, uint32_t right_node,
                         const uint8_t *key, size_t key_length,
                         hfs_btree_key_compare_fn compare, void *context)
{
    static int active;
    if (active)
        return HFS_ERR_UNSUPPORTED;
    active = 1;
    int rc = insert_parent_impl(btree, header, tx, path, level, right_node,
                                key, key_length, compare, context);
    active = 0;
    return rc;
}

static int insert_parent_impl(struct hfs_btree *btree,
                              struct tree_header *header,
                              struct hfs_txn *tx,
                              const struct insert_path *path,
                         unsigned int level, uint32_t right_node,
                         const uint8_t *key, size_t key_length,
                         hfs_btree_key_compare_fn compare, void *context)
{
    uint32_t parent_node = path[level - 1u].node;
    uint16_t child = path[level - 1u].child;
    static uint8_t parent[HFS_MAX_NODE_SIZE];
    static uint8_t right[HFS_MAX_NODE_SIZE];
    static uint8_t index_record[HFS_MAX_NODE_SIZE];

    int rc = read_node(btree, tx, parent_node, parent);
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
    rc = alloc_node(btree, tx, header, &new_right);
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
        static uint8_t next[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, tx, next_index, next);
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
        rc = alloc_node(btree, tx, header, &new_root);
        if (rc != HFS_OK)
            return rc;
        static uint8_t root[HFS_MAX_NODE_SIZE];
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

static int update_index_key(struct hfs_btree *btree, struct hfs_txn *tx,
                             uint32_t parent_node, uint16_t child,
                             const uint8_t *key, size_t key_length)
{
    static uint8_t parent[HFS_MAX_NODE_SIZE];
    int rc = read_node(btree, tx, parent_node, parent);
    if (rc != HFS_OK)
        return rc;

    const uint8_t *record;
    size_t record_length;
    rc = hfs_btree_node_record(parent, btree->node_size, child, &record,
                               &record_length);
    if (rc != HFS_OK)
        return rc;
    if (record_length < HFS_BTREE_KEY_LENGTH)
        return HFS_ERR_FORMAT;
    uint16_t stored = be16(record);
    if ((size_t)stored + HFS_BTREE_KEY_LENGTH + HFS_BTREE_CHILD_SIZE >
        record_length)
        return HFS_ERR_FORMAT;
    uint32_t child_node = be32(record + HFS_BTREE_KEY_LENGTH + stored);
    if (key_length + HFS_BTREE_KEY_LENGTH + HFS_BTREE_CHILD_SIZE >
        HFS_MAX_NODE_SIZE)
        return HFS_ERR_NO_SPACE;

    static uint8_t index_record[HFS_MAX_NODE_SIZE];
    put16(index_record, (uint16_t)key_length);
    memcpy(index_record + HFS_BTREE_KEY_LENGTH, key, key_length);
    put32(index_record + HFS_BTREE_KEY_LENGTH + key_length, child_node);
    size_t new_length = HFS_BTREE_KEY_LENGTH + key_length +
                        HFS_BTREE_CHILD_SIZE;

    rc = hfs_btree_node_delete(parent, btree->node_size, child);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_btree_node_insert(parent, btree->node_size, child,
                               index_record, new_length);
    if (rc != HFS_OK)
        return rc;
    return stage_node(btree, tx, parent_node, parent);
}

static int update_ancestor_keys(struct hfs_btree *btree, struct hfs_txn *tx,
                                const struct insert_path *path,
                                unsigned int level, const uint8_t *key,
                                size_t key_length)
{
    for (unsigned int l = level; l > 0; --l) {
        uint16_t child = path[l - 1u].child;
        int rc = update_index_key(btree, tx, path[l - 1u].node, child, key,
                                  key_length);
        if (rc != HFS_OK)
            return rc;
        if (child != 0 || l == 1)
            break;
    }
    return HFS_OK;
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

    static struct tree_header header;
    int rc = load_header(btree, tx, &header);
    if (rc != HFS_OK)
        return rc;

    struct insert_path path[HFS_BTREE_MAX_DEPTH];
    uint32_t node_number = header.root_node;
    unsigned int level = 0;
    static uint8_t node[HFS_MAX_NODE_SIZE];

    while (1) {
        rc = read_node(btree, tx, node_number, node);
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

    if (position == 0 && level > 0) {
        rc = update_ancestor_keys(btree, tx, path, level, new_key,
                                  new_key_length);
        if (rc != HFS_OK)
            return rc;
    }
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
    rc = alloc_node(btree, tx, &header, &right_node);
    if (rc != HFS_OK)
        return rc;

    static uint8_t right[HFS_MAX_NODE_SIZE];
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
    if (target == node && insert_at == 0 && level > 0) {
        rc = update_ancestor_keys(btree, tx, path, level, new_key,
                                  new_key_length);
        if (rc != HFS_OK)
            return rc;
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
        static uint8_t next[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, tx, next_leaf, next);
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
        rc = alloc_node(btree, tx, &header, &new_root);
        if (rc != HFS_OK)
            return rc;
        static uint8_t root[HFS_MAX_NODE_SIZE];
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
        static uint8_t index_record[HFS_MAX_NODE_SIZE];
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

static size_t node_used_bytes(const uint8_t *node, uint16_t node_size);
static int set_parent_key(uint8_t *parent, uint16_t node_size,
                          uint16_t index, const uint8_t *new_key,
                          size_t new_key_length);

static int rebalance_leaf_prev(struct hfs_btree *btree,
                               struct tree_header *header, struct hfs_txn *tx,
                               const struct insert_path *path,
                               unsigned int level, uint32_t node_number,
                               uint8_t *node, int first_changed)
{
    (void)first_changed;
    if (node_used_bytes(node, btree->node_size) >= (size_t)btree->node_size / 2u)
        return 0;
    uint32_t prev = be32(node + 4);
    if (prev == 0)
        return 0;

    static uint8_t prev_node[HFS_MAX_NODE_SIZE];
    int rc = read_node(btree, tx, prev, prev_node);
    if (rc != HFS_OK)
        return rc;
    if (prev_node[8] != 0xff)
        return 0;

    uint32_t parent_node = path[level - 1u].node;
    uint16_t child = path[level - 1u].child;
    if (child == 0)
        return 0;
    static uint8_t parent[HFS_MAX_NODE_SIZE];
    rc = read_node(btree, tx, parent_node, parent);
    if (rc != HFS_OK)
        return rc;
    uint16_t parent_count = be16(parent + 10);
    if (parent_count <= 1)
        return 0;

    static uint8_t merged[HFS_MAX_NODE_SIZE];
    memcpy(merged, prev_node, btree->node_size);
    uint16_t current_count = be16(node + 10);
    int fits = 1;
    for (uint16_t i = 0; i < current_count; ++i) {
        const uint8_t *record;
        size_t length;
        rc = hfs_btree_node_record(node, btree->node_size, i, &record,
                                   &length);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_btree_node_insert(merged, btree->node_size,
                                   be16(merged + 10), record, length);
        if (rc == HFS_ERR_NO_SPACE) {
            fits = 0;
            break;
        }
        if (rc != HFS_OK)
            return rc;
    }

    if (fits) {
        uint32_t after = be32(node);
        memcpy(prev_node, merged, btree->node_size);
        put32(prev_node, after);
        if (after != 0) {
            static uint8_t after_node[HFS_MAX_NODE_SIZE];
            rc = read_node(btree, tx, after, after_node);
            if (rc != HFS_OK)
                return rc;
            put32(after_node + 4, prev);
            rc = stage_node(btree, tx, after, after_node);
            if (rc != HFS_OK)
                return rc;
        } else {
            header->last_leaf = prev;
        }
        rc = hfs_btree_node_delete(parent, btree->node_size, child);
        if (rc != HFS_OK)
            return rc;
        rc = stage_node(btree, tx, parent_node, parent);
        if (rc != HFS_OK)
            return rc;
        map_set(header, node_number, 0);
        ++header->free_nodes;
        rc = stage_node(btree, tx, prev, prev_node);
        if (rc != HFS_OK)
            return rc;
        return 1;
    }

    uint16_t prev_count = be16(prev_node + 10);
    if (prev_count <= 1)
        return 0;
    const uint8_t *move_record;
    size_t move_length;
    rc = hfs_btree_node_record(prev_node, btree->node_size,
                               (uint16_t)(prev_count - 1u), &move_record,
                               &move_length);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_btree_node_insert(node, btree->node_size, 0, move_record,
                               move_length);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_btree_node_delete(prev_node, btree->node_size,
                               (uint16_t)(prev_count - 1u));
    if (rc != HFS_OK)
        return rc;

    const uint8_t *new_key;
    size_t new_key_length;
    rc = first_key(node, btree->node_size, &new_key, &new_key_length);
    if (rc != HFS_OK)
        return rc;
    rc = set_parent_key(parent, btree->node_size, child, new_key,
                        new_key_length);
    if (rc != HFS_OK)
        return rc;
    rc = stage_node(btree, tx, parent_node, parent);
    if (rc != HFS_OK)
        return rc;
    rc = stage_node(btree, tx, prev, prev_node);
    if (rc != HFS_OK)
        return rc;
    rc = stage_node(btree, tx, node_number, node);
    if (rc != HFS_OK)
        return rc;
    return 2;
}

static int unlink_leaf(struct hfs_btree *btree, struct tree_header *header,
                        struct hfs_txn *tx, uint32_t node_number,
                        const uint8_t *node)
{
    uint32_t previous = be32(node + 4);
    uint32_t next = be32(node);
    int rc;
    if (previous != 0) {
        static uint8_t prev_node[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, tx, previous, prev_node);
        if (rc != HFS_OK)
            return rc;
        put32(prev_node, next);
        rc = stage_node(btree, tx, previous, prev_node);
        if (rc != HFS_OK)
            return rc;
    } else {
        header->first_leaf = next;
    }
    if (next != 0) {
        static uint8_t next_node[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, tx, next, next_node);
        if (rc != HFS_OK)
            return rc;
        put32(next_node + 4, previous);
        rc = stage_node(btree, tx, next, next_node);
        if (rc != HFS_OK)
            return rc;
    } else {
        header->last_leaf = previous;
    }
    (void)node_number;
    return HFS_OK;
}

static int update_parent_key(struct hfs_btree *btree,
                             struct tree_header *header, struct hfs_txn *tx,
                             const struct insert_path *path,
                             unsigned int level, const uint8_t *new_key,
                             size_t new_key_length);

static size_t node_used_bytes(const uint8_t *node, uint16_t node_size)
{
    uint16_t count = be16(node + 10);
    uint16_t free_offset = be16(node + node_size - 2u * (count + 1u));
    if (free_offset < 14u)
        return node_size;
    return (size_t)(free_offset - 14u) + 2u * (count + 1u);
}

static int set_parent_key(uint8_t *parent, uint16_t node_size,
                          uint16_t index, const uint8_t *new_key,
                          size_t new_key_length)
{
    const uint8_t *old_record;
    size_t old_length;
    int rc = hfs_btree_node_record(parent, node_size, index, &old_record,
                                   &old_length);
    if (rc != HFS_OK || old_length < HFS_BTREE_KEY_LENGTH)
        return HFS_ERR_FORMAT;
    uint16_t old_key_length = be16(old_record);
    if ((size_t)old_key_length + HFS_BTREE_KEY_LENGTH +
        HFS_BTREE_CHILD_SIZE > old_length)
        return HFS_ERR_FORMAT;
    uint32_t child_node = be32(old_record + HFS_BTREE_KEY_LENGTH +
                               old_key_length);

    static uint8_t index_record[HFS_MAX_NODE_SIZE];
    if (new_key_length + HFS_BTREE_KEY_LENGTH + HFS_BTREE_CHILD_SIZE >
        sizeof(index_record))
        return HFS_ERR_NO_SPACE;
    size_t index_length = build_index(index_record, new_key, new_key_length,
                                      child_node);
    rc = hfs_btree_node_delete(parent, node_size, index);
    if (rc != HFS_OK)
        return rc;
    return hfs_btree_node_insert(parent, node_size, index, index_record,
                                 index_length);
}

static int merge_leaf_next(struct hfs_btree *btree,
                           struct tree_header *header, struct hfs_txn *tx,
                           const struct insert_path *path, unsigned int level,
                           uint32_t node_number, uint8_t *node,
                           int first_changed,
                           hfs_btree_key_compare_fn compare, void *context)
{
    (void)compare;
    (void)context;
    if (node_used_bytes(node, btree->node_size) >= (size_t)btree->node_size / 2u)
        return 0;
    uint32_t next = be32(node);
    if (next == 0)
        return 0;

    static uint8_t next_node[HFS_MAX_NODE_SIZE];
    int rc = read_node(btree, tx, next, next_node);
    if (rc != HFS_OK)
        return rc;
    if (next_node[8] != 0xff)
        return 0;

    static uint8_t merged[HFS_MAX_NODE_SIZE];
    memcpy(merged, node, btree->node_size);
    uint16_t next_count = be16(next_node + 10);
    int fits = 1;
    for (uint16_t i = 0; i < next_count; ++i) {
        const uint8_t *record;
        size_t length;
        rc = hfs_btree_node_record(next_node, btree->node_size, i, &record,
                                   &length);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_btree_node_insert(merged, btree->node_size,
                                   be16(merged + 10), record, length);
        if (rc == HFS_ERR_NO_SPACE) {
            fits = 0;
            break;
        }
        if (rc != HFS_OK)
            return rc;
    }

    uint32_t parent_node = path[level - 1u].node;
    uint16_t child = path[level - 1u].child;
    static uint8_t parent[HFS_MAX_NODE_SIZE];
    rc = read_node(btree, tx, parent_node, parent);
    if (rc != HFS_OK)
        return rc;
    uint16_t parent_count = be16(parent + 10);
    if (parent_count <= 1 || (uint16_t)(child + 1u) >= parent_count)
        return 0;

    if (!fits) {
        if (next_count <= 1)
            return 0;
        const uint8_t *move_record;
        size_t move_length;
        rc = hfs_btree_node_record(next_node, btree->node_size, 0,
                                   &move_record, &move_length);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_btree_node_insert(node, btree->node_size,
                                   be16(node + 10), move_record, move_length);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_btree_node_delete(next_node, btree->node_size, 0);
        if (rc != HFS_OK)
            return rc;
        const uint8_t *next_key;
        size_t next_key_length;
        rc = first_key(next_node, btree->node_size, &next_key,
                       &next_key_length);
        if (rc != HFS_OK)
            return rc;
        rc = set_parent_key(parent, btree->node_size,
                            (uint16_t)(child + 1u), next_key,
                            next_key_length);
        if (rc != HFS_OK)
            return rc;
        rc = stage_node(btree, tx, parent_node, parent);
        if (rc != HFS_OK)
            return rc;
        rc = stage_node(btree, tx, node_number, node);
        if (rc != HFS_OK)
            return rc;
        rc = stage_node(btree, tx, next, next_node);
        if (rc != HFS_OK)
            return rc;
        return 2;
    }

    if (first_changed) {
        const uint8_t *new_key;
        size_t new_key_length;
        rc = first_key(node, btree->node_size, &new_key, &new_key_length);
        if (rc != HFS_OK)
            return rc;
        const uint8_t *old_record;
        size_t old_length;
        rc = hfs_btree_node_record(parent, btree->node_size, child,
                                   &old_record, &old_length);
        if (rc != HFS_OK || old_length < HFS_BTREE_KEY_LENGTH)
            return HFS_ERR_FORMAT;
        uint16_t old_key_length = be16(old_record);
        if ((size_t)old_key_length + HFS_BTREE_KEY_LENGTH +
            HFS_BTREE_CHILD_SIZE > old_length)
            return HFS_ERR_FORMAT;
        uint32_t child_node = be32(old_record + HFS_BTREE_KEY_LENGTH +
                                   old_key_length);
        static uint8_t index_record[HFS_MAX_NODE_SIZE];
        size_t index_length = build_index(index_record, new_key,
                                           new_key_length, child_node);
        rc = hfs_btree_node_delete(parent, btree->node_size, child);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_btree_node_insert(parent, btree->node_size, child,
                                   index_record, index_length);
        if (rc != HFS_OK)
            return rc;
    }

    rc = hfs_btree_node_delete(parent, btree->node_size,
                               (uint16_t)(child + 1u));
    if (rc != HFS_OK)
        return rc;
    rc = stage_node(btree, tx, parent_node, parent);
    if (rc != HFS_OK)
        return rc;

    if (child == 0 && level > 1 && first_changed) {
        const uint8_t *parent_key;
        size_t parent_key_length;
        rc = first_key(parent, btree->node_size, &parent_key,
                       &parent_key_length);
        if (rc != HFS_OK)
            return rc;
        rc = update_parent_key(btree, header, tx, path, level - 1u,
                               parent_key, parent_key_length);
        if (rc != HFS_OK)
            return rc;
    }

    memcpy(node, merged, btree->node_size);
    put32(node, be32(next_node));
    uint32_t after = be32(next_node);
    if (after != 0) {
        static uint8_t after_node[HFS_MAX_NODE_SIZE];
        rc = read_node(btree, tx, after, after_node);
        if (rc != HFS_OK)
            return rc;
        put32(after_node + 4, node_number);
        rc = stage_node(btree, tx, after, after_node);
        if (rc != HFS_OK)
            return rc;
    } else {
        header->last_leaf = node_number;
    }
    map_set(header, next, 0);
    ++header->free_nodes;
    rc = stage_node(btree, tx, node_number, node);
    if (rc != HFS_OK)
        return rc;
    return 1;
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
    static uint8_t parent[HFS_MAX_NODE_SIZE];
    static uint8_t index_record[HFS_MAX_NODE_SIZE];

    int rc = read_node(btree, tx, parent_node, parent);
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

    static struct tree_header header;
    int rc = load_header(btree, tx, &header);
    if (rc != HFS_OK)
        return rc;

    struct insert_path path[HFS_BTREE_MAX_DEPTH];
    uint32_t node_number = header.root_node;
    unsigned int level = 0;
    static uint8_t node[HFS_MAX_NODE_SIZE];

    while (1) {
        rc = read_node(btree, tx, node_number, node);
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
        int first_changed = position == 0 && level > 0;

        /* If the root index has this leaf as its only child, collapse the
         * root to the leaf instead of keeping a one-record index root. */
        if (level == 1) {
            static uint8_t root[HFS_MAX_NODE_SIZE];
            rc = read_node(btree, tx, path[0].node, root);
            if (rc != HFS_OK)
                return rc;
            if (root[8] == 0 && be16(root + 10) == 1) {
                const uint8_t *root_record;
                size_t root_length;
                rc = hfs_btree_node_record(root, btree->node_size, 0,
                                           &root_record, &root_length);
                if (rc != HFS_OK || root_length < HFS_BTREE_KEY_LENGTH)
                    return HFS_ERR_FORMAT;
                uint16_t root_key_length = be16(root_record);
                if ((size_t)root_key_length + HFS_BTREE_KEY_LENGTH +
                    HFS_BTREE_CHILD_SIZE > root_length)
                    return HFS_ERR_FORMAT;
                uint32_t root_child = be32(root_record + HFS_BTREE_KEY_LENGTH +
                                           root_key_length);
                if (root_child == node_number) {
                    map_set(&header, path[0].node, 0);
                    ++header.free_nodes;
                    header.root_node = node_number;
                    header.depth = 1;
                    rc = stage_node(btree, tx, node_number, node);
                    if (rc != HFS_OK)
                        return rc;
                    return store_header(btree, tx, &header);
                }
            }
        }

        int merged = 0;
        if (level > 0) {
            merged = merge_leaf_next(btree, &header, tx, path, level,
                                     node_number, node, first_changed,
                                     compare, compare_context);
            if (merged < 0)
                return merged;
            if (merged == 0)
                merged = rebalance_leaf_prev(btree, &header, tx, path, level,
                                             node_number, node,
                                             first_changed);
            if (merged < 0)
                return merged;
        }
        if (merged > 0)
            return store_header(btree, tx, &header);
        if (first_changed) {
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

    static uint8_t parent[HFS_MAX_NODE_SIZE];
    rc = read_node(btree, tx, path[level - 1u].node, parent);
    if (rc != HFS_OK)
        return rc;
    uint16_t parent_count = be16(parent + 10);

    if (parent_count == 1) {
        if (level == 1)
            return HFS_ERR_UNSUPPORTED;
        static uint8_t grandparent[HFS_MAX_NODE_SIZE];
        uint32_t grandparent_node = path[level - 2u].node;
        uint16_t grandparent_child = path[level - 2u].child;
        rc = read_node(btree, tx, grandparent_node, grandparent);
        if (rc != HFS_OK)
            return rc;
        if (be16(grandparent + 10) <= 1)
            return HFS_ERR_UNSUPPORTED;

        rc = unlink_leaf(btree, &header, tx, node_number, node);
        if (rc != HFS_OK)
            return rc;
        map_set(&header, node_number, 0);
        ++header.free_nodes;
        map_set(&header, path[level - 1u].node, 0);
        ++header.free_nodes;

        rc = hfs_btree_node_delete(grandparent, btree->node_size,
                                   grandparent_child);
        if (rc != HFS_OK)
            return rc;
        if (be16(grandparent + 10) == 0)
            return HFS_ERR_UNSUPPORTED;
        rc = stage_node(btree, tx, grandparent_node, grandparent);
        if (rc != HFS_OK)
            return rc;
        if (grandparent_child == 0 && level > 2) {
            const uint8_t *grandparent_key;
            size_t grandparent_key_length;
            rc = first_key(grandparent, btree->node_size, &grandparent_key,
                           &grandparent_key_length);
            if (rc != HFS_OK)
                return rc;
            rc = update_parent_key(btree, &header, tx, path, level - 2u,
                                   grandparent_key, grandparent_key_length);
            if (rc != HFS_OK)
                return rc;
        }
        return store_header(btree, tx, &header);
    }

    if (parent_count == 0)
        return HFS_ERR_UNSUPPORTED;

    rc = unlink_leaf(btree, &header, tx, node_number, node);
    if (rc != HFS_OK)
        return rc;

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

int hfs_btree_find(struct hfs_btree *btree, struct hfs_txn *tx,
                   const uint8_t *key, size_t key_length,
                   hfs_btree_key_compare_fn compare, void *compare_context,
                   void *record, size_t record_capacity,
                   size_t *record_length)
{
    if (btree == NULL || tx == NULL || key == NULL || compare == NULL ||
        record == NULL || record_length == NULL)
        return HFS_ERR_ARGUMENT;

    static struct tree_header header;
    int rc = load_header(btree, tx, &header);
    if (rc != HFS_OK)
        return rc;

    uint32_t node_number = header.root_node;
    static uint8_t node[HFS_MAX_NODE_SIZE];
    unsigned int level = 0;
    while (1) {
        rc = read_node(btree, tx, node_number, node);
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
        node_number = be32(child_record + HFS_BTREE_KEY_LENGTH + stored);
        ++level;
    }

    uint16_t count = be16(node + 10);
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
        (void)stored;
        if (rc != HFS_OK)
            return rc;
        if (cmp == 0) {
            if (existing_length > record_capacity)
                return HFS_ERR_BUFFER;
            memcpy(record, existing, existing_length);
            *record_length = existing_length;
            return HFS_OK;
        }
        if (cmp > 0)
            break;
    }
    return HFS_ERR_NOT_FOUND;
}


int hfs_btree_update(struct hfs_btree *btree, struct hfs_txn *tx,
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

    static struct tree_header header;
    int rc = load_header(btree, tx, &header);
    if (rc != HFS_OK)
        return rc;

    uint32_t node_number = header.root_node;
    static uint8_t node[HFS_MAX_NODE_SIZE];
    unsigned int level = 0;
    while (1) {
        rc = read_node(btree, tx, node_number, node);
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
        node_number = be32(child_record + HFS_BTREE_KEY_LENGTH + stored);
        ++level;
    }

    uint16_t count = be16(node + 10);
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *existing;
        size_t existing_length;
        rc = hfs_btree_node_record(node, btree->node_size, i, &existing,
                                   &existing_length);
        if (rc != HFS_OK)
            return rc;
        int cmp;
        uint16_t stored;
        rc = compare_record(existing, existing_length, new_key, new_key_length,
                            compare, compare_context, &cmp, &stored);
        (void)stored;
        if (rc != HFS_OK)
            return rc;
        if (cmp == 0) {
            if (existing_length != length)
                return HFS_ERR_FORMAT;
            memcpy((uint8_t *)existing, record, length);
            return stage_node(btree, tx, node_number, node);
        }
        if (cmp > 0)
            break;
    }
    return HFS_ERR_NOT_FOUND;
}

int hfs_btree_grow(struct hfs_btree *btree, struct hfs_txn *tx)
{
    if (btree == NULL || tx == NULL)
        return HFS_ERR_ARGUMENT;
    static struct tree_header header;
    int rc = load_header(btree, tx, &header);
    if (rc != HFS_OK)
        return rc;
    rc = grow_locked(btree, tx, &header);
    if (rc != HFS_OK)
        return rc;
    return store_header(btree, tx, &header);
}
