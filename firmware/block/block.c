/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "block.h"

#include "error.h"

int helikon_block_init(struct helikon_block_device *device,
                       const struct helikon_block_ops *ops, void *context,
                       const struct helikon_block_info *info)
{
    if (device == NULL || ops == NULL || info == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (info->sector_size < 512u || info->sector_size > 4096u ||
        (info->sector_size & (info->sector_size - 1u)) != 0u ||
        info->sector_count == 0u)
        return HELIKON_ERR_ARGUMENT;

    device->ops = ops;
    device->context = context;
    device->info = *info;
    return HELIKON_OK;
}

static int range_valid(const struct helikon_block_device *device, uint64_t lba,
                       uint32_t count)
{
    if (device == NULL || device->ops == NULL)
        return 0;
    if (count == 0u || lba > device->info.sector_count)
        return 0;
    return count <= device->info.sector_count - lba;
}

int helikon_block_read(const struct helikon_block_device *device, uint64_t lba,
                       uint32_t count, void *buffer, uint32_t timeout_ms)
{
    if (!range_valid(device, lba, count) || buffer == NULL ||
        device->ops->read == NULL)
        return HELIKON_ERR_ARGUMENT;
    return device->ops->read(device->context, lba, count, buffer, timeout_ms);
}

int helikon_block_write(const struct helikon_block_device *device, uint64_t lba,
                        uint32_t count, const void *buffer,
                        uint32_t timeout_ms)
{
    if (!range_valid(device, lba, count) || buffer == NULL ||
        device->ops->write == NULL)
        return HELIKON_ERR_ARGUMENT;
    if ((device->info.flags & HELIKON_BLOCK_FLAG_READ_ONLY) != 0u)
        return HELIKON_ERR_READ_ONLY;
    return device->ops->write(device->context, lba, count, buffer, timeout_ms);
}

int helikon_block_flush(const struct helikon_block_device *device,
                        uint32_t timeout_ms)
{
    if (device == NULL || device->ops == NULL ||
        device->ops->flush == NULL)
        return HELIKON_ERR_ARGUMENT;
    if ((device->info.flags & HELIKON_BLOCK_FLAG_READ_ONLY) != 0u)
        return HELIKON_ERR_READ_ONLY;
    return device->ops->flush(device->context, timeout_ms);
}

int helikon_block_power(const struct helikon_block_device *device, int on,
                        uint32_t timeout_ms)
{
    if (device == NULL || device->ops == NULL ||
        device->ops->power == NULL)
        return HELIKON_ERR_ARGUMENT;
    return device->ops->power(device->context, on, timeout_ms);
}

uint64_t helikon_block_capacity_bytes(const struct helikon_block_device *device)
{
    if (device == NULL)
        return 0u;
    return device->info.sector_count * (uint64_t)device->info.sector_size;
}
