/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 6G board support package.
 *
 * The BSP is the only code that knows it is an iPod Classic 6G. It selects the
 * HAL implementations, supplies the accepted memory-map constants and wires
 * the read-only block device and display operations. Shared firmware modules
 * see function pointers and constants, never target #ifdefs.
 */
#ifndef HELIKON_BOARD_6G_BOARD_H
#define HELIKON_BOARD_6G_BOARD_H

#include <stdint.h>

#include "ata.h"
#include "backlight.h"
#include "block.h"
#include "buttons.h"
#include "clock.h"
#include "lcd.h"
#include "pmu.h"
#include "screen.h"

struct helikon_board {
    uint32_t id;
    const char *name;
    uint32_t image_base;
    uint32_t image_max;
    uint32_t boot_info_base;
    uint32_t boot_info_size;
    uint32_t kernel_base;
    uint32_t kernel_size;
};

struct helikon_board_runtime {
    const struct helikon_board *board;
    struct helikon_clock clock;
    struct helikon_pmu pmu;
    struct helikon_backlight backlight;
    struct helikon_lcd lcd;
    struct helikon_ata ata;
    struct helikon_buttons buttons;
    struct helikon_block_device block;
    const struct helikon_display_ops *display;
    void *display_context;
};

/* Fill the static pointers/constants. Does not touch hardware. */
void helikon_board_init(struct helikon_board_runtime *runtime);

int helikon_board_clock_init(struct helikon_board_runtime *runtime,
                             uint32_t timeout_ms);
int helikon_board_power_init(struct helikon_board_runtime *runtime,
                             uint32_t timeout_ms);
int helikon_board_display_init(struct helikon_board_runtime *runtime,
                               uint32_t timeout_ms);
int helikon_board_storage_init(struct helikon_board_runtime *runtime,
                               uint32_t timeout_ms);
int helikon_board_input_init(struct helikon_board_runtime *runtime,
                             uint32_t timeout_ms);

/*
 * Open the block layer's write gate. F1 never calls this. F2 calls it only
 * after the read-only self-test, a clean-journal check and a bootstate match.
 */
int helikon_board_enable_write(struct helikon_board_runtime *runtime);

#endif
