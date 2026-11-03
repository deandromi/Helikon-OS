/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_fork_io.h"

#include <string.h>

static int stage_volume_block(struct hfs_txn *tx, const struct hfs_volume *volume,
                              uint32_t volume_block, const uint8_t *data,
                              uint32_t offset, uint32_t length,
                              uint8_t *bounce)
{
    uint32_t block_size = volume->block_size;
    if (block_size > 32768u || block_size % tx->journal->sector_size != 0)
        return HFS_ERR_UNSUPPORTED;
    if (offset == 0 && length == block_size) {
        uint64_t sector = (uint64_t)volume_block *
                          (block_size / tx->journal->sector_size);
        return hfs_txn_stage(tx, sector, data, block_size);
    }

    uint64_t base = volume->base + (uint64_t)volume_block * block_size;
    if (volume->read(volume->context, base, bounce, block_size))
        return HFS_ERR_IO;
    memcpy(bounce + offset, data, length);
    uint64_t sector = (uint64_t)volume_block *
                      (block_size / tx->journal->sector_size);
    return hfs_txn_stage(tx, sector, bounce, block_size);
}

int hfs_fork_stage(struct hfs_txn *tx, const struct hfs_volume *volume,
                   const struct hfs_fork *fork, uint64_t offset,
                   const void *data, size_t length)
{
    if (tx == NULL || volume == NULL || fork == NULL || data == NULL ||
        length == 0)
        return HFS_ERR_ARGUMENT;
    if (offset > fork->size || length > fork->size - offset)
        return HFS_ERR_ARGUMENT;
    if (volume->block_size == 0 || volume->block_size > 32768u ||
        volume->block_size % tx->journal->sector_size != 0)
        return HFS_ERR_UNSUPPORTED;

    uint8_t bounce[32768];
    const uint8_t *source = data;
    uint64_t end = offset + length;
    uint64_t extent_start = 0;

    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        uint32_t count = fork->extents[i].count;
        if (count == 0)
            continue;
        uint64_t extent_bytes = (uint64_t)count * volume->block_size;
        uint64_t extent_end = extent_start + extent_bytes;
        if (offset < extent_end && end > extent_start) {
            uint64_t from = offset > extent_start ? offset : extent_start;
            uint64_t to = end < extent_end ? end : extent_end;
            uint64_t local = from - extent_start;
            uint32_t volume_block = fork->extents[i].start +
                                    (uint32_t)(local / volume->block_size);
            uint32_t in_block = (uint32_t)(local % volume->block_size);
            uint64_t remaining = to - from;
            const uint8_t *src = source + (from - offset);

            while (remaining > 0) {
                uint32_t chunk = volume->block_size - in_block;
                if (chunk > remaining)
                    chunk = (uint32_t)remaining;
                int rc = stage_volume_block(tx, volume, volume_block, src,
                                            in_block, chunk, bounce);
                if (rc != HFS_OK)
                    return rc;
                src += chunk;
                remaining -= chunk;
                ++volume_block;
                in_block = 0;
            }
        }
        if (extent_end >= end)
            return HFS_OK;
        extent_start = extent_end;
    }

    return HFS_ERR_UNSUPPORTED;
}
