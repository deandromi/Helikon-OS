/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stddef.h>
#include "ata.h"

#include "error.h"
#include "s5l8702_regs.h"

#define ATA_CMD_READ_SECTORS 0x20u
#define ATA_CMD_READ_MULTIPLE_EXT 0x29u
#define ATA_CMD_READ_MULTIPLE 0xc4u
#define ATA_CMD_WRITE_SECTORS 0x30u
#define ATA_CMD_WRITE_MULTIPLE_EXT 0x39u
#define ATA_CMD_FLUSH_CACHE 0xe7u
#define ATA_CMD_FLUSH_CACHE_EXT 0xeau
#define ATA_CMD_IDENTIFY 0xecu
#define ATA_CMD_SET_FEATURES 0xefu

#define ATA_IDENTIFY_WORDS 256u
#define ATA_CHUNK_SECTORS 256u

struct ata_attempt {
    int stage;
    int hdd_rc;
    int identify_rc;
};

static int ata_wait_ready_bits(struct helikon_ata *ata, uint32_t mask,
                               uint32_t timeout_ms)
{
    uint64_t start = helikon_clock_now_us(ata->clock);
    while ((S5L_ATA_PIO_READY & mask) != mask) {
        if (helikon_clock_expired(ata->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
    return HELIKON_OK;
}

static int ata_read_cbr(struct helikon_ata *ata, volatile uint32_t *reg,
                        uint16_t *value, uint32_t timeout_ms)
{
    int rc = ata_wait_ready_bits(ata, 0x2u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    (void)*reg;
    rc = ata_wait_ready_bits(ata, 0x1u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    *value = (uint16_t)(*reg & 0xffffu);
    return HELIKON_OK;
}

static int ata_write_cbr(struct helikon_ata *ata, volatile uint32_t *reg,
                         uint16_t value, uint32_t timeout_ms)
{
    int rc = ata_wait_ready_bits(ata, 0x2u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    *reg = value;
    return HELIKON_OK;
}

static int ata_wait_not_bsy(struct helikon_ata *ata, uint32_t timeout_ms)
{
    uint64_t start = helikon_clock_now_us(ata->clock);
    for (;;) {
        uint16_t status = 0;
        int rc = ata_read_cbr(ata, &S5L_ATA_PIO_CSD, &status, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if ((status & 0x80u) == 0u)
            return HELIKON_OK;
        if (helikon_clock_expired(ata->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
}

static int ata_wait_rdy(struct helikon_ata *ata, uint32_t timeout_ms)
{
    int rc = ata_wait_not_bsy(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    uint64_t start = helikon_clock_now_us(ata->clock);
    for (;;) {
        uint16_t status = 0;
        rc = ata_read_cbr(ata, &S5L_ATA_PIO_DAD, &status, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if ((status & 0x40u) != 0u)
            return HELIKON_OK;
        if (helikon_clock_expired(ata->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
}

static int ata_wait_start(struct helikon_ata *ata, uint32_t timeout_ms)
{
    int rc = ata_wait_not_bsy(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    uint64_t start = helikon_clock_now_us(ata->clock);
    for (;;) {
        uint16_t status = 0;
        rc = ata_read_cbr(ata, &S5L_ATA_PIO_DAD, &status, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if ((status & 0x01u) != 0u)
            return HELIKON_ERR_IO;
        if ((status & 0x88u) == 0x08u)
            return HELIKON_OK;
        if (helikon_clock_expired(ata->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
}

static int ata_wait_end(struct helikon_ata *ata, uint32_t timeout_ms)
{
    int rc = ata_wait_not_bsy(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    uint16_t status = 0;
    rc = ata_read_cbr(ata, &S5L_ATA_PIO_DAD, &status, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    if ((status & 0x01u) != 0u)
        return HELIKON_ERR_IO;
    if ((status & 0xe8u) == 0x40u)
        return HELIKON_OK;
    return HELIKON_ERR_IO;
}

static int ata_identify(struct helikon_ata *ata, uint32_t timeout_ms)
{
    /*
     * Match the donor storage_ata-6g.c sequence: wait only for !BSY, issue
     * IDENTIFY, wait for DRQ, and read the 256-word table. Do not require
     * DRDY first and do not require a post-transfer end status. This iFlash
     * SD bridge does not assert DRDY in the warm loader handoff state, which
     * made the previous ata_wait_rdy()/ata_wait_end() path fail with -1.
     */
    int rc = ata_wait_not_bsy(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR, 0u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_CSD, ATA_CMD_IDENTIFY, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_wait_start(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    for (uint32_t i = 0; i < ATA_IDENTIFY_WORDS; ++i) {
        rc = ata_read_cbr(ata, &S5L_ATA_PIO_DTR, &ata->identify[i],
                          timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
    }
    (void)ata_wait_not_bsy(ata, timeout_ms);
    return HELIKON_OK;
}

static int ata_set_feature(struct helikon_ata *ata, uint8_t feature,
                           uint8_t parameter, uint32_t timeout_ms)
{
    int rc = ata_wait_rdy(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR, 0u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_FED, feature, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_SCR, parameter, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_CSD, ATA_CMD_SET_FEATURES,
                       timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    return ata_wait_rdy(ata, timeout_ms);
}

static int ata_parse_geometry(struct helikon_ata *ata)
{
    uint32_t sectors28 = ((uint32_t)ata->identify[61] << 16) |
                         ata->identify[60];
    if ((ata->identify[83] & 0x0400u) != 0u &&
        sectors28 == 0x0fffffffu) {
        ata->total_sectors =
            ((uint64_t)ata->identify[103] << 48) |
            ((uint64_t)ata->identify[102] << 32) |
            ((uint64_t)ata->identify[101] << 16) |
            ata->identify[100];
        ata->lba48 = 1;
    } else {
        ata->total_sectors = sectors28;
        ata->lba48 = 0;
    }

    ata->sector_size = 512u;
    if ((ata->identify[106] & 0xd000u) == 0x5000u) {
        uint32_t bytes = ((uint32_t)ata->identify[117] |
                          ((uint32_t)ata->identify[118] << 16)) *
                         2u;
        if (bytes >= 512u && bytes <= 4096u &&
            (bytes & (bytes - 1u)) == 0u)
            ata->sector_size = bytes;
    }
    return ata->total_sectors == 0u ? HELIKON_ERR_IO : HELIKON_OK;
}

static int ata_configure_identified(struct helikon_ata *ata,
                                    uint32_t timeout_ms,
                                    struct ata_attempt *attempt)
{
    if (attempt != NULL)
        attempt->stage = 3;

    int rc = ata_parse_geometry(ata);
    if (rc != HELIKON_OK)
        return rc;

    uint32_t pio_time = 0x11f3u;
    if ((ata->identify[53] & 0x0002u) != 0u) {
        if ((ata->identify[64] & 0x0002u) != 0u)
            pio_time = 0x2072u; /* PIO mode 4 */
        else if ((ata->identify[64] & 0x0001u) != 0u)
            pio_time = 0x7083u; /* PIO mode 3 */
    }
    S5L_ATA_PIO_TIME = pio_time;

    if (attempt != NULL)
        attempt->stage = 4;
    rc = ata_set_feature(ata, 0x03u, 0x00u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    if ((ata->identify[82] & 0x0040u) != 0u) {
        if (attempt != NULL)
            attempt->stage = 5;
        rc = ata_set_feature(ata, 0xaau, 0x00u, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
    }

    ata->can_flush = ((ata->identify[83] & 0x2000u) != 0u) ||
                     ((ata->identify[83] & 0x1000u) != 0u) ||
                     ata->identify[80] >= 0x0020u;
    ata->write_cache = 0;
    if (attempt != NULL)
        attempt->stage = 6;
    if (ata->can_flush && (ata->identify[82] & 0x0020u) != 0u) {
        rc = ata_set_feature(ata, 0x02u, 0x00u, timeout_ms);
        if (rc == HELIKON_OK) {
            ata->write_cache = 1;
        } else {
            ata->write_cache = 0;
            (void)ata_wait_rdy(ata, timeout_ms);
        }
    }

    if (attempt != NULL)
        attempt->stage = 7;
    ata->powered = 1;
    return HELIKON_OK;
}

/*
 * Adopt the loader's live ATA controller.
 *
 * The firmware loader has just read this image from the same disk, so the
 * controller, device and PIO timing registers are already working. A second
 * IDENTIFY is what this iFlash SD bridge rejects; a plain sector read is the
 * exact operation the loader just completed. Do not reset the controller, do
 * not power-cycle the disk and do not send IDENTIFY.
 *
 * The read also probes LBA48: a large card's HFS volume can sit above the
 * LBA28 limit, and the command is valid at LBA 0, so support is measured
 * without the partition map. Geometry is completed from the partition table.
 */
static int ata_adopt(struct helikon_ata *ata, uint32_t timeout_ms,
                     struct ata_attempt *attempt)
{
    uint32_t adopt_timeout = timeout_ms > 1500u ? 1500u : timeout_ms;
    if (attempt != NULL) {
        attempt->stage = 8;
        attempt->hdd_rc = HELIKON_OK;
        attempt->identify_rc = HELIKON_OK;
    }

    /* The loader may have gated the clock; keep its controller configuration. */
    S5L_PCON(7) = 0x44444444u;
    S5L_PCON(8) = 0x44444444u;
    S5L_PCON(9) = 0x44444444u;
    S5L_PCON(10) = (S5L_PCON(10) & ~0xffffu) | 0x4444u;
    S5L_PWRCON(S5L_CLOCKGATE_ATA >> 5) &=
        ~(1u << (S5L_CLOCKGATE_ATA & 0x1fu));
    S5L_ATA_CFG = 1u << 6; /* PIO mode, as the loader left it */

    int rc = ata_wait_ready_bits(ata, 0x2u, adopt_timeout);
    if (rc != HELIKON_OK)
        return rc;
    if (attempt != NULL)
        attempt->stage = 9;

    ata->sector_size = 512u;
    ata->total_sectors = 0xffffffffu;
    ata->write_cache = 0;
    /*
     * The adapter is known to read and write, but its IDENTIFY table was
     * never fetched in adopt mode. FLUSH CACHE (0xE7) is the safe command;
     * do not infer FLUSH CACHE EXT from the scratch buffer.
     */
    ata->identify_valid = 0;
    /* Flush is attempted before commit; the journal gates on it. */
    ata->can_flush = 1;
    ata->powered = 1;
    ata->initialized = 1;

    if (attempt != NULL)
        attempt->stage = 10;
    ata->lba48 = 1;
    rc = helikon_ata_read(ata, 0u, 1u, ata->identify, adopt_timeout);
    if (rc != HELIKON_OK) {
        (void)ata_wait_not_bsy(ata, adopt_timeout);
        ata->lba48 = 0;
        rc = helikon_ata_read(ata, 0u, 1u, ata->identify, adopt_timeout);
    }
    if (rc != HELIKON_OK) {
        ata->sector_size = 0;
        ata->total_sectors = 0;
        ata->write_cache = 0;
        ata->can_flush = 0;
        ata->powered = 0;
        ata->initialized = 0;
        return rc;
    }

    if (attempt != NULL)
        attempt->stage = 11;
    return HELIKON_OK;
}

static int ata_power_up(struct helikon_ata *ata, uint32_t timeout_ms,
                        struct ata_attempt *attempt)
{
    if (attempt != NULL) {
        attempt->stage = 0;
        attempt->hdd_rc = HELIKON_OK;
        attempt->identify_rc = HELIKON_OK;
    }

    /*
     * The loader has already powered the disk to read the firmware image. A
     * redundant PMU enable write can fail on a warm PMU without meaning the
     * disk is unavailable; let the controller reset and identify decide.
     */
    int hdd_rc = helikon_pmu_hdd_power(ata->pmu, 1, timeout_ms);
    if (attempt != NULL)
        attempt->hdd_rc = hdd_rc;
    /*
     * After the loader's ata_sleepnow() power-down, the iFlash SD bridge needs
     * time to come back before the controller reset/IDENTIFY. A cold boot has
     * this time implicitly; the handoff does not.
     */
    helikon_clock_delay_us(ata->clock, 1000000u);
    int rc;

    /* ATA pin mux and controller reset, donor storage_ata-6g.c. */
    S5L_PCON(7) = 0x44444444u;
    S5L_PCON(8) = 0x44444444u;
    S5L_PCON(9) = 0x44444444u;
    S5L_PCON(10) = (S5L_PCON(10) & ~0xffffu) | 0x4444u;
    S5L_PWRCON(S5L_CLOCKGATE_ATA >> 5) &=
        ~(1u << (S5L_CLOCKGATE_ATA & 0x1fu));

    S5L_ATA_CFG = 1u;
    helikon_clock_delay_us(ata->clock, 10000u);
    S5L_ATA_CFG = 0u;
    helikon_clock_delay_us(ata->clock, 10000u);
    S5L_ATA_SWRST = 1u;
    helikon_clock_delay_us(ata->clock, 10000u);
    S5L_ATA_SWRST = 0u;
    helikon_clock_delay_us(ata->clock, 100000u);
    S5L_ATA_CONTROL = 1u;
    helikon_clock_delay_us(ata->clock, 200000u);
    S5L_ATA_PIO_TIME = 0x191f7u;
    S5L_ATA_PIO_LHR = 0u;
    S5L_ATA_CFG = 1u << 6;

    if (attempt != NULL)
        attempt->stage = 1;
    rc = ata_wait_ready_bits(ata, 0x2u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    if (attempt != NULL)
        attempt->stage = 2;
    rc = ata_identify(ata, timeout_ms);
    if (attempt != NULL)
        attempt->identify_rc = rc;
    if (rc != HELIKON_OK)
        return rc;
    ata->identify_valid = 1;

    return ata_configure_identified(ata, timeout_ms, attempt);
}

int helikon_ata_init(struct helikon_ata *ata, struct helikon_pmu *pmu,
                     struct helikon_clock *clock, uint32_t timeout_ms)
{
    if (ata == NULL || pmu == NULL || clock == NULL || !clock->initialized)
        return HELIKON_ERR_ARGUMENT;

    ata->pmu = pmu;
    ata->clock = clock;
    ata->identify_valid = 0;
    ata->sector_size = 0;
    ata->total_sectors = 0;
    ata->lba48 = 0;
    ata->write_cache = 0;
    ata->can_flush = 0;
    ata->powered = 0;
    ata->initialized = 0;

    /*
     * Prefer the loader's live controller. The bridge rejects a second
     * IDENTIFY, but it has just served the reads that loaded this image.
     */
    int rc = ata_adopt(ata, timeout_ms, NULL);
    if (rc == HELIKON_OK)
        return HELIKON_OK;

    /* No live handoff (cold boot or a loader that powered ATA down). */
    rc = ata_power_up(ata, timeout_ms, NULL);
    if (rc != HELIKON_OK)
        return rc;
    ata->initialized = 1;
    return HELIKON_OK;
}

#ifdef HELIKON_DIAG_ATA_PROBE
static void ata_probe_reset(struct helikon_ata *ata)
{
    ata->identify_valid = 0;
    ata->sector_size = 0;
    ata->total_sectors = 0;
    ata->lba48 = 0;
    ata->write_cache = 0;
    ata->can_flush = 0;
    ata->powered = 0;
    ata->initialized = 0;
}

int helikon_ata_init_probe(struct helikon_ata *ata, struct helikon_pmu *pmu,
                           struct helikon_clock *clock, uint32_t timeout_ms,
                           struct helikon_ata_probe *probe)
{
    if (ata == NULL || pmu == NULL || clock == NULL || probe == NULL ||
        !clock->initialized)
        return HELIKON_ERR_ARGUMENT;

    ata->pmu = pmu;
    ata->clock = clock;
    ata_probe_reset(ata);

    probe->adopt_rc = HELIKON_ERR_IO;
    probe->adopt_lba48 = 0;
    probe->adopt_word0 = 0;
    probe->adopt_csd = 0;
    probe->adopt_dad = 0;
    probe->adopt_ready = 0;
    probe->second_stage = -1;
    probe->second_rc = HELIKON_OK;
    probe->second_hdd_rc = HELIKON_OK;
    probe->second_identify_rc = HELIKON_OK;

    struct ata_attempt adopt;
    adopt.stage = -1;
    adopt.hdd_rc = HELIKON_OK;
    adopt.identify_rc = HELIKON_OK;
    int rc = ata_adopt(ata, timeout_ms, &adopt);
    probe->adopt_rc = rc;
    probe->adopt_lba48 = ata->lba48;
    probe->adopt_word0 = ata->identify[0];
    probe->adopt_csd = (uint16_t)(S5L_ATA_PIO_CSD & 0xffffu);
    probe->adopt_dad = (uint16_t)(S5L_ATA_PIO_DAD & 0xffffu);
    probe->adopt_ready = S5L_ATA_PIO_READY;
    if (rc == HELIKON_OK) {
        probe->first_stage = adopt.stage;
        probe->first_rc = HELIKON_OK;
        probe->first_hdd_rc = HELIKON_OK;
        probe->first_identify_rc = HELIKON_OK;
        probe->total_sectors = (uint32_t)ata->total_sectors;
        probe->identify_80 = 0;
        probe->identify_82 = 0;
        probe->identify_83 = 0;
        return HELIKON_OK;
    }

    ata_probe_reset(ata);
    struct ata_attempt first;
    first.stage = -1;
    first.hdd_rc = HELIKON_OK;
    first.identify_rc = HELIKON_OK;
    rc = ata_power_up(ata, timeout_ms, &first);
    probe->first_stage = first.stage;
    probe->first_rc = rc;
    probe->first_hdd_rc = first.hdd_rc;
    probe->first_identify_rc = first.identify_rc;

    if (rc == HELIKON_OK) {
        ata->initialized = 1;
    } else {
        helikon_clock_delay_us(clock, 100000u);
        ata_probe_reset(ata);
        struct ata_attempt second;
        second.stage = -1;
        second.hdd_rc = HELIKON_OK;
        second.identify_rc = HELIKON_OK;
        rc = ata_power_up(ata, timeout_ms, &second);
        probe->second_stage = second.stage;
        probe->second_rc = rc;
        probe->second_hdd_rc = second.hdd_rc;
        probe->second_identify_rc = second.identify_rc;
        if (rc == HELIKON_OK)
            ata->initialized = 1;
    }

    probe->total_sectors = (uint32_t)ata->total_sectors;
    probe->identify_80 = ata->identify[80];
    probe->identify_82 = ata->identify[82];
    probe->identify_83 = ata->identify[83];
    return rc;
}
#endif

static int ata_read_chunk(struct helikon_ata *ata, uint64_t lba,
                          uint32_t count, uint8_t *buffer, uint32_t timeout_ms)
{
    int rc = ata_wait_rdy(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR, 0u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    if (ata->lba48) {
        rc = ata_write_cbr(ata, &S5L_ATA_PIO_SCR, 0u, timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_SCR, (uint16_t)count,
                               timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LHR,
                               (uint16_t)(lba >> 40), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LMR,
                               (uint16_t)(lba >> 32), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LLR,
                               (uint16_t)(lba >> 24), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LHR,
                               (uint16_t)(lba >> 16), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LMR,
                               (uint16_t)(lba >> 8), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LLR, (uint16_t)lba,
                               timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR, 0x40u, timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_CSD,
                               ATA_CMD_READ_MULTIPLE_EXT, timeout_ms);
    } else {
        rc = ata_write_cbr(ata, &S5L_ATA_PIO_SCR, (uint16_t)count,
                           timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LHR,
                               (uint16_t)(lba >> 16), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LMR,
                               (uint16_t)(lba >> 8), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LLR, (uint16_t)lba,
                               timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR,
                               (uint16_t)(0xe0u | ((lba >> 24) & 0x0fu)),
                               timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_CSD,
                               ATA_CMD_READ_MULTIPLE, timeout_ms);
    }
    if (rc != HELIKON_OK)
        return rc;

    uint16_t *output = (uint16_t *)(void *)buffer;
    uint32_t words = ata->sector_size / 2u;
    for (uint32_t sector = 0; sector < count; ++sector) {
        rc = ata_wait_start(ata, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        for (uint32_t word = 0; word < words; ++word) {
            rc = ata_read_cbr(ata, &S5L_ATA_PIO_DTR, &output[word],
                              timeout_ms);
            if (rc != HELIKON_OK)
                return rc;
        }
        output += words;
        rc = ata_wait_end(ata, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
    }
    return HELIKON_OK;
}

static int ata_write_chunk(struct helikon_ata *ata, uint64_t lba,
                           uint32_t count, const uint8_t *buffer,
                           uint32_t timeout_ms)
{
    int rc = ata_wait_rdy(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR, 0u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    if (ata->lba48) {
        rc = ata_write_cbr(ata, &S5L_ATA_PIO_SCR, 0u, timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_SCR, (uint16_t)count,
                               timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LHR,
                               (uint16_t)(lba >> 40), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LMR,
                               (uint16_t)(lba >> 32), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LLR,
                               (uint16_t)(lba >> 24), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LHR,
                               (uint16_t)(lba >> 16), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LMR,
                               (uint16_t)(lba >> 8), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LLR, (uint16_t)lba,
                               timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR, 0x40u, timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_CSD,
                               ATA_CMD_WRITE_MULTIPLE_EXT, timeout_ms);
    } else {
        rc = ata_write_cbr(ata, &S5L_ATA_PIO_SCR, (uint16_t)count,
                           timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LHR,
                               (uint16_t)(lba >> 16), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LMR,
                               (uint16_t)(lba >> 8), timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_LLR, (uint16_t)lba,
                               timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR,
                               (uint16_t)(0xe0u | ((lba >> 24) & 0x0fu)),
                               timeout_ms);
        if (rc == HELIKON_OK)
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_CSD,
                               ATA_CMD_WRITE_SECTORS, timeout_ms);
    }
    if (rc != HELIKON_OK)
        return rc;

    const uint16_t *input = (const uint16_t *)(const void *)buffer;
    uint32_t words = ata->sector_size / 2u;
    for (uint32_t sector = 0; sector < count; ++sector) {
        rc = ata_wait_start(ata, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        for (uint32_t word = 0; word < words; ++word) {
            rc = ata_write_cbr(ata, &S5L_ATA_PIO_DTR, input[word],
                               timeout_ms);
            if (rc != HELIKON_OK)
                return rc;
        }
        input += words;
    }
    return ata_wait_end(ata, timeout_ms);
}

int helikon_ata_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms)
{
    struct helikon_ata *ata = context;
    if (ata == NULL || !ata->initialized || buffer == NULL || count == 0u)
        return HELIKON_ERR_ARGUMENT;
    if (lba > ata->total_sectors || count > ata->total_sectors - lba)
        return HELIKON_ERR_ARGUMENT;
    if ((((uintptr_t)buffer) & 1u) != 0u)
        return HELIKON_ERR_ARGUMENT;

    uint8_t *output = buffer;
    while (count != 0u) {
        uint32_t chunk = count > ATA_CHUNK_SECTORS ? ATA_CHUNK_SECTORS : count;
        int rc = ata_read_chunk(ata, lba, chunk, output, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        output += (size_t)chunk * ata->sector_size;
        lba += chunk;
        count -= chunk;
    }
    return HELIKON_OK;
}

int helikon_ata_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms)
{
    struct helikon_ata *ata = context;
    if (ata == NULL || !ata->initialized || buffer == NULL || count == 0u)
        return HELIKON_ERR_ARGUMENT;
    if (lba > ata->total_sectors || count > ata->total_sectors - lba)
        return HELIKON_ERR_ARGUMENT;
    if ((((uintptr_t)buffer) & 1u) != 0u)
        return HELIKON_ERR_ARGUMENT;

    const uint8_t *input = buffer;
    while (count != 0u) {
        uint32_t chunk = count > ATA_CHUNK_SECTORS ? ATA_CHUNK_SECTORS : count;
        int rc = ata_write_chunk(ata, lba, chunk, input, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        input += (size_t)chunk * ata->sector_size;
        lba += chunk;
        count -= chunk;
    }
    return HELIKON_OK;
}

int helikon_ata_flush(void *context, uint32_t timeout_ms)
{
    struct helikon_ata *ata = context;
    if (ata == NULL || !ata->initialized)
        return HELIKON_ERR_ARGUMENT;
    if (!ata->can_flush)
        return HELIKON_ERR_UNSUPPORTED;

    int rc = ata_wait_rdy(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_DVR, 0u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    uint8_t command = ATA_CMD_FLUSH_CACHE;
    if (ata->identify_valid && ata->lba48 &&
        (ata->identify[83] & 0x2000u) != 0u)
        command = ATA_CMD_FLUSH_CACHE_EXT;
    rc = ata_write_cbr(ata, &S5L_ATA_PIO_CSD, command, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    return ata_wait_rdy(ata, timeout_ms);
}

int helikon_ata_power(void *context, int on, uint32_t timeout_ms)
{
    (void)context;
    (void)on;
    (void)timeout_ms;
    return HELIKON_ERR_READ_ONLY;
}
