/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ata_block.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"

int helikon_ata_block_info(uint32_t sector_size, uint64_t total_sectors,
                           struct helikon_block_info *info)
{
    if (info == NULL)
        return HELIKON_ERR_ARGUMENT;
    /* 512 is the floor: no ATA device this firmware supports reports less. */
    if (sector_size < 512u || (sector_size & (sector_size - 1u)) != 0u)
        return HELIKON_ERR_FORMAT;
    if (total_sectors == 0u)
        return HELIKON_ERR_FORMAT;
    /*
     * A capacity that does not divide back is a sector count that was read from
     * the wrong words, and guessing which half is right is how a >128 GiB card
     * ends up with a 32-bit size.
     */
    if ((uint64_t)sector_size > 0xFFFFFFFFFFFFFFFFu / total_sectors)
        return HELIKON_ERR_FORMAT;

    info->sector_size = sector_size;
    info->sector_count = total_sectors;
    info->flags = 0u;
    return HELIKON_OK;
}

int helikon_ata_block_init(struct helikon_ata_block *block,
                           struct helikon_ata *ata, int writable)
{
    if (block == NULL || ata == NULL)
        return HELIKON_ERR_ARGUMENT;
    block->ata = ata;

    /* Without IDENTIFY the driver's sector size and count are placeholders. */
    if (ata->identify_valid == 0)
        return HELIKON_ERR_UNSUPPORTED;

    struct helikon_block_info info;
    int rc = helikon_ata_block_info(ata->sector_size, ata->total_sectors, &info);
    if (rc != HELIKON_OK)
        return rc;
    if (writable == 0)
        info.flags |= HELIKON_BLOCK_FLAG_READ_ONLY;

    /*
     * The ATA driver's signatures are the block ops' signatures, so the table is
     * the driver itself rather than four wrappers that could drift apart.
     */
    static const struct helikon_block_ops ops = {
        helikon_ata_read, helikon_ata_write, helikon_ata_flush,
        helikon_ata_power,
    };
    return helikon_block_init(&block->device, &ops, ata, &info);
}

const struct helikon_block_device *helikon_ata_block_device(
    const struct helikon_ata_block *block)
{
    if (block == NULL || block->ata == NULL)
        return NULL;
    return &block->device;
}
