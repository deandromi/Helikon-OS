/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ui_text.h"

#include <stddef.h>
#include <stdint.h>

#include "font_atlas.h"

#define TEXT_LIMIT 4096u
#define ELLIPSIS_CODEPOINT 0x2026u
#define QUESTION_CODEPOINT 0x003Fu

static int fallback_active;

static uint32_t text_length(const char *text)
{
    uint32_t length = 0u;
    if (text == NULL)
        return 0u;
    while (length < TEXT_LIMIT && text[length] != '\0')
        ++length;
    return length;
}

static int atlas_valid(void)
{
    return helikon_font_style_count == HELIKON_FONT_STYLE_COUNT &&
           helikon_font_glyph_count != 0u &&
           helikon_font_bits_bytes != 0u &&
           helikon_font_fallback_glyph_count != 0u &&
           helikon_font_fallback_bits_bytes != 0u;
}

void helikon_text_init(void)
{
    fallback_active = atlas_valid() ? 0 : 1;
}

int helikon_text_fallback_active(void)
{
    return fallback_active != 0;
}

void helikon_text_set_fallback(int active)
{
    fallback_active = active != 0;
    if (!atlas_valid())
        fallback_active = 1;
}

static const struct helikon_font_style *style_at(uint8_t style)
{
    if (style >= HELIKON_FONT_STYLE_COUNT || !atlas_valid()) {
        fallback_active = 1;
        return &helikon_font_styles[0];
    }
    return &helikon_font_styles[style];
}

uint32_t helikon_text_line_height(uint8_t style)
{
    return style_at(style)->line_height;
}

static const struct helikon_font_glyph *find_glyph(const struct helikon_font_style *face,
                                                   uint32_t codepoint,
                                                   int fallback)
{
    uint32_t first = fallback ? face->first_fallback : face->first_glyph;
    uint32_t count = fallback ? face->fallback_count : face->glyph_count;
    const struct helikon_font_glyph *table =
        fallback ? helikon_font_fallback_glyphs : helikon_font_glyphs;
    uint32_t low = 0u;
    uint32_t high = count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        uint32_t value = table[first + middle].codepoint;
        if (value == codepoint)
            return &table[first + middle];
        if (value < codepoint)
            low = middle + 1u;
        else
            high = middle;
    }
    return NULL;
}

static const struct helikon_font_glyph *glyph_for(const struct helikon_font_style *face,
                                                  uint32_t codepoint,
                                                  int fallback)
{
    const struct helikon_font_glyph *glyph =
        find_glyph(face, codepoint, fallback);
    if (glyph == NULL)
        glyph = find_glyph(face, QUESTION_CODEPOINT, fallback);
    return glyph;
}

static uint32_t utf8_next(const char *text, uint32_t *index)
{
    uint32_t position = *index;
    uint8_t first = (uint8_t)text[position];
    if (first < 0x80u) {
        *index = position + 1u;
        return first;
    }

    uint32_t codepoint = QUESTION_CODEPOINT;
    uint32_t length = 1u;
    if ((first & 0xe0u) == 0xc0u && text[position + 1u] != '\0' &&
        ((uint8_t)text[position + 1u] & 0xc0u) == 0x80u) {
        codepoint = ((uint32_t)(first & 0x1fu) << 6) |
                    ((uint8_t)text[position + 1u] & 0x3fu);
        length = 2u;
    } else if ((first & 0xf0u) == 0xe0u && text[position + 1u] != '\0' &&
               text[position + 2u] != '\0' &&
               ((uint8_t)text[position + 1u] & 0xc0u) == 0x80u &&
               ((uint8_t)text[position + 2u] & 0xc0u) == 0x80u) {
        codepoint = ((uint32_t)(first & 0x0fu) << 12) |
                    (((uint8_t)text[position + 1u] & 0x3fu) << 6) |
                    ((uint8_t)text[position + 2u] & 0x3fu);
        length = 3u;
    } else if ((first & 0xf8u) == 0xf0u && text[position + 1u] != '\0' &&
               text[position + 2u] != '\0' && text[position + 3u] != '\0' &&
               ((uint8_t)text[position + 1u] & 0xc0u) == 0x80u &&
               ((uint8_t)text[position + 2u] & 0xc0u) == 0x80u &&
               ((uint8_t)text[position + 3u] & 0xc0u) == 0x80u) {
        codepoint = ((uint32_t)(first & 0x07u) << 18) |
                    (((uint8_t)text[position + 1u] & 0x3fu) << 12) |
                    (((uint8_t)text[position + 2u] & 0x3fu) << 6) |
                    ((uint8_t)text[position + 3u] & 0x3fu);
        length = 4u;
        if (codepoint > 0x10ffffu)
            codepoint = QUESTION_CODEPOINT;
    }
    *index = position + length;
    return codepoint;
}

static uint32_t glyph_advance(const struct helikon_font_style *face,
                              uint32_t codepoint, int fallback)
{
    const struct helikon_font_glyph *glyph =
        glyph_for(face, codepoint, fallback);
    return glyph == NULL ? 0u : glyph->advance;
}

static uint32_t range_width(const struct helikon_font_style *face,
                            const char *text, uint32_t start, uint32_t end,
                            int fallback)
{
    uint32_t width = 0u;
    uint32_t position = start;
    while (position < end && text[position] != '\0') {
        uint32_t codepoint = utf8_next(text, &position);
        width += glyph_advance(face, codepoint, fallback);
    }
    return width;
}

static uint16_t blend565(uint16_t destination, uint16_t foreground,
                         uint8_t alpha)
{
    uint32_t inverse = 15u - alpha;
    uint32_t red = (((uint32_t)(foreground >> 11) & 0x1fu) * alpha +
                    ((uint32_t)(destination >> 11) & 0x1fu) * inverse + 7u) /
                   15u;
    uint32_t green = (((uint32_t)(foreground >> 5) & 0x3fu) * alpha +
                      ((uint32_t)(destination >> 5) & 0x3fu) * inverse + 7u) /
                     15u;
    uint32_t blue = (((uint32_t)foreground & 0x1fu) * alpha +
                     ((uint32_t)destination & 0x1fu) * inverse + 7u) /
                    15u;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void draw_glyph(struct helikon_screen *screen, int32_t pen_x,
                       int32_t baseline, uint16_t color,
                       const struct helikon_font_glyph *glyph, int fallback)
{
    if (screen == NULL || screen->pixels == NULL || glyph == NULL ||
        glyph->width == 0u || glyph->height == 0u)
        return;

    const uint8_t *bits = fallback ? helikon_font_fallback_bits
                                   : helikon_font_bits;
    uint32_t max_offset = fallback ? helikon_font_fallback_bits_bytes
                                   : helikon_font_bits_bytes;
    for (uint32_t row = 0u; row < glyph->height; ++row) {
        uint32_t row_offset = (uint32_t)glyph->offset +
                              row * (uint32_t)glyph->stride;
        if (row_offset > max_offset || (uint32_t)glyph->stride > max_offset - row_offset)
            return;
        for (uint32_t column = 0u; column < glyph->width; ++column) {
            uint32_t source = row_offset + column / (fallback ? 8u : 2u);
            if (source >= max_offset)
                break;
            uint8_t value = bits[source];
            uint8_t alpha;
            if (fallback) {
                alpha = (((uint32_t)value >> (7u - (column & 7u))) & 1u) != 0u
                            ? 15u
                            : 0u;
            } else {
                alpha = (column & 1u) == 0u ? (uint8_t)(value >> 4)
                                            : (uint8_t)(value & 0x0fu);
            }
            if (alpha == 0u)
                continue;

            int32_t pixel_x = pen_x + glyph->bearing_x + (int32_t)column;
            int32_t pixel_y = baseline + glyph->bearing_y + (int32_t)row;
            if (pixel_x < 0 || pixel_y < 0)
                continue;
            uint32_t x = (uint32_t)pixel_x;
            uint32_t y = (uint32_t)pixel_y;
            if (x >= screen->width || y >= screen->height)
                continue;
            uint16_t *pixel = &screen->pixels[(size_t)y * screen->width + x];
            if (fallback)
                *pixel = color;
            else
                *pixel = blend565(*pixel, color, alpha);
        }
    }
}

static uint32_t draw_range(struct helikon_screen *screen, int32_t x,
                           int32_t baseline, const struct helikon_font_style *face,
                           uint16_t color, const char *text, uint32_t start,
                           uint32_t end, int fallback)
{
    uint32_t position = start;
    int32_t pen = x;
    while (position < end && text[position] != '\0') {
        uint32_t codepoint = utf8_next(text, &position);
        const struct helikon_font_glyph *glyph =
            glyph_for(face, codepoint, fallback);
        if (glyph == NULL)
            continue;
        draw_glyph(screen, pen, baseline, color, glyph, fallback);
        pen += glyph->advance;
    }
    return (uint32_t)(pen - x);
}

static uint32_t ellipsis_width(const struct helikon_font_style *face,
                               int fallback)
{
    return glyph_advance(face, ELLIPSIS_CODEPOINT, fallback);
}

/* Largest prefix that fits, without adding an ellipsis. */
static uint32_t fit_range(const struct helikon_font_style *face,
                          const char *text, uint32_t start, uint32_t length,
                          uint32_t max_width, int fallback)
{
    uint32_t position = start;
    uint32_t width = 0u;
    uint32_t last_space = UINT32_MAX;
    while (position < length && text[position] != '\0') {
        uint32_t before = position;
        uint32_t codepoint = utf8_next(text, &position);
        uint32_t advance = glyph_advance(face, codepoint, fallback);
        if (width + advance > max_width)
            break;
        if (codepoint == 0x20u)
            last_space = before;
        width += advance;
    }
    uint32_t end = position;
    if (end < length && last_space != UINT32_MAX && last_space > start)
        end = last_space;
    if (end == start && start < length) {
        uint32_t next = start;
        (void)utf8_next(text, &next);
        end = next;
    }
    return end;
}

static uint32_t fit_range_ellipsis(const struct helikon_font_style *face,
                                   const char *text, uint32_t start,
                                   uint32_t length, uint32_t max_width,
                                   int fallback, uint32_t *out_width)
{
    uint32_t ellipsis = ellipsis_width(face, fallback);
    uint32_t position = start;
    uint32_t width = 0u;
    uint32_t last_space = UINT32_MAX;
    while (position < length && text[position] != '\0') {
        uint32_t before = position;
        uint32_t codepoint = utf8_next(text, &position);
        uint32_t advance = glyph_advance(face, codepoint, fallback);
        if (width + advance + ellipsis > max_width)
            break;
        if (codepoint == 0x20u)
            last_space = before;
        width += advance;
    }
    uint32_t end = position;
    if (end < length && last_space != UINT32_MAX && last_space > start)
        end = last_space;
    if (end == start && start < length) {
        uint32_t next = start;
        (void)utf8_next(text, &next);
        end = next;
    }
    *out_width = range_width(face, text, start, end, fallback);
    return end;
}

uint32_t helikon_text_width(uint8_t style, const char *text)
{
    const struct helikon_font_style *face = style_at(style);
    return range_width(face, text, 0u, text_length(text),
                       fallback_active != 0);
}

uint32_t helikon_text_draw(struct helikon_screen *screen, int32_t x,
                           int32_t baseline, uint8_t style, uint16_t color,
                           const char *text, uint32_t max_width)
{
    if (screen == NULL || text == NULL)
        return 0u;
    const struct helikon_font_style *face = style_at(style);
    int fallback = fallback_active != 0;
    uint32_t length = text_length(text);
    if (length == 0u)
        return 0u;

    if (max_width == 0u) {
        (void)draw_range(screen, x, baseline, face, color, text, 0u, length,
                         fallback);
        return range_width(face, text, 0u, length, fallback);
    }

    uint32_t full = range_width(face, text, 0u, length, fallback);
    if (full <= max_width) {
        (void)draw_range(screen, x, baseline, face, color, text, 0u, length,
                         fallback);
        return full;
    }

    uint32_t width = 0u;
    uint32_t end = fit_range_ellipsis(face, text, 0u, length, max_width,
                                      fallback, &width);
    uint32_t drawn = draw_range(screen, x, baseline, face, color, text, 0u, end,
                                fallback);
    (void)drawn;
    uint32_t ellipsis = ellipsis_width(face, fallback);
    if (width + ellipsis <= max_width) {
        const struct helikon_font_glyph *glyph =
            glyph_for(face, ELLIPSIS_CODEPOINT, fallback);
        if (glyph != NULL)
            draw_glyph(screen, x + (int32_t)width, baseline, color, glyph,
                       fallback);
        return width + ellipsis;
    }
    return width;
}

uint32_t helikon_text_draw_right(struct helikon_screen *screen, int32_t right,
                                 int32_t baseline, uint8_t style,
                                 uint16_t color, const char *text,
                                 uint32_t max_width)
{
    if (screen == NULL || text == NULL)
        return 0u;
    const struct helikon_font_style *face = style_at(style);
    int fallback = fallback_active != 0;
    uint32_t length = text_length(text);
    uint32_t width;
    uint32_t end;
    if (max_width == 0u ||
        range_width(face, text, 0u, length, fallback) <= max_width) {
        width = range_width(face, text, 0u, length, fallback);
        end = length;
        (void)draw_range(screen, right - (int32_t)width, baseline, face, color,
                         text, 0u, end, fallback);
        return width;
    }

    end = fit_range_ellipsis(face, text, 0u, length, max_width, fallback,
                             &width);
    uint32_t ellipsis = ellipsis_width(face, fallback);
    int32_t x = right - (int32_t)(width + (width + ellipsis <= max_width
                                               ? ellipsis
                                               : 0u));
    (void)draw_range(screen, x, baseline, face, color, text, 0u, end, fallback);
    if (width + ellipsis <= max_width) {
        const struct helikon_font_glyph *glyph =
            glyph_for(face, ELLIPSIS_CODEPOINT, fallback);
        if (glyph != NULL)
            draw_glyph(screen, x + (int32_t)width, baseline, color, glyph,
                       fallback);
        return width + ellipsis;
    }
    return width;
}

uint32_t helikon_text_draw_centered(struct helikon_screen *screen,
                                    int32_t center, int32_t baseline,
                                    uint8_t style, uint16_t color,
                                    const char *text)
{
    if (screen == NULL || text == NULL)
        return 0u;
    uint32_t width = helikon_text_width(style, text);
    int32_t x = center - (int32_t)(width / 2u);
    (void)helikon_text_draw(screen, x, baseline, style, color, text, 0u);
    return width;
}

uint32_t helikon_text_draw_wrapped(struct helikon_screen *screen, int32_t x,
                                   int32_t baseline, uint8_t style,
                                   uint16_t color, const char *text,
                                   uint32_t max_width, uint32_t max_lines,
                                   uint32_t line_height)
{
    if (screen == NULL || text == NULL || max_width == 0u || max_lines == 0u)
        return 0u;
    const struct helikon_font_style *face = style_at(style);
    int fallback = fallback_active != 0;
    uint32_t length = text_length(text);
    uint32_t pitch = line_height != 0u ? line_height : face->line_height;
    uint32_t position = 0u;
    uint32_t line = 0u;
    while (position < length && line < max_lines) {
        while (position < length && text[position] == ' ')
            ++position;
        if (position >= length)
            break;

        uint32_t remaining = range_width(face, text, position, length, fallback);
        uint32_t end;
        int truncated = 0;
        if (remaining <= max_width) {
            end = length;
        } else if (line + 1u == max_lines) {
            uint32_t width = 0u;
            end = fit_range_ellipsis(face, text, position, length, max_width,
                                     fallback, &width);
            truncated = 1;
        } else {
            end = fit_range(face, text, position, length, max_width, fallback);
        }
        if (end == position)
            break;

        uint32_t width = draw_range(screen, x, baseline + (int32_t)(line * pitch),
                                    face, color, text, position, end, fallback);
        if (truncated && end < length) {
            uint32_t ellipsis = ellipsis_width(face, fallback);
            if (width + ellipsis <= max_width) {
                const struct helikon_font_glyph *glyph =
                    glyph_for(face, ELLIPSIS_CODEPOINT, fallback);
                if (glyph != NULL)
                    draw_glyph(screen, x + (int32_t)width,
                               baseline + (int32_t)(line * pitch), color,
                               glyph, fallback);
            }
        }
        position = end;
        while (position < length && text[position] == ' ')
            ++position;
        ++line;
        if (end < length && line + 1u > max_lines)
            break;
    }
    return line;
}
