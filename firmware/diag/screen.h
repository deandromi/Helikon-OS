/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Tiny RGB565 screen and text renderer for F1 bring-up.
 *
 * The renderer owns no memory: it draws into a caller-supplied framebuffer and
 * pushes completed rectangles through a display operation. There is no font
 * rasterization per frame, no floating point and no storage access. The 3x5
 * bitmap font is scaled with integer arithmetic and is deliberately the only
 * font in F1; the first-party UI font atlas is F4.
 */
#ifndef HELIKON_DIAG_SCREEN_H
#define HELIKON_DIAG_SCREEN_H

#include <stdint.h>

#define HELIKON_SCREEN_WIDTH 320u
#define HELIKON_SCREEN_HEIGHT 240u

/* Off-white, ink, neutral, success and failure, RGB565 approximations. */
#define HELIKON_COLOR_BACKGROUND 0xef7cu
#define HELIKON_COLOR_INK 0x18c3u
#define HELIKON_COLOR_NEUTRAL 0x6b2bu
#define HELIKON_COLOR_SUCCESS 0x05ecu
#define HELIKON_COLOR_FAILURE 0xf986u

struct helikon_display_ops {
    /*
     * Copy a rectangle of RGB565 pixels to the panel. `pixels` points at the
     * top-left of the full framebuffer and `stride` is its row length in
     * pixels. May block for the panel transfer; it must not touch storage.
     * Returns HELIKON_OK or a negative error.
     */
    int (*present)(void *context, const uint16_t *pixels, uint32_t stride,
                   uint32_t x, uint32_t y, uint32_t width, uint32_t height);
};

struct helikon_screen {
    uint16_t *pixels;
    uint32_t width;
    uint32_t height;
    const struct helikon_display_ops *ops;
    void *ops_context;
    uint16_t foreground;
    uint16_t background;
};

void helikon_screen_init(struct helikon_screen *screen, uint16_t *pixels,
                         uint32_t width, uint32_t height,
                         const struct helikon_display_ops *ops,
                         void *ops_context);

void helikon_screen_set_colors(struct helikon_screen *screen,
                               uint16_t foreground, uint16_t background);
void helikon_screen_set_foreground(struct helikon_screen *screen,
                                   uint16_t foreground);
void helikon_screen_clear(struct helikon_screen *screen);
void helikon_screen_fill_rect(struct helikon_screen *screen, uint32_t x,
                              uint32_t y, uint32_t width, uint32_t height,
                              uint16_t color);

/* Draw at (x, y) with a 3x5 glyph scaled by `scale` (1..8). No wrapping. */
void helikon_screen_text(struct helikon_screen *screen, uint32_t x, uint32_t y,
                         uint32_t scale, const char *text);

int helikon_screen_present(struct helikon_screen *screen, uint32_t x,
                           uint32_t y, uint32_t width, uint32_t height);
int helikon_screen_present_all(struct helikon_screen *screen);

#endif
