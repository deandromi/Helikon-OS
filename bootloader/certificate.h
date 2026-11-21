/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Release-key certificates, rotation and revocation.
 *
 * A manifest names one `key_id`; this layer maps that id to a certified
 * release key and answers whether the image being booted is allowed to use it.
 * The loader holds only verification code: there is no signing path, no key
 * generation and no clock.
 *
 * Rotation is by image version, not by wall time. A bootloader has no trusted
 * clock, so a certificate that expired "at midnight" could be judged only by a
 * value the attacker supplies. The certificate instead binds a release key to an
 * inclusive range of signed manifest image versions: the new key takes over from
 * a version the release process chooses, and the old one stops being accepted
 * there. Two certificates may name the same key id only if their ranges do not
 * overlap; a store picks the certificate whose range covers the booting image.
 *
 * Revocation is a signed bundle of key ids. A revoked id fails closed even when
 * its certificate is otherwise valid, which is what a leaked release key needs.
 *
 * Wire format (big-endian):
 *
 *   certificate, 120 bytes
 *     0..3   version (1)
 *     4..7   key_id
 *     8..11  issuer_id
 *     12..15 first_image_version
 *     16..19 last_image_version
 *     20..23 flags (bit 0: may sign development images)
 *     24..55 public key
 *     56..119 Ed25519 signature over bytes 0..55
 *
 *   revocation bundle, 140 bytes
 *     0..3   version (1)
 *     4..7   serial
 *     8..11  count (0..16)
 *     12..75 revoked key ids, unused entries zero
 *     76..139 Ed25519 signature over bytes 0..75
 */
#ifndef HELIKON_CERTIFICATE_H
#define HELIKON_CERTIFICATE_H

#include "ed25519.h"
#include "keys.h"

#include <stddef.h>
#include <stdint.h>

#define HELIKON_CERTIFICATE_VERSION 1u
#define HELIKON_CERTIFICATE_SIGNED_SIZE 56u
#define HELIKON_CERTIFICATE_SIZE \
    (HELIKON_CERTIFICATE_SIGNED_SIZE + HELIKON_ED25519_SIGNATURE_SIZE)

#define HELIKON_CERTIFICATE_FLAG_DEVELOPMENT 0x00000001u

#define HELIKON_REVOCATIONS_VERSION 1u
#define HELIKON_REVOCATIONS_MAX 16u
#define HELIKON_REVOCATIONS_SIGNED_SIZE \
    (12u + HELIKON_REVOCATIONS_MAX * 4u)
#define HELIKON_REVOCATIONS_SIZE \
    (HELIKON_REVOCATIONS_SIGNED_SIZE + HELIKON_ED25519_SIGNATURE_SIZE)

struct helikon_key_certificate {
    uint32_t version;
    uint32_t key_id;
    uint32_t issuer_id;
    uint32_t first_image_version;
    uint32_t last_image_version;
    uint32_t flags;
    uint8_t key[HELIKON_ED25519_PUBLIC_KEY_SIZE];
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
};

struct helikon_revocations {
    uint32_t version;
    uint32_t serial;
    uint32_t count;
    uint32_t ids[HELIKON_REVOCATIONS_MAX];
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
};

/*
 * Parse one certificate. The length must be exactly HELIKON_CERTIFICATE_SIZE,
 * the version must be one this build understands, the version range must be
 * non-empty and the unused flag bits must be zero. Returns 0 on success.
 */
int helikon_certificate_parse(const uint8_t *data, size_t length,
                              struct helikon_key_certificate *certificate);

/*
 * Parse and check the issuer signature with the root key table. A NULL table,
 * an unknown or a non-signing issuer fails closed (returns -1).
 */
int helikon_certificate_verify(const uint8_t *data, size_t length,
                               const struct helikon_key_table *roots,
                               struct helikon_key_certificate *certificate);

/* 1 when the version is inside the certificate's inclusive range, else 0. */
int helikon_certificate_covers(const struct helikon_key_certificate *certificate,
                               uint32_t image_version);

int helikon_revocations_parse(const uint8_t *data, size_t length,
                              struct helikon_revocations *revocations);

/*
 * Parse and check the root signature. Unused id slots must be zero so that one
 * revoked set has exactly one valid encoding.
 */
int helikon_revocations_verify(const uint8_t *data, size_t length,
                               const struct helikon_key_table *roots,
                               struct helikon_revocations *revocations);

/* 1 when `key_id` is revoked, else 0. A NULL bundle revokes nothing. */
int helikon_revocations_contain(const struct helikon_revocations *revocations,
                                uint32_t key_id);

/*
 * A verification context: the roots, the certificates a build ships and,
 * optionally, a revocation bundle.
 */
struct helikon_key_store {
    const struct helikon_key_table *roots;
    const uint8_t *const *certificates; /* HELIKON_CERTIFICATE_SIZE blobs */
    size_t certificate_count;
    const uint8_t *revocations; /* NULL or HELIKON_REVOCATIONS_SIZE bytes */
    uint32_t image_version;     /* the signed version being booted */
};

/*
 * `helikon_signature_verify_fn` adapter for a key store: resolve `key_id` to a
 * certified key for `image_version`, refuse a revoked id, then verify the
 * signature with the certified key. Every failure returns -1; the function
 * never reports which check failed, so a caller cannot be talked into treating
 * a partial result as a pass.
 */
int helikon_key_store_verify(void *context, uint32_t key_id,
                             const uint8_t *message, size_t message_length,
                             const uint8_t *signature,
                             size_t signature_length);

#endif
