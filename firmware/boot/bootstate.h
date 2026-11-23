/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Firmware-side HFS+ bootstate parser and promoter (F2 / T-055).
 *
 * The on-disk format is the one the loader reads: two 256-byte slots in a
 * 512-byte file. The loader owns boot selection; the firmware only promotes
 * the slot that matches the image it is actually running. The file is
 * re-read through HFS+ before any write; boot-info is not trusted here.
 */
#ifndef HELIKON_BOOT_BOOTSTATE_H
#define HELIKON_BOOT_BOOTSTATE_H

#include "sha256.h"

#include <stddef.h>
#include <stdint.h>

#define HELIKON_BOOTSTATE_SIZE 512u
#define HELIKON_BOOTSTATE_SLOT_SIZE 256u
#define HELIKON_BOOTSTATE_SLOTS 2u
#define HELIKON_BOOTSTATE_VERSION 1u
#define HELIKON_BOOTSTATE_MAGIC "HBST"
#define HELIKON_BOOTSTATE_IMAGE_LOAD 0x08000000u
#define HELIKON_BOOTSTATE_IMAGE_MAX 0x00800000u

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
    struct helikon_bootstate_slot slots[HELIKON_BOOTSTATE_SLOTS];
    int selected; /* highest-sequence valid GOOD slot, or -1 */
};

/*
 * Validate the 512-byte file. A structurally valid file with no usable slot
 * still returns HELIKON_OK; inspect `slots[].valid` and `selected`.
 */
int helikon_bootstate_parse(const uint8_t *data, size_t length,
                            uint32_t expected_board,
                            struct helikon_bootstate *bootstate);

/*
 * Find the slot matching the running image (board, version, load address, size
 * and SHA-256). If it is TRYING, change it to GOOD with a sequence higher than
 * every valid slot and recompute its checksum. If it is already GOOD, leave
 * the file untouched. `changed` is 0 or 1 on success.
 *
 * The caller owns the 512-byte buffer and commits it through hfs_txn.
 */
int helikon_bootstate_promote(uint8_t *data, size_t length,
                              uint32_t expected_board,
                              uint32_t image_version,
                              uint32_t load_address,
                              uint32_t image_size,
                              const uint8_t image_sha256[HELIKON_SHA256_SIZE],
                              uint32_t *promoted_sequence, int *changed);

/*
 * Stage a TRYING slot for an image that is about to be installed, in the caller's
 * 512-byte buffer, and demote every other slot that names an image to FAILED.
 *
 * The demotion is the part that is easy to get wrong. The loader validates the
 * selected GOOD slot against the manifest it just verified, so a GOOD slot that
 * still describes the image we are replacing would turn every boot after the
 * update into a recovery screen. A TRYING slot for the replaced image is just as
 * wrong, because after the commit there is one image path and it holds the new
 * bytes. TRYING cannot be selected, so the next boot uses the verified default
 * image — the new one — and `helikon_bootstate_promote` turns this slot into GOOD
 * once the running image passes its self-test.
 *
 * A corrupt or absent file is not an error: this call writes the slot over it,
 * so an update repairs the boot state by replacing it. `sequence` receives the
 * new slot's sequence, which is higher than every valid slot.
 */
int helikon_bootstate_stage_trying(uint8_t *data, size_t length,
                                   uint32_t expected_board,
                                   uint32_t image_version,
                                   uint32_t load_address, uint32_t image_size,
                                   const uint8_t image_sha256[HELIKON_SHA256_SIZE],
                                   uint32_t *sequence);

#endif
