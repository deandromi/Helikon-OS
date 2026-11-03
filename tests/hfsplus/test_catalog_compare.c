/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog_compare.h"
#include "test_check.h"

#include <stdint.h>
#include <string.h>

static size_t build_key(uint8_t *key, uint32_t parent, const uint8_t *name,
                        size_t units)
{
    key[0] = (uint8_t)(parent >> 24);
    key[1] = (uint8_t)(parent >> 16);
    key[2] = (uint8_t)(parent >> 8);
    key[3] = (uint8_t)parent;
    key[4] = (uint8_t)(units >> 8);
    key[5] = (uint8_t)units;
    if (units != 0)
        memcpy(key + 6, name, units * 2u);
    return 6u + units * 2u;
}

static int test_case_insensitive(void)
{
    static const uint8_t upper[] = {
        0, 'A', 0, 'l', 0, 'p', 0, 'h', 0, 'a'
    };
    static const uint8_t lower[] = {
        0, 'a', 0, 'l', 0, 'p', 0, 'h', 0, 'a'
    };
    uint8_t a[16];
    uint8_t b[16];
    size_t a_length = build_key(a, 2, upper, 5);
    size_t b_length = build_key(b, 2, lower, 5);
    CHECK(hfs_catalog_key_compare(NULL, a, a_length, b, b_length) == 0);
    return 0;
}

static int test_parent_orders_first(void)
{
    static const uint8_t name[] = { 0, 'z' };
    uint8_t a[8];
    uint8_t b[8];
    size_t a_length = build_key(a, 2, name, 1);
    size_t b_length = build_key(b, 3, NULL, 0);
    CHECK(hfs_catalog_key_compare(NULL, a, a_length, b, b_length) < 0);
    CHECK(hfs_catalog_key_compare(NULL, b, b_length, a, a_length) > 0);
    return 0;
}

static int test_empty_name_first(void)
{
    static const uint8_t name[] = { 0, 'a' };
    uint8_t a[8];
    uint8_t b[8];
    size_t a_length = build_key(a, 2, NULL, 0);
    size_t b_length = build_key(b, 2, name, 1);
    CHECK(hfs_catalog_key_compare(NULL, a, a_length, b, b_length) < 0);
    return 0;
}

static int test_ignorable_equals_empty(void)
{
    static const uint8_t mark[] = { 0x20, 0x0c };
    uint8_t a[8];
    uint8_t b[8];
    size_t a_length = build_key(a, 2, mark, 1);
    size_t b_length = build_key(b, 2, NULL, 0);
    CHECK(hfs_catalog_key_compare(NULL, a, a_length, b, b_length) == 0);
    return 0;
}

static int test_composed_differs(void)
{
    static const uint8_t composed[] = { 0x00, 0xe9 };
    static const uint8_t decomposed[] = { 0, 'e', 0x03, 0x01 };
    uint8_t a[16];
    uint8_t b[16];
    size_t a_length = build_key(a, 2, composed, 1);
    size_t b_length = build_key(b, 2, decomposed, 2);
    CHECK(hfs_catalog_key_compare(NULL, a, a_length, b, b_length) != 0);
    return 0;
}

static int test_malformed_keys(void)
{
    static const uint8_t valid[] = { 0, 'a' };
    uint8_t a[8];
    uint8_t b[8];
    size_t b_length = build_key(b, 2, valid, 1);
    memset(a, 0, sizeof(a));
    CHECK(hfs_catalog_key_compare(NULL, a, 3, b, b_length) < 0);
    CHECK(hfs_catalog_key_compare(NULL, b, b_length, a, 3) > 0);

    /* Declared name length exceeds the available key bytes. */
    a[0] = 0;
    a[1] = 0;
    a[2] = 0;
    a[3] = 2;
    a[4] = 0;
    a[5] = 9;
    a[6] = 0;
    a[7] = 'a';
    int cmp = hfs_catalog_key_compare(NULL, a, 8, b, b_length);
    CHECK(cmp == 0 || cmp == -1 || cmp == 1);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "case-insensitive names", test_case_insensitive },
        { "parent orders first", test_parent_orders_first },
        { "empty name first", test_empty_name_first },
        { "ignorable equals empty", test_ignorable_equals_empty },
        { "composed differs", test_composed_differs },
        { "malformed keys", test_malformed_keys }
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
