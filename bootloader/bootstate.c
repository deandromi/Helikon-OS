/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bootstate.h"

#include "loader_error.h"
#include "manifest.h"

#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int valid_range(uint32_t load_address, uint32_t image_size)
{
    return image_size != 0 && image_size <= HELIKON_DRAM_SIZE &&
           load_address >= HELIKON_DRAM_BASE &&
           (load_address & 3u) == 0 &&
           (uint64_t)load_address + image_size <=
               (uint64_t)HELIKON_DRAM_BASE + HELIKON_DRAM_SIZE;
}

static int parse_slot(const uint8_t *data, uint32_t expected_board,
                      struct helikon_bootstate_slot *slot)
{
    if (memcmp(data, HELIKON_BOOTSTATE_MAGIC, 4u) != 0 ||
        le32(data + 4) != HELIKON_BOOTSTATE_VERSION)
        return 0;
    if (le32(data + 12) != expected_board)
        return 0;

    uint8_t state = data[60];
    if (state > HELIKON_BOOTSTATE_FAILED)
        return 0;
    if (data[61] != 0 || data[62] != 0 || data[63] != 0)
        return 0;
    for (size_t i = 96u; i < HELIKON_BOOTSTATE_SLOT_SIZE; ++i) {
        if (data[i] != 0)
            return 0;
    }

    uint32_t image_version = le32(data + 16);
    uint32_t load_address = le32(data + 20);
    uint32_t image_size = le32(data + 24);
    if (state != HELIKON_BOOTSTATE_EMPTY &&
        (image_version == 0 || !valid_range(load_address, image_size)))
        return 0;

    uint8_t digest[HELIKON_SHA256_SIZE];
    helikon_sha256(data, 64u, digest);
    if (memcmp(digest, data + 64u, sizeof(digest)) != 0)
        return 0;

    memset(slot, 0, sizeof(*slot));
    slot->valid = 1;
    slot->sequence = le32(data + 8);
    slot->board = le32(data + 12);
    slot->image_version = image_version;
    slot->load_address = load_address;
    slot->image_size = image_size;
    memcpy(slot->image_sha256, data + 28u, HELIKON_SHA256_SIZE);
    slot->state = state;
    return 1;
}

int helikon_bootstate_parse(const uint8_t *data, size_t length,
                            uint32_t expected_board,
                            struct helikon_bootstate *bootstate)
{
    if (data == NULL || bootstate == NULL)
        return HELIKON_LOADER_ERR_ARGUMENT;
    if (length != HELIKON_BOOTSTATE_SIZE)
        return HELIKON_LOADER_ERR_BOOTSTATE;

    struct helikon_bootstate_slot selected;
    int selected_index = -1;
    int have_selected = 0;
    for (unsigned int i = 0; i < HELIKON_BOOTSTATE_SLOTS; ++i) {
        struct helikon_bootstate_slot slot;
        const uint8_t *p = data + i * HELIKON_BOOTSTATE_SLOT_SIZE;
        if (!parse_slot(p, expected_board, &slot))
            continue;
        if (slot.state != HELIKON_BOOTSTATE_GOOD)
            continue;
        if (!have_selected || slot.sequence > selected.sequence) {
            selected = slot;
            selected_index = (int)i;
            have_selected = 1;
        }
    }
    if (!have_selected)
        return HELIKON_LOADER_ERR_NO_IMAGE;

    memset(bootstate, 0, sizeof(*bootstate));
    bootstate->selected = selected_index;
    bootstate->slot = selected;
    return HELIKON_LOADER_OK;
}
