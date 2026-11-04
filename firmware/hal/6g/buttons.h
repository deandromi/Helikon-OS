/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Polled 6G click wheel and buttons.
 *
 * F1 does not enable interrupts. The controller is initialized from the donor
 * s5l_clickwheel_init sequence and polled once per main-loop pass. The hold
 * switch is read from the PMU, not from a cached flag. The API reports raw
 * events and a wheel delta; acceleration and focus policy belong to the UI.
 */
#ifndef HELIKON_HAL_6G_BUTTONS_H
#define HELIKON_HAL_6G_BUTTONS_H

#include <stdint.h>

#include "clock.h"
#include "pmu.h"

#define HELIKON_BUTTON_SELECT 0x00000001u
#define HELIKON_BUTTON_MENU 0x00000002u
#define HELIKON_BUTTON_LEFT 0x00000004u
#define HELIKON_BUTTON_RIGHT 0x00000008u
#define HELIKON_BUTTON_PLAY 0x00000010u
#define HELIKON_BUTTON_ANY 0x0000001fu

struct helikon_buttons {
    struct helikon_pmu *pmu;
    struct helikon_clock *clock;
    uint8_t last_wheel;
    int wheel_valid;
    int initialized;
};

int helikon_buttons_init(struct helikon_buttons *buttons,
                         struct helikon_pmu *pmu, struct helikon_clock *clock,
                         uint32_t timeout_ms);

/*
 * Read one sample. `events` is a HELIKON_BUTTON_* mask, `wheel_delta` is the
 * signed change since the previous sample (0 when untouched), and `hold` is 1
 * when the hold switch is engaged. Does not block.
 */
int helikon_buttons_poll(struct helikon_buttons *buttons, uint32_t *events,
                         int32_t *wheel_delta, int *hold,
                         uint32_t timeout_ms);

#endif
