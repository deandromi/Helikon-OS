/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_BOOTSTATE_H
#define HELIKON_BOOTSTATE_H

#include "sha256.h"

#include <stddef.h>
#include <stdint.h>

#define HELIKON_BOOTSTATE_SIZE 512u
#define HELIKON_BOOTSTATE_SLOT_SIZE 256u
#define HELIKON_BOOTSTATE_SLOTS 2u
#define HELIKON_BOOTSTATE_VERSION 1u
#define HELIKON_BOOTSTATE_MAGIC "HBST"

enum helikon_bootstate_slot_state {
    HELIKON_BOOTSTATE_EMPTY = 0,
    HELIKON_BOOTSTATE_TRYING = 1,
    HELIKON_BOOTSTATE_GOOD = 2,
    HELIKON_BOOTSTATE_FAILED = 3
};

struct helikon_bootstate_slot {
    int valid;
    uint32_t sequence;
    uint32_t board;
    uint32_t image_version;
    uint32_t load_address;
    uint32_t image_size;
    uint8_t image_sha256[HELIKON_SHA256_SIZE];
    uint8_t state;
};

struct helikon_bootstate {
    int selected;
    struct helikon_bootstate_slot slot;
};

int helikon_bootstate_parse(const uint8_t *data, size_t length,
                            uint32_t expected_board,
                            struct helikon_bootstate *bootstate);

#endif
