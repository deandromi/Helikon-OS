/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_BOOTLOADER_SPLASH_H
#define HELIKON_BOOTLOADER_SPLASH_H

#include <stdint.h>

#define HELIKON_SPLASH_WIDTH 320u
#define HELIKON_SPLASH_HEIGHT 240u
#define HELIKON_SPLASH_STRIDE 40u
#define HELIKON_SPLASH_BYTES (HELIKON_SPLASH_STRIDE * HELIKON_SPLASH_HEIGHT)

extern const uint8_t helikon_splash_bits[HELIKON_SPLASH_BYTES];

/* 1 = ink, 0 = background. */
static inline int helikon_splash_pixel(unsigned int x, unsigned int y)
{
    if (x >= HELIKON_SPLASH_WIDTH || y >= HELIKON_SPLASH_HEIGHT)
        return 0;
    return (helikon_splash_bits[y * HELIKON_SPLASH_STRIDE + (x / 8u)] >>
            (7u - (x % 8u))) & 1u;
}

#endif
