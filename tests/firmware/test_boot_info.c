/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "boot_info.h"
#include "crc32.h"
#include "endian.h"
#include "error.h"
#include "mem.h"

#include "test_check.h"

#define TEST_BOARD 6u
#define TEST_LOAD 0x08000000u
#define TEST_SIZE 0x1000u

static void build_page(uint8_t *page, uint32_t board, uint32_t image_version,
                       uint32_t load, uint32_t size)
{
    helikon_memset(page, 0, HELIKON_BOOT_INFO_PAGE_SIZE);
    helikon_put_le32(page + 0, HELIKON_BOOT_INFO_MAGIC);
    helikon_put_le16(page + 4, HELIKON_BOOT_INFO_VERSION);
    helikon_put_le16(page + 6, HELIKON_BOOT_INFO_SIZE);
    helikon_put_le32(page + 8, 0x1234u);
    helikon_put_le32(page + 12, board);
    helikon_put_le32(page + 16, image_version);
    helikon_put_le32(page + 20, load);
    helikon_put_le32(page + 24, size);
    for (unsigned int i = 0; i < HELIKON_SHA256_SIZE; ++i)
        page[28 + i] = (uint8_t)(0xa0u + i);
    helikon_put_le32(page + 60, 7u);
    page[64] = 2u; /* GOOD */
    page[65] = 1u; /* clean */
    helikon_put_le16(page + 66, HELIKON_BOOT_INFO_FLAG_DEVELOPMENT |
                                   HELIKON_BOOT_INFO_FLAG_MANIFEST_VERIFIED);
    helikon_put_le32(page + 68, 123u);
    helikon_put_le32(page + 72, helikon_crc32(page, 72));

    uint8_t *manifest = page + HELIKON_BOOT_INFO_MANIFEST_OFFSET;
    helikon_memcpy(manifest, HELIKON_MANIFEST_MAGIC, 8u);
    helikon_put_le32(manifest + 8, 1u);
    helikon_put_le32(manifest + 12, board);
    helikon_put_le32(manifest + 16, image_version);
    helikon_put_le32(manifest + 20, load);
    helikon_put_le32(manifest + 24, size);
    helikon_memcpy(manifest + 32, page + 28, HELIKON_SHA256_SIZE);
}

static void update_crc(uint8_t *page)
{
    helikon_put_le32(page + 72, helikon_crc32(page, 72));
}

int main(void)
{
    uint8_t page[HELIKON_BOOT_INFO_PAGE_SIZE];
    struct helikon_boot_info_expect expect = {
        .page_base = (uintptr_t)page,
        .page_size = HELIKON_BOOT_INFO_PAGE_SIZE,
        .board_id = TEST_BOARD,
        .image_load = TEST_LOAD,
        .image_size = TEST_SIZE
    };
    struct helikon_boot_info info;

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    CHECK_EQ(HELIKON_OK, helikon_boot_info_parse((uintptr_t)page, &expect,
                                                 &info));
    CHECK_EQ(0x1234, info.loader_version);
    CHECK_EQ(TEST_BOARD, info.board);
    CHECK_EQ(3, info.image_version);
    CHECK_EQ(TEST_LOAD, info.load_address);
    CHECK_EQ(TEST_SIZE, info.image_size);
    CHECK_EQ(7, info.bootstate_sequence);
    CHECK_EQ(2, info.bootstate_state);
    CHECK_EQ(1, info.journal_state);
    CHECK_EQ(123, info.loader_stage_ms);
    CHECK(helikon_memcmp(info.manifest, "HELIKON1", 8) == 0);

    CHECK_EQ(HELIKON_ERR_NOT_FOUND,
             helikon_boot_info_parse(0, &expect, &info));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_boot_info_parse((uintptr_t)page, NULL, &info));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_boot_info_parse((uintptr_t)page, &expect, NULL));
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)(page + 2), &expect, &info));
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)(page + 4096), &expect,
                                     &info));
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)(page + 2048), &expect,
                                     &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    page[0] ^= 1u;
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    helikon_put_le16(page + 4, 2u);
    update_crc(page);
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    helikon_put_le16(page + 6, 256u);
    update_crc(page);
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    page[10] ^= 1u;
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, 7u, 3u, TEST_LOAD, TEST_SIZE);
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    helikon_put_le32(page + 24, TEST_SIZE + 1u);
    update_crc(page);
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD + 4u, TEST_SIZE);
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    helikon_put_le32(page + 24, (uint32_t)(expect.page_base - TEST_LOAD + 1u));
    update_crc(page);
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    page[76] = 1u;
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    page[HELIKON_BOOT_INFO_MANIFEST_OFFSET] ^= 1u;
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    helikon_put_le32(page + HELIKON_BOOT_INFO_MANIFEST_OFFSET + 12, 7u);
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    build_page(page, TEST_BOARD, 3u, TEST_LOAD, TEST_SIZE);
    page[HELIKON_BOOT_INFO_MANIFEST_OFFSET + 32] ^= 1u;
    CHECK_EQ(HELIKON_ERR_BOOT_INFO_INVALID,
             helikon_boot_info_parse((uintptr_t)page, &expect, &info));

    return helikon_test_report("boot-info");
}
