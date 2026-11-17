/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 6G ATA PIO read/write path.
 *
 * F1 uses identify and read. F2 adds bounded PIO write and cache flush for the
 * single bootstate transaction. The power-up/timing sequence is the donor
 * storage_ata-6g.c PIO path; it is a hardware sequence, not a measured
 * throughput claim. Every wait is bounded.
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
    /*
     * Adopt-mode sector buffer. It is larger than one 512-byte sector so a
     * 4 KiB logical-sector device cannot overflow it before IDENTIFY has
     * reported the real logical sector size.
     */
    uint16_t adopt_sector[2048];
    int identify_valid;
    uint32_t sector_size;
    uint32_t physical_sector_size;
    uint64_t total_sectors;
    int lba48;
    int ceata; /* PDAT11 bit 1; recorded, CE-ATA commands are not implemented */
    int write_cache;
    int can_flush;
    int powered;
    int initialized;
};

int helikon_ata_init(struct helikon_ata *ata, struct helikon_pmu *pmu,
                     struct helikon_clock *clock, uint32_t timeout_ms);

#ifdef HELIKON_DIAG_ATA_PROBE
struct helikon_ata_probe {
    int adopt_rc;
    int adopt_lba48;
    int adopt_power;
    int adopt_ceata;
    uint16_t adopt_pdat11;
    uint16_t adopt_word0;
    uint16_t adopt_csd;
    uint16_t adopt_dad;
    uint32_t adopt_ready;
    uint32_t adopt_control;
    int first_stage;
    int first_rc;
    int first_hdd_rc;
    int first_identify_rc;
    int second_stage;
    int second_rc;
    int second_hdd_rc;
    int second_identify_rc;
    uint32_t total_sectors; /* low 32 bits; capacity_bytes is authoritative */
    uint16_t identify_80;
    uint16_t identify_82;
    uint16_t identify_83;
    uint16_t identify_60;
    uint16_t identify_61;
    uint16_t identify_86;
    uint16_t identify_100;
    uint16_t identify_101;
    uint16_t identify_102;
    uint16_t identify_103;
    uint16_t identify_106;
    uint16_t identify_117;
    uint16_t identify_118;
    uint32_t lba28_sectors;
    uint64_t lba48_sectors;
    uint32_t log_sector_size;
    uint32_t physical_sector_size;
    uint64_t capacity_bytes;
    int lba48;
    int lba48_supported;
    int lba48_enabled;
    int identify_valid;
    int identify_attempted;
    int identify_stage;
    int identify_rc;
};

/*
 * Diagnostic-only ATA init: first tries to adopt the loader's live controller
 * with a plain sector read, then reports the failing substep of the full reset
 * path and retries it once. Substeps:
 * 8 adopt entry, 9 soft controller ready, 10 soft sector read,
 * 11 soft success, 12 reset without IDENTIFY, 13 reset sector read,
 * 14 adopt success;
 * 0 HDD power, 1 controller ready, 2 identify, 3 sector geometry,
 * 4 SET FEATURES 0x03, 5 SET FEATURES 0xAA, 6 write-cache hint, 7 success.
 */
int helikon_ata_init_probe(struct helikon_ata *ata, struct helikon_pmu *pmu,
                           struct helikon_clock *clock, uint32_t timeout_ms,
                           struct helikon_ata_probe *probe);

/*
 * Diagnostic-only late IDENTIFY. Call after the probe has finished all reads.
 * If init already obtained a valid IDENTIFY table this is a no-op. Otherwise it
 * runs the full reset/IDENTIFY path once, records the attempt and fills the raw
 * words and derived geometry. On failure the adopted read geometry is restored
 * so the probe screen still shows the safe read result. CE-ATA is not
 * identified: the call is refused with HELIKON_ERR_UNSUPPORTED.
 */
int helikon_ata_probe_identify(struct helikon_ata *ata, uint32_t timeout_ms,
                               struct helikon_ata_probe *probe);
#endif

/* Buffer must be at least count * sector_size bytes and 2-byte aligned. */
int helikon_ata_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms);
int helikon_ata_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms);
int helikon_ata_flush(void *context, uint32_t timeout_ms);
int helikon_ata_power(void *context, int on, uint32_t timeout_ms);

#endif
