/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Music library browser.
 *
 * The launch image embeds a HelikonDB catalog in its signed .rodata; the
 * browser reads it through the HDB reader and never touches storage. This is
 * the functional library test before the VFS and audio phases.
 */
#ifndef HELIKON_UI_MUSIC_H
#define HELIKON_UI_MUSIC_H

#include "buttons.h"
#include "clock.h"
#include "screen.h"

int helikon_music_run(struct helikon_screen *screen,
                      struct helikon_buttons *buttons,
                      struct helikon_clock *clock);

#endif
