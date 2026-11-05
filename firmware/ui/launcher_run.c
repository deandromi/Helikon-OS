/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "launcher_run.h"

#include "error.h"
#include "launcher.h"

int helikon_launcher_run(struct helikon_screen *screen,
                         struct helikon_buttons *buttons,
                         struct helikon_clock *clock)
{
    if (screen == NULL || buttons == NULL || clock == NULL)
        return -1;

    uint32_t selected = 0u;
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

            if (delta != 0) {
                uint32_t next = helikon_launcher_step(selected, delta);
                if (next != selected) {
                    selected = next;
                    helikon_launcher_draw(screen, selected);
                    idle_start = helikon_clock_now_us(clock);
                }
            }

            if ((events & (HELIKON_BUTTON_SELECT | HELIKON_BUTTON_MENU)) !=
                    0u ||
                hold)
                return (int)selected;
        }

        if (helikon_clock_expired(clock, idle_start, HELIKON_LAUNCHER_IDLE_MS))
            return (int)selected;

        helikon_clock_delay_us(clock, 20000u);
    }
}
