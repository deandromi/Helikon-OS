/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PCF5063x PMU operations for the iPod Classic 6G.
 *
 * The PMU is reached over I2C bus 0 at slave address 0xE6. Every wait is
 * bounded by the caller's timeout. The register sequence is the donor
 * pmu_preinit table (GPL-2.0-or-later, firmware/target/arm/s5l8702/ipod6g/
 * pmu-6g.c) and is treated as a hardware sequence, not as a measurement.
 *
 * The HAL exposes operations, not policy: no sleep timeout, no charger policy,
 * no retry loop. The boot flow decides what to do with a status value.
 */
#ifndef HELIKON_HAL_6G_PMU_H
#define HELIKON_HAL_6G_PMU_H

#include <stdint.h>

#include "clock.h"

enum helikon_pmu_register {
    HELIKON_PMU_REG_VERSION = 0x00,
    HELIKON_PMU_REG_OOCSHDWN = 0x0c,
    HELIKON_PMU_REG_OOCWAKE = 0x0d,
    HELIKON_PMU_REG_OOCSTAT = 0x12,
    HELIKON_PMU_REG_GPIOCTL = 0x13,
    HELIKON_PMU_REG_GPIO1CFG = 0x14,
    HELIKON_PMU_REG_GPIO2CFG = 0x15,
    HELIKON_PMU_REG_GPIO3CFG = 0x16,
    HELIKON_PMU_REG_GPOCFG = 0x17,
    HELIKON_PMU_REG_SVMCTL = 0x19,
    HELIKON_PMU_REG_AUTOOUT = 0x1a,
    HELIKON_PMU_REG_AUTOENA = 0x1b,
    HELIKON_PMU_REG_AUTOCTL = 0x1c,
    HELIKON_PMU_REG_AUTOMXC = 0x1d,
    HELIKON_PMU_REG_DOWN2OUT = 0x22,
    HELIKON_PMU_REG_DOWN2ENA = 0x23,
    HELIKON_PMU_REG_DOWN2CTL = 0x24,
    HELIKON_PMU_REG_DOWN2MXC = 0x25,
    HELIKON_PMU_REG_MEMLDOOUT = 0x26,
    HELIKON_PMU_REG_MEMLDOENA = 0x27,
    HELIKON_PMU_REG_LEDOUT = 0x28,
    HELIKON_PMU_REG_LEDENA = 0x29,
    HELIKON_PMU_REG_LEDCTL = 0x2a,
    HELIKON_PMU_REG_LEDDIM = 0x2b,
    HELIKON_PMU_REG_LDO1OUT = 0x2d,
    HELIKON_PMU_REG_LDO1ENA = 0x2e,
    HELIKON_PMU_REG_LDO2OUT = 0x2f,
    HELIKON_PMU_REG_LDO2ENA = 0x30,
    HELIKON_PMU_REG_LDO3OUT = 0x31,
    HELIKON_PMU_REG_LDO3ENA = 0x32,
    HELIKON_PMU_REG_LDO4OUT = 0x33,
    HELIKON_PMU_REG_LDO4ENA = 0x34,
    HELIKON_PMU_REG_LDO5OUT = 0x35,
    HELIKON_PMU_REG_LDO5ENA = 0x36,
    HELIKON_PMU_REG_LDO6OUT = 0x37,
    HELIKON_PMU_REG_LDO6ENA = 0x38,
    HELIKON_PMU_REG_HCLDOOUT = 0x39,
    HELIKON_PMU_REG_HCLDOENA = 0x3a,
    HELIKON_PMU_REG_STBYCTL1 = 0x3b,
    HELIKON_PMU_REG_STBYCTL2 = 0x3c,
    HELIKON_PMU_REG_MBCS1 = 0x4b,
    HELIKON_PMU_REG_INT1 = 0x02,
    HELIKON_PMU_REG_INT1M = 0x07,
    HELIKON_PMU_REG_INT6 = 0x85,
    HELIKON_PMU_REG_INT6M = 0x86,
    HELIKON_PMU_REG_GPIOSTAT = 0x87
};

struct helikon_pmu {
    struct helikon_clock *clock;
    int initialized;
};

/*
 * Probe and configure the PMU. Returns HELIKON_OK, HELIKON_ERR_IO on an I2C
 * timeout or HELIKON_ERR_ARGUMENT. Blocks for the duration of the register
 * sequence.
 */
int helikon_pmu_init(struct helikon_pmu *pmu, struct helikon_clock *clock,
                     uint32_t timeout_ms);

int helikon_pmu_read(struct helikon_pmu *pmu, uint8_t reg, uint8_t *value,
                     uint32_t timeout_ms);
int helikon_pmu_write(struct helikon_pmu *pmu, uint8_t reg, uint8_t value,
                      uint32_t timeout_ms);

int helikon_pmu_hdd_power(struct helikon_pmu *pmu, int on,
                          uint32_t timeout_ms);
int helikon_pmu_backlight_set(struct helikon_pmu *pmu, uint8_t brightness,
                              uint32_t timeout_ms);
int helikon_pmu_backlight_enable(struct helikon_pmu *pmu, int on,
                                 uint32_t timeout_ms);
int helikon_pmu_external_power(struct helikon_pmu *pmu, int *present,
                               uint32_t timeout_ms);
int helikon_pmu_hold_switch(struct helikon_pmu *pmu, int *locked,
                            uint32_t timeout_ms);

/* Enter PMU standby. Never writes the filesystem and never returns. */
void helikon_pmu_power_off(struct helikon_pmu *pmu, uint32_t timeout_ms)
    __attribute__((noreturn));

#endif
