/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_SPLASH_COLOR_H
#define HELIKON_SPLASH_COLOR_H

#include <stdint.h>

#define HELIKON_SPLASH_WIDTH 320u
#define HELIKON_SPLASH_HEIGHT 240u
#define HELIKON_SPLASH_HFS_X 186u
#define HELIKON_SPLASH_HFS_Y 204u
#define HELIKON_SPLASH_HFS_WIDTH 72u
#define HELIKON_SPLASH_HFS_HEIGHT 9u

enum helikon_splash_hfs_status {
    HELIKON_SPLASH_HFS_UNKNOWN = 0,
    HELIKON_SPLASH_HFS_OK = 1,
    HELIKON_SPLASH_HFS_FAIL = 2
};

/* A 16-colour 320x240 splash. The HFS+ label is composited at runtime. */
void helikon_splash_show(void);
void helikon_splash_set_hfs(int ok);

#endif
