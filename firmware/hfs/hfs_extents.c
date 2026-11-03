/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HFS+ extents-overflow reader.
 *
 * Derived from the read-only HFS+ parser in HFSPlus Rockboxed iPod
 * (firmware/hfsplus/hfsplus_ro.c, GPL-2.0-or-later). It reads data-fork overflow
 * records only; resource forks and writer-side mutation are out of scope.
 */
#include "hfs_extents.h"

#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t inline_blocks(const struct hfs_fork *fork)
{
    uint32_t blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        blocks += fork->extents[i].count;
    return blocks;
}

int hfs_extents_open(struct hfs_extents *extents, struct hfs_volume *volume,
                     void *scratch, size_t scratch_size)
{
    if (extents == NULL || volume == NULL || volume->extents.blocks == 0 ||
        volume->extents.size == 0)
        return HFS_ERR_UNSUPPORTED;
    return hfs_btree_open(&extents->btree, volume, &volume->extents, scratch,
                          scratch_size);
}

int hfs_extents_collect(struct hfs_extents *extents, uint32_t file_id,
                        uint32_t inline_count, uint32_t fork_blocks,
                        struct hfs_extent *out, size_t capacity,
                        size_t *count)
{
    if (extents == NULL || out == NULL || count == NULL || capacity == 0)
        return HFS_ERR_ARGUMENT;

    *count = 0;
    uint64_t sum = 0;
    struct hfs_btree_leaf_iterator iterator;
    int rc = hfs_btree_leaf_init(&extents->btree, &iterator);
    if (rc != HFS_OK)
        return rc;

    const uint8_t *record;
    size_t length;
    while ((rc = hfs_btree_leaf_next(&extents->btree, &iterator, &record,
                                     &length)) > 0) {
        if (length < 2u + 10u + 64u)
            return HFS_ERR_FORMAT;

        uint16_t key_length = be16(record);
        if (key_length != 10u)
            continue;
        const uint8_t *key = record + 2;
        if (key[0] != 0 || be32(key + 2) != file_id)
            continue;

        uint32_t logical = be32(key + 6);
        if (logical != inline_count + sum)
            return HFS_ERR_FORMAT;

        const uint8_t *data = key + 10;
        for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
            uint32_t start = be32(data + i * 8);
            uint32_t blocks = be32(data + i * 8 + 4);
            if (blocks == 0)
                continue;
            if (start == 0 || start >= extents->btree.volume->blocks ||
                blocks > extents->btree.volume->blocks - start)
                return HFS_ERR_FORMAT;
            if (*count >= capacity)
                return HFS_ERR_UNSUPPORTED;
            out[*count].start = start;
            out[*count].count = blocks;
            ++*count;
            sum += blocks;
        }

        if ((uint64_t)inline_count + sum == fork_blocks)
            break;
        if ((uint64_t)inline_count + sum > fork_blocks)
            return HFS_ERR_FORMAT;
    }

    if (rc < 0)
        return rc;
    return (uint64_t)inline_count + sum == fork_blocks ? HFS_OK
                                                       : HFS_ERR_FORMAT;
}

int hfs_extents_read(struct hfs_extents *extents, const struct hfs_fork *fork,
                     uint32_t file_id, uint64_t offset, void *buffer,
                     size_t length, size_t *done)
{
    if (extents == NULL || fork == NULL || done == NULL ||
        (buffer == NULL && length != 0))
        return HFS_ERR_ARGUMENT;

    *done = 0;
    if (offset >= fork->size || length == 0)
        return HFS_OK;
    if (length > fork->size - offset)
        length = (size_t)(fork->size - offset);

    uint32_t inline_count = inline_blocks(fork);
    if (inline_count == fork->blocks) {
        int rc = hfs_fork_read(extents->btree.volume, fork, offset, buffer,
                               length);
        if (rc != HFS_OK)
            return rc;
        *done = length;
        return HFS_OK;
    }

    struct hfs_extent overflow[HFS_OVERFLOW_EXTENT_MAX];
    size_t overflow_count = 0;
    int rc = hfs_extents_collect(extents, file_id, inline_count, fork->blocks,
                                 overflow, HFS_OVERFLOW_EXTENT_MAX,
                                 &overflow_count);
    if (rc != HFS_OK)
        return rc;

    const uint64_t block_size = extents->btree.volume->block_size;
    const uint64_t inline_bytes = (uint64_t)inline_count * block_size;
    uint8_t *out = buffer;
    size_t total = 0;

    if (offset < inline_bytes) {
        struct hfs_fork prefix = *fork;
        prefix.blocks = inline_count;
        prefix.size = inline_bytes;
        if (prefix.size > fork->size)
            prefix.size = fork->size;

        size_t chunk = length;
        if (inline_bytes - offset < chunk)
            chunk = (size_t)(inline_bytes - offset);
        rc = hfs_fork_read(extents->btree.volume, &prefix, offset, out, chunk);
        if (rc != HFS_OK)
            return rc;
        out += chunk;
        total += chunk;
        offset += chunk;
        length -= chunk;
    }

    if (length != 0) {
        uint64_t relative = offset - inline_bytes;
        uint64_t skip = relative / block_size;
        uint64_t within = relative % block_size;
        int first = 1;

        for (size_t i = 0; i < overflow_count && length != 0; ++i) {
            if (skip >= overflow[i].count) {
                skip -= overflow[i].count;
                continue;
            }

            struct hfs_fork one = { 0 };
            one.size = (uint64_t)overflow[i].count * block_size;
            one.blocks = overflow[i].count;
            one.extents[0] = overflow[i];

            uint64_t byte_offset = skip * block_size + (first ? within : 0);
            if (byte_offset >= one.size)
                return HFS_ERR_FORMAT;
            size_t chunk = length;
            if (one.size - byte_offset < chunk)
                chunk = (size_t)(one.size - byte_offset);
            rc = hfs_fork_read(extents->btree.volume, &one, byte_offset, out,
                               chunk);
            if (rc != HFS_OK)
                return rc;

            out += chunk;
            total += chunk;
            length -= chunk;
            skip = 0;
            within = 0;
            first = 0;
        }
    }

    *done = total;
    return length == 0 ? HFS_OK : HFS_ERR_FORMAT;
}
