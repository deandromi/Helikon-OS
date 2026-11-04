/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "keys.h"

int helikon_key_table_verify(void *context, uint32_t key_id,
                             const uint8_t *message, size_t message_length,
                             const uint8_t *signature,
                             size_t signature_length)
{
    const struct helikon_key_table *table = context;

    if (table == NULL || (table->keys == NULL && table->count != 0))
        return -1;
    if (message == NULL || signature == NULL ||
        signature_length != HELIKON_ED25519_SIGNATURE_SIZE)
        return -1;

    for (size_t i = 0; i < table->count; ++i) {
        if (table->keys[i].id == key_id)
            return helikon_ed25519_verify(table->keys[i].key, message,
                                          message_length, signature);
    }
    return -1;
}
