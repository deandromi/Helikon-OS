/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Pure ATA IDENTIFY geometry parser.
 *
 * This module owns no hardware and performs no I/O. It turns the 256-word
 * IDENTIFY table into the LBA28/LBA48 sector counts, the logical/physical
 * sector size and the byte capacity. The HAL calls it after IDENTIFY; host
 * tests call it directly so the boundary rules are testable.
 *
 * Unproven values are reported as zero, never guessed. A device that claims a
 * long logical sector but returns an invalid word 117/118 pair therefore
 * produces a zero sector size and an invalid geometry instead of a silent
 * 512-byte assumption.
 */
#ifndef HELIKON_BLOCK_ATA_GEOMETRY_H
#define HELIKON_BLOCK_ATA_GEOMETRY_H

#include <stdint.h>

#define HELIKON_ATA_IDENTIFY_WORDS 256u

struct helikon_ata_geometry {
    uint32_t lba28_sectors;      /* IDENTIFY words 60..61, always recorded */
    uint64_t lba48_sectors;      /* IDENTIFY words 100..103, always recorded */
    uint32_t logical_sector_size;
    uint32_t physical_sector_size;
    uint64_t total_sectors;      /* selected LBA28 or LBA48 count */
    uint64_t capacity_bytes;     /* total_sectors * logical_sector_size */
    int lba48;                   /* selected addressing mode */
    int lba48_supported;         /* IDENTIFY word 83 bit 10 */
    int lba48_enabled;           /* IDENTIFY word 86 bit 10 */
    int ceata;                   /* CE-ATA detected; words 100..103 used */
    int valid;                   /* non-zero capacity and proven sector size */
};

/*
 * Parse an IDENTIFY table. `ceata` is the PDAT11 bit-1 detection from the
 * diagnostic probe; normal builds pass 0. The donor rule is used exactly:
 * CE-ATA or (word 83 bit 10 and word 60/61 == 0x0fffffff) selects words
 * 100..103 as the capacity, otherwise words 60/61 are used.
 *
 * All fields are written. Returns HELIKON_OK when a non-zero capacity and a
 * proven logical sector size were derived, HELIKON_ERR_IO otherwise.
 */
int helikon_ata_geometry_parse(const uint16_t identify[HELIKON_ATA_IDENTIFY_WORDS],
                               int ceata,
                               struct helikon_ata_geometry *geometry);

#endif
