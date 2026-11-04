/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stddef.h>
#include "pmu.h"

#include "cpu.h"
#include "error.h"
#include "s5l8702_regs.h"

#define PMU_I2C_BUS 0u
#define PMU_I2C_ADDRESS 0xe6u
#define PMU_I2C_OWN_ADDRESS 0x40u

struct pmu_register_value {
    uint8_t reg;
    uint8_t value;
};

/*
 * Donor pmu_preinit sequence. Every value is a register write from the proven
 * Rockbox 6G driver; the table is idempotent on a warm PMU.
 */
static const struct pmu_register_value pmu_preinit_table[] = {
    { 0x0c, 0x00 }, /* reset OOC shutdown */
    { HELIKON_PMU_REG_LDO1OUT, 0x15 }, /* 3000 mV */
    { HELIKON_PMU_REG_LDO1ENA, 0x01 },
    { HELIKON_PMU_REG_LDO2OUT, 0x15 },
    { HELIKON_PMU_REG_LDO2ENA, 0x01 },
    { HELIKON_PMU_REG_LDO3OUT, 0x15 }, /* LCD */
    { HELIKON_PMU_REG_LDO3ENA, 0x01 },
    { HELIKON_PMU_REG_LDO4OUT, 0x09 }, /* codec 1800 mV */
    { HELIKON_PMU_REG_LDO4ENA, 0x01 },
    { HELIKON_PMU_REG_LDO5OUT, 0x15 },
    { HELIKON_PMU_REG_LDO5ENA, 0x00 },
    { HELIKON_PMU_REG_LDO6OUT, 0x15 }, /* click wheel */
    { HELIKON_PMU_REG_LDO6ENA, 0x04 },
    { HELIKON_PMU_REG_HCLDOOUT, 0x18 },
    { HELIKON_PMU_REG_HCLDOENA, 0x00 },
    { HELIKON_PMU_REG_STBYCTL1, 0x00 },
    { HELIKON_PMU_REG_STBYCTL2, 0x8c },
    { HELIKON_PMU_REG_GPIOCTL, 0x03 },
    { HELIKON_PMU_REG_GPIO1CFG, 0x00 },
    { HELIKON_PMU_REG_GPIO2CFG, 0x00 },
    { HELIKON_PMU_REG_GPIO3CFG, 0x07 },
    { HELIKON_PMU_REG_DOWN2OUT, 0x2f }, /* SDRAM 1800 mV */
    { HELIKON_PMU_REG_DOWN2ENA, 0x01 },
    { HELIKON_PMU_REG_DOWN2CTL, 0x00 },
    { HELIKON_PMU_REG_DOWN2MXC, 0x10 },
    { HELIKON_PMU_REG_MEMLDOOUT, 0x09 },
    { HELIKON_PMU_REG_MEMLDOENA, 0x01 },
    { HELIKON_PMU_REG_AUTOOUT, 0x6f }, /* HDD 3400 mV */
    { HELIKON_PMU_REG_AUTOENA, 0x00 },
    { HELIKON_PMU_REG_AUTOCTL, 0x00 },
    { HELIKON_PMU_REG_AUTOMXC, 0x59 },
    { HELIKON_PMU_REG_SVMCTL, 0x08 },
    { 0x58, 0x00 }, /* reserved */
    { HELIKON_PMU_REG_INT1M, 0xff },
    { 0x08, 0xff },
    { 0x09, 0xff },
    { 0x0a, 0xff },
    { 0x0b, 0xff },
    { HELIKON_PMU_REG_INT6M, 0xff },
    { HELIKON_PMU_REG_OOCWAKE, 0xdf },
    { 0x0e, 0xaa }, /* OOCTIM1 */
    { 0x0f, 0x4a }, /* OOCTIM2 */
    { 0x10, 0x05 }, /* OOCMODE */
    { 0x11, 0x27 }, /* OOCCTL */
    { HELIKON_PMU_REG_GPOCFG, 0x01 },
    { HELIKON_PMU_REG_LEDENA, 0x00 },
    { HELIKON_PMU_REG_LEDCTL, 0x05 },
    { HELIKON_PMU_REG_LEDDIM, 0x06 }
};

static int i2c_wait_ready(struct helikon_pmu *pmu, uint32_t timeout_ms)
{
    uint64_t start = helikon_clock_now_us(pmu->clock);
    while (S5L_IICUNK10(PMU_I2C_BUS) != 0u) {
        if (helikon_clock_expired(pmu->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
    return HELIKON_OK;
}

static int i2c_wait_io(struct helikon_pmu *pmu, uint32_t timeout_ms)
{
    uint64_t start = helikon_clock_now_us(pmu->clock);
    while ((S5L_IICSTAT(PMU_I2C_BUS) & (1u << 5)) != 0u &&
           (S5L_IICSTA2(PMU_I2C_BUS) & ((1u << 8) | (1u << 13))) == 0u) {
        int rc = i2c_wait_ready(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if (helikon_clock_expired(pmu->clock, start, timeout_ms))
            return HELIKON_ERR_IO;
    }
    S5L_IICSTA2(PMU_I2C_BUS) |= (1u << 8) | (1u << 13);
    return HELIKON_OK;
}

static void i2c_clock_on(void)
{
    S5L_PWRCON(S5L_CLOCKGATE_I2C0 >> 5) &=
        ~(1u << (S5L_CLOCKGATE_I2C0 & 0x1fu));
}

static void i2c_clock_off(void)
{
    S5L_PWRCON(S5L_CLOCKGATE_I2C0 >> 5) |=
        (1u << (S5L_CLOCKGATE_I2C0 & 0x1fu));
}

static int i2c_start(struct helikon_pmu *pmu, int read, uint32_t timeout_ms)
{
    uint32_t mode = read ? 0x80u : 0xc0u;
    int rc = i2c_wait_ready(pmu, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    S5L_IICCON(PMU_I2C_BUS) = (1u << 7); /* ACK enable, PCLK source */
    S5L_IICSTAT(PMU_I2C_BUS) = mode;
    rc = i2c_wait_ready(pmu, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    S5L_IICDS(PMU_I2C_BUS) = PMU_I2C_ADDRESS | (read ? 1u : 0u);
    rc = i2c_wait_ready(pmu, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    S5L_IICSTAT(PMU_I2C_BUS) = mode | 0x30u;
    rc = i2c_wait_io(pmu, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    if ((S5L_IICSTAT(PMU_I2C_BUS) & 1u) != 0u)
        return HELIKON_ERR_IO;
    return HELIKON_OK;
}

static int i2c_stop(struct helikon_pmu *pmu, uint32_t timeout_ms)
{
    int rc = i2c_wait_ready(pmu, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    S5L_IICSTAT(PMU_I2C_BUS) &= ~0x20u;
    rc = i2c_wait_ready(pmu, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    S5L_IICCON(PMU_I2C_BUS) = 0x10u;
    return i2c_wait_io(pmu, timeout_ms);
}

static int i2c_write_bytes(struct helikon_pmu *pmu, int address,
                           const uint8_t *data, size_t length,
                           uint32_t timeout_ms)
{
    int rc = i2c_start(pmu, 0, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    if (address >= 0) {
        uint8_t value = (uint8_t)address;
        rc = i2c_wait_ready(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        S5L_IICDS(PMU_I2C_BUS) = value;
        helikon_clock_delay_us(pmu->clock, 5u);
        rc = i2c_wait_ready(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        S5L_IICCON(PMU_I2C_BUS) = S5L_IICCON(PMU_I2C_BUS);
        rc = i2c_wait_io(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if ((S5L_IICSTAT(PMU_I2C_BUS) & 1u) != 0u)
            return HELIKON_ERR_IO;
    }

    for (size_t i = 0; i < length; ++i) {
        rc = i2c_wait_ready(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        S5L_IICDS(PMU_I2C_BUS) = data[i];
        helikon_clock_delay_us(pmu->clock, 5u);
        rc = i2c_wait_ready(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        S5L_IICCON(PMU_I2C_BUS) = S5L_IICCON(PMU_I2C_BUS);
        rc = i2c_wait_io(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if ((S5L_IICSTAT(PMU_I2C_BUS) & 1u) != 0u)
            return HELIKON_ERR_IO;
    }
    return HELIKON_OK;
}

static int i2c_read_bytes(struct helikon_pmu *pmu, uint8_t *data,
                          size_t length, uint32_t timeout_ms)
{
    int rc = i2c_start(pmu, 1, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    for (size_t i = 0; i < length; ++i) {
        rc = i2c_wait_ready(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        if (i + 1u == length)
            S5L_IICCON(PMU_I2C_BUS) &= ~0x80u; /* NAK the last byte */
        rc = i2c_wait_io(pmu, timeout_ms);
        if (rc != HELIKON_OK)
            return rc;
        data[i] = (uint8_t)S5L_IICDS(PMU_I2C_BUS);
    }
    return HELIKON_OK;
}

static int i2c_preinit(struct helikon_pmu *pmu, uint32_t timeout_ms)
{
    /* PCON3: route the two I2C0 pins, matching the donor preinit. */
    S5L_PCON(3) = (S5L_PCON(3) & ~0x00000ff0u) | 0x00000220u;
    i2c_clock_on();

    int rc = i2c_wait_ready(pmu, timeout_ms);
    if (rc == HELIKON_OK) {
        S5L_IICADD(PMU_I2C_BUS) = PMU_I2C_OWN_ADDRESS;
        rc = i2c_wait_ready(pmu, timeout_ms);
    }
    if (rc == HELIKON_OK) {
        S5L_IICUNK14(PMU_I2C_BUS) = 1u; /* SRCCLK = ECLK */
        rc = i2c_wait_ready(pmu, timeout_ms);
    }
    if (rc == HELIKON_OK) {
        S5L_IICUNK18(PMU_I2C_BUS) = 0u;
        rc = i2c_wait_ready(pmu, timeout_ms);
    }
    if (rc == HELIKON_OK) {
        S5L_IICSTAT(PMU_I2C_BUS) = 0x80u;
        rc = i2c_wait_ready(pmu, timeout_ms);
    }
    if (rc == HELIKON_OK) {
        S5L_IICCON(PMU_I2C_BUS) = 0u;
        rc = i2c_wait_ready(pmu, timeout_ms);
    }
    if (rc == HELIKON_OK)
        S5L_IICSTA2(PMU_I2C_BUS) = 0x3f00u;
    i2c_clock_off();
    return rc;
}

static int pmu_write_raw(struct helikon_pmu *pmu, uint8_t reg, uint8_t value,
                         uint32_t timeout_ms)
{
    int rc = i2c_write_bytes(pmu, reg, &value, 1u, timeout_ms);
    int stop = i2c_stop(pmu, timeout_ms);
    return rc != HELIKON_OK ? rc : stop;
}

static int pmu_read_raw(struct helikon_pmu *pmu, uint8_t reg, uint8_t *value,
                        uint32_t timeout_ms)
{
    int rc = i2c_write_bytes(pmu, reg, NULL, 0u, timeout_ms);
    if (rc == HELIKON_OK)
        rc = i2c_stop(pmu, timeout_ms);
    if (rc == HELIKON_OK)
        rc = i2c_read_bytes(pmu, value, 1u, timeout_ms);
    int stop = i2c_stop(pmu, timeout_ms);
    return rc != HELIKON_OK ? rc : stop;
}

int helikon_pmu_init(struct helikon_pmu *pmu, struct helikon_clock *clock,
                     uint32_t timeout_ms)
{
    if (pmu == NULL || clock == NULL || !clock->initialized)
        return HELIKON_ERR_ARGUMENT;

    pmu->clock = clock;
    pmu->initialized = 0;
    int rc = i2c_preinit(pmu, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    i2c_clock_on();
    rc = i2c_wait_ready(pmu, timeout_ms);
    for (size_t i = 0; rc == HELIKON_OK &&
                       i < sizeof(pmu_preinit_table) /
                               sizeof(pmu_preinit_table[0]); ++i) {
        rc = i2c_write_bytes(pmu, pmu_preinit_table[i].reg,
                             &pmu_preinit_table[i].value, 1u, timeout_ms);
        if (rc != HELIKON_OK)
            break;
        rc = i2c_stop(pmu, timeout_ms);
    }

    if (rc == HELIKON_OK) {
        /* Clear latched interrupts after masking them. */
        uint8_t discarded[5];
        rc = i2c_write_bytes(pmu, HELIKON_PMU_REG_INT1, NULL, 0u, timeout_ms);
        if (rc == HELIKON_OK)
            rc = i2c_stop(pmu, timeout_ms);
        if (rc == HELIKON_OK)
            rc = i2c_read_bytes(pmu, discarded, sizeof(discarded),
                                timeout_ms);
        if (rc == HELIKON_OK)
            rc = i2c_stop(pmu, timeout_ms);
        if (rc == HELIKON_OK) {
            uint8_t int6;
            rc = i2c_write_bytes(pmu, HELIKON_PMU_REG_INT6, NULL, 0u,
                                 timeout_ms);
            if (rc == HELIKON_OK)
                rc = i2c_stop(pmu, timeout_ms);
            if (rc == HELIKON_OK)
                rc = i2c_read_bytes(pmu, &int6, 1u, timeout_ms);
            if (rc == HELIKON_OK)
                rc = i2c_stop(pmu, timeout_ms);
        }
    }
    i2c_clock_off();

    if (rc != HELIKON_OK)
        return rc;

    uint8_t version = 0;
    rc = helikon_pmu_read(pmu, HELIKON_PMU_REG_VERSION, &version, timeout_ms);
    if (rc != HELIKON_OK || version == 0u || version == 0xffu)
        return HELIKON_ERR_IO;

    pmu->initialized = 1;
    return HELIKON_OK;
}

int helikon_pmu_read(struct helikon_pmu *pmu, uint8_t reg, uint8_t *value,
                     uint32_t timeout_ms)
{
    if (pmu == NULL || value == NULL || pmu->clock == NULL)
        return HELIKON_ERR_ARGUMENT;
    i2c_clock_on();
    int rc = pmu_read_raw(pmu, reg, value, timeout_ms);
    i2c_clock_off();
    return rc;
}

int helikon_pmu_write(struct helikon_pmu *pmu, uint8_t reg, uint8_t value,
                      uint32_t timeout_ms)
{
    if (pmu == NULL || pmu->clock == NULL)
        return HELIKON_ERR_ARGUMENT;
    i2c_clock_on();
    int rc = pmu_write_raw(pmu, reg, value, timeout_ms);
    i2c_clock_off();
    return rc;
}

int helikon_pmu_hdd_power(struct helikon_pmu *pmu, int on,
                          uint32_t timeout_ms)
{
    return helikon_pmu_write(pmu, HELIKON_PMU_REG_AUTOENA, on ? 0x01u : 0x00u,
                             timeout_ms);
}

int helikon_pmu_backlight_set(struct helikon_pmu *pmu, uint8_t brightness,
                              uint32_t timeout_ms)
{
    return helikon_pmu_write(pmu, HELIKON_PMU_REG_LEDOUT, brightness,
                             timeout_ms);
}

int helikon_pmu_backlight_enable(struct helikon_pmu *pmu, int on,
                                 uint32_t timeout_ms)
{
    return helikon_pmu_write(pmu, HELIKON_PMU_REG_LEDENA, on ? 0x01u : 0x00u,
                             timeout_ms);
}

int helikon_pmu_external_power(struct helikon_pmu *pmu, int *present,
                               uint32_t timeout_ms)
{
    if (present == NULL)
        return HELIKON_ERR_ARGUMENT;
    uint8_t status = 0;
    int rc = helikon_pmu_read(pmu, HELIKON_PMU_REG_MBCS1, &status, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    *present = (status & 0x01u) != 0u || (status & 0x04u) != 0u;
    return HELIKON_OK;
}

int helikon_pmu_hold_switch(struct helikon_pmu *pmu, int *locked,
                            uint32_t timeout_ms)
{
    if (locked == NULL)
        return HELIKON_ERR_ARGUMENT;
    uint8_t status = 0;
    int rc = helikon_pmu_read(pmu, HELIKON_PMU_REG_GPIOSTAT, &status,
                              timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    *locked = (status & 0x02u) == 0u;
    return HELIKON_OK;
}

void helikon_pmu_power_off(struct helikon_pmu *pmu, uint32_t timeout_ms)
{
    if (pmu != NULL && pmu->initialized) {
        /* Wake on USB insertion or the accessory input; then standby. */
        (void)helikon_pmu_write(pmu, HELIKON_PMU_REG_OOCWAKE, 0x06u,
                                timeout_ms);
        (void)helikon_pmu_write(pmu, HELIKON_PMU_REG_OOCSHDWN, 0x01u,
                                timeout_ms);
    }
    helikon_cpu_disable_interrupts();
    for (;;)
        helikon_cpu_idle();
}
