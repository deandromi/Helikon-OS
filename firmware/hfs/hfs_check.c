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
#include "hfs_extents.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

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
 * Read a fork through a resolved extent map: the inline extents plus whatever
 * the extents overflow tree supplied, already in logical order. A range no run
 * covers is a hole, and a hole in the allocation file is a finding, not a
 * reason to guess.
 */
static int read_map(struct hfs_volume *volume, const struct hfs_extent *map,
                    size_t count, uint64_t offset, void *buffer, size_t length)
{
    uint8_t *out = buffer;
    const uint64_t block_size = volume->block_size;
    size_t done = 0u;
    while (done < length) {
        uint64_t run_start = 0u;
        int found = 0;
        for (size_t index = 0u; index < count; ++index) {
            uint64_t run_bytes = (uint64_t)map[index].count * block_size;
            if (offset < run_start || offset >= run_start + run_bytes) {
                run_start += run_bytes;
                continue;
            }
            uint64_t within = offset - run_start;
            size_t chunk = length - done;
            if ((uint64_t)chunk > run_bytes - within)
                chunk = (size_t)(run_bytes - within);
            if ((uint64_t)chunk > block_size)
                chunk = (size_t)block_size;
            uint64_t physical = volume->base +
                                (uint64_t)map[index].start * block_size + within;
            if (volume->read == NULL ||
                volume->read(volume->context, physical, out + done, chunk) != 0)
                return HFS_ERR_IO;
            done += chunk;
            offset += chunk;
            found = 1;
            break;
        }
        if (found == 0)
            return HFS_ERR_UNSUPPORTED;
    }
    return HFS_OK;
}

/*
 * The allocation bitmap. Its fork may spill into the extents overflow tree, so
 * the map is resolved first: a large volume's bitmap is several megabytes in
 * dozens of runs, and skipping it would leave the free-block count unverified on
 * exactly the volumes where it matters most. The pass is I/O-bound by design and
 * belongs off the boot path.
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
    if (needed > HFS_CHECK_BITMAP_BYTES) {
        report->skipped |= HFS_CHECK_SKIPPED_BITMAP;
        return;
    }

    /* HFS+ gives the allocation file a fixed CNID of 6. */
    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_extents extents;
    struct hfs_extent map[HFS_OVERFLOW_EXTENT_MAX];
    size_t map_count = 0u;
    uint32_t inline_count = inline_blocks(fork);
    int have_map = 0;
    if (inline_count == fork->blocks) {
        for (size_t index = 0u; index < HFS_EXTENT_COUNT; ++index) {
            map[index] = fork->extents[index];
        }
        map_count = HFS_EXTENT_COUNT;
        have_map = 1;
    } else if (hfs_extents_open(&extents, volume, scratch, sizeof(scratch)) ==
                   HFS_OK &&
               hfs_extents_collect(&extents, 6u, inline_count, fork->blocks, map,
                                   HFS_OVERFLOW_EXTENT_MAX, &map_count) ==
                   HFS_OK) {
        have_map = 1;
    }
    if (have_map == 0) {
        report->skipped |= HFS_CHECK_SKIPPED_BITMAP;
        return;
    }

    uint8_t buffer[4096];
    uint32_t counted = 0u;
    uint64_t block = 0u;
    uint64_t byte_offset = 0u;
    while (byte_offset < needed) {
        uint32_t chunk = (uint32_t)(needed - byte_offset);
        if (chunk > sizeof(buffer))
            chunk = sizeof(buffer);
        if (read_map(volume, map, map_count, byte_offset, buffer, chunk) !=
            HFS_OK) {
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

/*
 * The catalog: walk every leaf record, check the key the record claims to have,
 * and count what is actually there. The volume header keeps the file and folder
 * counts, and fsck checks them the same way; a count that drifts is the quietest
 * kind of catalog damage.
 */
static void check_catalog(struct hfs_volume *volume,
                          struct hfs_check_report *report)
{
    uint8_t header[HFS_VOLUME_HEADER_SIZE];
    if (volume->read == NULL ||
        volume->read(volume->context,
                     volume->base + HFS_VOLUME_HEADER_OFFSET, header,
                     sizeof(header)) != 0) {
        report->problems |= HFS_CHECK_CATALOG_RECORD;
        return;
    }
    report->header_files = be32(header + 32);
    report->header_folders = be32(header + 36);

    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_btree btree;
    if (hfs_btree_open(&btree, volume, &volume->catalog, scratch,
                       sizeof(scratch)) != HFS_OK) {
        report->problems |= HFS_CHECK_BTREE_HEADER;
        return;
    }
    if (btree.empty != 0u) {
        report->catalog_files = 0u;
        report->catalog_folders = 0u;
    } else {
        struct hfs_btree_leaf_iterator iterator;
        hfs_btree_leaf_init(&btree, &iterator);
        for (;;) {
            const uint8_t *record = NULL;
            size_t length = 0u;
            int rc = hfs_btree_leaf_next(&btree, &iterator, &record, &length);
            if (rc < 0) {
                report->problems |= HFS_CHECK_CATALOG_RECORD;
                break;
            }
            if (rc == 0)
                break;
            /*
             * Catalog key: keyLength(2), parentID(4), nodeName(2 + 2*n). The
             * keyLength field counts everything after itself, so the record
             * type sits at `2 + keyLength`. A key that does not fit its record,
             * or a type byte outside 1..4, is a damaged leaf.
             */
            uint16_t key_length = be16(record);
            size_t key_total = (size_t)key_length + 2u;
            if (key_length < 6u || key_length > btree.node_size + 4u ||
                key_total >= length) {
                report->problems |= HFS_CHECK_CATALOG_RECORD;
                continue;
            }
            uint16_t name_length = be16(record + 6);
            if ((uint32_t)name_length * 2u + 6u > key_length) {
                report->problems |= HFS_CHECK_CATALOG_RECORD;
                continue;
            }
            /* Catalog records start with a 16-bit recordType, not a byte. */
            if (key_total + 2u > length) {
                report->problems |= HFS_CHECK_CATALOG_RECORD;
                continue;
            }
            uint32_t parent = be32(record + 2);
            uint16_t type = be16(record + key_total);
            /*
             * The root folder has a catalog record of its own — parentID 1, its
             * name is the volume name — but the volume header's folder count
             * excludes it, so it is not counted here either. Every other folder
             * has a real parent CNID (the root's is 2).
             */
            if (type == 1u) {
                if (parent != 1u)
                    report->catalog_folders += 1u;
            } else if (type == 2u) {
                report->catalog_files += 1u;
            }
        }
    }

    if (report->catalog_files != report->header_files ||
        report->catalog_folders != report->header_folders)
        report->problems |= HFS_CHECK_CATALOG_COUNT;
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

    if (report->catalog_nodes != 0u)
        check_catalog(volume, report);
    return HFS_OK;
}
