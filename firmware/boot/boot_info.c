/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "boot_info.h"

#include "crc32.h"
#include "endian.h"
#include "error.h"
#include "mem.h"
#include "static_assert.h"

HELIKON_STATIC_ASSERT(sizeof(struct helikon_boot_info_page) ==
                          HELIKON_BOOT_INFO_PAGE_SIZE,
                      boot_info_page_size);
HELIKON_STATIC_ASSERT(offsetof(struct helikon_boot_info_page, image_sha256) ==
                          28u, boot_info_sha_offset);
HELIKON_STATIC_ASSERT(offsetof(struct helikon_boot_info_page, crc32) == 72u,
                      boot_info_crc_offset);
HELIKON_STATIC_ASSERT(offsetof(struct helikon_boot_info_page, manifest) ==
                          HELIKON_BOOT_INFO_MANIFEST_OFFSET,
                      boot_info_manifest_offset);
HELIKON_STATIC_ASSERT(offsetof(struct helikon_boot_info_page, reserved1) ==
                          HELIKON_BOOT_INFO_RESERVED_OFFSET,
                      boot_info_reserved_offset);

static int all_zero(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (data[i] != 0u)
            return 0;
    }
    return 1;
}

int helikon_boot_info_parse(uintptr_t address,
                            const struct helikon_boot_info_expect *expect,
                            struct helikon_boot_info *info)
{
    if (expect == NULL || info == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (expect->page_size < HELIKON_BOOT_INFO_SIZE)
        return HELIKON_ERR_ARGUMENT;

    /* An older loader or a host test may pass no page at all. */
    if (address == 0u)
        return HELIKON_ERR_NOT_FOUND;
    if ((address & 3u) != 0u)
        return HELIKON_ERR_BOOT_INFO_INVALID;

    uintptr_t page_base = expect->page_base;
    if (address < page_base)
        return HELIKON_ERR_BOOT_INFO_INVALID;
    uintptr_t offset = address - page_base;
    if (offset > (uintptr_t)expect->page_size ||
        HELIKON_BOOT_INFO_PAGE_SIZE >
            (size_t)((uintptr_t)expect->page_size - offset))
        return HELIKON_ERR_BOOT_INFO_INVALID;

    const uint8_t *page = (const uint8_t *)address;

    if (helikon_le32(page + 0) != HELIKON_BOOT_INFO_MAGIC ||
        helikon_le16(page + 4) != HELIKON_BOOT_INFO_VERSION ||
        helikon_le16(page + 6) != HELIKON_BOOT_INFO_SIZE)
        return HELIKON_ERR_BOOT_INFO_INVALID;

    uint32_t expected_crc = helikon_le32(page + 72);
    if (helikon_crc32(page, 72) != expected_crc)
        return HELIKON_ERR_BOOT_INFO_INVALID;

    uint32_t board = helikon_le32(page + 12);
    if (board != expect->board_id)
        return HELIKON_ERR_BOOT_INFO_INVALID;

    uint32_t image_load = helikon_le32(page + 20);
    uint32_t image_size = helikon_le32(page + 24);
    if (image_size == 0u || image_size != expect->image_size ||
        image_load != expect->image_load)
        return HELIKON_ERR_BOOT_INFO_INVALID;

    uint64_t image_end = (uint64_t)image_load + (uint64_t)image_size;
    if (image_end > (uint64_t)page_base)
        return HELIKON_ERR_BOOT_INFO_INVALID;

    /* The first 512 bytes are defined for version 1; reserved is zero. */
    if (!all_zero(page + 76, 180u) || !all_zero(page + 512, 3584u))
        return HELIKON_ERR_BOOT_INFO_INVALID;

    const uint8_t *manifest = page + HELIKON_BOOT_INFO_MANIFEST_OFFSET;
    if (helikon_memcmp(manifest, HELIKON_MANIFEST_MAGIC, 8u) != 0)
        return HELIKON_ERR_BOOT_INFO_INVALID;
    if (helikon_le32(manifest + 12) != board ||
        helikon_le32(manifest + 16) != helikon_le32(page + 16) ||
        helikon_le32(manifest + 20) != image_load ||
        helikon_le32(manifest + 24) != image_size ||
        helikon_memcmp(manifest + 32, page + 28,
                       HELIKON_SHA256_SIZE) != 0)
        return HELIKON_ERR_BOOT_INFO_INVALID;

    helikon_memset(info, 0, sizeof(*info));
    info->loader_version = helikon_le32(page + 8);
    info->board = board;
    info->image_version = helikon_le32(page + 16);
    info->load_address = image_load;
    info->image_size = image_size;
    helikon_memcpy(info->image_sha256, page + 28, HELIKON_SHA256_SIZE);
    info->bootstate_sequence = helikon_le32(page + 60);
    info->bootstate_state = page[64];
    info->journal_state = page[65];
    info->flags = helikon_le16(page + 66);
    info->loader_stage_ms = helikon_le32(page + 68);
    helikon_memcpy(info->manifest, manifest, HELIKON_MANIFEST_SIZE);
    return HELIKON_OK;
}
