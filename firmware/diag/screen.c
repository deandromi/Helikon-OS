/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "screen.h"

#include <stddef.h>

#include "error.h"

#define GLYPH_WIDTH 3u
#define GLYPH_HEIGHT 5u
#define GLYPH_FIRST 32u
#define GLYPH_LAST 126u

/*
 * 3x5 bitmap font. Each entry has five rows; bit 2 is the left column, bit 1
 * the middle and bit 0 the right. Lowercase input is uppercased by the
 * renderer, so the table only needs the printable range's uppercase forms and
 * punctuation. Undefined positions are blank.
 */
static const uint8_t font[GLYPH_LAST - GLYPH_FIRST + 1u][GLYPH_HEIGHT] = {
    [' ' - GLYPH_FIRST] = { 0x0, 0x0, 0x0, 0x0, 0x0 },
    ['!' - GLYPH_FIRST] = { 0x2, 0x2, 0x2, 0x0, 0x2 },
    ['"' - GLYPH_FIRST] = { 0x5, 0x5, 0x0, 0x0, 0x0 },
    ['#' - GLYPH_FIRST] = { 0x5, 0x7, 0x5, 0x7, 0x5 },
    ['%' - GLYPH_FIRST] = { 0x5, 0x1, 0x2, 0x4, 0x5 },
    ['\'' - GLYPH_FIRST] = { 0x2, 0x2, 0x0, 0x0, 0x0 },
    ['(' - GLYPH_FIRST] = { 0x2, 0x4, 0x4, 0x4, 0x2 },
    [')' - GLYPH_FIRST] = { 0x2, 0x1, 0x1, 0x1, 0x2 },
    ['*' - GLYPH_FIRST] = { 0x5, 0x2, 0x7, 0x2, 0x5 },
    ['+' - GLYPH_FIRST] = { 0x0, 0x2, 0x7, 0x2, 0x0 },
    [',' - GLYPH_FIRST] = { 0x0, 0x0, 0x0, 0x2, 0x4 },
    ['-' - GLYPH_FIRST] = { 0x0, 0x0, 0x7, 0x0, 0x0 },
    ['.' - GLYPH_FIRST] = { 0x0, 0x0, 0x0, 0x0, 0x2 },
    ['/' - GLYPH_FIRST] = { 0x1, 0x1, 0x2, 0x4, 0x4 },
    ['0' - GLYPH_FIRST] = { 0x7, 0x5, 0x5, 0x5, 0x7 },
    ['1' - GLYPH_FIRST] = { 0x2, 0x6, 0x2, 0x2, 0x7 },
    ['2' - GLYPH_FIRST] = { 0x6, 0x1, 0x2, 0x4, 0x7 },
    ['3' - GLYPH_FIRST] = { 0x6, 0x1, 0x2, 0x1, 0x6 },
    ['4' - GLYPH_FIRST] = { 0x5, 0x5, 0x7, 0x1, 0x1 },
    ['5' - GLYPH_FIRST] = { 0x7, 0x4, 0x6, 0x1, 0x6 },
    ['6' - GLYPH_FIRST] = { 0x3, 0x4, 0x7, 0x5, 0x7 },
    ['7' - GLYPH_FIRST] = { 0x7, 0x1, 0x2, 0x2, 0x2 },
    ['8' - GLYPH_FIRST] = { 0x7, 0x5, 0x7, 0x5, 0x7 },
    ['9' - GLYPH_FIRST] = { 0x7, 0x5, 0x7, 0x1, 0x6 },
    [':' - GLYPH_FIRST] = { 0x0, 0x2, 0x0, 0x2, 0x0 },
    [';' - GLYPH_FIRST] = { 0x0, 0x2, 0x0, 0x2, 0x4 },
    ['<' - GLYPH_FIRST] = { 0x1, 0x2, 0x4, 0x2, 0x1 },
    ['=' - GLYPH_FIRST] = { 0x0, 0x7, 0x0, 0x7, 0x0 },
    ['>' - GLYPH_FIRST] = { 0x4, 0x2, 0x1, 0x2, 0x4 },
    ['?' - GLYPH_FIRST] = { 0x6, 0x1, 0x2, 0x0, 0x2 },
    ['@' - GLYPH_FIRST] = { 0x7, 0x5, 0x7, 0x4, 0x3 },
    ['A' - GLYPH_FIRST] = { 0x2, 0x5, 0x7, 0x5, 0x5 },
    ['B' - GLYPH_FIRST] = { 0x6, 0x5, 0x6, 0x5, 0x6 },
    ['C' - GLYPH_FIRST] = { 0x3, 0x4, 0x4, 0x4, 0x3 },
    ['D' - GLYPH_FIRST] = { 0x6, 0x5, 0x5, 0x5, 0x6 },
    ['E' - GLYPH_FIRST] = { 0x7, 0x4, 0x6, 0x4, 0x7 },
    ['F' - GLYPH_FIRST] = { 0x7, 0x4, 0x6, 0x4, 0x4 },
    ['G' - GLYPH_FIRST] = { 0x3, 0x4, 0x5, 0x5, 0x3 },
    ['H' - GLYPH_FIRST] = { 0x5, 0x5, 0x7, 0x5, 0x5 },
    ['I' - GLYPH_FIRST] = { 0x7, 0x2, 0x2, 0x2, 0x7 },
    ['J' - GLYPH_FIRST] = { 0x1, 0x1, 0x1, 0x5, 0x2 },
    ['K' - GLYPH_FIRST] = { 0x5, 0x5, 0x6, 0x5, 0x5 },
    ['L' - GLYPH_FIRST] = { 0x4, 0x4, 0x4, 0x4, 0x7 },
    ['M' - GLYPH_FIRST] = { 0x5, 0x7, 0x7, 0x5, 0x5 },
    ['N' - GLYPH_FIRST] = { 0x5, 0x7, 0x7, 0x7, 0x5 },
    ['O' - GLYPH_FIRST] = { 0x2, 0x5, 0x5, 0x5, 0x2 },
    ['P' - GLYPH_FIRST] = { 0x6, 0x5, 0x6, 0x4, 0x4 },
    ['Q' - GLYPH_FIRST] = { 0x2, 0x5, 0x5, 0x3, 0x1 },
    ['R' - GLYPH_FIRST] = { 0x6, 0x5, 0x6, 0x5, 0x5 },
    ['S' - GLYPH_FIRST] = { 0x3, 0x4, 0x2, 0x1, 0x6 },
    ['T' - GLYPH_FIRST] = { 0x7, 0x2, 0x2, 0x2, 0x2 },
    ['U' - GLYPH_FIRST] = { 0x5, 0x5, 0x5, 0x5, 0x7 },
    ['V' - GLYPH_FIRST] = { 0x5, 0x5, 0x5, 0x5, 0x2 },
    ['W' - GLYPH_FIRST] = { 0x5, 0x5, 0x7, 0x7, 0x5 },
    ['X' - GLYPH_FIRST] = { 0x5, 0x5, 0x2, 0x5, 0x5 },
    ['Y' - GLYPH_FIRST] = { 0x5, 0x5, 0x2, 0x2, 0x2 },
    ['Z' - GLYPH_FIRST] = { 0x7, 0x1, 0x2, 0x4, 0x7 },
    ['[' - GLYPH_FIRST] = { 0x3, 0x2, 0x2, 0x2, 0x3 },
    ['\\' - GLYPH_FIRST] = { 0x4, 0x4, 0x2, 0x1, 0x1 },
    [']' - GLYPH_FIRST] = { 0x6, 0x2, 0x2, 0x2, 0x6 },
    ['^' - GLYPH_FIRST] = { 0x2, 0x5, 0x0, 0x0, 0x0 },
    ['_' - GLYPH_FIRST] = { 0x0, 0x0, 0x0, 0x0, 0x7 },
    ['`' - GLYPH_FIRST] = { 0x4, 0x2, 0x0, 0x0, 0x0 },
    ['{' - GLYPH_FIRST] = { 0x1, 0x2, 0x6, 0x2, 0x1 },
    ['|' - GLYPH_FIRST] = { 0x2, 0x2, 0x2, 0x2, 0x2 },
    ['}' - GLYPH_FIRST] = { 0x4, 0x2, 0x3, 0x2, 0x4 },
    ['~' - GLYPH_FIRST] = { 0x0, 0x3, 0x6, 0x0, 0x0 }
};

void helikon_screen_init(struct helikon_screen *screen, uint16_t *pixels,
                         uint32_t width, uint32_t height,
                         const struct helikon_display_ops *ops,
                         void *ops_context)
{
    if (screen == NULL)
        return;
    screen->pixels = pixels;
    screen->width = width;
    screen->height = height;
    screen->ops = ops;
    screen->ops_context = ops_context;
    screen->foreground = HELIKON_COLOR_INK;
    screen->background = HELIKON_COLOR_BACKGROUND;
}

void helikon_screen_set_colors(struct helikon_screen *screen,
                               uint16_t foreground, uint16_t background)
{
    if (screen == NULL)
        return;
    screen->foreground = foreground;
    screen->background = background;
}

void helikon_screen_set_foreground(struct helikon_screen *screen,
                                   uint16_t foreground)
{
    if (screen != NULL)
        screen->foreground = foreground;
}

void helikon_screen_fill_rect(struct helikon_screen *screen, uint32_t x,
                              uint32_t y, uint32_t width, uint32_t height,
                              uint16_t color)
{
    if (screen == NULL || screen->pixels == NULL)
        return;
    if (x >= screen->width || y >= screen->height)
        return;
    if (width > screen->width - x)
        width = screen->width - x;
    if (height > screen->height - y)
        height = screen->height - y;

    for (uint32_t row = 0; row < height; ++row) {
        uint16_t *line = screen->pixels + (size_t)(y + row) * screen->width + x;
        for (uint32_t column = 0; column < width; ++column)
            line[column] = color;
    }
}

void helikon_screen_clear(struct helikon_screen *screen)
{
    if (screen == NULL)
        return;
    helikon_screen_fill_rect(screen, 0, 0, screen->width, screen->height,
                             screen->background);
}

void helikon_screen_text(struct helikon_screen *screen, uint32_t x, uint32_t y,
                         uint32_t scale, const char *text)
{
    if (screen == NULL || text == NULL || screen->pixels == NULL)
        return;
    if (scale == 0u || scale > 8u)
        return;

    uint32_t cursor = x;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        unsigned char character = (unsigned char)text[i];
        if (character >= 'a' && character <= 'z')
            character = (unsigned char)(character - 'a' + 'A');
        if (character < GLYPH_FIRST || character > GLYPH_LAST)
            character = ' ';

        const uint8_t *glyph = font[character - GLYPH_FIRST];
        for (uint32_t row = 0; row < GLYPH_HEIGHT; ++row) {
            uint8_t bits = glyph[row];
            for (uint32_t column = 0; column < GLYPH_WIDTH; ++column) {
                if ((bits & (uint8_t)(4u >> column)) == 0u)
                    continue;
                helikon_screen_fill_rect(
                    screen, cursor + column * scale, y + row * scale,
                    scale, scale, screen->foreground);
            }
        }
        cursor += (GLYPH_WIDTH + 1u) * scale;
        if (cursor >= screen->width)
            break;
    }
}

void helikon_screen_bitmap(struct helikon_screen *screen, uint32_t x,
                           uint32_t y, uint32_t width, uint32_t height,
                           const uint16_t *pixels, uint32_t stride)
{
    if (screen == NULL || screen->pixels == NULL || pixels == NULL ||
        width == 0u || height == 0u || stride < width)
        return;
    if (x >= screen->width || y >= screen->height)
        return;
    if (width > screen->width - x)
        width = screen->width - x;
    if (height > screen->height - y)
        height = screen->height - y;

    for (uint32_t row = 0; row < height; ++row) {
        uint16_t *line = screen->pixels + (size_t)(y + row) * screen->width + x;
        const uint16_t *source = pixels + (size_t)row * stride;
        for (uint32_t column = 0; column < width; ++column)
            line[column] = source[column];
    }
}

int helikon_screen_present(struct helikon_screen *screen, uint32_t x,
                           uint32_t y, uint32_t width, uint32_t height)
{
    if (screen == NULL || screen->pixels == NULL || screen->ops == NULL ||
        screen->ops->present == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (x >= screen->width || y >= screen->height)
        return HELIKON_ERR_ARGUMENT;
    if (width > screen->width - x)
        width = screen->width - x;
    if (height > screen->height - y)
        height = screen->height - y;
    if (width == 0u || height == 0u)
        return HELIKON_ERR_ARGUMENT;

    return screen->ops->present(screen->ops_context, screen->pixels,
                                screen->width, x, y, width, height);
}

int helikon_screen_present_all(struct helikon_screen *screen)
{
    if (screen == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_screen_present(screen, 0, 0, screen->width, screen->height);
}
