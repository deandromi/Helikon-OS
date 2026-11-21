/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Boot-info page writer for the D-28 / ADR-0004 entry ABI.
 *
 * The loader writes a 4 KiB page at 0x087ff000 before the jump and passes its
 * address in r0. `firmware/boot/boot_info.c` parses the same bytes and is the
 * authority on validity; this module only fills them, and it refuses to fill a
 * page whose facts disagree with the signed manifest it is about to describe.
 *
 * The loader never writes the filesystem here: the page lives in DRAM.
 */
#ifndef HELIKON_BOOT_BOOT_INFO_PAGE_H
#define HELIKON_BOOT_BOOT_INFO_PAGE_H

#include <stddef.h>
#include <stdint.h>

#include "boot_info.h"

/* ``loader_stage_ms`` is measured by the caller: the loader has a tick counter,
 * not a wall clock, and the firmware reads it as a duration in milliseconds. */
struct helikon_loader_handoff {
    uint32_t loader_version;
    uint32_t board;
    uint32_t image_version;
    uint32_t load_address;
    uint32_t image_size;
    const uint8_t *image_sha256; /* HELIKON_SHA256_SIZE bytes */
    uint32_t bootstate_sequence;
    uint8_t bootstate_state;
    uint8_t journal_state;
    uint16_t flags;
    uint32_t loader_stage_ms;
    const uint8_t *manifest; /* HELIKON_MANIFEST_SIZE raw bytes */
};

/*
 * Fill ``page`` with a version-1 boot-info page. Returns HELIKON_OK, or
 * HELIKON_ERR_ARGUMENT for a missing argument, a short buffer or an unaligned
 * page base, or HELIKON_ERR_FORMAT when the manifest bytes do not describe the
 * same image as the facts (board, version, load address, size or SHA-256).
 */
int helikon_boot_info_page_build(void *page, size_t page_size,
                                 const struct helikon_loader_handoff *handoff);

#endif
