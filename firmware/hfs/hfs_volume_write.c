/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_volume_write.h"

#include <string.h>

#define MODIFY_DATE_OFFSET 20u
#define FILE_COUNT_OFFSET 32u
#define FOLDER_COUNT_OFFSET 36u
#define NEXT_CNID_OFFSET 64u
#define WRITE_COUNT_OFFSET 68u

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/*
 * Stage the journal-sector-sized block containing a volume header, not just
 * the 512-byte header. A journal with 4 KiB sectors requires whole-sector
 * transaction blocks; a 512-byte stage is rejected by hfs_txn_stage().
 */
static int stage_one(struct hfs_txn *tx, const struct hfs_volume *volume,
                     uint64_t offset, const struct hfs_volume_update *update)
{
    uint32_t sector_size = tx->journal->sector_size;
    if (sector_size < 512u || sector_size > 4096u ||
        volume->length < sector_size || offset > volume->length ||
        sector_size > volume->length - offset % sector_size)
        return HFS_ERR_FORMAT;

    uint64_t block_start = offset - offset % sector_size;
    if (block_start > volume->length - sector_size)
        return HFS_ERR_FORMAT;

    static uint8_t block[4096];
    if (volume->read(volume->context, volume->base + block_start, block,
                     sector_size))
        return HFS_ERR_IO;

    uint8_t *header = block + (offset - block_start);
    if (update->set_modify_date)
        put32(header + MODIFY_DATE_OFFSET, update->modify_date);
    if (update->set_file_count)
        put32(header + FILE_COUNT_OFFSET, update->file_count);
    if (update->set_folder_count)
        put32(header + FOLDER_COUNT_OFFSET, update->folder_count);
    if (update->set_next_cnid)
        put32(header + NEXT_CNID_OFFSET, update->next_cnid);
    put32(header + WRITE_COUNT_OFFSET,
          be32(header + WRITE_COUNT_OFFSET) + 1u);

    return hfs_txn_stage(tx, block_start / sector_size, block, sector_size);
}

int hfs_volume_stage(struct hfs_txn *tx, const struct hfs_volume *volume,
                     const struct hfs_volume_update *update)
{
    if (tx == NULL || volume == NULL || update == NULL)
        return HFS_ERR_ARGUMENT;
    if (volume->length < 2048u)
        return HFS_ERR_FORMAT;

    int rc = stage_one(tx, volume, HFS_VOLUME_HEADER_OFFSET, update);
    if (rc != HFS_OK)
        return rc;
    return stage_one(tx, volume, volume->length - 1024u, update);
}
