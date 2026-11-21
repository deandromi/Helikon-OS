/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Label colours for the bootloader splash.
 *
 * The owner artwork supplies the illustration, title and background; only the
 * `HFS+ DETAILS` label is composited at runtime. These are the three colours it
 * can take, plus the off-white they are drawn on.
 *
 * Small text on a 2.5-inch panel has to clear 4.5:1, and the owner green
 * `#00BF63` (2.10:1 on the label background) did not. The values below keep the
 * owner hues and are the darkest step that still reads as green and red. The
 * ratios are measured on the RGB565 values the panel actually shows, and
 * `tests/bootloader/test_splash_color.c` fails the build if a colour drops below
 * the floor.
 */
#ifndef HELIKON_SPLASH_LABEL_H
#define HELIKON_SPLASH_LABEL_H

/* The label area of the base image is this exact off-white. */
#define HELIKON_SPLASH_LABEL_BG_R 239u
#define HELIKON_SPLASH_LABEL_BG_G 237u
#define HELIKON_SPLASH_LABEL_BG_B 229u

/* Probing: neutral grey, 4.87:1. */
#define HELIKON_SPLASH_LABEL_UNKNOWN_R 107u
#define HELIKON_SPLASH_LABEL_UNKNOWN_G 102u
#define HELIKON_SPLASH_LABEL_UNKNOWN_B 92u

/* HFS+ readable: deep green, 4.65:1 (4.79:1 after RGB565). */
#define HELIKON_SPLASH_LABEL_OK_R 0u
#define HELIKON_SPLASH_LABEL_OK_G 122u
#define HELIKON_SPLASH_LABEL_OK_B 61u

/* HFS+ unreadable: deep red, 4.89:1 (4.78:1 after RGB565). */
#define HELIKON_SPLASH_LABEL_FAIL_R 200u
#define HELIKON_SPLASH_LABEL_FAIL_G 30u
#define HELIKON_SPLASH_LABEL_FAIL_B 30u

#endif
