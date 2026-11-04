/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_KEYS_H
#define HELIKON_KEYS_H

#include "ed25519.h"

#include <stddef.h>
#include <stdint.h>

struct helikon_public_key {
    uint32_t id;
    uint8_t key[HELIKON_ED25519_PUBLIC_KEY_SIZE];
};

struct helikon_key_table {
    const struct helikon_public_key *keys;
    size_t count;
};

/*
 * `helikon_signature_verify_fn` adapter for a public-key table. Unknown key
 * IDs fail closed. The table may be empty; it then rejects every signature.
 */
int helikon_key_table_verify(void *context, uint32_t key_id,
                             const uint8_t *message, size_t message_length,
                             const uint8_t *signature,
                             size_t signature_length);

#endif
