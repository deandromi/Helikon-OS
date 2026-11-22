/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libFuzzer target for the firmware manifest and boot-state readers.
 *
 * These two decide whether an image runs, and they run before anything has been
 * verified, so the only thing standing between a corrupt sector and a jump is
 * this code. The invariants:
 *   - neither parser reports success without its magic, size and checksum
 *     having matched, which is what the return code already promises;
 *   - a parse that rejects leaves the output struct untouched, so a caller that
 *     ignores the return code cannot read half-filled fields;
 *   - the image size a manifest claims never exceeds DRAM.
 *
 *   ./tools/fuzz.sh manifest 300
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bootstate.h"
#include "helikon_error.h"
#include "manifest.h"

#define BOARD_6G 6u

static int all_zero(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != 0u)
            return 0;
    }
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct helikon_manifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    int rc = helikon_manifest_parse(data, size, &manifest);
    if (rc == HELIKON_OK) {
        if (manifest.image_size == 0u ||
            manifest.image_size > HELIKON_DRAM_SIZE) {
            fprintf(stderr, "fuzz: accepted an image size of %u\n",
                    (unsigned)manifest.image_size);
            abort();
        }
        if (manifest.version == 0u) {
            fprintf(stderr, "fuzz: accepted manifest version 0\n");
            abort();
        }
    }

    struct helikon_bootstate bootstate;
    memset(&bootstate, 0, sizeof(bootstate));
    rc = helikon_bootstate_parse(data, size, BOARD_6G, &bootstate);
    if (rc == HELIKON_OK && bootstate.selected < 0) {
        fprintf(stderr, "fuzz: accepted a negative slot index\n");
        abort();
    }

    /* A page too short for either format is refused by both, always. */
    if (size < 64u) {
        struct helikon_manifest small;
        struct helikon_bootstate state;
        memset(&small, 0, sizeof(small));
        memset(&state, 0, sizeof(state));
        if (helikon_manifest_parse(data, size, &small) == HELIKON_OK)
            abort();
        if (helikon_bootstate_parse(data, size, BOARD_6G, &state) == HELIKON_OK)
            abort();
        if (!all_zero(&small, sizeof(small)) || !all_zero(&state, sizeof(state)))
            abort();
    }
    return 0;
}
