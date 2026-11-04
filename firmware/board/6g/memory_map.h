/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Accepted 6G memory map (D-27 / ADR-0003).
 *
 * These are BSP constants, not measurements. The linker script and the
 * compile-time assertions below must agree with this header. A different board
 * supplies a different memory_map.h; shared core code never asks which board
 * it is on.
 */
#ifndef HELIKON_BOARD_6G_MEMORY_MAP_H
#define HELIKON_BOARD_6G_MEMORY_MAP_H

#define HELIKON_IMAGE_BASE 0x08000000u
#define HELIKON_IMAGE_MAX 0x007ff000u /* 8 MiB - 4 KiB */
#define HELIKON_BOOT_INFO_BASE 0x087ff000u
#define HELIKON_BOOT_INFO_SIZE 0x00001000u
#define HELIKON_KERNEL_BASE 0x08800000u
#define HELIKON_KERNEL_SIZE 0x00800000u
#define HELIKON_UI_BASE 0x09000000u
#define HELIKON_UI_SIZE 0x00800000u
#define HELIKON_AUDIO_BASE 0x09800000u
#define HELIKON_AUDIO_SIZE 0x01800000u /* 24 MiB */
#define HELIKON_DB_BASE 0x0b000000u
#define HELIKON_DB_SIZE 0x00800000u /* 8 MiB */
#define HELIKON_RESERVE_BASE 0x0b800000u
#define HELIKON_RESERVE_SIZE 0x00800000u

#define HELIKON_IMAGE_END (HELIKON_IMAGE_BASE + HELIKON_IMAGE_MAX)
#define HELIKON_KERNEL_END (HELIKON_KERNEL_BASE + HELIKON_KERNEL_SIZE)
#define HELIKON_UI_END (HELIKON_UI_BASE + HELIKON_UI_SIZE)
#define HELIKON_AUDIO_END (HELIKON_AUDIO_BASE + HELIKON_AUDIO_SIZE)
#define HELIKON_DB_END (HELIKON_DB_BASE + HELIKON_DB_SIZE)
#define HELIKON_RESERVE_END (HELIKON_RESERVE_BASE + HELIKON_RESERVE_SIZE)

#define HELIKON_DRAM_SIZE 0x04000000u /* 64 MiB */

#endif
