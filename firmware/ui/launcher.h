/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Launcher home screen.
 *
 * The static tiles (folio, wordmark, divider, icons and labels) are generated
 * from tools/render-ui-mockup.py by tools/make-launcher-assets.py. They use
 * the tracked IBM Plex Sans fallback, not the proprietary ITC Benguiat face.
 * The firmware draws only the six cells' plinths and the focus marks, so the
 * selected cell can change without another asset.
 */
#ifndef HELIKON_UI_LAUNCHER_H
#define HELIKON_UI_LAUNCHER_H

#include <stdint.h>

#include "screen.h"

#define HELIKON_LAUNCHER_CELLS 6u
#define HELIKON_LAUNCHER_PAPER 0xef7cu
#define HELIKON_LAUNCHER_INK 0x2103u

void helikon_launcher_draw(struct helikon_screen *screen, uint32_t selected);

/*
 * Move the focus one step. dx/dy are -1, 0 or 1; the grid is three columns by
 * two rows and wraps in both axes. Returns the new cell index.
 */
uint32_t helikon_launcher_move(uint32_t selected, int dx, int dy);

/*
 * Step the focus through the six cells in row-major order (the order printed
 * on the launcher), wrapping at either end. The 6G has no up/down buttons, so
 * the wheel and left/right use this cursor.
 */
uint32_t helikon_launcher_step(uint32_t selected, int delta);

#endif
