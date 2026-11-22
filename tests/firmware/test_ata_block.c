/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host test for the ATA-to-block bridge (T-126).
 *
 * The bridge itself is ten lines, but one of them is the arithmetic that turns a
 * sector size and a sector count into a capacity, and that arithmetic is where a
 * disk larger than 128 GiB stops being a disk. The ATA driver is replaced by
 * stubs here on purpose: what is under test is the info the block device
 * reports, the read-only gate in front of the driver, and the fact that the
 * operations really are the driver's functions rather than four wrappers that
 * could drift apart.
 */
#include "ata_block.h"

#include "helikon_error.h"
#include "test_check.h"

/* The driver, replaced. Each call is counted so the test can prove delegation. */
static int read_calls;
static int write_calls;

int helikon_ata_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms)
{
    (void)context;
    (void)lba;
    (void)count;
    (void)buffer;
    (void)timeout_ms;
    ++read_calls;
    return HELIKON_OK;
}

int helikon_ata_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms)
{
    (void)context;
    (void)lba;
    (void)count;
    (void)buffer;
    (void)timeout_ms;
    ++write_calls;
    return HELIKON_OK;
}

int helikon_ata_flush(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    return HELIKON_OK;
}

int helikon_ata_power(void *context, int on, uint32_t timeout_ms)
{
    (void)context;
    (void)on;
    (void)timeout_ms;
    return HELIKON_OK;
}

static void test_info(void)
{
    struct helikon_block_info info;

    CHECK_EQ(HELIKON_OK, helikon_ata_block_info(512u, 0x100000u, &info));
    CHECK_EQ(512, info.sector_size);
    CHECK_EQ(0x100000, info.sector_count);
    CHECK_EQ(0, info.flags);

    /* 1 TiB at 512 bytes: the count passes 2^31, the capacity 2^40. */
    CHECK_EQ(HELIKON_OK,
             helikon_ata_block_info(512u, 0x80000000ull, &info));
    CHECK_EQ(0x80000000ull, info.sector_count);

    /* A 4 KiB logical-sector device reports sectors, not bytes. */
    CHECK_EQ(HELIKON_OK, helikon_ata_block_info(4096u, 0x100000u, &info));
    CHECK_EQ(4096, info.sector_size);

    /* What cannot be a disk. */
    CHECK_EQ(HELIKON_ERR_FORMAT, helikon_ata_block_info(0u, 16u, &info));
    CHECK_EQ(HELIKON_ERR_FORMAT, helikon_ata_block_info(256u, 16u, &info));
    CHECK_EQ(HELIKON_ERR_FORMAT, helikon_ata_block_info(513u, 16u, &info));
    CHECK_EQ(HELIKON_ERR_FORMAT, helikon_ata_block_info(512u, 0u, &info));
    /* 512 bytes times more sectors than a 64-bit capacity can hold. */
    CHECK_EQ(HELIKON_ERR_FORMAT,
             helikon_ata_block_info(512u, 0xFFFFFFFFFFFFFFFFull / 256ull, &info));
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_ata_block_info(512u, 16u, NULL));
}

static void test_init(void)
{
    struct helikon_ata ata;
    struct helikon_ata_block block;

    for (unsigned int index = 0; index < sizeof(ata); ++index)
        ((unsigned char *)&ata)[index] = 0;

    /* No IDENTIFY: the driver's numbers are placeholders, not a disk. */
    CHECK_EQ(HELIKON_ERR_UNSUPPORTED, helikon_ata_block_init(&block, &ata, 0));
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_ata_block_init(NULL, &ata, 0));
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_ata_block_init(&block, NULL, 0));

    ata.identify_valid = 1;
    ata.sector_size = 512u;
    ata.total_sectors = 0x100000000ull; /* 2 TiB: above the 32-bit sector count */

    CHECK_EQ(HELIKON_OK, helikon_ata_block_init(&block, &ata, 0));
    CHECK(helikon_ata_block_device(&block) == &block.device);
    CHECK(block.device.context == &ata);
    CHECK_EQ(0x100000000ull, block.device.info.sector_count);
    CHECK_EQ(512, block.device.info.sector_size);
    CHECK((block.device.info.flags & HELIKON_BLOCK_FLAG_READ_ONLY) != 0u);
    CHECK_EQ(0x20000000000ull, helikon_block_capacity_bytes(&block.device));

    /* Writable only when asked: the normal firmware is read-only. */
    CHECK_EQ(HELIKON_OK, helikon_ata_block_init(&block, &ata, 1));
    CHECK((block.device.info.flags & HELIKON_BLOCK_FLAG_READ_ONLY) == 0u);

    /* A geometry that cannot be a disk is refused, not rounded. */
    ata.sector_size = 513u;
    CHECK_EQ(HELIKON_ERR_FORMAT, helikon_ata_block_init(&block, &ata, 0));
    CHECK(helikon_ata_block_device(NULL) == NULL);
}

static void test_delegation(void)
{
    struct helikon_ata ata;
    struct helikon_ata_block block;
    unsigned char sector[512];
    struct helikon_block_info info;

    for (unsigned int index = 0; index < sizeof(ata); ++index)
        ((unsigned char *)&ata)[index] = 0;
    ata.identify_valid = 1;
    ata.sector_size = 512u;
    ata.total_sectors = 0x1000u;
    CHECK_EQ(HELIKON_OK, helikon_ata_block_init(&block, &ata, 0));

    CHECK_EQ(HELIKON_OK,
             helikon_block_read(&block.device, 0u, 1u, sector, 1000u));
    CHECK_EQ(1, read_calls);
    CHECK_EQ(HELIKON_ERR_READ_ONLY,
             helikon_block_write(&block.device, 0u, 1u, sector, 1000u));
    CHECK_EQ(0, write_calls); /* the gate is in front of the driver */

    /* Past the end never reaches the driver; the block layer answers ARGUMENT. */
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_block_read(&block.device, 0x1000u, 1u, sector, 1000u));
    CHECK_EQ(1, read_calls);
    CHECK_EQ(HELIKON_OK, helikon_ata_block_info(512u, 0x1000u, &info));
    CHECK_EQ(0x200000, helikon_block_capacity_bytes(&block.device));
}

int main(void)
{
    test_info();
    test_init();
    test_delegation();
    return helikon_test_report("ata block");
}
