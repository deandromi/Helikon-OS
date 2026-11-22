/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_BITMAP_H
#define HELIKON_HFS_BITMAP_H

#include "hfs_txn.h"
#include "hfs_volume.h"

#include <stdint.h>

struct hfs_bitmap {
    struct hfs_volume *volume;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t modify_date; /* 0 leaves the volume modify date unchanged. */
};

int hfs_bitmap_open(struct hfs_bitmap *bitmap, struct hfs_volume *volume);

/*
 * Find a free run of `count` blocks. `tx` may be NULL to search the committed
 * bitmap; pass the open transaction so a search after a staged allocation does
 * not hand out the same block twice.
 *
 * The search starts at `start_hint` and wraps to the start of the volume, so a
 * hint above the free space never turns into NO_SPACE. A run never spans two
 * bitmap blocks, so one allocation is bounded by what a single block of the
 * bitmap describes (block_size * 8 blocks) and a larger `count` is refused.
 * A `start_hint` at or past the end of the volume starts the search at block 0.
 */
int hfs_bitmap_find(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                    uint32_t start_hint, uint32_t count,
                    uint32_t *first_block);
int hfs_bitmap_alloc(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                     uint32_t start_hint, uint32_t count,
                     uint32_t *first_block);

/*
 * Marks a range free. The whole range is validated before anything is staged:
 * a refused free (a block that is not allocated) leaves the transaction
 * untouched rather than keeping half-cleared bits.
 */
int hfs_bitmap_free(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                    uint32_t start, uint32_t count);

/*
 * Read the committed bitmap for one block: 1 when it is allocated, 0 when it is
 * free. Read-only and outside any transaction, which is what a checker or a
 * host test wants; a staged allocation needs the transaction overlay instead.
 */
int hfs_bitmap_block_used(struct hfs_bitmap *bitmap, uint32_t block, int *used);

#endif
