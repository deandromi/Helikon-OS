/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "manifest.h"

#include "loader_error.h"

#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int helikon_manifest_parse(const uint8_t *data, size_t length,
                           struct helikon_manifest *manifest)
{
    if (data == NULL || manifest == NULL)
        return HELIKON_LOADER_ERR_ARGUMENT;
    if (length != HELIKON_MANIFEST_SIZE)
        return HELIKON_LOADER_ERR_SIZE;
    if (memcmp(data, HELIKON_MANIFEST_MAGIC, 8u) != 0)
        return HELIKON_LOADER_ERR_MAGIC;

    uint32_t version = le32(data + 8);
    if (version != HELIKON_MANIFEST_VERSION)
        return HELIKON_LOADER_ERR_VERSION;

    uint32_t image_size = le32(data + 24);
    uint32_t load_address = le32(data + 20);
    if (image_size == 0 || image_size > HELIKON_DRAM_SIZE)
        return HELIKON_LOADER_ERR_SIZE;
    if (load_address < HELIKON_DRAM_BASE ||
        (load_address & 3u) != 0 ||
        (uint64_t)load_address + image_size >
            (uint64_t)HELIKON_DRAM_BASE + HELIKON_DRAM_SIZE)
        return HELIKON_LOADER_ERR_ADDRESS;

    uint32_t key_id = le32(data + 28);
    if (key_id == 0)
        return HELIKON_LOADER_ERR_SIGNATURE;

    uint32_t flags = le32(data + 64);
    if ((flags & ~HELIKON_MANIFEST_FLAG_DEVELOPMENT) != 0)
        return HELIKON_LOADER_ERR_FLAGS;

    for (size_t i = 68u; i < HELIKON_MANIFEST_SIGNED_SIZE; ++i) {
        if (data[i] != 0)
            return HELIKON_LOADER_ERR_FLAGS;
    }

    memset(manifest, 0, sizeof(*manifest));
    manifest->version = version;
    manifest->board = le32(data + 12);
    manifest->image_version = le32(data + 16);
    manifest->load_address = load_address;
    manifest->image_size = image_size;
    manifest->key_id = key_id;
    manifest->flags = flags;
    memcpy(manifest->image_sha256, data + 32, HELIKON_SHA256_SIZE);
    memcpy(manifest->signature, data + HELIKON_MANIFEST_SIGNED_SIZE,
           HELIKON_MANIFEST_SIGNATURE_SIZE);
    return HELIKON_LOADER_OK;
}

int helikon_manifest_verify(const uint8_t *data, size_t length,
                            uint32_t expected_board, const uint8_t *image,
                            size_t image_length, int allow_development,
                            helikon_signature_verify_fn verify,
                            void *verify_context,
                            struct helikon_manifest *manifest)
{
    if (data == NULL || image == NULL || manifest == NULL)
        return HELIKON_LOADER_ERR_ARGUMENT;

    struct helikon_manifest parsed;
    int rc = helikon_manifest_parse(data, length, &parsed);
    if (rc != HELIKON_LOADER_OK)
        return rc;
    if (parsed.board != expected_board)
        return HELIKON_LOADER_ERR_BOARD;
    if (parsed.image_version == 0)
        return HELIKON_LOADER_ERR_IMAGE_VERSION;
    if (parsed.flags & HELIKON_MANIFEST_FLAG_DEVELOPMENT) {
        if (!allow_development)
            return HELIKON_LOADER_ERR_FLAGS;
    }
    if (image_length != parsed.image_size)
        return HELIKON_LOADER_ERR_SIZE;

    uint8_t digest[HELIKON_SHA256_SIZE];
    helikon_sha256(image, image_length, digest);
    if (memcmp(digest, parsed.image_sha256, sizeof(digest)) != 0)
        return HELIKON_LOADER_ERR_HASH;

    if (verify == NULL)
        return HELIKON_LOADER_ERR_SIGNATURE;
    rc = verify(verify_context, parsed.key_id, data, HELIKON_MANIFEST_SIGNED_SIZE,
                parsed.signature, sizeof(parsed.signature));
    if (rc != 0)
        return HELIKON_LOADER_ERR_SIGNATURE;

    *manifest = parsed;
    return HELIKON_LOADER_OK;
}
