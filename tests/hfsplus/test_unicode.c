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

static int test_compare_ascii_case(void)
{
    static const uint8_t upper[] = { 0, 'F', 0, 'i', 0, 'l', 0, 'e' };
    static const uint8_t lower[] = { 0, 'f', 0, 'i', 0, 'l', 0, 'e' };
    static const uint8_t a[] = { 0, 'a' };
    static const uint8_t b[] = { 0, 'b' };
    CHECK(hfs_fast_unicode_compare(upper, 4, lower, 4) == 0);
    CHECK(hfs_fast_unicode_compare(a, 1, b, 1) < 0);
    CHECK(hfs_fast_unicode_compare(b, 1, a, 1) > 0);
    CHECK(hfs_fast_unicode_compare(NULL, 0, NULL, 0) == 0);
    CHECK(hfs_fast_unicode_compare(NULL, 0, a, 1) < 0);
    CHECK(hfs_fast_unicode_compare(a, 1, NULL, 0) > 0);
    return 0;
}

static int test_compare_latin1(void)
{
    static const uint8_t ae_upper[] = { 0x00, 0xc6 };
    static const uint8_t ae_lower[] = { 0x00, 0xe6 };
    static const uint8_t grave_upper[] = { 0x00, 0xc0 };
    static const uint8_t grave_lower[] = { 0x00, 0xe0 };
    static const uint8_t decomposed_upper[] = { 0, 'A', 0x03, 0x00 };
    static const uint8_t decomposed_lower[] = { 0, 'a', 0x03, 0x00 };
    CHECK(hfs_fast_unicode_compare(ae_upper, 1, ae_lower, 1) == 0);
    CHECK(hfs_fast_unicode_compare(grave_upper, 1, grave_lower, 1) != 0);
    CHECK(hfs_fast_unicode_compare(decomposed_upper, 2,
                                   decomposed_lower, 2) == 0);
    return 0;
}

static int test_compare_ignorable(void)
{
    static const uint8_t with_mark[] = {
        0, 'a', 0x20, 0x0c, 0, 'b'
    };
    static const uint8_t plain[] = { 0, 'a', 0, 'b' };
    static const uint8_t only_mark[] = { 0x20, 0x0c };
    CHECK(hfs_fast_unicode_compare(with_mark, 3, plain, 2) == 0);
    CHECK(hfs_fast_unicode_compare(only_mark, 1, NULL, 0) == 0);
    return 0;
}

static int test_compare_nul_sentinel(void)
{
    static const uint8_t with_nul[] = { 0, 'A', 0, 0, 0, 'B' };
    static const uint8_t plain[] = { 0, 'A', 0, 'B' };
    CHECK(hfs_fast_unicode_compare(with_nul, 3, plain, 2) != 0);
    return 0;
}

static int test_compare_prefix(void)
{
    static const uint8_t short_name[] = { 0, 'a', 0, 'b' };
    static const uint8_t long_name[] = { 0, 'a', 0, 'b', 0, 'c' };
    CHECK(hfs_fast_unicode_compare(short_name, 2, long_name, 3) < 0);
    CHECK(hfs_fast_unicode_compare(long_name, 3, short_name, 2) > 0);
    return 0;
}

static int test_compare_greek(void)
{
    static const uint8_t capital[] = { 0x03, 0x91 };
    static const uint8_t small[] = { 0x03, 0xb1 };
    static const uint8_t final_sigma[] = { 0x03, 0xc2 };
    static const uint8_t sigma[] = { 0x03, 0xc3 };
    CHECK(hfs_fast_unicode_compare(capital, 1, small, 1) == 0);
    CHECK(hfs_fast_unicode_compare(final_sigma, 1, sigma, 1) != 0);
    return 0;
}

static int test_compare_circled_exception(void)
{
    static const uint8_t capital[] = { 0x24, 0xb6 };
    static const uint8_t small[] = { 0x24, 0xd0 };
    CHECK(hfs_fast_unicode_compare(capital, 1, small, 1) != 0);
    return 0;
}

static int test_compare_surrogates(void)
{
    static const uint8_t emoji[] = { 0xd8, 0x3d, 0xde, 0x00 };
    static const uint8_t same[] = { 0xd8, 0x3d, 0xde, 0x00 };
    static const uint8_t bmp[] = { 0xff, 0xfd };
    CHECK(hfs_fast_unicode_compare(emoji, 2, same, 2) == 0);
    CHECK(hfs_fast_unicode_compare(emoji, 2, bmp, 1) < 0);
    return 0;
}

static size_t normalize(const uint8_t *name, size_t units, uint8_t *dest,
                        size_t capacity)
{
    size_t result = 0;
    int rc = hfs_unicode_normalize_name(name, units, dest, capacity, &result);
    if (rc != HFS_OK)
        return 0;
    return result;
}

static int test_normalize_composed(void)
{
    static const uint8_t composed[] = { 0x00, 0xe9 };
    static const uint8_t expected[] = { 0, 'e', 0x03, 0x01 };
    static const uint8_t upper[] = { 0x00, 0xc0 };
    static const uint8_t upper_expected[] = { 0, 'A', 0x03, 0x00 };
    static const uint8_t lower[] = { 0x00, 0xe0 };
    static const uint8_t lower_expected[] = { 0, 'a', 0x03, 0x00 };
    uint8_t out[16];
    size_t units = normalize(composed, 1, out, sizeof(out));
    CHECK(units == 2);
    CHECK(memcmp(out, expected, sizeof(expected)) == 0);

    uint8_t upper_out[16];
    uint8_t lower_out[16];
    size_t upper_units = normalize(upper, 1, upper_out, sizeof(upper_out));
    size_t lower_units = normalize(lower, 1, lower_out, sizeof(lower_out));
    CHECK(upper_units == 2);
    CHECK(lower_units == 2);
    CHECK(memcmp(upper_out, upper_expected, sizeof(upper_expected)) == 0);
    CHECK(memcmp(lower_out, lower_expected, sizeof(lower_expected)) == 0);
    CHECK(hfs_fast_unicode_compare(upper_out, upper_units,
                                   lower_out, lower_units) == 0);
    return 0;
}

static int test_normalize_decomposed_unchanged(void)
{
    static const uint8_t decomposed[] = { 0, 'e', 0x03, 0x01 };
    uint8_t out[16];
    size_t units = normalize(decomposed, 2, out, sizeof(out));
    CHECK(units == 2);
    CHECK(memcmp(out, decomposed, sizeof(decomposed)) == 0);
    return 0;
}

static int test_normalize_hangul(void)
{
    static const uint8_t ga[] = { 0xac, 0x00 };
    static const uint8_t gag[] = { 0xac, 0x01 };
    static const uint8_t ga_expected[] = { 0x11, 0x00, 0x11, 0x61 };
    static const uint8_t gag_expected[] = {
        0x11, 0x00, 0x11, 0x61, 0x11, 0xa8
    };
    uint8_t out[16];
    size_t units = normalize(ga, 1, out, sizeof(out));
    CHECK(units == 2);
    CHECK(memcmp(out, ga_expected, sizeof(ga_expected)) == 0);
    units = normalize(gag, 1, out, sizeof(out));
    CHECK(units == 3);
    CHECK(memcmp(out, gag_expected, sizeof(gag_expected)) == 0);
    return 0;
}

static int test_normalize_order(void)
{
    static const uint8_t unsorted[] = {
        0, 'a', 0x03, 0x01, 0x03, 0x23
    };
    static const uint8_t sorted[] = {
        0, 'a', 0x03, 0x23, 0x03, 0x01
    };
    uint8_t out[16];
    size_t units = normalize(unsorted, 3, out, sizeof(out));
    CHECK(units == 3);
    CHECK(memcmp(out, sorted, sizeof(sorted)) == 0);
    return 0;
}

static int test_normalize_excluded(void)
{
    static const uint8_t ohm[] = { 0x21, 0x26 };
    static const uint8_t cjk[] = { 0xf9, 0x00 };
    uint8_t out[16];
    size_t units = normalize(ohm, 1, out, sizeof(out));
    CHECK(units == 1);
    CHECK(memcmp(out, ohm, sizeof(ohm)) == 0);
    units = normalize(cjk, 1, out, sizeof(out));
    CHECK(units == 1);
    CHECK(memcmp(out, cjk, sizeof(cjk)) == 0);
    return 0;
}

static int test_normalize_invalid(void)
{
    static const uint8_t unpaired[] = { 0xd8, 0x3d };
    static const uint8_t nul[] = { 0, 'a', 0, 0 };
    static const uint8_t only_mark[] = { 0x20, 0x0c };
    static const uint8_t emoji[] = { 0xd8, 0x3d, 0xde, 0x00 };
    uint8_t out[16];
    size_t units = 0;
    CHECK(hfs_unicode_normalize_name(unpaired, 1, out, sizeof(out),
                                     &units) == HFS_ERR_NAME_INVALID);
    CHECK(hfs_unicode_normalize_name(nul, 2, out, sizeof(out), &units) ==
          HFS_ERR_NAME_INVALID);
    CHECK(hfs_unicode_normalize_name(only_mark, 1, out, sizeof(out),
                                     &units) == HFS_ERR_NAME_INVALID);
    CHECK(hfs_unicode_normalize_name(emoji, 2, out, sizeof(out), &units) ==
          HFS_OK);
    CHECK(units == 2);
    return 0;
}

static int test_normalize_limits(void)
{
    uint8_t name[255 * 2];
    uint8_t out[255 * 2];
    size_t units = 0;
    for (size_t i = 0; i < 254; ++i)
        put16(name + i * 2, 'a');
    put16(name + 254 * 2, 0x00e9);
    CHECK(hfs_unicode_normalize_name(name, 255, out, sizeof(out), &units) ==
          HFS_ERR_NAME_INVALID);
    put16(name + 254 * 2, 'b');
    CHECK(hfs_unicode_normalize_name(name, 255, out, sizeof(out), &units) ==
          HFS_OK);
    CHECK(units == 255);
    CHECK(hfs_unicode_normalize_name(name, 255, out, 2, &units) ==
          HFS_ERR_BUFFER);
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
        {"empty name", test_empty_name},
        {"compare ASCII case", test_compare_ascii_case},
        {"compare Latin-1", test_compare_latin1},
        {"compare ignorable", test_compare_ignorable},
        {"compare NUL sentinel", test_compare_nul_sentinel},
        {"compare prefix", test_compare_prefix},
        {"compare Greek", test_compare_greek},
        {"compare circled exception", test_compare_circled_exception},
        {"compare surrogates", test_compare_surrogates},
        {"normalize composed", test_normalize_composed},
        {"normalize decomposed unchanged", test_normalize_decomposed_unchanged},
        {"normalize Hangul", test_normalize_hangul},
        {"normalize order", test_normalize_order},
        {"normalize excluded", test_normalize_excluded},
        {"normalize invalid", test_normalize_invalid},
        {"normalize limits", test_normalize_limits}
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
