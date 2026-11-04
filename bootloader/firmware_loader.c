/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "firmware_loader.h"

#include "loader_error.h"

#include <string.h>

static int read_exact(helikon_file_read_fn read_file, void *context,
                      const char *path, uint64_t offset, void *buffer,
                      size_t length)
{
    size_t done = 0;
    int rc = read_file(context, path, offset, buffer, length, &done);
    if (rc != 0)
        return rc;
    return done == length ? 0 : HELIKON_LOADER_ERR_IO;
}

static int bootstate_matches(const struct helikon_manifest *manifest,
                             const struct helikon_bootstate_slot *slot)
{
    return slot->board == manifest->board &&
           slot->image_version == manifest->image_version &&
           slot->load_address == manifest->load_address &&
           slot->image_size == manifest->image_size &&
           memcmp(slot->image_sha256, manifest->image_sha256,
                  HELIKON_SHA256_SIZE) == 0;
}

int helikon_loader_run(helikon_file_read_fn read_file, void *context,
                       const char *manifest_path, const char *image_path,
                       const char *bootstate_path, uint32_t expected_board,
                       int allow_development,
                       helikon_signature_verify_fn verify,
                       void *verify_context, uint8_t *image_buffer,
                       size_t image_capacity,
                       struct helikon_loader_result *result)
{
    if (read_file == NULL || manifest_path == NULL || image_path == NULL ||
        image_buffer == NULL || result == NULL)
        return HELIKON_LOADER_ERR_ARGUMENT;

    uint8_t manifest_data[HELIKON_MANIFEST_SIZE];
    int rc = read_exact(read_file, context, manifest_path, 0, manifest_data,
                        sizeof(manifest_data));
    if (rc != 0)
        return rc;

    struct helikon_manifest manifest;
    rc = helikon_manifest_verify_signature(
        manifest_data, sizeof(manifest_data), expected_board,
        allow_development, verify, verify_context, &manifest);
    if (rc != HELIKON_LOADER_OK)
        return rc;
    if (manifest.image_size > image_capacity)
        return HELIKON_LOADER_ERR_BUFFER;

    /*
     * The signed manifest is verified before any image byte reaches the
     * destination buffer. The image hash is checked immediately after the
     * read, before the caller may use the bytes.
     */
    rc = read_exact(read_file, context, image_path, 0, image_buffer,
                    manifest.image_size);
    if (rc != 0)
        return rc;

    rc = helikon_manifest_verify_image(&manifest, image_buffer,
                                       manifest.image_size);
    if (rc != HELIKON_LOADER_OK)
        return rc;

    memset(result, 0, sizeof(*result));
    result->manifest = manifest;
    result->image = image_buffer;
    result->image_size = manifest.image_size;

    if (bootstate_path != NULL) {
        uint8_t bootstate_data[HELIKON_BOOTSTATE_SIZE];
        rc = read_exact(read_file, context, bootstate_path, 0,
                        bootstate_data, sizeof(bootstate_data));
        if (rc != 0) {
            /*
             * Boot state is advisory. A missing, truncated or unreadable file
             * falls back to the already verified default image. It is never a
             * bootloader write path.
             */
            result->bootstate_valid = 0;
            return HELIKON_LOADER_OK;
        }

        struct helikon_bootstate bootstate;
        rc = helikon_bootstate_parse(bootstate_data, sizeof(bootstate_data),
                                     expected_board, &bootstate);
        if (rc == HELIKON_LOADER_ERR_NO_IMAGE ||
            rc == HELIKON_LOADER_ERR_BOOTSTATE) {
            result->bootstate_valid = 0;
            return HELIKON_LOADER_OK;
        }
        if (rc != HELIKON_LOADER_OK)
            return rc;
        if (!bootstate_matches(&manifest, &bootstate.slot))
            return HELIKON_LOADER_ERR_BOOTSTATE;

        result->bootstate = bootstate;
        result->bootstate_valid = 1;
    }

    return HELIKON_LOADER_OK;
}
