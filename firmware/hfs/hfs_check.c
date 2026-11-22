/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only structural check of an HFS+ volume. See `hfs_check.h` for what is
 * and is not covered.
 *
 * Ground rules, the same ones the writer obeys:
 *  - read-only: the volume's write callback is never used here, and this file
 *    includes no write path at all;
 *  - allocation-free: one stack scratch buffer, bounded, no malloc;
 *  - findings, not exceptions: a corrupt fork does not stop the check, it sets
 *    a bit and the next check runs. A report that says "four things are wrong"
 *    is more useful than the first error, and a scrub must not give up on a
 *    volume because one tree is broken.
 */
#include "hfs_check.h"

#include <stddef.h>

#include "hfs_btree.h"

static int power2(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static uint32_t inline_blocks(const struct hfs_fork *fork)
{
    uint32_t total = 0u;
    for (uint32_t index = 0u; index < HFS_EXTENT_COUNT; ++index) {
        uint32_t count = fork->extents[index].count;
        if (count > UINT32_MAX - total)
            return UINT32_MAX;
        total += count;
    }
    return total;
}

/*
 * One fork's geometry: every extent inside the volume, no more blocks claimed
 * than the extents can hold, and a byte size that fits in those blocks.
 */
static void check_fork(const struct hfs_volume *volume,
                       const struct hfs_fork *fork, uint32_t *problems)
{
    uint32_t total = 0u;
    for (uint32_t index = 0u; index < HFS_EXTENT_COUNT; ++index) {
        uint32_t start = fork->extents[index].start;
        uint32_t count = fork->extents[index].count;
        if (count == 0u)
            continue;
        if (start >= volume->blocks || count > volume->blocks - start) {
            *problems |= HFS_CHECK_EXTENT_OUTSIDE;
            continue;
        }
        if (count > UINT32_MAX - total) {
            *problems |= HFS_CHECK_FORK_BLOCK_OVERFLOW;
            return;
        }
        total += count;
    }

    if (total > fork->blocks) {
        *problems |= HFS_CHECK_FORK_BLOCK_OVERFLOW;
        return;
    }
    /*
     * The inline extents may cover less than `blocks` when the rest lives in the
     * extents overflow tree; they may never cover more, and the byte size may
     * never exceed what the blocks can hold.
     */
    if (fork->size > (uint64_t)fork->blocks * (uint64_t)volume->block_size)
        *problems |= HFS_CHECK_FORK_SIZE_MISMATCH;
}

static void check_header(struct hfs_volume *volume, struct hfs_check_report *report)
{
    report->block_size = volume->block_size;
    report->blocks = volume->blocks;
    report->free_blocks_claimed = volume->free_blocks;

    if (!power2(volume->block_size) || volume->block_size < 512u ||
        volume->block_size > 65536u)
        report->problems |= HFS_CHECK_BAD_BLOCK_SIZE;
    if (volume->blocks == 0u)
        report->problems |= HFS_CHECK_BAD_BLOCK_COUNT;
    if (volume->free_blocks > volume->blocks)
        report->problems |= HFS_CHECK_FREE_EXCEEDS_TOTAL;
}

/*
 * The allocation bitmap. It is only counted when the whole fork is inline: a
 * large volume keeps most of its bitmap in the extents overflow tree, and
 * claiming a count without chasing those records would be a lie.
 */
static void check_allocation(struct hfs_volume *volume,
                             struct hfs_check_report *report)
{
    const struct hfs_fork *fork = &volume->allocation;
    uint64_t needed = ((uint64_t)volume->blocks + 7u) / 8u;
    if (fork->size < needed) {
        report->problems |= HFS_CHECK_ALLOCATION_TOO_SMALL;
        return;
    }
    if (inline_blocks(fork) != fork->blocks) {
        report->skipped |= HFS_CHECK_SKIPPED_BITMAP;
        return;
    }
    if (needed > HFS_CHECK_BITMAP_BYTES) {
        report->skipped |= HFS_CHECK_SKIPPED_BITMAP;
        return;
    }

    uint8_t buffer[512];
    uint32_t counted = 0u;
    uint64_t block = 0u;
    uint64_t byte_offset = 0u;
    while (byte_offset < needed) {
        uint32_t chunk = (uint32_t)(needed - byte_offset);
        if (chunk > sizeof(buffer))
            chunk = sizeof(buffer);
        if (hfs_fork_read(volume, fork, byte_offset, buffer, chunk) != HFS_OK) {
            report->skipped |= HFS_CHECK_SKIPPED_BITMAP;
            return;
        }
        for (uint32_t index = 0u; index < chunk; ++index) {
            uint8_t byte = buffer[index];
            for (uint32_t bit = 0u; bit < 8u; ++bit) {
                /*
                 * The last byte of the allocation file is padded when the block
                 * count is not a multiple of eight. macOS writes those padding
                 * bits as zero on a freshly formatted volume, so they are not a
                 * finding: only the real blocks are counted and compared.
                 */
                if (block < volume->blocks &&
                    (byte & (uint8_t)(0x80u >> bit)) == 0u)
                    counted += 1u;
                block += 1u;
            }
        }
        byte_offset += chunk;
    }

    report->bitmap_counted = 1;
    report->free_blocks_counted = counted;
    if (counted != volume->free_blocks)
        report->problems |= HFS_CHECK_FREE_MISMATCH;
}

/*
 * The three metadata trees. `hfs_btree_open` already validates the header record
 * and its indices, so the check reuses it rather than duplicating the rules: a
 * tree the reader refuses is a tree the volume cannot trust.
 */
static uint32_t check_tree(struct hfs_volume *volume, const struct hfs_fork *fork,
                           uint16_t *depth, uint32_t *nodes)
{
    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_btree btree;
    int rc = hfs_btree_open(&btree, volume, fork, scratch, sizeof(scratch));
    if (rc != HFS_OK) {
        /* A tree that cannot be opened is a header finding; a tree whose size
         * does not match its node count is the fork's own finding. */
        return rc == HFS_ERR_UNSUPPORTED ? HFS_CHECK_BTREE_SIZE
                                         : HFS_CHECK_BTREE_HEADER;
    }
    *depth = btree.depth;
    *nodes = btree.total_nodes;
    if (btree.total_nodes == 0u)
        return HFS_CHECK_BTREE_HEADER;
    if ((uint64_t)btree.total_nodes * btree.node_size >
        (uint64_t)fork->blocks * volume->block_size)
        return HFS_CHECK_BTREE_SIZE;
    if (btree.empty == 0u &&
        (btree.root_node >= btree.total_nodes ||
         btree.first_leaf >= btree.total_nodes ||
         btree.last_leaf >= btree.total_nodes))
        return HFS_CHECK_BTREE_INDEX;
    return 0u;
}

int hfs_check_volume(struct hfs_volume *volume, struct hfs_check_report *report)
{
    if (volume == NULL || report == NULL)
        return HFS_ERR_ARGUMENT;

    for (size_t index = 0u; index < sizeof(*report); ++index)
        ((uint8_t *)report)[index] = 0u;

    check_header(volume, report);
    if ((report->problems & HFS_CHECK_BAD_BLOCK_SIZE) != 0u ||
        (report->problems & HFS_CHECK_BAD_BLOCK_COUNT) != 0u) {
        /* Without a block size and a block count nothing else can be checked
         * honestly: every fork geometry below depends on both. */
        return HFS_OK;
    }

    check_fork(volume, &volume->allocation, &report->problems);
    check_fork(volume, &volume->extents, &report->problems);
    check_fork(volume, &volume->catalog, &report->problems);
    check_fork(volume, &volume->attributes_file, &report->problems);

    check_allocation(volume, report);

    report->problems |= check_tree(volume, &volume->catalog, &report->catalog_depth,
                                   &report->catalog_nodes);
    report->problems |= check_tree(volume, &volume->extents, &report->extents_depth,
                                   &report->extents_nodes);
    report->problems |= check_tree(volume, &volume->attributes_file,
                                   &report->attributes_depth,
                                   &report->attributes_nodes);
    return HFS_OK;
}
