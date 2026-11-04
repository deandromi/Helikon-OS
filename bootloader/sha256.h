/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_SHA256_H
#define HELIKON_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define HELIKON_SHA256_SIZE 32u

struct helikon_sha256 {
    uint32_t state[8];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
};

void helikon_sha256_init(struct helikon_sha256 *ctx);
void helikon_sha256_update(struct helikon_sha256 *ctx, const void *data,
                           size_t length);
void helikon_sha256_final(struct helikon_sha256 *ctx,
                          uint8_t digest[HELIKON_SHA256_SIZE]);
void helikon_sha256(const void *data, size_t length,
                    uint8_t digest[HELIKON_SHA256_SIZE]);

#endif
