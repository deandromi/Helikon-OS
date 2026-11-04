/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Boot-info page parser for the accepted D-28 / ADR-0004 entry ABI.
 *
 * The loader writes a 4 KiB page at 0x087ff000 and passes its physical address
 * in r0. This module validates the page and exposes a small verified view. It
 * does not trust the page for writes: bootstate remains the durable record and
 * the firmware re-reads it before any write.
 *
 * Validation order: r0 range/alignment, magic/version/size/CRC, board,
 * image size/load/range, manifest copy. A missing page (r0 = 0) returns
 * HELIKON_ERR_NOT_FOUND; any invalid page returns
 * HELIKON_ERR_BOOT_INFO_INVALID. Both send the caller to the read-only
 * diagnostic path.
 */
#ifndef HELIKON_BOOT_BOOT_INFO_H
#define HELIKON_BOOT_BOOT_INFO_H

#include <stddef.h>
#include <stdint.h>

#define HELIKON_BOOT_INFO_MAGIC 0x31494248u /* "HBI1", little-endian */
#define HELIKON_BOOT_INFO_VERSION 1u
#define HELIKON_BOOT_INFO_SIZE 512u
#define HELIKON_BOOT_INFO_PAGE_SIZE 4096u
#define HELIKON_BOOT_INFO_MANIFEST_OFFSET 256u
#define HELIKON_BOOT_INFO_RESERVED_OFFSET 512u

#define HELIKON_MANIFEST_SIZE 256u
#define HELIKON_MANIFEST_MAGIC "HELIKON1"
#define HELIKON_SHA256_SIZE 32u

#define HELIKON_BOOT_INFO_FLAG_DEVELOPMENT 0x0001u
#define HELIKON_BOOT_INFO_FLAG_BOOTSTATE_PRESENT 0x0002u
#define HELIKON_BOOT_INFO_FLAG_MANIFEST_VERIFIED 0x0004u

struct helikon_boot_info_page {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t loader_version;
    uint32_t board;
    uint32_t image_version;
    uint32_t load_address;
    uint32_t image_size;
    uint8_t image_sha256[HELIKON_SHA256_SIZE];
    uint32_t bootstate_sequence;
    uint8_t bootstate_state;
    uint8_t journal_state;
    uint16_t flags;
    uint32_t loader_stage_ms;
    uint32_t crc32;
    uint8_t reserved0[180];
    uint8_t manifest[HELIKON_MANIFEST_SIZE];
    uint8_t reserved1[3584];
};

/* Facts the running image expects the loader to have described. */
struct helikon_boot_info_expect {
    uintptr_t page_base;
    uint32_t page_size;
    uint32_t board_id;
    uint32_t image_load;
    uint32_t image_size;
};

/* Verified, non-pointer view for the rest of the firmware. */
struct helikon_boot_info {
    uint32_t loader_version;
    uint32_t board;
    uint32_t image_version;
    uint32_t load_address;
    uint32_t image_size;
    uint8_t image_sha256[HELIKON_SHA256_SIZE];
    uint32_t bootstate_sequence;
    uint8_t bootstate_state;
    uint8_t journal_state;
    uint16_t flags;
    uint32_t loader_stage_ms;
    uint8_t manifest[HELIKON_MANIFEST_SIZE];
};

int helikon_boot_info_parse(uintptr_t address,
                            const struct helikon_boot_info_expect *expect,
                            struct helikon_boot_info *info);

#endif
