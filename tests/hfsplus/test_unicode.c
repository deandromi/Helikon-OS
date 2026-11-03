/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_unicode.h"
#include "test_check.h"

#include <stdint.h>
#include <string.h>

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static int test_ascii(void)
{
    static const uint8_t source[] = {
        0, 'A', 0, '.', 0, 'b', 0, 'i', 0, 'n'
    };
    char out[16];
    int rc = hfs_utf16be_to_utf8(source, 5, out, sizeof(out));
    CHECK(rc == HFS_OK);
    CHECK(strcmp(out, "A.bin") == 0);
    return 0;
}

static int test_two_byte(void)
{
    static const uint8_t source[] = { 0x00, 0xe9 };
    char out[8];
    int rc = hfs_utf16be_to_utf8(source, 1, out, sizeof(out));
    CHECK(rc == HFS_OK);
    CHECK((unsigned char)out[0] == 0xc3);
    CHECK((unsigned char)out[1] == 0xa9);
    CHECK(out[2] == '\0');
    return 0;
}

static int test_three_byte(void)
{
    static const uint8_t source[] = { 0x20, 0xac };
    char out[8];
    int rc = hfs_utf16be_to_utf8(source, 1, out, sizeof(out));
    CHECK(rc == HFS_OK);
    CHECK((unsigned char)out[0] == 0xe2);
    CHECK((unsigned char)out[1] == 0x82);
    CHECK((unsigned char)out[2] == 0xac);
    CHECK(out[3] == '\0');
    return 0;
}

static int test_surrogate_pair(void)
{
    static const uint8_t source[] = { 0xd8, 0x3d, 0xde, 0x00 };
    char out[8];
    int rc = hfs_utf16be_to_utf8(source, 2, out, sizeof(out));
    CHECK(rc == HFS_OK);
    CHECK((unsigned char)out[0] == 0xf0);
    CHECK((unsigned char)out[1] == 0x9f);
    CHECK((unsigned char)out[2] == 0x98);
    CHECK((unsigned char)out[3] == 0x80);
    CHECK(out[4] == '\0');
    return 0;
}

static int test_unpaired_surrogates(void)
{
    static const uint8_t high[] = { 0xd8, 0x3d };
    static const uint8_t low[] = { 0xde, 0x00 };
    char out[8];
    CHECK(hfs_utf16be_to_utf8(high, 1, out, sizeof(out)) ==
          HFS_ERR_NAME_INVALID);
    CHECK(hfs_utf16be_to_utf8(low, 1, out, sizeof(out)) ==
          HFS_ERR_NAME_INVALID);
    return 0;
}

static int test_nul_rejected(void)
{
    static const uint8_t source[] = { 0x00, 0x00 };
    char out[8];
    CHECK(hfs_utf16be_to_utf8(source, 1, out, sizeof(out)) ==
          HFS_ERR_NAME_INVALID);
    return 0;
}

static int test_buffer_too_small(void)
{
    uint8_t source[8];
    char out[2];
    put16(source, 'A');
    put16(source + 2, 'B');
    CHECK(hfs_utf16be_to_utf8(source, 2, out, sizeof(out)) ==
          HFS_ERR_BUFFER);
    return 0;
}

static int test_empty_name(void)
{
    char out[8];
    int rc = hfs_utf16be_to_utf8(NULL, 0, out, sizeof(out));
    CHECK(rc == HFS_OK);
    CHECK(out[0] == '\0');
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"ascii", test_ascii},
        {"two byte", test_two_byte},
        {"three byte", test_three_byte},
        {"surrogate pair", test_surrogate_pair},
        {"unpaired surrogates", test_unpaired_surrogates},
        {"NUL rejected", test_nul_rejected},
        {"buffer too small", test_buffer_too_small},
        {"empty name", test_empty_name}
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
