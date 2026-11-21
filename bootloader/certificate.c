/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "certificate.h"

#include <string.h>

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

int helikon_certificate_parse(const uint8_t *data, size_t length,
                              struct helikon_key_certificate *certificate)
{
    if (data == NULL || certificate == NULL ||
        length != HELIKON_CERTIFICATE_SIZE)
        return -1;

    memset(certificate, 0, sizeof(*certificate));
    certificate->version = be32(data);
    certificate->key_id = be32(data + 4);
    certificate->issuer_id = be32(data + 8);
    certificate->first_image_version = be32(data + 12);
    certificate->last_image_version = be32(data + 16);
    certificate->flags = be32(data + 20);
    memcpy(certificate->key, data + 24, HELIKON_ED25519_PUBLIC_KEY_SIZE);
    memcpy(certificate->signature, data + HELIKON_CERTIFICATE_SIGNED_SIZE,
           HELIKON_ED25519_SIGNATURE_SIZE);

    if (certificate->version != HELIKON_CERTIFICATE_VERSION)
        return -1;
    if (certificate->first_image_version > certificate->last_image_version)
        return -1;
    if ((certificate->flags & ~HELIKON_CERTIFICATE_FLAG_DEVELOPMENT) != 0u)
        return -1;
    return 0;
}

int helikon_certificate_verify(const uint8_t *data, size_t length,
                               const struct helikon_key_table *roots,
                               struct helikon_key_certificate *certificate)
{
    if (roots == NULL || roots->keys == NULL || roots->count == 0)
        return -1;
    if (helikon_certificate_parse(data, length, certificate) != 0)
        return -1;

    /*
     * The issuer must be a root this build carries. `helikon_key_table_verify`
     * already fails closed on an unknown id, so a non-root issuer cannot sign
     * itself in.
     */
    struct helikon_key_table table = *roots;
    return helikon_key_table_verify(&table, certificate->issuer_id, data,
                                    HELIKON_CERTIFICATE_SIGNED_SIZE,
                                    certificate->signature,
                                    HELIKON_ED25519_SIGNATURE_SIZE);
}

int helikon_certificate_covers(const struct helikon_key_certificate *certificate,
                               uint32_t image_version)
{
    if (certificate == NULL)
        return 0;
    return image_version >= certificate->first_image_version &&
           image_version <= certificate->last_image_version;
}

int helikon_revocations_parse(const uint8_t *data, size_t length,
                              struct helikon_revocations *revocations)
{
    if (data == NULL || revocations == NULL ||
        length != HELIKON_REVOCATIONS_SIZE)
        return -1;

    memset(revocations, 0, sizeof(*revocations));
    revocations->version = be32(data);
    revocations->serial = be32(data + 4);
    revocations->count = be32(data + 8);
    memcpy(revocations->signature, data + HELIKON_REVOCATIONS_SIGNED_SIZE,
           HELIKON_ED25519_SIGNATURE_SIZE);

    if (revocations->version != HELIKON_REVOCATIONS_VERSION)
        return -1;
    if (revocations->count > HELIKON_REVOCATIONS_MAX)
        return -1;
    for (uint32_t i = 0; i < HELIKON_REVOCATIONS_MAX; ++i) {
        uint32_t id = be32(data + 12u + i * 4u);
        if (i >= revocations->count && id != 0u)
            return -1; /* unused slots must be zero: one encoding per set */
        revocations->ids[i] = id;
    }
    return 0;
}

int helikon_revocations_verify(const uint8_t *data, size_t length,
                               const struct helikon_key_table *roots,
                               struct helikon_revocations *revocations)
{
    if (roots == NULL || roots->keys == NULL || roots->count == 0)
        return -1;
    if (helikon_revocations_parse(data, length, revocations) != 0)
        return -1;

    for (size_t i = 0; i < roots->count; ++i) {
        if (helikon_ed25519_verify(roots->keys[i].key, data,
                                   HELIKON_REVOCATIONS_SIGNED_SIZE,
                                   revocations->signature) == 0)
            return 0;
    }
    return -1;
}

int helikon_revocations_contain(const struct helikon_revocations *revocations,
                                uint32_t key_id)
{
    if (revocations == NULL)
        return 0;
    for (uint32_t i = 0; i < revocations->count && i < HELIKON_REVOCATIONS_MAX;
         ++i) {
        if (revocations->ids[i] == key_id)
            return 1;
    }
    return 0;
}

int helikon_key_store_verify(void *context, uint32_t key_id,
                             const uint8_t *message, size_t message_length,
                             const uint8_t *signature,
                             size_t signature_length)
{
    const struct helikon_key_store *store = context;
    if (store == NULL || store->roots == NULL || message == NULL ||
        signature == NULL ||
        signature_length != HELIKON_ED25519_SIGNATURE_SIZE)
        return -1;
    if (store->certificates == NULL && store->certificate_count != 0)
        return -1;

    struct helikon_revocations revocations;
    int have_revocations = 0;
    if (store->revocations != NULL) {
        if (helikon_revocations_verify(store->revocations,
                                       HELIKON_REVOCATIONS_SIZE,
                                       store->roots, &revocations) != 0)
            return -1; /* a bundle we cannot authenticate fails closed */
        have_revocations = 1;
        if (helikon_revocations_contain(&revocations, key_id))
            return -1;
    }

    for (size_t i = 0; i < store->certificate_count; ++i) {
        const uint8_t *blob = store->certificates[i];
        struct helikon_key_certificate certificate;
        if (blob == NULL)
            continue;
        if (helikon_certificate_parse(blob, HELIKON_CERTIFICATE_SIZE,
                                      &certificate) != 0)
            continue;
        if (certificate.key_id != key_id)
            continue;
        if (!helikon_certificate_covers(&certificate, store->image_version))
            continue;
        if (helikon_certificate_verify(blob, HELIKON_CERTIFICATE_SIZE,
                                       store->roots, &certificate) != 0)
            continue;
        /* Re-check the fields the decision above used, from the verified copy. */
        if (certificate.key_id != key_id ||
            !helikon_certificate_covers(&certificate, store->image_version))
            continue;
        if (have_revocations &&
            helikon_revocations_contain(&revocations, certificate.key_id))
            continue;
        return helikon_ed25519_verify(certificate.key, message, message_length,
                                      signature);
    }
    return -1;
}
