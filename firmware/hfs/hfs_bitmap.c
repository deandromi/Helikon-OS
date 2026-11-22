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
                             uint32_t volume_block, uint8_t *buffer,
                             struct hfs_txn *tx)
{
    uint64_t offset = volume->base + (uint64_t)volume_block *
                                      volume->block_size;
    if (volume->read(volume->context, offset, buffer, volume->block_size))
        return HFS_ERR_IO;
    if (tx != NULL) {
        int rc = hfs_txn_overlay(tx, (uint64_t)volume_block *
                                 volume->block_size, buffer,
                                 volume->block_size);
        if (rc < 0)
            return rc;
    }
    return HFS_OK;
}

static int stage_bitmap_block(struct hfs_txn *tx, uint32_t volume_block,
                              uint32_t block_size, const uint8_t *buffer)
{
    if (block_size % tx->journal->sector_size != 0)
        return HFS_ERR_UNSUPPORTED;
    uint64_t sector = (uint64_t)volume_block *
                      (block_size / tx->journal->sector_size);
    return hfs_txn_stage_replace(tx, sector, buffer, block_size);
}

static int stage_header_at(struct hfs_txn *tx,
                           const struct hfs_volume *volume, uint64_t offset,
                           uint32_t free_blocks, uint32_t modify_date)
{
    uint32_t sector_size = tx->journal->sector_size;
    if (sector_size < 512u || sector_size > 4096u ||
        volume->length < sector_size || offset > volume->length ||
        sector_size > volume->length - offset % sector_size)
        return HFS_ERR_FORMAT;

    uint64_t block_start = offset - offset % sector_size;
    static uint8_t block[4096];
    int rc = hfs_txn_read(tx, volume, block_start, block, sector_size);
    if (rc != HFS_OK)
        return rc;

    uint8_t *header = block + (offset - block_start);
    put_be32(header + HFS_VH_FREE_BLOCKS, free_blocks);
    if (modify_date != 0)
        put_be32(header + 20u, modify_date);
    put_be32(header + HFS_VH_WRITE_COUNT,
             be32(header + HFS_VH_WRITE_COUNT) + 1u);
    return hfs_txn_stage_replace(tx, block_start / sector_size, block,
                                 sector_size);
}

static int stage_header(struct hfs_txn *tx, const struct hfs_volume *volume,
                        uint32_t free_blocks, uint32_t modify_date)
{
    if (volume->length < 2048u)
        return HFS_ERR_FORMAT;

    int rc = stage_header_at(tx, volume, HFS_VOLUME_HEADER_OFFSET,
                             free_blocks, modify_date);
    if (rc != HFS_OK)
        return rc;
    return stage_header_at(tx, volume, volume->length - 1024u, free_blocks,
                           modify_date);
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

/*
 * Scans for a run of `count` free blocks whose first block lies in [low, high).
 * A run never spans two map blocks: the search is per bitmap block, which bounds
 * one allocation by what a single block of the bitmap describes. On success the
 * block that holds the run is left in `buffer` with its number and the bit
 * offset, so the caller can set the bits without reading it twice.
 */
static int bitmap_scan(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                       uint32_t low, uint32_t high, uint32_t count,
                       uint32_t *first_block, uint8_t *buffer,
                       uint32_t *volume_block, uint32_t *bit_out)
{
    if (low >= high)
        return HFS_ERR_NO_SPACE;
    if (high > bitmap->total_blocks)
        high = bitmap->total_blocks;

    const uint32_t bits_per_block = bitmap->block_size * 8u;
    const uint32_t first_bitmap = low / bits_per_block;
    const uint32_t last_bitmap = (high - 1u) / bits_per_block;

    for (uint32_t index = first_bitmap; index <= last_bitmap; ++index) {
        const uint32_t block_start = index * bits_per_block;
        const uint32_t first = index == first_bitmap ? low - block_start : 0u;
        uint32_t end = bitmap->total_blocks - block_start;
        if (end > bits_per_block)
            end = bits_per_block;

        uint32_t block = 0u;
        int rc = map_bitmap_block(bitmap->volume, block_start, &block);
        if (rc != HFS_OK)
            return rc;
        rc = read_bitmap_block(bitmap->volume, block, buffer, tx);
        if (rc != HFS_OK)
            return rc;

        for (uint32_t bit = first; bit + count <= end; ++bit) {
            uint32_t run = 0u;
            while (run < count && !bit_is_set(buffer, bit + run))
                ++run;
            if (run != count)
                continue;
            *first_block = block_start + bit;
            *volume_block = block;
            *bit_out = bit;
            return HFS_OK;
        }
    }
    return HFS_ERR_NO_SPACE;
}

/*
 * Searches from the hint to the end of the volume and then wraps to the start.
 * The from-hint pass comes first so a tree fork that grows next to its own last
 * extent still prefers the adjacent block; the wrapped pass is what stops a
 * high hint from turning free space below it into "disk full". A run that
 * starts below the hint and extends past it is found by the second pass,
 * because it bounds where a run may start, not where it may end. A hint at or
 * past the end of the volume starts the search at block 0 instead of failing.
 */
static int bitmap_find_run(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                           uint32_t start_hint, uint32_t count,
                           uint32_t *first_block, uint8_t *buffer,
                           uint32_t *volume_block, uint32_t *bit)
{
    if (start_hint >= bitmap->total_blocks)
        start_hint = 0u;

    int rc = bitmap_scan(bitmap, tx, start_hint, bitmap->total_blocks, count,
                         first_block, buffer, volume_block, bit);
    if (rc != HFS_ERR_NO_SPACE)
        return rc;
    return bitmap_scan(bitmap, tx, 0u, start_hint, count, first_block, buffer,
                       volume_block, bit);
}

int hfs_bitmap_find(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                    uint32_t start_hint, uint32_t count,
                    uint32_t *first_block)
{
    if (bitmap == NULL || first_block == NULL || count == 0)
        return HFS_ERR_ARGUMENT;
    if (count > bitmap->free_blocks || count > bitmap->total_blocks ||
        count > bitmap->block_size * 8u)
        return HFS_ERR_NO_SPACE;

    static uint8_t buffer[32768];
    if (bitmap->block_size > sizeof(buffer))
        return HFS_ERR_UNSUPPORTED;

    uint32_t volume_block = 0u;
    uint32_t bit = 0u;
    return bitmap_find_run(bitmap, tx, start_hint, count, first_block, buffer,
                           &volume_block, &bit);
}

int hfs_bitmap_alloc(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                     uint32_t start_hint, uint32_t count,
                     uint32_t *first_block)
{
    if (bitmap == NULL || tx == NULL || first_block == NULL || count == 0)
        return HFS_ERR_ARGUMENT;
    if (count > bitmap->free_blocks || count > bitmap->total_blocks ||
        count > bitmap->block_size * 8u)
        return HFS_ERR_NO_SPACE;

    static uint8_t buffer[32768];
    if (bitmap->block_size > sizeof(buffer))
        return HFS_ERR_UNSUPPORTED;

    uint32_t block = 0u;
    uint32_t volume_block = 0u;
    uint32_t bit = 0u;
    int rc = bitmap_find_run(bitmap, tx, start_hint, count, &block, buffer,
                             &volume_block, &bit);
    if (rc != HFS_OK)
        return rc;

    for (uint32_t i = 0u; i < count; ++i)
        set_bit(buffer, bit + i, 1);
    rc = stage_bitmap_block(tx, volume_block, bitmap->block_size, buffer);
    if (rc != HFS_OK)
        return rc;

    uint32_t new_free = bitmap->free_blocks - count;
    rc = stage_header(tx, bitmap->volume, new_free, bitmap->modify_date);
    if (rc != HFS_OK)
        return rc;
    bitmap->free_blocks = new_free;
    bitmap->volume->free_blocks = new_free;
    *first_block = block;
    return HFS_OK;
}

int hfs_bitmap_block_used(struct hfs_bitmap *bitmap, uint32_t block, int *used)
{
    if (bitmap == NULL || used == NULL || block >= bitmap->total_blocks)
        return HFS_ERR_ARGUMENT;

    static uint8_t buffer[32768];
    if (bitmap->block_size == 0 || bitmap->block_size > sizeof(buffer))
        return HFS_ERR_UNSUPPORTED;

    uint32_t bits_per_block = bitmap->block_size * 8u;
    uint32_t index = block / bits_per_block;
    uint32_t volume_block = 0;
    int rc = map_bitmap_block(bitmap->volume, index * bits_per_block,
                              &volume_block);
    if (rc != HFS_OK)
        return rc;
    rc = read_bitmap_block(bitmap->volume, volume_block, buffer, NULL);
    if (rc != HFS_OK)
        return rc;
    *used = bit_is_set(buffer, block - index * bits_per_block) ? 1 : 0;
    return HFS_OK;
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
    static uint8_t buffer[32768];
    if (bitmap->block_size > sizeof(buffer))
        return HFS_ERR_UNSUPPORTED;

    /*
     * Validate both ends of the range before staging anything. A range can
     * cross a bitmap block, and the staging of the first block is not undone by
     * a refusal in the second: the transaction would keep half-cleared bits
     * that the next commit writes without a matching free-block count. A
     * refused free must leave the transaction exactly as it was.
     */
    for (uint32_t check = start; check < start + count;) {
        uint32_t index = check / bits_per_block;
        uint32_t block_start = index * bits_per_block;
        uint32_t chunk_end = block_start + bits_per_block;
        if (chunk_end > start + count)
            chunk_end = start + count;

        uint32_t volume_block;
        int rc = map_bitmap_block(bitmap->volume, block_start, &volume_block);
        if (rc != HFS_OK)
            return rc;
        rc = read_bitmap_block(bitmap->volume, volume_block, buffer, tx);
        if (rc != HFS_OK)
            return rc;
        for (uint32_t b = check; b < chunk_end; ++b) {
            if (!bit_is_set(buffer, b - block_start))
                return HFS_ERR_FORMAT;
        }
        check = chunk_end;
    }

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
        rc = read_bitmap_block(bitmap->volume, volume_block, buffer, tx);
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
    int rc = stage_header(tx, bitmap->volume, new_free,
                              bitmap->modify_date);
    if (rc != HFS_OK)
        return rc;
    bitmap->free_blocks = new_free;
    bitmap->volume->free_blocks = new_free;
    return HFS_OK;
}
