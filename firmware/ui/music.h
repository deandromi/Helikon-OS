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
#include "hdb.h"
#include "screen.h"

int helikon_music_run(struct helikon_screen *screen,
                      struct helikon_buttons *buttons,
                      struct helikon_clock *clock);

/*
 * The same browser over a catalog somebody else opened: the boot path mounts
 * the volume, opens `/.helikon/music.hdb` and hands the reader a `struct hdb *`,
 * so the hub never learns whether its bytes came from a signed image or from
 * disk. `opened` must stay alive and open for the whole call; the hub does not
 * close it, and the caller owns closing it after the call returns.
 */
int helikon_music_run_db(struct helikon_screen *screen,
                         struct helikon_buttons *buttons,
                         struct helikon_clock *clock, struct hdb *opened);

#endif
