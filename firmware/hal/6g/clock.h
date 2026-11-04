/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Monotonic clock for timeouts and stage timing.
 *
 * Timer E is the donor's 12 MHz ECLK / prescaler 12 free-running counter, so
 * its low word ticks at 1 MHz. The driver extends it to 64 bits by counting
 * wraps; callers must sample often enough not to miss two wraps (about 71
 * minutes per wrap in F1). No wall-clock or RTC claim is made here.
 */
#ifndef HELIKON_HAL_6G_CLOCK_H
#define HELIKON_HAL_6G_CLOCK_H

#include <stdint.h>

struct helikon_clock {
    uint64_t last_us;
    uint32_t high;
    int initialized;
};

/* Returns HELIKON_OK or HELIKON_ERR_IO if timer E never starts. */
int helikon_clock_init(struct helikon_clock *clock, uint32_t timeout_ms);

/* May be called from a panic path after a successful init. */
uint64_t helikon_clock_now_us(struct helikon_clock *clock);
uint64_t helikon_clock_now_ms(struct helikon_clock *clock);

/* Bounded busy wait; `microseconds` is the bound. */
void helikon_clock_delay_us(struct helikon_clock *clock, uint32_t microseconds);

int helikon_clock_expired(struct helikon_clock *clock, uint64_t start_us,
                          uint32_t timeout_ms);

#endif
