/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded IBM Plex Sans text rendering for the Music flow.
 *
 * The glyph atlas is generated at build time (tools/make-ui-font-atlas.py) and
 * lives in read-only .rodata. This module never rasterizes a font, never
 * allocates and never walks an unbounded string: every draw takes an explicit
 * width limit or a line count.
 *
 * The 4-bit alpha atlas is the normal path. A separate 1-bit fallback set at
 * the same sizes exists for atlas failure and is reported by
 * helikon_text_fallback_active() so the UI can show the substitution.
 */
#ifndef HELIKON_UI_UI_TEXT_H
#define HELIKON_UI_UI_TEXT_H

#include <stdint.h>

#include "font_atlas.h"
#include "screen.h"

void helikon_text_init(void);

/* Read-only status for the current atlas; also usable by host tests. */
int helikon_text_fallback_active(void);
void helikon_text_set_fallback(int active);

uint32_t helikon_text_line_height(uint8_t style);
uint32_t helikon_text_width(uint8_t style, const char *text);

/*
 * Draw one line at a baseline. `max_width` 0 means no limit. A positive limit
 * truncates at a word boundary with U+2026 and never shrinks the type. Returns
 * the drawn width in pixels.
 */
uint32_t helikon_text_draw(struct helikon_screen *screen, int32_t x,
                           int32_t baseline, uint8_t style, uint16_t color,
                           const char *text, uint32_t max_width);

uint32_t helikon_text_draw_right(struct helikon_screen *screen, int32_t right,
                                 int32_t baseline, uint8_t style,
                                 uint16_t color, const char *text,
                                 uint32_t max_width);

uint32_t helikon_text_draw_centered(struct helikon_screen *screen,
                                    int32_t center, int32_t baseline,
                                    uint8_t style, uint16_t color,
                                    const char *text);

/*
 * Draw up to `max_lines` lines at `line_height` pitch. The last visible line
 * ends with U+2026 when text remains. `line_height` 0 uses the style's D-23
 * line height. Returns the number of lines drawn.
 */
uint32_t helikon_text_draw_wrapped(struct helikon_screen *screen, int32_t x,
                                   int32_t baseline, uint8_t style,
                                   uint16_t color, const char *text,
                                   uint32_t max_width, uint32_t max_lines,
                                   uint32_t line_height);

#endif
