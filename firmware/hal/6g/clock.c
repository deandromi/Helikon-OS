/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stddef.h>
#include "clock.h"

#include "error.h"
#include "s5l8702_regs.h"

#define CG16_DISABLE_BIT 0x8000u
#define CG16_SEL_OSC 0u
#define CG16_DIV1_ONE 0u
#define CG16_DIV2_ONE 0u

static void clockgate_enable(uint32_t gate, int enable)
{
    uint32_t index = gate >> 5;
    uint32_t bit = 1u << (gate & 0x1fu);
    if (enable)
        S5L_PWRCON(index) &= ~bit;
    else
        S5L_PWRCON(index) |= bit;
}

static int cg16_config_rtime(uint16_t value, uint32_t timeout_ms)
{
    uint32_t address = S5L_CLK_BASE + 0x10u;
    uint32_t shift = 16u;
    uint32_t mask = 0xffffu << shift;
    uint32_t current = HELIKON_REG32(address);
    HELIKON_REG32(address) = (current & ~mask) | ((uint32_t)value << shift);

    uint64_t spins = (uint64_t)timeout_ms * 10000u;
    if (spins == 0u)
        spins = 10000u;
    for (uint64_t i = 0; i < spins; ++i) {
        if ((HELIKON_REG32(address) & mask) == ((uint32_t)value << shift))
            return HELIKON_OK;
    }
    return HELIKON_ERR_IO;
}

int helikon_clock_init(struct helikon_clock *clock, uint32_t timeout_ms)
{
    if (clock == NULL)
        return HELIKON_ERR_ARGUMENT;

    clock->last_us = 0;
    clock->high = 0;
    clock->initialized = 0;

    /* Select OSC0 for the real-time clock gate and enable its output. */
    S5L_PLLMODE &= ~S5L_PLLMODE_OSCSEL;
    uint16_t rtime = (uint16_t)((CG16_SEL_OSC << 12) |
                                (CG16_DIV1_ONE << 0) |
                                (CG16_DIV2_ONE << 4));
    int rc = cg16_config_rtime((uint16_t)(rtime & ~CG16_DISABLE_BIT),
                               timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    clockgate_enable(S5L_CLOCKGATE_TIMER, 1);

    /* TE_CS = ECLK / 1, ECLK source, interval mode. */
    S5L_TECON = (4u << 8) | (1u << 6) | (0u << 4);
    S5L_TEPRE = 11u; /* 12 MHz / (11 + 1) = 1 MHz */
    S5L_TEDATA0 = 0xffffffffu;
    S5L_TECMD = (1u << 1) | (1u << 0); /* clear, enable */

    uint32_t first = S5L_TECNT;
    uint64_t spins = (uint64_t)timeout_ms * 10000u;
    if (spins == 0u)
        spins = 10000u;
    int running = 0;
    for (uint64_t i = 0; i < spins; ++i) {
        if (S5L_TECNT != first) {
            running = 1;
            break;
        }
    }
    if (!running)
        return HELIKON_ERR_IO;

    clock->last_us = S5L_TECNT;
    clock->high = 0;
    clock->initialized = 1;
    return HELIKON_OK;
}

uint64_t helikon_clock_now_us(struct helikon_clock *clock)
{
    if (clock == NULL || !clock->initialized)
        return 0u;

    uint32_t low = S5L_TECNT;
    uint32_t previous = (uint32_t)clock->last_us;
    if (low < previous)
        ++clock->high;
    clock->last_us = ((uint64_t)clock->high << 32) | low;
    return clock->last_us;
}

uint64_t helikon_clock_now_ms(struct helikon_clock *clock)
{
    return helikon_clock_now_us(clock) / 1000u;
}

void helikon_clock_delay_us(struct helikon_clock *clock, uint32_t microseconds)
{
    if (clock == NULL || !clock->initialized)
        return;
    uint64_t start = helikon_clock_now_us(clock);
    while (helikon_clock_now_us(clock) - start < (uint64_t)microseconds)
        ;
}

int helikon_clock_expired(struct helikon_clock *clock, uint64_t start_us,
                          uint32_t timeout_ms)
{
    if (clock == NULL || !clock->initialized)
        return 1;
    uint64_t now = helikon_clock_now_us(clock);
    if (now < start_us)
        return 0;
    return now - start_us >= (uint64_t)timeout_ms * 1000u;
}
