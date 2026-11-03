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
};

int hfs_bitmap_open(struct hfs_bitmap *bitmap, struct hfs_volume *volume);

int hfs_bitmap_alloc(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                     uint32_t start_hint, uint32_t count,
                     uint32_t *first_block);

int hfs_bitmap_free(struct hfs_bitmap *bitmap, struct hfs_txn *tx,
                    uint32_t start, uint32_t count);

#endif
