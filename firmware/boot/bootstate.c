/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bootstate.h"

#include "error.h"
#include "mem.h"

#define SLOT_STATE_OFFSET 60u
#define SLOT_RESERVED_OFFSET 61u
#define SLOT_CHECKSUM_OFFSET 64u
#define SLOT_RESERVED_TAIL 96u

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static int valid_range(uint32_t load_address, uint32_t image_size)
{
    return image_size != 0u && image_size <= HELIKON_BOOTSTATE_IMAGE_MAX &&
           load_address >= HELIKON_BOOTSTATE_IMAGE_LOAD &&
           (load_address & 3u) == 0u &&
           (uint64_t)load_address + image_size <=
               (uint64_t)HELIKON_BOOTSTATE_IMAGE_LOAD +
                   HELIKON_BOOTSTATE_IMAGE_MAX;
}

static int parse_slot(const uint8_t *data, uint32_t expected_board,
                      struct helikon_bootstate_slot *slot)
{
    if (helikon_memcmp(data, HELIKON_BOOTSTATE_MAGIC, 4u) != 0 ||
        le32(data + 4) != HELIKON_BOOTSTATE_VERSION ||
        le32(data + 12) != expected_board)
        return 0;

    uint8_t state = data[SLOT_STATE_OFFSET];
    if (state > HELIKON_BOOTSTATE_FAILED ||
        data[SLOT_RESERVED_OFFSET] != 0u ||
        data[SLOT_RESERVED_OFFSET + 1u] != 0u ||
        data[SLOT_RESERVED_OFFSET + 2u] != 0u)
        return 0;

    for (size_t i = SLOT_RESERVED_TAIL; i < HELIKON_BOOTSTATE_SLOT_SIZE; ++i) {
        if (data[i] != 0u)
            return 0;
    }

    uint32_t image_version = le32(data + 16);
    uint32_t load_address = le32(data + 20);
    uint32_t image_size = le32(data + 24);
    if (state != HELIKON_BOOTSTATE_EMPTY &&
        (image_version == 0u || !valid_range(load_address, image_size)))
        return 0;

    uint8_t digest[HELIKON_SHA256_SIZE];
    helikon_sha256(data, SLOT_CHECKSUM_OFFSET, digest);
    if (helikon_memcmp(digest, data + SLOT_CHECKSUM_OFFSET,
                       sizeof(digest)) != 0)
        return 0;

    helikon_memset(slot, 0, sizeof(*slot));
    slot->valid = 1;
    slot->sequence = le32(data + 8);
    slot->board = le32(data + 12);
    slot->image_version = image_version;
    slot->load_address = load_address;
    slot->image_size = image_size;
    helikon_memcpy(slot->image_sha256, data + 28u,
                   sizeof(slot->image_sha256));
    slot->state = state;
    return 1;
}

int helikon_bootstate_parse(const uint8_t *data, size_t length,
                            uint32_t expected_board,
                            struct helikon_bootstate *bootstate)
{
    if (data == NULL || bootstate == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (length != HELIKON_BOOTSTATE_SIZE)
        return HELIKON_ERR_BOOTSTATE;

    helikon_memset(bootstate, 0, sizeof(*bootstate));
    bootstate->selected = -1;

    int have_good = 0;
    for (unsigned int i = 0; i < HELIKON_BOOTSTATE_SLOTS; ++i) {
        const uint8_t *p = data + i * HELIKON_BOOTSTATE_SLOT_SIZE;
        struct helikon_bootstate_slot *slot = &bootstate->slots[i];
        if (!parse_slot(p, expected_board, slot))
            continue;
        if (slot->state == HELIKON_BOOTSTATE_GOOD &&
            (!have_good ||
             slot->sequence > bootstate->slots[bootstate->selected].sequence)) {
            bootstate->selected = (int)i;
            have_good = 1;
        }
    }
    return HELIKON_OK;
}

static int slot_matches(const struct helikon_bootstate_slot *slot,
                        uint32_t image_version, uint32_t load_address,
                        uint32_t image_size,
                        const uint8_t image_sha256[HELIKON_SHA256_SIZE])
{
    return slot->valid && slot->board != 0u &&
           slot->image_version == image_version &&
           slot->load_address == load_address &&
           slot->image_size == image_size &&
           helikon_memcmp(slot->image_sha256, image_sha256,
                          HELIKON_SHA256_SIZE) == 0;
}

int helikon_bootstate_promote(uint8_t *data, size_t length,
                              uint32_t expected_board,
                              uint32_t image_version,
                              uint32_t load_address,
                              uint32_t image_size,
                              const uint8_t image_sha256[HELIKON_SHA256_SIZE],
                              uint32_t *promoted_sequence, int *changed)
{
    if (data == NULL || image_sha256 == NULL || promoted_sequence == NULL ||
        changed == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (length != HELIKON_BOOTSTATE_SIZE)
        return HELIKON_ERR_BOOTSTATE;

    struct helikon_bootstate bootstate;
    int rc = helikon_bootstate_parse(data, length, expected_board,
                                     &bootstate);
    if (rc != HELIKON_OK)
        return rc;

    int match = -1;
    for (unsigned int i = 0; i < HELIKON_BOOTSTATE_SLOTS; ++i) {
        if (!slot_matches(&bootstate.slots[i], image_version, load_address,
                          image_size, image_sha256))
            continue;
        if (match < 0 ||
            bootstate.slots[i].sequence >
                bootstate.slots[match].sequence)
            match = (int)i;
    }
    if (match < 0)
        return HELIKON_ERR_BOOTSTATE;

    const struct helikon_bootstate_slot *slot = &bootstate.slots[match];
    if (slot->state == HELIKON_BOOTSTATE_GOOD) {
        *promoted_sequence = slot->sequence;
        *changed = 0;
        return HELIKON_OK;
    }
    if (slot->state != HELIKON_BOOTSTATE_TRYING)
        return HELIKON_ERR_BOOTSTATE;

    uint32_t max_sequence = 0u;
    int have_sequence = 0;
    for (unsigned int i = 0; i < HELIKON_BOOTSTATE_SLOTS; ++i) {
        if (!bootstate.slots[i].valid)
            continue;
        if (!have_sequence ||
            bootstate.slots[i].sequence > max_sequence) {
            max_sequence = bootstate.slots[i].sequence;
            have_sequence = 1;
        }
    }
    if (!have_sequence || max_sequence == UINT32_MAX)
        return HELIKON_ERR_BOOTSTATE;

    uint8_t *raw = data + (size_t)match * HELIKON_BOOTSTATE_SLOT_SIZE;
    put32(raw + 8u, max_sequence + 1u);
    raw[SLOT_STATE_OFFSET] = HELIKON_BOOTSTATE_GOOD;
    helikon_sha256(raw, SLOT_CHECKSUM_OFFSET, raw + SLOT_CHECKSUM_OFFSET);

    *promoted_sequence = max_sequence + 1u;
    *changed = 1;
    return HELIKON_OK;
}
