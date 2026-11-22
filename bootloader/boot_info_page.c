/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "boot_info_page.h"

#include <string.h>

#include "helikon_error.h"

/* Page field offsets from ADR-0004; the firmware parser is the authority. */
#define HANDOFF_CRC_OFFSET 72u
#define HANDOFF_CRC_BYTES 72u
#define HANDOFF_MANIFEST_MAGIC_BYTES 8u
#define HANDOFF_MANIFEST_BOARD 12u
#define HANDOFF_MANIFEST_IMAGE_VERSION 16u
#define HANDOFF_MANIFEST_LOAD 20u
#define HANDOFF_MANIFEST_SIZE_FIELD 24u
#define HANDOFF_MANIFEST_SHA 32u

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    data[2] = (uint8_t)((value >> 16) & 0xffu);
    data[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
}

/*
 * Bit-identical to firmware/lib/crc32.c: CRC-32/ISO-HDLC, reflected, initial
 * 0xffffffff, final inversion. Duplicated rather than copied so the loader does
 * not link the firmware's libc shims; tests/bootloader/test_boot_info_page.c
 * builds a page and parses it with the firmware parser, so any divergence in
 * this function fails the suite immediately.
 */
static uint32_t handoff_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffu;
    for (size_t index = 0u; index < length; ++index) {
        crc ^= data[index];
        for (unsigned int bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int helikon_boot_info_page_build(void *page, size_t page_size,
                                 const struct helikon_loader_handoff *handoff)
{
    if (page == NULL || handoff == NULL || handoff->image_sha256 == NULL ||
        handoff->manifest == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (page_size < HELIKON_BOOT_INFO_PAGE_SIZE)
        return HELIKON_ERR_ARGUMENT;
    if (((uintptr_t)page & 3u) != 0u)
        return HELIKON_ERR_ARGUMENT;

    /* The page must not describe an image the signed manifest does not. A
     * disagreement here is a boot bug, and a wrong page is worse than none. */
    if (memcmp(handoff->manifest, HELIKON_MANIFEST_MAGIC,
               HANDOFF_MANIFEST_MAGIC_BYTES) != 0)
        return HELIKON_ERR_FORMAT;
    if (read_le32(handoff->manifest + HANDOFF_MANIFEST_BOARD) !=
            handoff->board ||
        read_le32(handoff->manifest + HANDOFF_MANIFEST_IMAGE_VERSION) !=
            handoff->image_version ||
        read_le32(handoff->manifest + HANDOFF_MANIFEST_LOAD) !=
            handoff->load_address ||
        read_le32(handoff->manifest + HANDOFF_MANIFEST_SIZE_FIELD) !=
            handoff->image_size ||
        memcmp(handoff->manifest + HANDOFF_MANIFEST_SHA,
               handoff->image_sha256, HELIKON_SHA256_SIZE) != 0)
        return HELIKON_ERR_FORMAT;

    uint8_t *bytes = page;
    memset(bytes, 0, HELIKON_BOOT_INFO_PAGE_SIZE);

    write_le32(bytes + 0u, HELIKON_BOOT_INFO_MAGIC);
    write_le16(bytes + 4u, HELIKON_BOOT_INFO_VERSION);
    write_le16(bytes + 6u, HELIKON_BOOT_INFO_SIZE);
    write_le32(bytes + 8u, handoff->loader_version);
    write_le32(bytes + 12u, handoff->board);
    write_le32(bytes + 16u, handoff->image_version);
    write_le32(bytes + 20u, handoff->load_address);
    write_le32(bytes + 24u, handoff->image_size);
    memcpy(bytes + 28u, handoff->image_sha256, HELIKON_SHA256_SIZE);
    write_le32(bytes + 60u, handoff->bootstate_sequence);
    bytes[64u] = handoff->bootstate_state;
    bytes[65u] = handoff->journal_state;
    write_le16(bytes + 66u, handoff->flags);
    write_le32(bytes + 68u, handoff->loader_stage_ms);
    write_le32(bytes + HANDOFF_CRC_OFFSET,
               handoff_crc32(bytes, HANDOFF_CRC_BYTES));

    memcpy(bytes + HELIKON_BOOT_INFO_MANIFEST_OFFSET, handoff->manifest,
           HELIKON_MANIFEST_SIZE);
    return HELIKON_OK;
}
