/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_ED25519_H
#define HELIKON_ED25519_H

#include <stddef.h>
#include <stdint.h>

#define HELIKON_ED25519_PUBLIC_KEY_SIZE 32u
#define HELIKON_ED25519_SIGNATURE_SIZE 64u
#define HELIKON_ED25519_MESSAGE_MAX 192u

/*
 * Verify a detached Ed25519 signature over `message`.
 *
 * Returns 0 only when the signature is valid for the given public key and
 * message. A NULL key/signature, a message longer than
 * HELIKON_ED25519_MESSAGE_MAX or an invalid signature returns -1.
 *
 * This module contains verification code only. It has no key generation, no
 * signing path and no random number source.
 */
int helikon_ed25519_verify(
    const uint8_t public_key[HELIKON_ED25519_PUBLIC_KEY_SIZE],
    const uint8_t *message, size_t message_length,
    const uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE]);

#endif
