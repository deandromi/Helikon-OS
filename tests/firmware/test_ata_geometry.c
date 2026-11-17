/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ata_geometry.h"
#include "error.h"

#include "test_check.h"

static void clear_identify(uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS])
{
    for (unsigned int i = 0; i < HELIKON_ATA_IDENTIFY_WORDS; ++i)
        identify[i] = 0u;
}

static void set_lba28(uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS],
                      uint32_t sectors)
{
    identify[60] = (uint16_t)sectors;
    identify[61] = (uint16_t)(sectors >> 16);
}

static void set_lba48(uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS],
                      uint64_t sectors)
{
    identify[100] = (uint16_t)sectors;
    identify[101] = (uint16_t)(sectors >> 16);
    identify[102] = (uint16_t)(sectors >> 32);
    identify[103] = (uint16_t)(sectors >> 48);
}

static void test_lba28_512(void)
{
    uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS] = { 0 };
    struct helikon_ata_geometry geometry;

    set_lba28(identify, 0x00561234u);
    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(0x00561234u, geometry.lba28_sectors);
    CHECK_EQ(0u, geometry.lba48_sectors);
    CHECK_EQ(0, geometry.lba48);
    CHECK_EQ(0, geometry.lba48_supported);
    CHECK_EQ(0, geometry.lba48_enabled);
    CHECK_EQ(512u, geometry.logical_sector_size);
    CHECK_EQ(512u, geometry.physical_sector_size);
    CHECK_EQ(0x00561234ull, geometry.total_sectors);
    CHECK_EQ(0x00561234ull * 512ull, geometry.capacity_bytes);
    CHECK(geometry.valid);
}

static void test_lba48_full_word(void)
{
    uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS] = { 0 };
    struct helikon_ata_geometry geometry;

    set_lba28(identify, 0x0fffffffu);
    set_lba48(identify, 0x00019abc56781234ull);
    identify[83] = 0x0400u;
    identify[86] = 0x0400u;

    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(0x0fffffffu, geometry.lba28_sectors);
    CHECK_EQ(0x00019abc56781234ull, geometry.lba48_sectors);
    CHECK_EQ(1, geometry.lba48);
    CHECK_EQ(1, geometry.lba48_supported);
    CHECK_EQ(1, geometry.lba48_enabled);
    CHECK_EQ(0x00019abc56781234ull, geometry.total_sectors);
    CHECK_EQ(0x00019abc56781234ull * 512ull, geometry.capacity_bytes);
    CHECK(geometry.valid);
}

static void test_lba28_boundary(void)
{
    uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS] = { 0 };
    struct helikon_ata_geometry geometry;

    /* 2^28 - 1 sectors with bit 10 set selects LBA48. */
    set_lba28(identify, 0x0fffffffu);
    identify[83] = 0x0400u;
    CHECK_EQ(HELIKON_ERR_IO,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(1, geometry.lba48);
    CHECK_EQ(0ull, geometry.total_sectors);
    CHECK_EQ(0ull, geometry.capacity_bytes);

    /* Without a valid LBA48 word set the same count stays LBA28. */
    identify[83] = 0u;
    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(0, geometry.lba48);
    CHECK_EQ(0x0fffffffull, geometry.total_sectors);
    CHECK_EQ(0x0fffffffull * 512ull, geometry.capacity_bytes);
    CHECK_EQ(131072ull * 1024ull * 1024ull - 512ull,
             geometry.capacity_bytes);

    /* Exactly 2^28 sectors stays LBA28 under the strict donor rule. */
    set_lba28(identify, 0x10000000u);
    identify[83] = 0x0400u;
    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(0, geometry.lba48);
    CHECK_EQ(0x10000000ull, geometry.total_sectors);
    CHECK_EQ(0x10000000ull * 512ull, geometry.capacity_bytes);

    /* 2^28 - 2 also stays LBA28. */
    set_lba28(identify, 0x0ffffffeu);
    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(0, geometry.lba48);
    CHECK_EQ(0x0ffffffeu, (uint32_t)geometry.total_sectors);

    /* Supported but not enabled is recorded and still selects LBA48. */
    set_lba28(identify, 0x0fffffffu);
    set_lba48(identify, 0x0fffffffull);
    identify[83] = 0x0400u;
    identify[86] = 0u;
    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(1, geometry.lba48);
    CHECK_EQ(0, geometry.lba48_enabled);
    CHECK_EQ(0x0fffffffull, geometry.total_sectors);
}

static void test_sector_sizes(void)
{
    uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS] = { 0 };
    struct helikon_ata_geometry geometry;

    /* 4 KiB logical and physical sector. */
    set_lba28(identify, 1000u);
    identify[106] = 0x5000u; /* word 117/118 valid, long logical sector */
    identify[117] = 2048u;   /* 2048 words = 4096 bytes */
    identify[118] = 0u;
    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(4096u, geometry.logical_sector_size);
    CHECK_EQ(4096u, geometry.physical_sector_size);
    CHECK_EQ(1000ull * 4096ull, geometry.capacity_bytes);

    /* 512-byte logical, 4 KiB physical (8 logical per physical). */
    clear_identify(identify);
    set_lba28(identify, 1000u);
    identify[106] = 0x2003u; /* bit 13 set, 2^3 logical per physical */
    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(512u, geometry.logical_sector_size);
    CHECK_EQ(4096u, geometry.physical_sector_size);
    CHECK_EQ(1000ull * 512ull, geometry.capacity_bytes);

    /* A long-sector claim with no valid size is unknown, not 512. */
    clear_identify(identify);
    set_lba28(identify, 1000u);
    identify[106] = 0x5000u;
    identify[117] = 0u;
    identify[118] = 0u;
    CHECK_EQ(HELIKON_ERR_IO,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(0u, geometry.logical_sector_size);
    CHECK_EQ(0u, geometry.physical_sector_size);
    CHECK_EQ(0ull, geometry.capacity_bytes);
    CHECK(!geometry.valid);
}

static void test_ceata(void)
{
    uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS] = { 0 };
    struct helikon_ata_geometry geometry;

    /* CE-ATA selects words 100..103 even when word 83 is clear. */
    set_lba28(identify, 0x00010000u);
    set_lba48(identify, 0x0000000040000000ull);
    identify[83] = 0u;
    CHECK_EQ(HELIKON_OK,
             helikon_ata_geometry_parse(identify, 1, &geometry));
    CHECK_EQ(1, geometry.ceata);
    CHECK_EQ(1, geometry.lba48);
    CHECK_EQ(0, geometry.lba48_supported);
    CHECK_EQ(0x0000000040000000ull, geometry.total_sectors);
    CHECK_EQ(0x0000000040000000ull * 512ull, geometry.capacity_bytes);

    /* CE-ATA with no valid word set is zero/unknown, never LBA28. */
    clear_identify(identify);
    set_lba28(identify, 0x00010000u);
    CHECK_EQ(HELIKON_ERR_IO,
             helikon_ata_geometry_parse(identify, 1, &geometry));
    CHECK_EQ(1, geometry.lba48);
    CHECK_EQ(0ull, geometry.total_sectors);
    CHECK_EQ(0ull, geometry.capacity_bytes);
}

static void test_empty(void)
{
    uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS] = { 0 };
    struct helikon_ata_geometry geometry;

    CHECK_EQ(HELIKON_ERR_IO,
             helikon_ata_geometry_parse(identify, 0, &geometry));
    CHECK_EQ(0u, geometry.lba28_sectors);
    CHECK_EQ(0ull, geometry.lba48_sectors);
    CHECK_EQ(0, geometry.lba48);
    CHECK_EQ(512u, geometry.logical_sector_size);
    CHECK_EQ(0ull, geometry.capacity_bytes);
    CHECK(!geometry.valid);
}

int main(void)
{
    test_lba28_512();
    test_lba48_full_word();
    test_lba28_boundary();
    test_sector_sizes();
    test_ceata();
    test_empty();
    return helikon_test_report("block/ata_geometry");
}
