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

static int test_escaped_decode(void)
{
    static const uint8_t nul[] = { 0, 0 };
    static const uint8_t lone[] = { 0xd8, 0x3d };
    static const uint8_t pair[] = { 0xd8, 0x3d, 0xde, 0x00 };
    char out[16];
    int rc = hfs_utf16be_to_utf8_escaped(nul, 1, out, sizeof(out));
    CHECK(rc == HFS_OK);
    CHECK((unsigned char)out[0] == 0xe2);
    CHECK((unsigned char)out[1] == 0x90);
    CHECK((unsigned char)out[2] == 0x80);
    CHECK(out[3] == '\0');

    rc = hfs_utf16be_to_utf8_escaped(lone, 1, out, sizeof(out));
    CHECK(rc == HFS_OK);
    CHECK((unsigned char)out[0] == 0xef);
    CHECK((unsigned char)out[1] == 0xbf);
    CHECK((unsigned char)out[2] == 0xbd);
    CHECK(out[3] == '\0');

    rc = hfs_utf16be_to_utf8_escaped(pair, 2, out, sizeof(out));
    CHECK(rc == HFS_OK);
    CHECK((unsigned char)out[0] == 0xf0);
    CHECK((unsigned char)out[1] == 0x9f);
    CHECK((unsigned char)out[2] == 0x98);
    CHECK((unsigned char)out[3] == 0x80);
    CHECK(out[4] == '\0');
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

static int test_utf8_ascii(void)
{
    uint8_t out[16];
    size_t units = 0;
    int rc = hfs_utf8_to_utf16be("A.bin", 5, out, sizeof(out), &units);
    CHECK(rc == HFS_OK);
    CHECK(units == 5);
    static const uint8_t expected[] = { 0, 'A', 0, '.', 0, 'b', 0, 'i', 0, 'n' };
    CHECK(memcmp(out, expected, sizeof(expected)) == 0);
    return 0;
}

static int test_utf8_round_trip(void)
{
    /* One name per length class, plus the boundaries either side of them. */
    static const char *const names[] = {
        "Z", "a", "file.txt", "Caf\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
        "\xe4\xb8\xad\xe6\x96\x87", "\xed\x95\x9c\xea\xb8\x80"
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        uint8_t units_bytes[512];
        size_t units = 0;
        int rc = hfs_utf8_to_utf16be(names[i], strlen(names[i]), units_bytes,
                                     sizeof(units_bytes), &units);
        CHECK(rc == HFS_OK);
        char back[256];
        rc = hfs_utf16be_to_utf8(units_bytes, units, back, sizeof(back));
        CHECK(rc == HFS_OK);
        CHECK(strcmp(back, names[i]) == 0);
    }
    return 0;
}

static int test_utf8_surrogate_pair(void)
{
    uint8_t out[8];
    size_t units = 0;
    int rc = hfs_utf8_to_utf16be("\xf0\x9f\x98\x80", 4, out, sizeof(out),
                                 &units);
    CHECK(rc == HFS_OK);
    CHECK(units == 2);
    CHECK(out[0] == 0xd8 && out[1] == 0x3d);
    CHECK(out[2] == 0xde && out[3] == 0x00);
    return 0;
}

static int test_utf8_rejects_invalid(void)
{
    uint8_t out[16];
    size_t units = 0;
    /* Overlong '/', a surrogate half, a lone continuation and a truncated
     * sequence: none of them is a name HFS+ could store. */
    CHECK(hfs_utf8_to_utf16be("\xc0\xaf", 2, out, sizeof(out), &units) ==
          HFS_ERR_NAME_INVALID);
    CHECK(hfs_utf8_to_utf16be("\xed\xa0\x80", 3, out, sizeof(out), &units) ==
          HFS_ERR_NAME_INVALID);
    CHECK(hfs_utf8_to_utf16be("\x80", 1, out, sizeof(out), &units) ==
          HFS_ERR_NAME_INVALID);
    CHECK(hfs_utf8_to_utf16be("\xe2\x82", 2, out, sizeof(out), &units) ==
          HFS_ERR_NAME_INVALID);
    CHECK(hfs_utf8_to_utf16be("\xf4\x90\x80\x80", 4, out, sizeof(out),
                              &units) == HFS_ERR_NAME_INVALID);
    CHECK(hfs_utf8_to_utf16be("a", 0, out, sizeof(out), &units) ==
          HFS_ERR_NAME_INVALID);
    CHECK(hfs_utf8_to_utf16be(NULL, 1, out, sizeof(out), &units) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_utf8_to_utf16be("a", 1, NULL, sizeof(out), &units) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_utf8_to_utf16be("a", 1, out, sizeof(out), NULL) ==
          HFS_ERR_ARGUMENT);
    return 0;
}

static int test_utf8_limits(void)
{
    char name[300];
    uint8_t units_bytes[600];
    size_t units = 0;

    /* 255 units is the longest HFS+ key; the byte buffer has to be big enough
     * as well, and either limit reports itself rather than truncating. */
    memset(name, 'a', 255);
    CHECK(hfs_utf8_to_utf16be(name, 255, units_bytes, sizeof(units_bytes),
                              &units) == HFS_OK);
    CHECK(units == 255);

    memset(name, 'a', 256);
    CHECK(hfs_utf8_to_utf16be(name, 256, units_bytes, sizeof(units_bytes),
                              &units) == HFS_ERR_NAME_INVALID);

    CHECK(hfs_utf8_to_utf16be("abc", 3, units_bytes, 4, &units) ==
          HFS_ERR_BUFFER);
    CHECK(hfs_utf8_to_utf16be("abc", 3, units_bytes, 0, &units) ==
          HFS_ERR_BUFFER);
    return 0;
}

static int test_utf8_matches_normalizer(void)
{
    /*
     * The encoder's output is what the normalizer takes, so a composed name and
     * its decomposed form have to end at the same catalog key through both.
     */
    static const char composed[] = "Caf\xc3\xa9";
    static const char decomposed[] = "Cafe\xcc\x81";
    uint8_t a[512];
    uint8_t b[512];
    uint8_t canon_a[512];
    uint8_t canon_b[512];
    size_t units_a = 0;
    size_t units_b = 0;
    size_t canon_units_a = 0;
    size_t canon_units_b = 0;

    CHECK(hfs_utf8_to_utf16be(composed, strlen(composed), a, sizeof(a),
                              &units_a) == HFS_OK);
    CHECK(hfs_utf8_to_utf16be(decomposed, strlen(decomposed), b, sizeof(b),
                              &units_b) == HFS_OK);
    CHECK(hfs_unicode_normalize_name(a, units_a, canon_a, sizeof(canon_a),
                                     &canon_units_a) == HFS_OK);
    CHECK(hfs_unicode_normalize_name(b, units_b, canon_b, sizeof(canon_b),
                                     &canon_units_b) == HFS_OK);
    CHECK(canon_units_a == canon_units_b);
    CHECK(memcmp(canon_a, canon_b, canon_units_a * 2) == 0);
    return 0;
}

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
        {"normalize limits", test_normalize_limits},
        {"escaped decode", test_escaped_decode},
        {"utf8 ascii", test_utf8_ascii},
        {"utf8 round trip", test_utf8_round_trip},
        {"utf8 surrogate pair", test_utf8_surrogate_pair},
        {"utf8 rejects invalid", test_utf8_rejects_invalid},
        {"utf8 limits", test_utf8_limits},
        {"utf8 matches normalizer", test_utf8_matches_normalizer}
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
