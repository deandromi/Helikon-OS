/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_EXTENTS_H
#define HELIKON_HFS_EXTENTS_H

#include "hfs_btree.h"
#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

#define HFS_OVERFLOW_EXTENT_MAX 128u

struct hfs_extents {
    struct hfs_btree btree;
};

int hfs_extents_open(struct hfs_extents *extents, struct hfs_volume *volume,
                     void *scratch, size_t scratch_size);

int hfs_extents_collect(struct hfs_extents *extents, uint32_t file_id,
                        uint32_t inline_blocks, uint32_t fork_blocks,
                        struct hfs_extent *out, size_t capacity,
                        size_t *count);

/*
 * Read a file through its inline and extents-overflow extents. The fork and
 * file id come from the catalog entry.
 */
int hfs_extents_read(struct hfs_extents *extents, const struct hfs_fork *fork,
                     uint32_t file_id, uint64_t offset, void *buffer,
                     size_t length, size_t *done);

#endif
