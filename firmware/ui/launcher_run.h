/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Launcher input loop; separate from the drawing module so host tests can
 * render without the HAL. */
#ifndef HELIKON_UI_LAUNCHER_RUN_H
#define HELIKON_UI_LAUNCHER_RUN_H

#include "buttons.h"
#include "clock.h"
#include "screen.h"

#define HELIKON_LAUNCHER_IDLE_MS 60000u

/*
 * Draw the launcher and poll the click wheel and buttons. LEFT/RIGHT/UP/DOWN
 * and the wheel move the focus; SELECT or MENU returns. Returns the selected
 * cell index after SELECT, MENU or the idle timeout.
 */
int helikon_launcher_run(struct helikon_screen *screen,
                         struct helikon_buttons *buttons,
                         struct helikon_clock *clock);

#endif
