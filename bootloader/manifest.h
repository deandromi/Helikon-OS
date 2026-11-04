/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_MANIFEST_H
#define HELIKON_MANIFEST_H

#include "sha256.h"

#include <stddef.h>
#include <stdint.h>

#define HELIKON_MANIFEST_SIZE 256u
#define HELIKON_MANIFEST_SIGNED_SIZE 192u
#define HELIKON_MANIFEST_SIGNATURE_SIZE 64u
#define HELIKON_MANIFEST_VERSION 1u
#define HELIKON_MANIFEST_MAGIC "HELIKON1"

#define HELIKON_BOARD_IPOD6G 6u
#define HELIKON_BOARD_IPOD7G 7u

#define HELIKON_DRAM_BASE 0x08000000u
#define HELIKON_DRAM_SIZE (8u * 1024u * 1024u)

#define HELIKON_MANIFEST_FLAG_DEVELOPMENT 0x00000001u

struct helikon_manifest {
    uint32_t version;
    uint32_t board;
    uint32_t image_version;
    uint32_t load_address;
    uint32_t image_size;
    uint32_t key_id;
    uint32_t flags;
    uint8_t image_sha256[HELIKON_SHA256_SIZE];
    uint8_t signature[HELIKON_MANIFEST_SIGNATURE_SIZE];
};

/*
 * Verify a manifest and the image bytes it describes.
 *
 * `verify` must implement Ed25519 verification for `key_id` over the first
 * HELIKON_MANIFEST_SIGNED_SIZE bytes of `manifest`. A NULL hook fails closed.
 * `allow_development` permits HELIKON_MANIFEST_FLAG_DEVELOPMENT.
 */
typedef int (*helikon_signature_verify_fn)(
    void *context, uint32_t key_id, const uint8_t *message,
    size_t message_length, const uint8_t *signature, size_t signature_length);

int helikon_manifest_parse(const uint8_t *data, size_t length,
                           struct helikon_manifest *manifest);

int helikon_manifest_verify(const uint8_t *data, size_t length,
                            uint32_t expected_board, const uint8_t *image,
                            size_t image_length, int allow_development,
                            helikon_signature_verify_fn verify,
                            void *verify_context,
                            struct helikon_manifest *manifest);

#endif
