/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher.h"

#include <stddef.h>

#include "launcher_assets.h"

#define CELL_X(index) (24u + ((index) % 3u) * 104u)
#define CELL_Y(index) (64u + ((index) / 3u) * 86u)

static void blit(struct helikon_screen *screen,
                 const struct helikon_launcher_bitmap *bitmap)
{
    if (screen == NULL || screen->pixels == NULL || bitmap == NULL ||
        bitmap->pixels == NULL)
        return;

    for (uint32_t row = 0; row < bitmap->height; ++row) {
        int32_t y = (int32_t)bitmap->y + (int32_t)row;
        if (y < 0 || y >= (int32_t)screen->height)
            continue;
        for (uint32_t column = 0; column < bitmap->width; ++column) {
            int32_t x = (int32_t)bitmap->x + (int32_t)column;
            if (x < 0 || x >= (int32_t)screen->width)
                continue;
            screen->pixels[(uint32_t)y * screen->width + (uint32_t)x] =
                bitmap->pixels[row * bitmap->width + column];
        }
    }
}

static void draw_diamond(struct helikon_screen *screen, uint32_t cx,
                         uint32_t cy, int radius, uint16_t color)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        int distance = dy < 0 ? -dy : dy;
        int half = radius - distance;
        uint32_t x = (uint32_t)((int32_t)cx - half);
        uint32_t y = (uint32_t)((int32_t)cy + dy);
        helikon_screen_fill_rect(screen, x, y, (uint32_t)(half * 2 + 1), 1u,
                                 color);
    }
}

void helikon_launcher_draw(struct helikon_screen *screen, uint32_t selected)
{
    if (screen == NULL || screen->pixels == NULL)
        return;

    helikon_screen_fill_rect(screen, 0u, 0u, screen->width, screen->height,
                             HELIKON_LAUNCHER_PAPER);
    blit(screen, &helikon_launcher_background);

    for (uint32_t index = 0; index < HELIKON_LAUNCHER_CELLS; ++index) {
        uint32_t x = CELL_X(index);
        uint32_t y = CELL_Y(index);
        blit(screen, &helikon_launcher_icons[index]);
        helikon_screen_fill_rect(screen, x + 20u, y + 56u, 24u, 1u,
                                 helikon_launcher_plinths[index]);
        blit(screen, &helikon_launcher_labels[index]);
    }

    if (selected < HELIKON_LAUNCHER_CELLS) {
        const struct helikon_launcher_bitmap *label =
            &helikon_launcher_labels[selected];
        uint32_t cell_x = CELL_X(selected);
        uint32_t cell_y = CELL_Y(selected);
        uint32_t center = (uint32_t)label->x + (uint32_t)label->width / 2u;
        uint32_t width = (uint32_t)label->width + 10u;
        uint32_t left = center - width / 2u;

        draw_diamond(screen, cell_x + 32u, cell_y + 6u, 2,
                     HELIKON_LAUNCHER_INK);
        helikon_screen_fill_rect(screen, left, cell_y + 84u, width, 2u,
                                 HELIKON_LAUNCHER_INK);
    }

    (void)helikon_screen_present_all(screen);
}

uint32_t helikon_launcher_move(uint32_t selected, int dx, int dy)
{
    if (selected >= HELIKON_LAUNCHER_CELLS)
        selected = 0u;

    int column = (int)(selected % 3u) + dx;
    int row = (int)(selected / 3u) + dy;
    column = (column % 3 + 3) % 3;
    row = (row % 2 + 2) % 2;
    return (uint32_t)(row * 3 + column);
}

uint32_t helikon_launcher_step(uint32_t selected, int delta)
{
    if (selected >= HELIKON_LAUNCHER_CELLS)
        selected = 0u;
    int next = (int)selected + delta;
    next = (next % (int)HELIKON_LAUNCHER_CELLS +
            (int)HELIKON_LAUNCHER_CELLS) % (int)HELIKON_LAUNCHER_CELLS;
    return (uint32_t)next;
}
