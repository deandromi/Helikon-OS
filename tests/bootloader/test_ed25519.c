/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ed25519.h"
#include "keys.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct vector {
    const char *public_key;
    const char *message;
    const char *signature;
};

/* RFC 8032 section 7.1, TEST 1-3. */
static const struct vector rfc8032[] = {
    {
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        "",
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"
    },
    {
        "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
        "72",
        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
        "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"
    },
    {
        "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
        "af82",
        "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac"
        "18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"
    },
    /*
     * A fixed 192-byte manifest-length vector, generated once with a
     * throwaway key. Only the public key and signature are retained; this
     * catches buffer-size and multi-block SHA-512 regressions.
     */
    {
        "ae02e65dc76ffb06954e48d4a9fb06e6d9e755cef91803937d82140019f96346",
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f"
        "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f"
        "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
        "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf",
        "a62db3d4baa36e7629d11fee2b7c10eeacb72366c7f7e303f201f811d361ece0"
        "a06e1e7af80cdc65f87ae94336f21a8164fb1c79353664591fc629e0fc266109"
    }
};

static int nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int from_hex(const char *hex, uint8_t *out, size_t out_size)
{
    size_t length = strlen(hex);
    if ((length & 1u) != 0 || length / 2u != out_size)
        return -1;
    for (size_t i = 0; i < out_size; ++i) {
        int high = nibble(hex[i * 2u]);
        int low = nibble(hex[i * 2u + 1u]);
        if (high < 0 || low < 0)
            return -1;
        out[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
}

static int decode(const struct vector *v, uint8_t *public_key,
                  uint8_t *message, size_t *message_length, uint8_t *signature)
{
    size_t units = strlen(v->message);
    if ((units & 1u) != 0 || units / 2u > HELIKON_ED25519_MESSAGE_MAX)
        return -1;
    *message_length = units / 2u;
    if (from_hex(v->public_key, public_key,
                 HELIKON_ED25519_PUBLIC_KEY_SIZE) != 0)
        return -1;
    if (from_hex(v->signature, signature,
                 HELIKON_ED25519_SIGNATURE_SIZE) != 0)
        return -1;
    if (from_hex(v->message, message, *message_length) != 0)
        return -1;
    return 0;
}

static int test_rfc8032_vectors(void)
{
    for (size_t i = 0; i < sizeof(rfc8032) / sizeof(rfc8032[0]); ++i) {
        uint8_t public_key[HELIKON_ED25519_PUBLIC_KEY_SIZE];
        uint8_t message[HELIKON_ED25519_MESSAGE_MAX];
        uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
        size_t message_length = 0;
        CHECK(decode(&rfc8032[i], public_key, message, &message_length,
                     signature) == 0);
        CHECK(helikon_ed25519_verify(public_key, message, message_length,
                                     signature) == 0);
    }
    return 0;
}

static int test_tampering(void)
{
    uint8_t public_key[HELIKON_ED25519_PUBLIC_KEY_SIZE];
    uint8_t message[HELIKON_ED25519_MESSAGE_MAX];
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
    size_t message_length = 0;
    CHECK(decode(&rfc8032[2], public_key, message, &message_length,
                 signature) == 0);

    uint8_t broken[HELIKON_ED25519_SIGNATURE_SIZE];
    memcpy(broken, signature, sizeof(broken));
    broken[0] ^= 1u;
    CHECK(helikon_ed25519_verify(public_key, message, message_length,
                                 broken) != 0);

    memcpy(broken, signature, sizeof(broken));
    broken[HELIKON_ED25519_SIGNATURE_SIZE - 1u] ^= 0x80u;
    CHECK(helikon_ed25519_verify(public_key, message, message_length,
                                 broken) != 0);

    uint8_t changed[HELIKON_ED25519_MESSAGE_MAX];
    memcpy(changed, message, message_length);
    changed[0] ^= 1u;
    CHECK(helikon_ed25519_verify(public_key, changed, message_length,
                                 signature) != 0);

    public_key[0] ^= 1u;
    CHECK(helikon_ed25519_verify(public_key, message, message_length,
                                 signature) != 0);
    return 0;
}

static int test_rejects_bad_arguments(void)
{
    uint8_t public_key[HELIKON_ED25519_PUBLIC_KEY_SIZE] = { 0 };
    uint8_t message[HELIKON_ED25519_MESSAGE_MAX] = { 0 };
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE] = { 0 };
    CHECK(helikon_ed25519_verify(NULL, message, 0, signature) != 0);
    CHECK(helikon_ed25519_verify(public_key, NULL, 1, signature) != 0);
    CHECK(helikon_ed25519_verify(public_key, message, 0, NULL) != 0);
    CHECK(helikon_ed25519_verify(public_key, message,
                                 HELIKON_ED25519_MESSAGE_MAX + 1u,
                                 signature) != 0);
    CHECK(helikon_ed25519_verify(public_key, message, 0, signature) != 0);
    return 0;
}

static int test_key_table(void)
{
    uint8_t public_key[HELIKON_ED25519_PUBLIC_KEY_SIZE];
    uint8_t message[HELIKON_ED25519_MESSAGE_MAX];
    uint8_t signature[HELIKON_ED25519_SIGNATURE_SIZE];
    size_t message_length = 0;
    CHECK(decode(&rfc8032[0], public_key, message, &message_length,
                 signature) == 0);

    struct helikon_public_key keys[1];
    memset(keys, 0, sizeof(keys));
    keys[0].id = 7;
    memcpy(keys[0].key, public_key, sizeof(public_key));
    struct helikon_key_table table = { keys, 1 };

    CHECK(helikon_key_table_verify(&table, 7, message, message_length,
                                   signature, sizeof(signature)) == 0);
    CHECK(helikon_key_table_verify(&table, 8, message, message_length,
                                   signature, sizeof(signature)) != 0);

    uint8_t broken[HELIKON_ED25519_SIGNATURE_SIZE];
    memcpy(broken, signature, sizeof(broken));
    broken[0] ^= 1u;
    CHECK(helikon_key_table_verify(&table, 7, message, message_length,
                                   broken, sizeof(broken)) != 0);

    struct helikon_key_table empty = { NULL, 0 };
    CHECK(helikon_key_table_verify(&empty, 7, message, message_length,
                                   signature, sizeof(signature)) != 0);
    CHECK(helikon_key_table_verify(NULL, 7, message, message_length,
                                   signature, sizeof(signature)) != 0);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "RFC 8032 vectors", test_rfc8032_vectors },
        { "tampered signatures", test_tampering },
        { "bad arguments", test_rejects_bad_arguments },
        { "public-key table", test_key_table }
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
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
    printf("%zu tests passed\n", sizeof(tests) / sizeof(tests[0]));
    return 0;
}
