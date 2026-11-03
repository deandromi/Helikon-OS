/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_volume_write.h"

#include <string.h>

#define HEADER_SIZE 512u
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

static int stage_one(struct hfs_txn *tx, const struct hfs_volume *volume,
                     uint64_t offset, const struct hfs_volume_update *update)
{
    uint8_t header[HEADER_SIZE];
    if (volume->read(volume->context, volume->base + offset, header,
                     sizeof(header)))
        return HFS_ERR_IO;

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

    if (offset % 512u != 0)
        return HFS_ERR_FORMAT;
    return hfs_txn_stage(tx, offset / 512u, header, sizeof(header));
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
