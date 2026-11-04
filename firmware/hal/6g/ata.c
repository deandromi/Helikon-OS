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
    int rc = ata_wait_rdy(ata, timeout_ms);
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
    return ata_wait_end(ata, timeout_ms);
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

static int ata_power_up(struct helikon_ata *ata, uint32_t timeout_ms)
{
    int rc = helikon_pmu_hdd_power(ata->pmu, 1, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

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

    rc = ata_wait_ready_bits(ata, 0x2u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    rc = ata_identify(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

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
    if (ata->total_sectors == 0u)
        return HELIKON_ERR_IO;

    uint32_t pio_time = 0x11f3u;
    if ((ata->identify[53] & 0x0002u) != 0u) {
        if ((ata->identify[64] & 0x0002u) != 0u)
            pio_time = 0x2072u; /* PIO mode 4 */
        else if ((ata->identify[64] & 0x0001u) != 0u)
            pio_time = 0x7083u; /* PIO mode 3 */
    }
    S5L_ATA_PIO_TIME = pio_time;

    rc = ata_set_feature(ata, 0x03u, 0x00u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    if ((ata->identify[82] & 0x0040u) != 0u) {
        rc = ata_set_feature(ata, 0xaau, 0x00u, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
    }

    /*
     * F2 must not claim durability without a cache flush. Enable the volatile
     * write cache only when the device advertises a flush command; otherwise
     * leave the cache policy alone and let hfs_txn fail closed at flush time.
     */
    ata->can_flush = ((ata->identify[83] & 0x2000u) != 0u) ||
                     ((ata->identify[83] & 0x1000u) != 0u) ||
                     ata->identify[80] >= 0x0020u;
    ata->write_cache = 0;
    if (ata->can_flush && (ata->identify[82] & 0x0020u) != 0u) {
        rc = ata_set_feature(ata, 0x02u, 0x00u, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        ata->write_cache = 1;
    }

    ata->powered = 1;
    return HELIKON_OK;
}

int helikon_ata_init(struct helikon_ata *ata, struct helikon_pmu *pmu,
                     struct helikon_clock *clock, uint32_t timeout_ms)
{
    if (ata == NULL || pmu == NULL || clock == NULL || !clock->initialized)
        return HELIKON_ERR_ARGUMENT;

    ata->pmu = pmu;
    ata->clock = clock;
    ata->sector_size = 0;
    ata->total_sectors = 0;
    ata->lba48 = 0;
    ata->write_cache = 0;
    ata->can_flush = 0;
    ata->powered = 0;
    ata->initialized = 0;

    int rc = ata_power_up(ata, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    ata->initialized = 1;
    return HELIKON_OK;
}

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
    if (ata->lba48 && (ata->identify[83] & 0x2000u) != 0u)
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
