/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stddef.h>
#include "buttons.h"

#include "error.h"
#include "s5l8702_regs.h"

#define WHEEL_CLICKS_PER_ROTATION 96

static void clickwheel_init(void)
{
    S5L_PWRCON(S5L_CLOCKGATE_CWHEEL >> 5) &=
        ~(1u << (S5L_CLOCKGATE_CWHEEL & 0x1fu));
    S5L_PCON(14) = (S5L_PCON(14) & ~0x00ffff00u) | 0x00222200u;
    S5L_WHEEL00 = 0u;
    S5L_WHEELINT = 7u;
    S5L_WHEEL10 = 1u;
    S5L_WHEEL00 = 0x380000u;
    S5L_WHEEL08 = 0x20000u;
    S5L_WHEELTX = 0x8000023au;
    S5L_WHEEL04 |= 1u;
}

int helikon_buttons_init(struct helikon_buttons *buttons,
                         struct helikon_pmu *pmu, struct helikon_clock *clock,
                         uint32_t timeout_ms)
{
    if (buttons == NULL || pmu == NULL || clock == NULL ||
        !clock->initialized)
        return HELIKON_ERR_ARGUMENT;

    buttons->pmu = pmu;
    buttons->clock = clock;
    buttons->last_wheel = 0;
    buttons->wheel_valid = 0;
    buttons->initialized = 0;

    clickwheel_init();

    /*
     * The controller can return the last WHEELTX command echo until its first
     * real event arrives. A cold boot with no button held therefore never
     * reported 0x8000001a and the launcher treated a healthy wheel as a fatal
     * input failure. Wait briefly for a clean status, but treat configuration
     * as success: poll() validates every later sample and the UI keeps polling
     * until the first button or wheel event.
     */
    uint32_t settle_ms = timeout_ms < 20u ? timeout_ms : 20u;
    uint64_t start = helikon_clock_now_us(clock);
    while (!helikon_clock_expired(clock, start, settle_ms)) {
        uint32_t status = S5L_WHEELRX;
        if ((status & 0x800000ffu) == 0x8000001au)
            break;
        helikon_clock_delay_us(clock, 1000u);
    }
    buttons->initialized = 1;
    return HELIKON_OK;
}

int helikon_buttons_poll(struct helikon_buttons *buttons, uint32_t *events,
                         int32_t *wheel_delta, int *hold,
                         uint32_t timeout_ms)
{
    if (buttons == NULL || events == NULL || wheel_delta == NULL ||
        hold == NULL || !buttons->initialized)
        return HELIKON_ERR_ARGUMENT;

    *events = 0;
    *wheel_delta = 0;
    *hold = 0;

    if (buttons->pmu != NULL) {
        int locked = 0;
        int rc = helikon_pmu_hold_switch(buttons->pmu, &locked, timeout_ms);
        if (rc == HELIKON_OK)
            *hold = locked;
    }

    /*
     * The wheel controller latches events in WHEELINT. The donor's interrupt
     * handler clears the pending bits before reading WHEELRX; do the same, or
     * the RX status keeps reporting the last latched value and never sees a
     * new button or wheel movement.
     */
    uint32_t latch = S5L_WHEELINT;
    if ((latch & 4u) != 0u)
        S5L_WHEELINT = 4u;
    if ((latch & 2u) != 0u)
        S5L_WHEELINT = 2u;
    if ((latch & 1u) != 0u)
        S5L_WHEELINT = 1u;

    uint32_t status = S5L_WHEELRX;
    uint8_t wheel = 0;
    int touched = 0;

    if ((status & 0x800000ffu) == 0x8000001au) {
        if ((status & 0x00000100u) != 0u)
            *events |= HELIKON_BUTTON_SELECT;
        if ((status & 0x00000200u) != 0u)
            *events |= HELIKON_BUTTON_RIGHT;
        if ((status & 0x00000400u) != 0u)
            *events |= HELIKON_BUTTON_LEFT;
        if ((status & 0x00000800u) != 0u)
            *events |= HELIKON_BUTTON_PLAY;
        if ((status & 0x00001000u) != 0u)
            *events |= HELIKON_BUTTON_MENU;
        if ((status & 0x40000000u) != 0u) {
            touched = 1;
            wheel = (uint8_t)((status >> 16) & 0x7fu);
        }
    } else {
        return HELIKON_ERR_IO;
    }

    if (!touched) {
        buttons->wheel_valid = 0;
        return HELIKON_OK;
    }

    if (buttons->wheel_valid) {
        int32_t delta = (int32_t)wheel - (int32_t)buttons->last_wheel;
        if (delta < -(WHEEL_CLICKS_PER_ROTATION / 2))
            delta += WHEEL_CLICKS_PER_ROTATION;
        else if (delta > WHEEL_CLICKS_PER_ROTATION / 2)
            delta -= WHEEL_CLICKS_PER_ROTATION;
        *wheel_delta = delta;
    }
    buttons->last_wheel = wheel;
    buttons->wheel_valid = 1;
    return HELIKON_OK;
}
