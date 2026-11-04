/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 6G backlight.
 *
 * The backlight is a PMU LED-converter output. This module exposes brightness
 * and on/off operations only; it does not own a dimming timeout or a power
 * policy. The default brightness is a bring-up value, not a panel measurement.
 */
#ifndef HELIKON_HAL_6G_BACKLIGHT_H
#define HELIKON_HAL_6G_BACKLIGHT_H

#include <stdint.h>

#include "pmu.h"

#define HELIKON_BACKLIGHT_DEFAULT 0x20u
#define HELIKON_BACKLIGHT_MAX 0x3fu

struct helikon_backlight {
    struct helikon_pmu *pmu;
    int initialized;
};

int helikon_backlight_init(struct helikon_backlight *backlight,
                           struct helikon_pmu *pmu, uint32_t timeout_ms);
int helikon_backlight_set(struct helikon_backlight *backlight,
                          uint8_t brightness, uint32_t timeout_ms);
int helikon_backlight_enable(struct helikon_backlight *backlight, int on,
                             uint32_t timeout_ms);

#endif
