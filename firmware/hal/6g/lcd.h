/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 6G LCD panel interface.
 *
 * Panel detection and the command sequences are ported from the proven Rockbox
 * 6G driver (GPL-2.0-or-later, firmware/target/arm/s5l8702/ipod6g/lcd-6g.c
 * and firmware/target/arm/s5l8702/lcd-s5l8702.c). The panel type strapping and
 * timing are donor facts, not a Helikon-OS measurement.
 *
 * The HAL exposes init and present. It does not own a framebuffer, a theme, a
 * backlight timeout or a UI policy. Every status wait has a timeout.
 */
#ifndef HELIKON_HAL_6G_LCD_H
#define HELIKON_HAL_6G_LCD_H

#include <stdint.h>

#include "clock.h"

#define HELIKON_LCD_WIDTH 320u
#define HELIKON_LCD_HEIGHT 240u

struct helikon_lcd {
    struct helikon_clock *clock;
    uint8_t panel_type;
    uint8_t cmdset;
    uint32_t cmd_mode;
    uint32_t frame_mode;
    int initialized;
};

int helikon_lcd_init(struct helikon_lcd *lcd, struct helikon_clock *clock,
                     uint32_t timeout_ms);

/* Matches struct helikon_display_ops::present. */
int helikon_lcd_present(void *context, const uint16_t *pixels, uint32_t stride,
                        uint32_t x, uint32_t y, uint32_t width,
                        uint32_t height);

#endif
