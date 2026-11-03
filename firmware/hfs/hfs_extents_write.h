/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_EXTENTS_WRITE_H
#define HELIKON_HFS_EXTENTS_WRITE_H

#include "hfs_txn.h"
#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

/*
 * One extents-overflow record holds extents 8..15 of a data fork. The key is
 * forkType=0, fileID, startBlock=8. `count` is 1..8.
 */
int hfs_extents_overflow_set(struct hfs_volume *volume, struct hfs_txn *tx,
                             uint32_t file_id, uint32_t start_block,
                             const struct hfs_extent *extents, size_t count);

int hfs_extents_overflow_get(struct hfs_volume *volume, struct hfs_txn *tx,
                             uint32_t file_id, uint32_t start_block,
                             struct hfs_extent *extents, size_t capacity,
                             size_t *count);

int hfs_extents_overflow_clear(struct hfs_volume *volume, struct hfs_txn *tx,
                               uint32_t file_id, uint32_t start_block);

#endif
