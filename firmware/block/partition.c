/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "partition.h"

#include "bounce.h"
#include "endian.h"
#include "error.h"
#include "mem.h"

#define PARTITION_RECORD_SIZE 512u

static int probe_hfs(const struct helikon_block_device *device,
                     uint64_t offset, uint64_t length, uint8_t *scratch,
                     size_t scratch_size, uint32_t timeout_ms)
{
    uint8_t header[512];

    if (length < 1536u || offset > helikon_block_capacity_bytes(device) ||
        length > helikon_block_capacity_bytes(device) - offset)
        return 0;

    int rc = helikon_block_read_bytes(device, offset + 1024u, header,
                                      sizeof(header), scratch, scratch_size,
                                      timeout_ms);
    if (rc != HELIKON_OK)
        return 0;

    if (helikon_be16(header) != 0x482bu || helikon_be16(header + 2) != 4u)
        return 0;

    uint32_t block_size = helikon_be32(header + 40);
    uint32_t blocks = helikon_be32(header + 44);
    uint32_t free_blocks = helikon_be32(header + 48);
    if (block_size < 512u || (block_size & (block_size - 1u)) != 0u ||
        blocks == 0u || free_blocks > blocks ||
        (uint64_t)blocks * block_size > length)
        return 0;

    return 0xaf;
}

static int add_partition(struct helikon_partition *parts, unsigned int capacity,
                         unsigned int *count, uint64_t offset, uint64_t length,
                         uint32_t unit, uint8_t type,
                         const struct helikon_block_device *device)
{
    if (length == 0u || offset > helikon_block_capacity_bytes(device) ||
        length > helikon_block_capacity_bytes(device) - offset)
        return HELIKON_ERR_FORMAT;

    for (unsigned int i = 0; i < *count; ++i) {
        if (offset < parts[i].offset + parts[i].length &&
            parts[i].offset < offset + length)
            return HELIKON_ERR_FORMAT;
    }
    if (*count >= capacity)
        return HELIKON_ERR_BUFFER;

    parts[*count].offset = offset;
    parts[*count].length = length;
    parts[*count].unit = unit;
    parts[*count].type = type;
    ++*count;
    return HELIKON_OK;
}

static int scan_apm(const struct helikon_block_device *device,
                    struct helikon_partition *parts, unsigned int capacity,
                    uint8_t *scratch, size_t scratch_size, uint32_t timeout_ms,
                    unsigned int *count, struct helikon_partition_map *map)
{
    uint8_t record[PARTITION_RECORD_SIZE];

    int rc = helikon_block_read_bytes(device, 0, record, sizeof(record),
                                      scratch, scratch_size, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    if (helikon_be16(record) != 0x4552u)
        return HELIKON_ERR_UNSUPPORTED;

    uint32_t unit = helikon_be16(record + 2);
    uint32_t sb_blocks = helikon_be32(record + 4);
    if (map != NULL) {
        map->kind = (uint8_t)HELIKON_PARTITION_MAP_APM;
        map->unit = unit;
        map->sb_block_size = unit;
        map->sb_block_count = sb_blocks;
    }
    if (unit < 512u || unit > 4096u || (unit & (unit - 1u)) != 0u ||
        (uint64_t)unit > helikon_block_capacity_bytes(device))
        return HELIKON_ERR_FORMAT;

    /*
     * Apple's block-0 offset 4 is sbBlkCount, the size of the device in
     * blocks, not the number of partition-map entries. The entry count is
     * pmMapBlkCnt in the first PM record at block 1. Read that record first
     * and bound the count before iterating.
     */
    rc = helikon_block_read_bytes(device, unit, record, sizeof(record),
                                  scratch, scratch_size, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    if (helikon_be16(record) != 0x504du)
        return HELIKON_ERR_FORMAT;

    uint32_t entries = helikon_be32(record + 4);
    if (map != NULL)
        map->pm_map_blk_count = entries;
    if (entries == 0u || entries > 4096u ||
        (uint64_t)(entries + 1u) * unit >
            helikon_block_capacity_bytes(device))
        return HELIKON_ERR_FORMAT;

    for (uint32_t index = 1u; index <= entries; ++index) {
        rc = helikon_block_read_bytes(device, (uint64_t)index * unit, record,
                                      sizeof(record), scratch, scratch_size,
                                      timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if (helikon_be16(record) != 0x504du ||
            helikon_be32(record + 4) != entries)
            return HELIKON_ERR_FORMAT;
        if (helikon_memcmp(record + 48, "Apple_HFS", 10u) != 0)
            continue;

        uint32_t start = helikon_be32(record + 8);
        uint32_t blocks = helikon_be32(record + 12);
        if (map != NULL && map->hfs_unit == 0u) {
            map->hfs_start = start;
            map->hfs_blocks = blocks;
            map->hfs_unit = unit;
        }
        rc = add_partition(parts, capacity, count, (uint64_t)start * unit,
                           (uint64_t)blocks * unit, unit, 0xaf, device);
        if (rc == HELIKON_ERR_BUFFER)
            return rc;
        if (rc != HELIKON_OK)
            continue;
    }
    return HELIKON_OK;
}

static int scan_mbr(const struct helikon_block_device *device,
                    struct helikon_partition *parts, unsigned int capacity,
                    uint8_t *scratch, size_t scratch_size, uint32_t timeout_ms,
                    unsigned int *count, struct helikon_partition_map *map)
{
    uint8_t record[PARTITION_RECORD_SIZE];

    int rc = helikon_block_read_bytes(device, 0, record, sizeof(record),
                                      scratch, scratch_size, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    if (record[510] != 0x55u || record[511] != 0xaau)
        return HELIKON_ERR_UNSUPPORTED;
    if (map != NULL)
        map->kind = (uint8_t)HELIKON_PARTITION_MAP_MBR;

    for (unsigned int i = 0; i < 4u; ++i) {
        const uint8_t *entry = record + 446u + i * 16u;
        if (entry[4] != 0xafu)
            continue;

        uint32_t start = helikon_le32(entry + 8);
        uint32_t blocks = helikon_le32(entry + 12);
        if (start == 0u || blocks == 0u)
            continue;

        uint32_t units[2] = { 512u, 4096u };
        for (unsigned int u = 0; u < 2u; ++u) {
            uint64_t offset = (uint64_t)start * units[u];
            uint64_t length = (uint64_t)blocks * units[u];
            if (probe_hfs(device, offset, length, scratch, scratch_size,
                          timeout_ms) != 0xaf)
                continue;
            if (map != NULL && map->hfs_unit == 0u) {
                map->hfs_start = start;
                map->hfs_blocks = blocks;
                map->hfs_unit = units[u];
                map->unit = units[u];
            }
            rc = add_partition(parts, capacity, count, offset, length,
                               units[u], 0xaf, device);
            if (rc == HELIKON_ERR_BUFFER)
                return rc;
            break;
        }
    }
    return HELIKON_OK;
}

int helikon_partition_scan_info(const struct helikon_block_device *device,
                                struct helikon_partition *parts,
                                unsigned int capacity, uint8_t *scratch,
                                size_t scratch_size, uint32_t timeout_ms,
                                unsigned int *count,
                                struct helikon_partition_map *map)
{
    if (device == NULL || parts == NULL || capacity == 0u || scratch == NULL ||
        scratch_size < PARTITION_RECORD_SIZE || count == NULL)
        return HELIKON_ERR_ARGUMENT;

    *count = 0;
    if (map != NULL)
        helikon_memset(map, 0, sizeof(*map));

    int rc = scan_apm(device, parts, capacity, scratch, scratch_size,
                      timeout_ms, count, map);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_UNSUPPORTED &&
        rc != HELIKON_ERR_FORMAT)
        return rc;

    if (*count == 0u) {
        rc = scan_mbr(device, parts, capacity, scratch, scratch_size,
                      timeout_ms, count, map);
        if (rc != HELIKON_OK && rc != HELIKON_ERR_UNSUPPORTED &&
            rc != HELIKON_ERR_FORMAT)
            return rc;
    }

    if (*count == 0u) {
        uint64_t capacity_bytes = helikon_block_capacity_bytes(device);
        if (probe_hfs(device, 0, capacity_bytes, scratch, scratch_size,
                      timeout_ms) == 0xaf) {
            rc = add_partition(parts, capacity, count, 0, capacity_bytes,
                               device->info.sector_size, 0xaf, device);
            if (rc != HELIKON_OK)
                return rc;
            if (map != NULL) {
                map->kind = (uint8_t)HELIKON_PARTITION_MAP_HFS;
                map->unit = device->info.sector_size;
                map->hfs_unit = device->info.sector_size;
                map->hfs_start = 0u;
            }
        }
    }

    return (int)*count;
}

int helikon_partition_scan(const struct helikon_block_device *device,
                           struct helikon_partition *parts,
                           unsigned int capacity, uint8_t *scratch,
                           size_t scratch_size, uint32_t timeout_ms,
                           unsigned int *count)
{
    return helikon_partition_scan_info(device, parts, capacity, scratch,
                                       scratch_size, timeout_ms, count, NULL);
}
