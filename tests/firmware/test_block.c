/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "block.h"
#include "bounce.h"
#include "endian.h"
#include "error.h"
#include "mem.h"
#include "partition.h"

#include "test_check.h"

#define DISK_SECTORS 128u
#define SECTOR_SIZE 512u
#define APM_UNIT SECTOR_SIZE
#define APM_ENTRIES 2u
/* Apple sbBlkCount is the device size, not the partition-map entry count. */
#define APM_SB_BLOCK_COUNT 0x0ffffff0u

struct fake_disk {
    uint8_t data[DISK_SECTORS * SECTOR_SIZE];
    uint64_t fail_lba;
    int fail;
};

static int fake_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    if (disk->fail && lba == disk->fail_lba)
        return HELIKON_ERR_IO;
    if (lba > DISK_SECTORS || count > DISK_SECTORS - lba)
        return HELIKON_ERR_IO;
    helikon_memcpy(buffer, disk->data + (size_t)lba * SECTOR_SIZE,
                   (size_t)count * SECTOR_SIZE);
    return HELIKON_OK;
}

static int fake_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    if (disk->fail && lba == disk->fail_lba)
        return HELIKON_ERR_IO;
    if (lba > DISK_SECTORS || count > DISK_SECTORS - lba)
        return HELIKON_ERR_IO;
    helikon_memcpy(disk->data + (size_t)lba * SECTOR_SIZE, buffer,
                   (size_t)count * SECTOR_SIZE);
    return HELIKON_OK;
}

static int fake_flush(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    return HELIKON_OK;
}

static int fake_power(void *context, int on, uint32_t timeout_ms)
{
    (void)context;
    (void)on;
    (void)timeout_ms;
    return HELIKON_OK;
}

static const struct helikon_block_ops ops = {
    .read = fake_read,
    .write = fake_write,
    .flush = fake_flush,
    .power = fake_power
};

static void put_be32(uint8_t *data, uint32_t value)
{
    helikon_put_be32(data, value);
}

static void build_apm(struct fake_disk *disk)
{
    helikon_memset(disk, 0, sizeof(*disk));
    disk->data[0] = 0x45u; /* 'E' */
    disk->data[1] = 0x52u; /* 'R' */
    disk->data[2] = 0x02u; /* unit 512, big-endian */
    disk->data[3] = 0x00u;
    put_be32(disk->data + 4, APM_SB_BLOCK_COUNT);

    uint8_t *entry1 = disk->data + APM_UNIT;
    entry1[0] = 0x50u; /* 'P' */
    entry1[1] = 0x4du; /* 'M' */
    put_be32(entry1 + 4, APM_ENTRIES);
    put_be32(entry1 + 8, 4u);  /* start block */
    put_be32(entry1 + 12, 64u); /* block count */
    helikon_memcpy(entry1 + 48, "Apple_HFS", 10u);

    uint8_t *entry2 = disk->data + 2u * APM_UNIT;
    entry2[0] = 0x50u;
    entry2[1] = 0x4du;
    put_be32(entry2 + 4, APM_ENTRIES);
    put_be32(entry2 + 8, 68u);
    put_be32(entry2 + 12, 60u);
    helikon_memcpy(entry2 + 48, "Apple_Free", 10u);

    uint8_t *hfs = disk->data + 4u * SECTOR_SIZE + 1024u;
    hfs[0] = 0x48u; /* 'H' */
    hfs[1] = 0x2bu; /* '+' */
    hfs[2] = 0x00u;
    hfs[3] = 0x04u;
    put_be32(hfs + 40, 512u);
    put_be32(hfs + 44, 64u);
    put_be32(hfs + 48, 32u);
}

int main(void)
{
    struct fake_disk disk;
    struct helikon_block_info info = {
        .sector_size = SECTOR_SIZE,
        .sector_count = DISK_SECTORS,
        .flags = HELIKON_BLOCK_FLAG_READ_ONLY
    };
    struct helikon_block_device device;

    helikon_memset(&disk, 0, sizeof(disk));
    for (unsigned int i = 0; i < sizeof(disk.data); ++i)
        disk.data[i] = (uint8_t)(i & 0xffu);

    CHECK_EQ(HELIKON_OK, helikon_block_init(&device, &ops, &disk, &info));
    CHECK_EQ(SECTOR_SIZE, device.info.sector_size);
    CHECK_EQ(DISK_SECTORS * SECTOR_SIZE,
             helikon_block_capacity_bytes(&device));

    uint8_t sector[SECTOR_SIZE];
    CHECK_EQ(HELIKON_OK, helikon_block_read(&device, 0, 1, sector, 100));
    CHECK(helikon_memcmp(sector, disk.data, SECTOR_SIZE) == 0);
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_block_read(&device, DISK_SECTORS, 1, sector, 100));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_block_read(&device, 0, 0, sector, 100));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_block_read(&device, 0, 1, NULL, 100));
    CHECK_EQ(HELIKON_ERR_READ_ONLY,
             helikon_block_write(&device, 0, 1, sector, 100));
    CHECK_EQ(HELIKON_ERR_READ_ONLY, helikon_block_flush(&device, 100));

    uint8_t scratch[SECTOR_SIZE];
    uint8_t bytes[100];
    CHECK_EQ(HELIKON_OK, helikon_block_read_bytes(&device, 513, bytes,
                                                  sizeof(bytes), scratch,
                                                  sizeof(scratch), 100));
    CHECK(helikon_memcmp(bytes, disk.data + 513, sizeof(bytes)) == 0);
    uint8_t aligned[1024];
    CHECK_EQ(HELIKON_OK, helikon_block_read_bytes(&device, 0, aligned,
                                                  sizeof(aligned), scratch,
                                                  sizeof(scratch), 100));
    CHECK(helikon_memcmp(aligned, disk.data, sizeof(aligned)) == 0);
    CHECK_EQ(HELIKON_ERR_BUFFER,
             helikon_block_read_bytes(&device, 513, bytes, sizeof(bytes),
                                      scratch, 256, 100));

    struct helikon_block_info rw_info = {
        .sector_size = SECTOR_SIZE,
        .sector_count = DISK_SECTORS,
        .flags = 0
    };
    struct helikon_block_device rw_device;
    CHECK_EQ(HELIKON_OK, helikon_block_init(&rw_device, &ops, &disk,
                                            &rw_info));
    uint8_t write_sector[SECTOR_SIZE];
    helikon_memset(write_sector, 0x3c, sizeof(write_sector));
    CHECK_EQ(HELIKON_OK,
             helikon_block_write(&rw_device, 5, 1, write_sector, 100));
    CHECK(helikon_memcmp(disk.data + 5 * SECTOR_SIZE, write_sector,
                         SECTOR_SIZE) == 0);
    CHECK_EQ(HELIKON_OK, helikon_block_flush(&rw_device, 100));

    uint8_t partial[100];
    helikon_memset(partial, 0x7e, sizeof(partial));
    CHECK_EQ(HELIKON_OK,
             helikon_block_write_bytes(&rw_device, 6 * SECTOR_SIZE + 100,
                                       partial, sizeof(partial), scratch,
                                       sizeof(scratch), 100));
    CHECK(helikon_memcmp(disk.data + 6 * SECTOR_SIZE + 100, partial,
                         sizeof(partial)) == 0);
    uint8_t copy[512];
    helikon_memset(copy, 0x11, sizeof(copy));
    CHECK_EQ(HELIKON_OK,
             helikon_block_write_bytes(&rw_device, 7 * SECTOR_SIZE, copy,
                                       sizeof(copy), scratch,
                                       sizeof(scratch), 100));
    CHECK(helikon_memcmp(disk.data + 7 * SECTOR_SIZE, copy,
                         sizeof(copy)) == 0);
    CHECK_EQ(HELIKON_ERR_BUFFER,
             helikon_block_write_bytes(&rw_device, 8 * SECTOR_SIZE + 1,
                                       partial, sizeof(partial), scratch,
                                       256, 100));
    CHECK_EQ(HELIKON_ERR_READ_ONLY,
             helikon_block_write(&device, 0, 1, write_sector, 100));

    struct helikon_partition parts[4];
    struct helikon_partition_map map;
    unsigned int count = 0;
    build_apm(&disk);
    int rc = helikon_partition_scan_info(&device, parts, 4, scratch,
                                         sizeof(scratch), 100, &count, &map);
    CHECK_EQ(1, rc);
    CHECK_EQ(1, count);
    CHECK_EQ(4 * SECTOR_SIZE, parts[0].offset);
    CHECK_EQ(64 * SECTOR_SIZE, parts[0].length);
    CHECK_EQ(0xaf, parts[0].type);
    /*
     * The entry count must come from pmMapBlkCnt in the first PM record, not
     * from the huge sbBlkCount at block-0 offset 4.
     */
    CHECK_EQ(HELIKON_PARTITION_MAP_APM, map.kind);
    CHECK_EQ(APM_UNIT, map.unit);
    CHECK_EQ(APM_UNIT, map.sb_block_size);
    CHECK_EQ(APM_SB_BLOCK_COUNT, map.sb_block_count);
    CHECK_EQ(APM_ENTRIES, map.pm_map_blk_count);
    CHECK_EQ(4u, map.hfs_start);
    CHECK_EQ(64u, map.hfs_blocks);
    CHECK_EQ(APM_UNIT, map.hfs_unit);

    build_apm(&disk);
    put_be32(disk.data + APM_UNIT + 4, 0u); /* zero pmMapBlkCnt */
    rc = helikon_partition_scan_info(&device, parts, 4, scratch,
                                     sizeof(scratch), 100, &count, &map);
    CHECK_EQ(0, rc);
    CHECK_EQ(0, count);
    CHECK_EQ(0u, map.pm_map_blk_count);

    build_apm(&disk);
    put_be32(disk.data + APM_UNIT + 4, 5000u); /* unbounded pmMapBlkCnt */
    rc = helikon_partition_scan_info(&device, parts, 4, scratch,
                                     sizeof(scratch), 100, &count, &map);
    CHECK_EQ(0, rc);
    CHECK_EQ(0, count);
    CHECK_EQ(5000u, map.pm_map_blk_count);

    build_apm(&disk);
    put_be32(disk.data + 4, 0xffffffffu); /* larger sbBlkCount still ignored */
    rc = helikon_partition_scan_info(&device, parts, 4, scratch,
                                     sizeof(scratch), 100, &count, &map);
    CHECK_EQ(1, rc);
    CHECK_EQ(1, count);
    CHECK_EQ(0xffffffffu, map.sb_block_count);
    CHECK_EQ(APM_ENTRIES, map.pm_map_blk_count);

    build_apm(&disk);
    disk.data[4 * SECTOR_SIZE + 1024] = 0x00u; /* corrupt HFS signature */
    rc = helikon_partition_scan(&device, parts, 4, scratch,
                                sizeof(scratch), 100, &count);
    CHECK_EQ(1, rc);
    CHECK_EQ(1, count);
    CHECK_EQ(0xaf, parts[0].type);

    build_apm(&disk);
    disk.data[0] = 0x00u; /* corrupt APM signature */
    rc = helikon_partition_scan(&device, parts, 4, scratch,
                                sizeof(scratch), 100, &count);
    CHECK_EQ(0, rc);

    helikon_memset(disk.data, 0, sizeof(disk.data));
    rc = helikon_partition_scan(&device, parts, 4, scratch,
                                sizeof(scratch), 100, &count);
    CHECK_EQ(0, rc);

    build_apm(&disk);
    disk.fail = 1;
    disk.fail_lba = 0;
    rc = helikon_partition_scan(&device, parts, 4, scratch,
                                sizeof(scratch), 100, &count);
    CHECK_EQ(HELIKON_ERR_IO, rc);

    return helikon_test_report("block/partition");
}
