/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stddef.h>
#include "backlight.h"

#include "error.h"

int helikon_backlight_init(struct helikon_backlight *backlight,
                           struct helikon_pmu *pmu, uint32_t timeout_ms)
{
    if (backlight == NULL || pmu == NULL || !pmu->initialized)
        return HELIKON_ERR_ARGUMENT;

    backlight->pmu = pmu;
    backlight->initialized = 0;

    /* LEDCTL: overvoltage protection, 500 mA OCP limit. */
    int rc = helikon_pmu_write(pmu, HELIKON_PMU_REG_LEDCTL, 0x05u,
                               timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    /* LEDDIM: T_dimstep = 16 * 0x14 / 32768. */
    rc = helikon_pmu_write(pmu, HELIKON_PMU_REG_LEDDIM, 0x14u, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = helikon_backlight_set(backlight, HELIKON_BACKLIGHT_DEFAULT,
                               timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = helikon_backlight_enable(backlight, 1, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    backlight->initialized = 1;
    return HELIKON_OK;
}

int helikon_backlight_set(struct helikon_backlight *backlight,
                          uint8_t brightness, uint32_t timeout_ms)
{
    if (backlight == NULL || backlight->pmu == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (brightness > HELIKON_BACKLIGHT_MAX)
        brightness = HELIKON_BACKLIGHT_MAX;
    return helikon_pmu_backlight_set(backlight->pmu, brightness, timeout_ms);
}

int helikon_backlight_enable(struct helikon_backlight *backlight, int on,
                             uint32_t timeout_ms)
{
    if (backlight == NULL || backlight->pmu == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_pmu_backlight_enable(backlight->pmu, on, timeout_ms);
}
