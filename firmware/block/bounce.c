/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bounce.h"

#include "error.h"
#include "mem.h"

static int read_aligned(const struct helikon_block_device *device,
                        uint64_t offset, uint8_t *output, size_t length,
                        uint32_t timeout_ms)
{
    uint32_t sector_size = device->info.sector_size;
    uint64_t lba = offset / sector_size;
    size_t sectors = length / sector_size;

    while (sectors != 0u) {
        uint32_t chunk = sectors > 0xffffu ? 0xffffu : (uint32_t)sectors;
        int rc = helikon_block_read(device, lba, chunk, output, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        lba += chunk;
        output += (size_t)chunk * sector_size;
        sectors -= chunk;
    }
    return HELIKON_OK;
}

int helikon_block_read_bytes(const struct helikon_block_device *device,
                             uint64_t offset, void *buffer, size_t length,
                             uint8_t *scratch, size_t scratch_size,
                             uint32_t timeout_ms)
{
    if (device == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    if (length == 0u)
        return HELIKON_OK;

    uint64_t capacity = helikon_block_capacity_bytes(device);
    if (offset > capacity || (uint64_t)length > capacity - offset)
        return HELIKON_ERR_ARGUMENT;

    uint8_t *output = buffer;
    uint32_t sector_size = device->info.sector_size;

    /*
     * The ATA PIO fast path is happy with a 4-byte aligned destination. A
     * 512-byte sector is also 4-byte aligned, so only the destination and the
     * byte offset decide whether the direct path is safe.
     */
    if ((offset % sector_size) == 0u && (length % sector_size) == 0u &&
        (((uintptr_t)output & 3u) == 0u))
        return read_aligned(device, offset, output, length, timeout_ms);

    if (scratch == NULL || scratch_size < sector_size)
        return HELIKON_ERR_BUFFER;

    while (length != 0u) {
        uint64_t lba = offset / sector_size;
        uint32_t in_sector = (uint32_t)(offset % sector_size);
        size_t chunk = sector_size - in_sector;
        if (chunk > length)
            chunk = length;

        int rc = helikon_block_read(device, lba, 1, scratch, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        helikon_memcpy(output, scratch + in_sector, chunk);

        output += chunk;
        offset += chunk;
        length -= chunk;
    }
    return HELIKON_OK;
}

static int write_aligned(const struct helikon_block_device *device,
                         uint64_t offset, const uint8_t *input, size_t length,
                         uint32_t timeout_ms)
{
    uint32_t sector_size = device->info.sector_size;
    uint64_t lba = offset / sector_size;
    size_t sectors = length / sector_size;

    while (sectors != 0u) {
        uint32_t chunk = sectors > 0xffffu ? 0xffffu : (uint32_t)sectors;
        int rc = helikon_block_write(device, lba, chunk, input, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        lba += chunk;
        input += (size_t)chunk * sector_size;
        sectors -= chunk;
    }
    return HELIKON_OK;
}

int helikon_block_write_bytes(const struct helikon_block_device *device,
                              uint64_t offset, const void *buffer,
                              size_t length, uint8_t *scratch,
                              size_t scratch_size, uint32_t timeout_ms)
{
    if (device == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    if (length == 0u)
        return HELIKON_OK;

    uint64_t capacity = helikon_block_capacity_bytes(device);
    if (offset > capacity || (uint64_t)length > capacity - offset)
        return HELIKON_ERR_ARGUMENT;

    const uint8_t *input = buffer;
    uint32_t sector_size = device->info.sector_size;

    if ((offset % sector_size) == 0u && (length % sector_size) == 0u &&
        (((uintptr_t)input & 3u) == 0u))
        return write_aligned(device, offset, input, length, timeout_ms);

    if (scratch == NULL || scratch_size < sector_size)
        return HELIKON_ERR_BUFFER;

    while (length != 0u) {
        uint64_t lba = offset / sector_size;
        uint32_t in_sector = (uint32_t)(offset % sector_size);
        size_t chunk = sector_size - in_sector;
        if (chunk > length)
            chunk = length;

        int rc = helikon_block_read(device, lba, 1, scratch, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        helikon_memcpy(scratch + in_sector, input, chunk);
        rc = helikon_block_write(device, lba, 1, scratch, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;

        input += chunk;
        offset += chunk;
        length -= chunk;
    }
    return HELIKON_OK;
}
