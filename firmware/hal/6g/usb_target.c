/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The real S5L8702 seam for the USB controller driver.
 *
 * Separated from `usb.c` so the driver itself compiles on the host against a
 * mock (`tests/usb/test_usb_hal.c`). This file is the only place where the DWC
 * register sequence meets real memory, the timer-E clock and the VIC. It is
 * compiled for the target only.
 *
 * The core still has to be proven on a 6G: nothing here has run on hardware.
 */
#include <stdint.h>

#include "clock.h"
#include "s5l8702_regs.h"
#include "usb.h"
#include "usb_hw.h"
#include "vic.h"

static uint32_t target_read(void *context, uint32_t address)
{
    (void)context;
    return HELIKON_REG32(address);
}

static void target_write(void *context, uint32_t address, uint32_t value)
{
    (void)context;
    HELIKON_REG32(address) = value;
}

/* The DWC FIFO is 32-bit wide; a partial word is zero padded on the high byte
 * because the core counts bytes, not words. */
static void target_fifo_write(void *context, uint32_t address,
                              const void *buffer, uint32_t length)
{
    (void)context;
    const uint8_t *bytes = buffer;
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)address;
    for (uint32_t offset = 0u; offset < length; offset += 4u) {
        uint32_t word = 0u;
        for (uint32_t index = 0u; index < 4u && offset + index < length;
             ++index) {
            word |= (uint32_t)bytes[offset + index] << (8u * index);
        }
        *fifo = word;
    }
}

static void target_fifo_read(void *context, uint32_t address, void *buffer,
                             uint32_t length)
{
    (void)context;
    uint8_t *bytes = buffer;
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)address;
    for (uint32_t offset = 0u; offset < length; offset += 4u) {
        uint32_t word = *fifo;
        for (uint32_t index = 0u; index < 4u && offset + index < length;
             ++index) {
            bytes[offset + index] = (uint8_t)(word >> (8u * index));
        }
    }
}

static void target_delay_us(void *context, uint32_t microseconds)
{
    helikon_clock_delay_us((struct helikon_clock *)context, microseconds);
}

static void target_clock_enable(void *context, unsigned int gate)
{
    (void)context;
    uint32_t index = gate >= 32u ? 1u : 0u;
    uint32_t bit = gate % 32u;
    S5L_PWRCON(index) = S5L_PWRCON(index) | (1u << bit);
}

static void target_irq_enable(void *context, int enable)
{
    (void)context;
    if (enable)
        (void)helikon_vic_enable(HELIKON_VIC_IRQ_USB);
    else
        (void)helikon_vic_disable(HELIKON_VIC_IRQ_USB);
}

struct helikon_usb_hw helikon_usb_hw_s5l8702(struct helikon_clock *clock)
{
    struct helikon_usb_hw hw;
    hw.context = clock;
    hw.read = target_read;
    hw.write = target_write;
    hw.fifo_read = target_fifo_read;
    hw.fifo_write = target_fifo_write;
    hw.delay_us = target_delay_us;
    hw.clock_enable = target_clock_enable;
    hw.irq_enable = target_irq_enable;
    return hw;
}
