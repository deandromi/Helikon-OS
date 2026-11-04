/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 6G ATA PIO read path.
 *
 * F1 is read-only: identify and read are implemented, write and flush exist
 * only to satisfy the block contract and return HELIKON_ERR_READ_ONLY. The
 * power-up/timing sequence is the donor storage_ata-6g.c PIO path; it is a
 * hardware sequence, not a measured throughput claim. Every wait is bounded.
 */
#ifndef HELIKON_HAL_6G_ATA_H
#define HELIKON_HAL_6G_ATA_H

#include <stdint.h>

#include "clock.h"
#include "pmu.h"

struct helikon_ata {
    struct helikon_pmu *pmu;
    struct helikon_clock *clock;
    uint16_t identify[256];
    uint32_t sector_size;
    uint64_t total_sectors;
    int lba48;
    int powered;
    int initialized;
};

int helikon_ata_init(struct helikon_ata *ata, struct helikon_pmu *pmu,
                     struct helikon_clock *clock, uint32_t timeout_ms);

/* Buffer must be at least count * sector_size bytes and 2-byte aligned. */
int helikon_ata_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms);
int helikon_ata_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms);
int helikon_ata_flush(void *context, uint32_t timeout_ms);
int helikon_ata_power(void *context, int on, uint32_t timeout_ms);

#endif
