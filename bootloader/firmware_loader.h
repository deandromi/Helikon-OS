/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_FIRMWARE_LOADER_H
#define HELIKON_FIRMWARE_LOADER_H

#include "bootstate.h"
#include "manifest.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Read exactly `length` bytes from `path` at `offset`. Return 0 on success,
 * HELIKON_LOADER_ERR_NOT_FOUND when the path is absent, another negative
 * HELIKON_LOADER_* code on failure.
 */
typedef int (*helikon_file_read_fn)(void *context, const char *path,
                                    uint64_t offset, void *buffer,
                                    size_t length, size_t *done);

struct helikon_loader_result {
    struct helikon_manifest manifest;
    struct helikon_bootstate bootstate;
    int bootstate_valid;
    uint8_t *image;
    size_t image_size;
};

/*
 * Read and verify the manifest and image, then optionally select a boot-state
 * slot. The loader never writes and never jumps. `image_buffer` must hold at
 * least the manifest's image size.
 *
 * A missing or corrupt bootstate is not fatal: the manifest image is the
 * default. A valid bootstate that disagrees with the manifest fails closed.
 */
int helikon_loader_run(helikon_file_read_fn read_file, void *context,
                       const char *manifest_path, const char *image_path,
                       const char *bootstate_path, uint32_t expected_board,
                       int allow_development,
                       helikon_signature_verify_fn verify,
                       void *verify_context, uint8_t *image_buffer,
                       size_t image_capacity,
                       struct helikon_loader_result *result);

#endif
