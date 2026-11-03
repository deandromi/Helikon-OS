/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_bitmap.h"

#include <string.h>

#define HFS_VOLUME_HEADER_OFFSET 1024u
#define HFS_VOLUME_HEADER_SIZE 512u
#define HFS_VH_FREE_BLOCKS 48u
#define HFS_VH_WRITE_COUNT 68u

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int power2(uint32_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

static int map_bitmap_block(const struct hfs_volume *volume, uint32_t bit,
                            uint32_t *volume_block)
{
    uint64_t fork_block = ((uint64_t)bit / 8u) / volume->block_size;
    uint64_t seen = 0;

    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        uint32_t count = volume->allocation.extents[i].count;
        if (count == 0)
            continue;
        if (fork_block < seen + count) {
            *volume_block = volume->allocation.extents[i].start +
                            (uint32_t)(fork_block - seen);
            return HFS_OK;
        }
        seen += count;
    }
    return HFS_ERR_UNSUPPORTED;
}

static int read_bitmap_block(const struct hfs_volume *volume,
                             uint32_t volume_block, uint8_t *buffer)
{
    uint64_t offset = volume->base + (uint64_t)volume_block *
                                      volume->block_size;
    return volume->read(volume->context, offset, buffer,
                        volume->block_size) ? HFS_ERR_IO : HFS_OK;
}

static int stage_bitmap_block(struct hfs_txn *tx, uint32_t volume_block,
                              uint32_t block_size, const uint8_t *buffer)
{
    if (block_size % tx->journal->sector_size != 0)
        return HFS_ERR_UNSUPPORTED;
    uint64_t sector = (uint64_t)volume_block *
                      (block_size / tx->journal->sector_size);
    return hfs_txn_stage(tx, sector, buffer, block_size);
}

static int stage_header(struct hfs_txn *tx, const struct hfs_volume *volume,
                        uint32_t free_blocks)
{
    uint8_t header[HFS_VOLUME_HEADER_SIZE];
    if (volume->length < 2048u)
        return HFS_ERR_FORMAT;

    uint64_t primary = volume->base + HFS_VOLUME_HEADER_OFFSET;
    if (volume->read(volume->context, primary, header, sizeof(header)))
        return HFS_ERR_IO;
    put_be32(header + HFS_VH_FREE_BLOCKS, free_blocks);
    put_be32(header + HFS_VH_WRITE_COUNT,
             be32(header + HFS_VH_WRITE_COUNT) + 1u);
    int rc = hfs_txn_stage(tx, HFS_VOLUME_HEADER_OFFSET / 512u, header,
                           sizeof(header));
    if (rc != HFS_OK)
        return rc;

    uint64_t alternate_offset = volume->length - 1024u;
    uint64_t alternate = volume->base + alternate_offset;
    if (volume->read(volume->context, alternate, header, sizeof(header)))
        return HFS_ERR_IO;
    put_be32(header + HFS_VH_FREE_BLOCKS, free_blocks);
    put_be32(header + HFS_VH_WRITE_COUNT,
             be32(header + HFS_VH_WRITE_COUNT) + 1u);
    return hfs_txn_stage(tx, alternate_offset / 512u, header, sizeof(header));
}

static int bit_is_set(const uint8_t *buffer, uint32_t bit)
{
    return (buffer[bit / 8u] & (uint8_t)(0x80u >> (bit % 8u))) != 0;
}

static void set_bit(uint8_t *buffer, uint32_t bit, int value)
{
    uint8_t mask = (uint8_t)(0x80u >> (bit % 8u));
    if (value)
        buffer[bit / 8u] |= mask;
    else
        buffer[bit / 8u] &= (uint8_t)~mask;
}

int hfs_bitmap_open(struct hfs_bitmap *bitmap, struct hfs_volume *volume)
{
    if (bitmap == NULL || volume == NULL)
        return HFS_ERR_ARGUMENT;
    if (!power2(volume->block_size) || volume->block_size < 512u ||
        volume->block_size % 512u != 0 || volume->blocks == 0 ||
        volume->allocation.blocks == 0)
        return HFS_ERR_FORMAT;
    if (volume->allocation.size < ((uint64_t)volume->blocks + 7u) / 8u)
        return HFS_ERR_FORMAT;
    if (volume->free_blocks > volume->blocks)
        return HFS_ERR_FORMAT;

    memset(bitmap, 0, sizeof(*bitmap));
    bitmap->volume = volume;
    bitmap->block_size = volume->block_size;
    bitmap->total_blocks = volume->blocks;
    bitmap->free_blocks = volume->free_blocks;
    return HFS_OK;
}

int hfs_bitmap_find(struct hfs_bitmap *bitmap, uint32_t start_hint,
                    uint32_t count, uint32_t *first_block)
{
    if (bitmap == NULL || first_block == NULL || count == 0)
        return HFS_ERR_ARGUMENT;
    if (start_hint >= bitmap->total_blocks ||
        count > bitmap->free_blocks || count > bitmap->total_blocks)
        return HFS_ERR_NO_SPACE;

    uint32_t bits_per_block = bitmap->block_size * 8u;
    if (count > bits_per_block)
        return HFS_ERR_NO_SPACE;
    uint32_t first_bitmap = start_hint / bits_per_block;
    uint32_t last_bitmap = (bitmap->total_blocks - 1u) / bits_per_block;
    uint8_t buffer[32768];
    if (bitmap->block_size > sizeof(buffer))
        return HFS_ERR_UNSUPPORTED;

    for (uint32_t index = first_bitmap; index <= last_bitmap; ++index) {
        uint32_t block_start = index * bits_per_block;
        uint32_t first = index == first_bitmap ?
                         start_hint - block_start : 0u;
        uint32_t end = bitmap->total_blocks - block_start;
        if (end > bits_per_block)
            end = bits_per_block;
        uint32_t volume_block;
        int rc = map_bitmap_block(bitmap->volume, block_start, &volume_block);
        if (rc != HFS_OK)
            return rc;
        rc = read_bitmap_block(bitmap->volume, volume_block, buffer);
        if (rc != HFS_OK)
            return rc;
        for (uint32_t bit = first; bit + count <= end; ++bit) {
            uint32_t run = 0;
            while (run < count && !bit_is_set(buffer, bit + run))
                ++run;
            if (run == count) {
                *first_block = block_start + bit;
                return HFS_OK;
            }
        }
    }
    return HFS_ERR_NO_SPACE;
}

int hfs_bitmap_alloc(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                     uint32_t start_hint, uint32_t count,
                     uint32_t *first_block)
{
    if (bitmap == NULL || tx == NULL || first_block == NULL || count == 0)
        return HFS_ERR_ARGUMENT;
    if (start_hint >= bitmap->total_blocks ||
        count > bitmap->free_blocks || count > bitmap->total_blocks)
        return HFS_ERR_NO_SPACE;

    uint32_t bits_per_block = bitmap->block_size * 8u;
    if (count > bits_per_block)
        return HFS_ERR_NO_SPACE;

    uint32_t first_bitmap = start_hint / bits_per_block;
    uint32_t last_bitmap = (bitmap->total_blocks - 1u) / bits_per_block;
    uint8_t buffer[32768];

    if (bitmap->block_size > sizeof(buffer))
        return HFS_ERR_UNSUPPORTED;

    for (uint32_t index = first_bitmap; index <= last_bitmap; ++index) {
        uint32_t block_start = index * bits_per_block;
        uint32_t first = index == first_bitmap ?
                         start_hint - block_start : 0u;
        uint32_t end = bitmap->total_blocks - block_start;
        if (end > bits_per_block)
            end = bits_per_block;

        uint32_t volume_block;
        int rc = map_bitmap_block(bitmap->volume, block_start, &volume_block);
        if (rc != HFS_OK)
            return rc;
        rc = read_bitmap_block(bitmap->volume, volume_block, buffer);
        if (rc != HFS_OK)
            return rc;

        for (uint32_t bit = first; bit + count <= end; ++bit) {
            uint32_t run = 0;
            while (run < count && !bit_is_set(buffer, bit + run))
                ++run;
            if (run != count)
                continue;

            for (uint32_t i = 0; i < count; ++i)
                set_bit(buffer, bit + i, 1);
            rc = stage_bitmap_block(tx, volume_block, bitmap->block_size,
                                    buffer);
            if (rc != HFS_OK)
                return rc;
            uint32_t new_free = bitmap->free_blocks - count;
            rc = stage_header(tx, bitmap->volume, new_free);
            if (rc != HFS_OK)
                return rc;
            bitmap->free_blocks = new_free;
            bitmap->volume->free_blocks = new_free;
            *first_block = block_start + bit;
            return HFS_OK;
        }
    }
    return HFS_ERR_NO_SPACE;
}

int hfs_bitmap_free(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                    uint32_t start, uint32_t count)
{
    if (bitmap == NULL || tx == NULL || count == 0)
        return HFS_ERR_ARGUMENT;
    if (start >= bitmap->total_blocks ||
        count > bitmap->total_blocks - start)
        return HFS_ERR_ARGUMENT;
    if (count > bitmap->total_blocks - bitmap->free_blocks)
        return HFS_ERR_FORMAT;

    uint32_t bits_per_block = bitmap->block_size * 8u;
    uint8_t buffer[32768];
    if (bitmap->block_size > sizeof(buffer))
        return HFS_ERR_UNSUPPORTED;

    uint32_t block = start;
    uint32_t end_block = start + count;
    while (block < end_block) {
        uint32_t index = block / bits_per_block;
        uint32_t block_start = index * bits_per_block;
        uint32_t chunk_end = block_start + bits_per_block;
        if (chunk_end > end_block)
            chunk_end = end_block;

        uint32_t volume_block;
        int rc = map_bitmap_block(bitmap->volume, block_start, &volume_block);
        if (rc != HFS_OK)
            return rc;
        rc = read_bitmap_block(bitmap->volume, volume_block, buffer);
        if (rc != HFS_OK)
            return rc;

        for (uint32_t b = block; b < chunk_end; ++b) {
            if (!bit_is_set(buffer, b - block_start))
                return HFS_ERR_FORMAT;
            set_bit(buffer, b - block_start, 0);
        }
        rc = stage_bitmap_block(tx, volume_block, bitmap->block_size, buffer);
        if (rc != HFS_OK)
            return rc;
        block = chunk_end;
    }

    uint32_t new_free = bitmap->free_blocks + count;
    int rc = stage_header(tx, bitmap->volume, new_free);
    if (rc != HFS_OK)
        return rc;
    bitmap->free_blocks = new_free;
    bitmap->volume->free_blocks = new_free;
    return HFS_OK;
}
