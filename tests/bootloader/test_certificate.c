/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "certificate.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Signature fixtures. The keys are throwaway seeds (0x11 root, 0x22 and 0x33
 * release, 0x44 a foreign issuer) and the certificates were minted with
 * tools/make-key-certificate.py, which is the same offline tool a release would
 * use. Ed25519 is deterministic, so these bytes are reproducible: minting the
 * same fields with the same key prints the same certificate.
 *
 *   certificate A        key 7, issuer 3, image versions 1..20
 *   certificate B        key 8, issuer 3, image versions 21..0xffffffff
 *   certificate A (rot.) key 7, issuer 3, image versions 21..0xffffffff
 *   certificate X        key 7, issuer 9 (not a root this test carries)
 *   revocations          serial 5, revoking key 7
 */

static const char root_public_hex[] =
    "d04ab232742bb4ab3a1368bd4615e4e6"
    "d0224ab71a016baf8520a332c9778737";

static const char release_a_public_hex[] =
    "a09aa5f47a6759802ff955f8dc2d2a14"
    "a5c99d23be97f864127ff9383455a4f0";

static const char release_b_public_hex[] =
    "17cb79fb2b4120f2b1ec65e4198d6e08"
    "b28e813feb01e4a400839b85e18080ce";

static const char certificate_a_hex[] =
    "00000001000000070000000300000001"
    "0000001400000000a09aa5f47a675980"
    "2ff955f8dc2d2a14a5c99d23be97f864"
    "127ff9383455a4f065313705052d65b8"
    "7a2142c3558544684d9fc7726a240e30"
    "c0903e4e34eeaf831502c3df06dc8afd"
    "7d5e1253756fe4624a226b3cb8b0b933"
    "455d3f769804fa03";

static const char certificate_b_hex[] =
    "00000001000000080000000300000015"
    "ffffffff0000000017cb79fb2b4120f2"
    "b1ec65e4198d6e08b28e813feb01e4a4"
    "00839b85e18080ce3746f2a330bfb59f"
    "6c8ef51695538289b57de39030c69eb3"
    "8ca1441fd5d275cd77c59792b5097104"
    "f1ba9203b5446a430d9221c96b5547c0"
    "bb322ad614859e01";

static const char certificate_a_rotated_hex[] =
    "00000001000000070000000300000015"
    "ffffffff00000000a09aa5f47a675980"
    "2ff955f8dc2d2a14a5c99d23be97f864"
    "127ff9383455a4f0e07dc365f169be5f"
    "d52715be0d339c8cc51f9676d39a17c2"
    "d996fda90922456469fee6c81eec5f12"
    "25ff1803e2c3111efecb66c5c3e91f23"
    "17e348ed3d665b00";

static const char certificate_unknown_issuer_hex[] =
    "00000001000000070000000900000001"
    "ffffffff00000000a09aa5f47a675980"
    "2ff955f8dc2d2a14a5c99d23be97f864"
    "127ff9383455a4f01407ae08b128df51"
    "ea713e3238c69113a11c6cacc09c5e7e"
    "99ffaeba4c8bc044a68951aeb9995121"
    "412a7326402d902223c12a705cc97113"
    "d4c55dc88f59a80d";

static const char revocations_hex[] =
    "00000001000000050000000100000007"
    "00000000000000000000000000000000"
    "00000000000000000000000000000000"
    "00000000000000000000000000000000"
    "000000000000000000000000afe172de"
    "7badbfc76ade51c464aec8b2051ea075"
    "b6f5ea3e8e499c5607f74a56d8d74dac"
    "682a379b9b0f112d7683d4c07e977386"
    "343c13e070befeb9ef334c0c";

static const char message_hex[] =
    "000102030405060708090a0b0c0d0e0f"
    "101112131415161718191a1b1c1d1e1f"
    "202122232425262728292a2b2c2d2e2f"
    "3031323334353637";

static const char message_signature_hex[] =
    "f7803a2772525dcd9943e3e5059bb5de"
    "10a1ca6d3664a23de8b7854566592cf1"
    "5920e1d4d75c01ec7e117e09d8dcfc98"
    "3ad3e184711cdf6980995ef8329f320c";


#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static size_t unhex(const char *text, uint8_t *out, size_t capacity)
{
    size_t length = strlen(text);
    if (length % 2u != 0u || length / 2u > capacity)
        return 0;
    for (size_t i = 0; i < length / 2u; ++i) {
        unsigned int high = 0;
        unsigned int low = 0;
        char pair[3] = { text[i * 2u], text[i * 2u + 1u], 0 };
        if (sscanf(pair, "%1x", &high) != 1 ||
            sscanf(pair + 1, "%1x", &low) != 1)
            return 0;
        out[i] = (uint8_t)((high << 4) | low);
    }
    return length / 2u;
}

static struct helikon_public_key root_keys[1];
static struct helikon_key_table roots;

static int load_fixtures(void)
{
    CHECK(unhex(root_public_hex, root_keys[0].key,
                HELIKON_ED25519_PUBLIC_KEY_SIZE) ==
          HELIKON_ED25519_PUBLIC_KEY_SIZE);
    root_keys[0].id = 3;
    roots.keys = root_keys;
    roots.count = ARRAY_SIZE(root_keys);
    return 0;
}

static int test_certificate_parses_and_verifies(void)
{
    CHECK(load_fixtures() == 0);

    uint8_t blob[HELIKON_CERTIFICATE_SIZE];
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));

    struct helikon_key_certificate certificate;
    CHECK(helikon_certificate_parse(blob, sizeof(blob), &certificate) == 0);
    CHECK(certificate.version == HELIKON_CERTIFICATE_VERSION);
    CHECK(certificate.key_id == 7);
    CHECK(certificate.issuer_id == 3);
    CHECK(certificate.first_image_version == 1);
    CHECK(certificate.last_image_version == 20);
    CHECK(certificate.flags == 0);

    uint8_t expected_key[HELIKON_ED25519_PUBLIC_KEY_SIZE];
    CHECK(unhex(release_a_public_hex, expected_key, sizeof(expected_key)) ==
          sizeof(expected_key));
    CHECK(memcmp(certificate.key, expected_key, sizeof(expected_key)) == 0);

    CHECK(helikon_certificate_verify(blob, sizeof(blob), &roots, &certificate) ==
          0);

    /* The second certificate carries a different id, range and key. */
    uint8_t blob_b[HELIKON_CERTIFICATE_SIZE];
    CHECK(unhex(certificate_b_hex, blob_b, sizeof(blob_b)) == sizeof(blob_b));
    struct helikon_key_certificate certificate_b;
    CHECK(helikon_certificate_verify(blob_b, sizeof(blob_b), &roots,
                                     &certificate_b) == 0);
    CHECK(certificate_b.key_id == 8);
    CHECK(certificate_b.first_image_version == 21);
    CHECK(certificate_b.last_image_version == 0xffffffffu);
    uint8_t expected_b[HELIKON_ED25519_PUBLIC_KEY_SIZE];
    CHECK(unhex(release_b_public_hex, expected_b, sizeof(expected_b)) ==
          sizeof(expected_b));
    CHECK(memcmp(certificate_b.key, expected_b, sizeof(expected_b)) == 0);
    CHECK(memcmp(certificate_b.key, certificate.key, sizeof(expected_b)) != 0);
    return 0;
}

static int test_certificate_range(void)
{
    CHECK(load_fixtures() == 0);
    uint8_t blob[HELIKON_CERTIFICATE_SIZE];
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    struct helikon_key_certificate certificate;
    CHECK(helikon_certificate_verify(blob, sizeof(blob), &roots, &certificate) ==
          0);

    CHECK(helikon_certificate_covers(&certificate, 1) == 1);
    CHECK(helikon_certificate_covers(&certificate, 5) == 1);
    CHECK(helikon_certificate_covers(&certificate, 20) == 1);
    CHECK(helikon_certificate_covers(&certificate, 0) == 0);
    CHECK(helikon_certificate_covers(&certificate, 21) == 0);
    CHECK(helikon_certificate_covers(&certificate, 0xffffffffu) == 0);
    CHECK(helikon_certificate_covers(NULL, 5) == 0);
    return 0;
}

static int test_certificate_rejects_tampering(void)
{
    CHECK(load_fixtures() == 0);
    uint8_t blob[HELIKON_CERTIFICATE_SIZE];
    struct helikon_key_certificate certificate;

    /* One flipped signature byte. */
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[HELIKON_CERTIFICATE_SIZE - 1u] ^= 0x01u;
    CHECK(helikon_certificate_verify(blob, sizeof(blob), &roots, &certificate) !=
          0);

    /* One flipped key byte: a valid signature over a different key. */
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[24] ^= 0x80u;
    CHECK(helikon_certificate_verify(blob, sizeof(blob), &roots, &certificate) !=
          0);

    /* The range field is inside the signature. */
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[19] = 40u;
    CHECK(helikon_certificate_verify(blob, sizeof(blob), &roots, &certificate) !=
          0);

    /* A shorter or longer blob is not a certificate. */
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    CHECK(helikon_certificate_verify(blob, sizeof(blob) - 1u, &roots,
                                     &certificate) != 0);
    CHECK(helikon_certificate_parse(blob, sizeof(blob) - 1u, &certificate) != 0);
    return 0;
}

static int test_certificate_rejects_bad_versions_and_flags(void)
{
    CHECK(load_fixtures() == 0);
    uint8_t blob[HELIKON_CERTIFICATE_SIZE];
    struct helikon_key_certificate certificate;

    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    CHECK(helikon_certificate_parse(blob, sizeof(blob), &certificate) == 0);

    /* Version 2 of the format is not one this build understands. */
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[3] = 2u;
    CHECK(helikon_certificate_parse(blob, sizeof(blob), &certificate) != 0);

    /* first > last is an empty range, not a wildcard. */
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[12] = 0u;
    blob[15] = 30u;
    CHECK(helikon_certificate_parse(blob, sizeof(blob), &certificate) != 0);

    /* Undefined flag bits are refused, not ignored. */
    CHECK(unhex(certificate_a_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[23] = 0x02u;
    CHECK(helikon_certificate_parse(blob, sizeof(blob), &certificate) != 0);
    return 0;
}

static int test_certificate_rejects_foreign_issuer(void)
{
    CHECK(load_fixtures() == 0);
    uint8_t blob[HELIKON_CERTIFICATE_SIZE];
    CHECK(unhex(certificate_unknown_issuer_hex, blob, sizeof(blob)) ==
          sizeof(blob));

    struct helikon_key_certificate certificate;
    CHECK(helikon_certificate_parse(blob, sizeof(blob), &certificate) == 0);
    CHECK(certificate.issuer_id == 9);
    CHECK(helikon_certificate_verify(blob, sizeof(blob), &roots, &certificate) !=
          0);

    /* An empty root table is not a wildcard either. */
    struct helikon_key_table empty = { NULL, 0 };
    CHECK(helikon_certificate_verify(blob, sizeof(blob), &empty,
                                     &certificate) != 0);
    CHECK(helikon_certificate_verify(blob, sizeof(blob), NULL, &certificate) !=
          0);
    return 0;
}

static int test_revocations(void)
{
    CHECK(load_fixtures() == 0);
    uint8_t blob[HELIKON_REVOCATIONS_SIZE];
    CHECK(unhex(revocations_hex, blob, sizeof(blob)) == sizeof(blob));

    struct helikon_revocations revocations;
    CHECK(helikon_revocations_parse(blob, sizeof(blob), &revocations) == 0);
    CHECK(revocations.version == HELIKON_REVOCATIONS_VERSION);
    CHECK(revocations.serial == 5);
    CHECK(revocations.count == 1);
    CHECK(revocations.ids[0] == 7);
    CHECK(helikon_revocations_contain(&revocations, 7) == 1);
    CHECK(helikon_revocations_contain(&revocations, 8) == 0);
    CHECK(helikon_revocations_contain(NULL, 7) == 0);
    CHECK(helikon_revocations_verify(blob, sizeof(blob), &roots, &revocations) ==
          0);

    /* A revoked id inside the signed region cannot be edited. */
    CHECK(unhex(revocations_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[15] = 8u;
    CHECK(helikon_revocations_verify(blob, sizeof(blob), &roots,
                                     &revocations) != 0);

    /* An unused slot must be zero: two encodings of one set would be a bug. */
    CHECK(unhex(revocations_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[16 + 3] = 9u;
    CHECK(helikon_revocations_parse(blob, sizeof(blob), &revocations) != 0);

    /* A count above the array is refused before it is used. */
    CHECK(unhex(revocations_hex, blob, sizeof(blob)) == sizeof(blob));
    blob[11] = (uint8_t)(HELIKON_REVOCATIONS_MAX + 1u);
    CHECK(helikon_revocations_parse(blob, sizeof(blob), &revocations) != 0);
    return 0;
}

static int test_store_resolves_a_certified_key(void)
{
    CHECK(load_fixtures() == 0);

    uint8_t certificate_a[HELIKON_CERTIFICATE_SIZE];
    CHECK(unhex(certificate_a_hex, certificate_a, sizeof(certificate_a)) ==
          sizeof(certificate_a));
    const uint8_t *certificates[1] = { certificate_a };

    uint8_t message[HELIKON_CERTIFICATE_SIGNED_SIZE];
    CHECK(unhex(message_hex, message, sizeof(message)) == sizeof(message));
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
    CHECK(unhex(message_signature_hex, signature, sizeof(signature)) ==
          sizeof(signature));

    struct helikon_key_store store;
    memset(&store, 0, sizeof(store));
    store.roots = &roots;
    store.certificates = certificates;
    store.certificate_count = ARRAY_SIZE(certificates);
    store.image_version = 5;

    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature, sizeof(signature)) == 0);

    /* The same signature under an id the store does not certify. */
    CHECK(helikon_key_store_verify(&store, 8, message, sizeof(message),
                                   signature, sizeof(signature)) != 0);

    /* A one-byte change to the message. */
    message[0] ^= 0x01u;
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature, sizeof(signature)) != 0);
    return 0;
}

static int test_store_enforces_the_version_range(void)
{
    CHECK(load_fixtures() == 0);

    uint8_t certificate_a[HELIKON_CERTIFICATE_SIZE];
    uint8_t certificate_b[HELIKON_CERTIFICATE_SIZE];
    CHECK(unhex(certificate_a_hex, certificate_a, sizeof(certificate_a)) ==
          sizeof(certificate_a));
    CHECK(unhex(certificate_b_hex, certificate_b, sizeof(certificate_b)) ==
          sizeof(certificate_b));
    const uint8_t *certificates[2] = { certificate_a, certificate_b };

    uint8_t message[HELIKON_CERTIFICATE_SIGNED_SIZE];
    CHECK(unhex(message_hex, message, sizeof(message)) == sizeof(message));
    uint8_t signature_a[HELIKON_ED25519_SIGNATURE_SIZE];
    CHECK(unhex(message_signature_hex, signature_a, sizeof(signature_a)) ==
          sizeof(signature_a));

    struct helikon_key_store store;
    memset(&store, 0, sizeof(store));
    store.roots = &roots;
    store.certificates = certificates;
    store.certificate_count = ARRAY_SIZE(certificates);

    /* Image 5: key 7 is certified, key 8 starts at 21. */
    store.image_version = 5;
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature_a, sizeof(signature_a)) == 0);
    CHECK(helikon_key_store_verify(&store, 8, message, sizeof(message),
                                   signature_a, sizeof(signature_a)) != 0);

    /* Image 21: the other way round. */
    store.image_version = 21;
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature_a, sizeof(signature_a)) != 0);
    CHECK(helikon_key_store_verify(&store, 8, message, sizeof(message),
                                   signature_a, sizeof(signature_a)) != 0);
    return 0;
}

static int test_store_rotation(void)
{
    CHECK(load_fixtures() == 0);

    uint8_t certificate_a[HELIKON_CERTIFICATE_SIZE];
    uint8_t certificate_a_rotated[HELIKON_CERTIFICATE_SIZE];
    CHECK(unhex(certificate_a_hex, certificate_a, sizeof(certificate_a)) ==
          sizeof(certificate_a));
    CHECK(unhex(certificate_a_rotated_hex, certificate_a_rotated,
                sizeof(certificate_a_rotated)) ==
          sizeof(certificate_a_rotated));
    const uint8_t *certificates[2] = { certificate_a, certificate_a_rotated };

    uint8_t message[HELIKON_CERTIFICATE_SIGNED_SIZE];
    CHECK(unhex(message_hex, message, sizeof(message)) == sizeof(message));
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
    CHECK(unhex(message_signature_hex, signature, sizeof(signature)) ==
          sizeof(signature));

    struct helikon_key_store store;
    memset(&store, 0, sizeof(store));
    store.roots = &roots;
    store.certificates = certificates;
    store.certificate_count = ARRAY_SIZE(certificates);

    /* Same key id, two windows: the covering one answers for that version. */
    for (uint32_t version = 1; version <= 40; version += 3) {
        store.image_version = version;
        CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                       signature, sizeof(signature)) == 0);
    }

    /* Outside both windows the same key is not accepted. */
    store.image_version = 0;
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature, sizeof(signature)) != 0);
    return 0;
}

static int test_store_refuses_a_revoked_key(void)
{
    CHECK(load_fixtures() == 0);

    uint8_t certificate_a[HELIKON_CERTIFICATE_SIZE];
    uint8_t certificate_b[HELIKON_CERTIFICATE_SIZE];
    uint8_t revocations[HELIKON_REVOCATIONS_SIZE];
    CHECK(unhex(certificate_a_hex, certificate_a, sizeof(certificate_a)) ==
          sizeof(certificate_a));
    CHECK(unhex(certificate_b_hex, certificate_b, sizeof(certificate_b)) ==
          sizeof(certificate_b));
    CHECK(unhex(revocations_hex, revocations, sizeof(revocations)) ==
          sizeof(revocations));
    const uint8_t *certificates[2] = { certificate_a, certificate_b };

    uint8_t message[HELIKON_CERTIFICATE_SIGNED_SIZE];
    CHECK(unhex(message_hex, message, sizeof(message)) == sizeof(message));
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
    CHECK(unhex(message_signature_hex, signature, sizeof(signature)) ==
          sizeof(signature));

    struct helikon_key_store store;
    memset(&store, 0, sizeof(store));
    store.roots = &roots;
    store.certificates = certificates;
    store.certificate_count = ARRAY_SIZE(certificates);
    store.revocations = revocations;
    store.image_version = 5;

    /* Key 7 is revoked even though its certificate covers image 5. */
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature, sizeof(signature)) != 0);

    /* An unauthenticated bundle fails closed for every key, not just one. */
    uint8_t tampered[HELIKON_REVOCATIONS_SIZE];
    memcpy(tampered, revocations, sizeof(tampered));
    tampered[HELIKON_REVOCATIONS_SIZE - 1u] ^= 0x01u;
    store.revocations = tampered;
    store.image_version = 21;
    CHECK(helikon_key_store_verify(&store, 8, message, sizeof(message),
                                   signature, sizeof(signature)) != 0);
    return 0;
}

static int test_store_rejects_bad_arguments(void)
{
    uint8_t certificate_a[HELIKON_CERTIFICATE_SIZE];
    CHECK(unhex(certificate_a_hex, certificate_a, sizeof(certificate_a)) ==
          sizeof(certificate_a));
    const uint8_t *certificates[1] = { certificate_a };
    uint8_t message[8];
    memset(message, 0, sizeof(message));
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
    memset(signature, 0, sizeof(signature));

    struct helikon_key_store store;
    memset(&store, 0, sizeof(store));
    store.roots = &roots;
    store.certificates = certificates;
    store.certificate_count = ARRAY_SIZE(certificates);
    store.image_version = 5;

    /* No roots at all: nothing verifies. */
    struct helikon_key_table empty = { NULL, 0 };
    store.roots = &empty;
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature, sizeof(signature)) != 0);

    /* No certificates at all, and a NULL table with a non-zero count. */
    store.roots = &roots;
    store.certificate_count = 0;
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature, sizeof(signature)) != 0);
    store.certificates = NULL;
    store.certificate_count = 1;
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature, sizeof(signature)) != 0);

    /* NULL context and a short signature are refused before any lookup. */
    CHECK(helikon_key_store_verify(NULL, 7, message, sizeof(message), signature,
                                   sizeof(signature)) != 0);
    store.certificates = certificates;
    store.certificate_count = 1;
    CHECK(helikon_key_store_verify(&store, 7, message, sizeof(message),
                                   signature, sizeof(signature) - 1u) != 0);
    return 0;
}

int main(void)
{
    static const struct test_case {
        const char *name;
        int (*run)(void);
    } tests[] = {
        { "certificate parses and verifies", test_certificate_parses_and_verifies },
        { "certificate version range", test_certificate_range },
        { "certificate tampering", test_certificate_rejects_tampering },
        { "certificate version and flags", test_certificate_rejects_bad_versions_and_flags },
        { "certificate foreign issuer", test_certificate_rejects_foreign_issuer },
        { "revocations", test_revocations },
        { "store resolves a certified key", test_store_resolves_a_certified_key },
        { "store version range", test_store_enforces_the_version_range },
        { "store rotation", test_store_rotation },
        { "store revoked key", test_store_refuses_a_revoked_key },
        { "store bad arguments", test_store_rejects_bad_arguments }
    };
    int failures = 0;
    for (size_t i = 0; i < ARRAY_SIZE(tests); ++i) {
        int rc = tests[i].run();
        if (rc == 0) {
            printf("PASS %s\n", tests[i].name);
        } else {
            printf("FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("%zu tests passed\n", ARRAY_SIZE(tests));
    return 0;
}
