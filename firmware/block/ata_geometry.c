/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ata_geometry.h"

#include <stddef.h>

#include "error.h"

#define ATA_LBA28_MAX 0x0fffffffu
#define ATA_WORD_83_LBA48 (1u << 10)
#define ATA_WORD_86_LBA48 (1u << 10)
#define ATA_WORD_106_LONG_SECTOR 0x5000u
#define ATA_WORD_106_LONG_SECTOR_MASK 0xd000u
#define ATA_WORD_106_MULTIPLE_LOGICAL 0x2000u
#define ATA_WORD_106_SECTOR_SHIFT 0x000fu
#define ATA_SECTOR_SIZE_MIN 512u
#define ATA_SECTOR_SIZE_MAX 4096u
#define ATA_PHYSICAL_SIZE_MAX 65536u

static int power_of_two(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static uint32_t parse_logical_sector_size(
    const uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS])
{
    /*
     * Word 106 bits 14 and 12 announce a logical sector longer than 256
     * words; words 117..118 hold its size in words. A missing or malformed
     * pair is unknown, not 512.
     */
    if ((identify[106] & ATA_WORD_106_LONG_SECTOR_MASK) !=
        ATA_WORD_106_LONG_SECTOR)
        return 512u;

    uint32_t words = (uint32_t)identify[117] |
                     ((uint32_t)identify[118] << 16);
    uint64_t bytes = (uint64_t)words * 2u;
    if (bytes < ATA_SECTOR_SIZE_MIN || bytes > ATA_SECTOR_SIZE_MAX ||
        !power_of_two((uint32_t)bytes))
        return 0u;
    return (uint32_t)bytes;
}

static uint32_t parse_physical_sector_size(
    const uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS], uint32_t logical)
{
    if (logical == 0u)
        return 0u;
    if ((identify[106] & ATA_WORD_106_MULTIPLE_LOGICAL) == 0u)
        return logical;

    uint32_t shift = identify[106] & ATA_WORD_106_SECTOR_SHIFT;
    uint64_t bytes = (uint64_t)logical << shift;
    if (bytes > ATA_PHYSICAL_SIZE_MAX || !power_of_two((uint32_t)bytes))
        return 0u;
    return (uint32_t)bytes;
}

int helikon_ata_geometry_parse(
    const uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS], int ceata,
    struct helikon_ata_geometry *geometry)
{
    if (identify == NULL || geometry == NULL)
        return HELIKON_ERR_ARGUMENT;

    geometry->lba28_sectors = ((uint32_t)identify[61] << 16) |
                              (uint32_t)identify[60];
    geometry->lba48_sectors = ((uint64_t)identify[103] << 48) |
                              ((uint64_t)identify[102] << 32) |
                              ((uint64_t)identify[101] << 16) |
                              (uint64_t)identify[100];
    geometry->logical_sector_size = parse_logical_sector_size(identify);
    geometry->physical_sector_size =
        parse_physical_sector_size(identify,
                                   geometry->logical_sector_size);
    geometry->lba48_supported =
        (identify[83] & ATA_WORD_83_LBA48) != 0u;
    geometry->lba48_enabled = (identify[86] & ATA_WORD_86_LBA48) != 0u;
    geometry->ceata = ceata != 0;

    /*
     * Rockbox ipod6g donor rule: CE-ATA, or a device that advertises LBA48
     * and reports the LBA28 maximum, uses the 48-bit word set. Any other
     * device stays on LBA28 even when bit 10 is set.
     */
    geometry->lba48 = geometry->ceata ||
                      (geometry->lba48_supported &&
                       geometry->lba28_sectors == ATA_LBA28_MAX);
    geometry->total_sectors = geometry->lba48
                                  ? geometry->lba48_sectors
                                  : (uint64_t)geometry->lba28_sectors;
    geometry->capacity_bytes =
        geometry->total_sectors * (uint64_t)geometry->logical_sector_size;
    geometry->valid = geometry->total_sectors != 0u &&
                      geometry->logical_sector_size != 0u;

    return geometry->valid ? HELIKON_OK : HELIKON_ERR_IO;
}
