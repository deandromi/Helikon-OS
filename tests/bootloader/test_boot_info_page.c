/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host check of the boot-info page writer against the firmware parser.
 *
 * The loader fills a 4 KiB page and the firmware parses it; both halves live in
 * this repository but in different build trees, so this test is the only place
 * they meet. It builds a page from known facts and a synthetic manifest, then
 * runs the real `helikon_boot_info_parse()` over it and compares every field.
 * It also proves the writer refuses a page whose manifest disagrees with the
 * facts, and that the parser rejects a corrupted page.
 *
 * Host test only: no DRAM page at 0x087ff000, no device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_info.h"
#include "boot_info_page.h"
#include "helikon_error.h"

#define PAGE_ADDR ((uintptr_t)page)
#define IMAGE_LOAD 0x08000000u
#define IMAGE_SIZE 0x1234u

static uint8_t page[HELIKON_BOOT_INFO_PAGE_SIZE] __attribute__((aligned(4)));
static uint8_t manifest[HELIKON_MANIFEST_SIZE];
static uint8_t image_sha[HELIKON_SHA256_SIZE];

static int check(int condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", what);
        return 1;
    }
    printf("ok   %s\n", what);
    return 0;
}

static void put_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    data[2] = (uint8_t)((value >> 16) & 0xffu);
    data[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void build_manifest(uint32_t board, uint32_t image_version,
                           uint32_t load, uint32_t size)
{
    memset(manifest, 0, sizeof(manifest));
    memcpy(manifest, "HELIKON1", 8u);
    put_le32(manifest + 8u, 1u); /* manifest version */
    put_le32(manifest + 12u, board);
    put_le32(manifest + 16u, image_version);
    put_le32(manifest + 20u, load);
    put_le32(manifest + 24u, size);
    memcpy(manifest + 32u, image_sha, HELIKON_SHA256_SIZE);
}

static struct helikon_boot_info_expect expected(void)
{
    struct helikon_boot_info_expect expect;
    memset(&expect, 0, sizeof(expect));
    expect.page_base = PAGE_ADDR;
    expect.page_size = HELIKON_BOOT_INFO_PAGE_SIZE;
    /* The page the loader really writes, above the synthetic image. Using
     * PAGE_ADDR here made the check depend on where the host linker put the
     * array: macOS places it high, an ELF -no-pie build places it at 0x40xxxx,
     * below the 0x08000000 load address, and every field check then failed. */
    expect.image_ceiling = 0x087ff000u;
    expect.board_id = 6u;
    expect.image_load = IMAGE_LOAD;
    expect.image_size = IMAGE_SIZE;
    return expect;
}

static struct helikon_loader_handoff facts(uint32_t board,
                                           uint32_t image_version,
                                           uint32_t load, uint32_t size)
{
    struct helikon_loader_handoff handoff;
    memset(&handoff, 0, sizeof(handoff));
    handoff.loader_version = 42u;
    handoff.board = board;
    handoff.image_version = image_version;
    handoff.load_address = load;
    handoff.image_size = size;
    handoff.image_sha256 = image_sha;
    handoff.bootstate_sequence = 7u;
    handoff.bootstate_state = 2u;
    handoff.journal_state = 1u;
    handoff.flags = HELIKON_BOOT_INFO_FLAG_DEVELOPMENT |
                    HELIKON_BOOT_INFO_FLAG_MANIFEST_VERIFIED;
    handoff.loader_stage_ms = 1234u;
    handoff.manifest = manifest;
    return handoff;
}

int main(void)
{
    int failures = 0;

    for (size_t i = 0u; i < HELIKON_SHA256_SIZE; ++i)
        image_sha[i] = (uint8_t)(0xa0u + i);
    build_manifest(6u, 15u, IMAGE_LOAD, IMAGE_SIZE);

    /* 1. A complete page round-trips through the firmware parser. */
    struct helikon_loader_handoff handoff = facts(6u, 15u, IMAGE_LOAD,
                                                  IMAGE_SIZE);
    failures += check(helikon_boot_info_page_build(page, sizeof(page),
                                                   &handoff) == HELIKON_OK,
                      "writer builds a page");
    struct helikon_boot_info_expect expect = expected();
    struct helikon_boot_info info;
    failures += check(helikon_boot_info_parse(PAGE_ADDR, &expect, &info) ==
                          HELIKON_OK,
                      "firmware parser accepts the page");
    failures += check(info.loader_version == 42u, "loader version survives");
    failures += check(info.board == 6u, "board survives");
    failures += check(info.image_version == 15u, "image version survives");
    failures += check(info.load_address == IMAGE_LOAD, "load address survives");
    failures += check(info.image_size == IMAGE_SIZE, "image size survives");
    failures += check(memcmp(info.image_sha256, image_sha,
                             HELIKON_SHA256_SIZE) == 0,
                      "image hash survives");
    failures += check(info.bootstate_sequence == 7u, "bootstate sequence");
    failures += check(info.bootstate_state == 2u, "bootstate state");
    failures += check(info.journal_state == 1u, "journal state");
    failures += check(info.flags == (HELIKON_BOOT_INFO_FLAG_DEVELOPMENT |
                                     HELIKON_BOOT_INFO_FLAG_MANIFEST_VERIFIED),
                      "flags survive");
    failures += check(info.loader_stage_ms == 1234u, "stage time survives");
    failures += check(memcmp(info.manifest, manifest, HELIKON_MANIFEST_SIZE) ==
                          0,
                      "manifest copy survives");

    /* 2. Everything outside the defined fields stays zero, because the parser
     * rejects a page with dirty reserved bytes. */
    {
        int dirty = 0;
        for (size_t i = 76u; i < 256u; ++i)
            dirty |= page[i] != 0u;
        for (size_t i = 512u; i < HELIKON_BOOT_INFO_PAGE_SIZE; ++i)
            dirty |= page[i] != 0u;
        failures += check(dirty == 0, "reserved bytes stay zero");
    }

    /* 3. The writer refuses a manifest that disagrees with the facts, so a
     * wrong page never reaches the firmware. */
    handoff = facts(6u, 15u, IMAGE_LOAD, IMAGE_SIZE + 4u);
    failures += check(helikon_boot_info_page_build(page, sizeof(page),
                                                   &handoff) ==
                          HELIKON_ERR_FORMAT,
                      "writer refuses a mismatched image size");
    handoff = facts(7u, 15u, IMAGE_LOAD, IMAGE_SIZE);
    failures += check(helikon_boot_info_page_build(page, sizeof(page),
                                                   &handoff) ==
                          HELIKON_ERR_FORMAT,
                      "writer refuses a mismatched board");
    handoff = facts(6u, 15u, IMAGE_LOAD, IMAGE_SIZE);
    image_sha[0] ^= 0xffu;
    failures += check(helikon_boot_info_page_build(page, sizeof(page),
                                                   &handoff) ==
                          HELIKON_ERR_FORMAT,
                      "writer refuses a mismatched image hash");
    image_sha[0] ^= 0xffu;

    handoff = facts(6u, 15u, IMAGE_LOAD, IMAGE_SIZE);
    failures += check(helikon_boot_info_page_build(page, 512u, &handoff) ==
                          HELIKON_ERR_ARGUMENT,
                      "writer refuses a buffer shorter than the page");
    failures += check(helikon_boot_info_page_build(NULL, sizeof(page),
                                                   &handoff) ==
                          HELIKON_ERR_ARGUMENT,
                      "writer refuses a null page");

    /* 4. Rebuild the good page, then prove the parser rejects corruption. */
    failures += check(helikon_boot_info_page_build(page, sizeof(page),
                                                   &handoff) == HELIKON_OK,
                      "writer rebuilds the page");
    page[10] ^= 0x01u; /* inside the CRC-protected header */
    failures += check(helikon_boot_info_parse(PAGE_ADDR, &expect, &info) ==
                          HELIKON_ERR_BOOT_INFO_INVALID,
                      "parser rejects a corrupted header");
    page[10] ^= 0x01u;

    page[256u] ^= 0x01u; /* manifest copy no longer matches the header */
    failures += check(helikon_boot_info_parse(PAGE_ADDR, &expect, &info) ==
                          HELIKON_ERR_BOOT_INFO_INVALID,
                      "parser rejects a corrupted manifest copy");
    page[256u] ^= 0x01u;

    failures += check(helikon_boot_info_parse(PAGE_ADDR, &expect, &info) ==
                          HELIKON_OK,
                      "page is valid again after the corruption is undone");

    /* 5. A zero pointer still means "no page", which is the no-args path the
     * development test payload uses. */
    failures += check(helikon_boot_info_parse(0u, &expect, &info) ==
                          HELIKON_ERR_NOT_FOUND,
                      "zero pointer reports no page");

    if (failures != 0) {
        fprintf(stderr, "%d boot-info page check(s) failed\n", failures);
        return 1;
    }
    printf("PASS boot info page\n");
    return 0;
}
