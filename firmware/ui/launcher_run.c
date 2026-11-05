/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher_run.h"

#include "error.h"
#include "launcher.h"

static const char *const app_names[HELIKON_LAUNCHER_CELLS] = {
    "MUSIC", "PODCASTS", "AUDIOBOOKS", "PLAYLISTS", "GAMES", "SETTINGS",
};

/*
 * Placeholder application surface. It exists to prove the launcher's
 * selection path and to give SELECT a visible result and a way back. The real
 * Music, Podcasts and other screens replace it one by one.
 */
static void draw_app_screen(struct helikon_screen *screen, uint32_t selected)
{
    if (selected >= HELIKON_LAUNCHER_CELLS)
        selected = 0u;

    helikon_screen_fill_rect(screen, 0u, 0u, screen->width, screen->height,
                             HELIKON_LAUNCHER_PAPER);
    helikon_screen_set_colors(screen, HELIKON_LAUNCHER_INK,
                              HELIKON_LAUNCHER_PAPER);
    helikon_screen_text(screen, 20u, 48u, 4u, app_names[selected]);
    helikon_screen_text(screen, 20u, 108u, 2u, "APP SCREEN COMING NEXT");
    helikon_screen_text(screen, 20u, 136u, 2u,
                        "THIS BUILD PROVES THE LAUNCHER");
    helikon_screen_text(screen, 20u, 152u, 2u, "SELECTION AND FOCUS PATH");
    helikon_screen_text(screen, 20u, 204u, 2u, "MENU OR SELECT: BACK");
    (void)helikon_screen_present_all(screen);
}

int helikon_launcher_run(struct helikon_screen *screen,
                         struct helikon_buttons *buttons,
                         struct helikon_clock *clock)
{
    if (screen == NULL || buttons == NULL || clock == NULL)
        return -1;

    uint32_t selected = 0u;
    int app_open = 0;
    helikon_launcher_draw(screen, selected);
    uint64_t idle_start = helikon_clock_now_us(clock);

    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        int delta = 0;

        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            if ((events & HELIKON_BUTTON_LEFT) != 0u)
                delta = -1;
            else if ((events & HELIKON_BUTTON_RIGHT) != 0u)
                delta = 1;
            else if (wheel > 0)
                delta = 1;
            else if (wheel < 0)
                delta = -1;

            if (app_open) {
                if ((events & (HELIKON_BUTTON_MENU |
                               HELIKON_BUTTON_SELECT |
                               HELIKON_BUTTON_PLAY)) != 0u) {
                    app_open = 0;
                    helikon_launcher_draw(screen, selected);
                    idle_start = helikon_clock_now_us(clock);
                }
            } else {
                if ((events & (HELIKON_BUTTON_SELECT |
                               HELIKON_BUTTON_PLAY)) != 0u) {
                    app_open = 1;
                    draw_app_screen(screen, selected);
                    idle_start = helikon_clock_now_us(clock);
                } else if ((events & HELIKON_BUTTON_MENU) != 0u) {
                    return (int)selected;
                } else if (delta != 0) {
                    uint32_t next = helikon_launcher_step(selected, delta);
                    if (next != selected) {
                        selected = next;
                        helikon_launcher_draw(screen, selected);
                        idle_start = helikon_clock_now_us(clock);
                    }
                }
            }

            if (hold)
                return (int)selected;
        }

        if (helikon_clock_expired(clock, idle_start, HELIKON_LAUNCHER_IDLE_MS))
            return (int)selected;

        helikon_clock_delay_us(clock, 20000u);
    }
}
