/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "crc32.h"
#include "endian.h"
#include "fixed.h"
#include "format.h"
#include "mem.h"
#include "string.h"

#include "test_check.h"

int main(void)
{
    char source[] = "abcdef";
    char destination[8] = { 0 };
    helikon_memcpy(destination, source, sizeof(source));
    CHECK(helikon_memcmp(destination, source, sizeof(source)) == 0);

    char overlap[] = "0123456789";
    helikon_memmove(overlap + 2, overlap, 5);
    CHECK(helikon_memcmp(overlap, "0101234789", 10) == 0);

    uint8_t fill[8];
    helikon_memset(fill, 0x5a, sizeof(fill));
    for (size_t i = 0; i < sizeof(fill); ++i)
        CHECK(fill[i] == 0x5a);

    CHECK(helikon_memcmp("abc", "abd", 3) < 0);
    CHECK(helikon_memcmp("abd", "abc", 3) > 0);
    CHECK(helikon_memcmp("abc", "abc", 3) == 0);

    CHECK_EQ(3, helikon_strlen("hello", 3));
    CHECK_EQ(5, helikon_strlen("hello", 10));
    CHECK_EQ(0, helikon_strlen(NULL, 10));
    CHECK(helikon_strcmp("abc", "abc") == 0);
    CHECK(helikon_strcmp("abc", "abd") < 0);
    CHECK(helikon_strncmp("abcdef", "abcxyz", 3) == 0);
    CHECK(helikon_strncmp("abcdef", "abcxyz", 4) != 0);

    char copy[4];
    CHECK_EQ(6, helikon_strlcpy(copy, "abcdef", sizeof(copy)));
    CHECK(helikon_strcmp(copy, "abc") == 0);

    uint8_t bytes[8];
    helikon_put_be32(bytes, 0x12345678u);
    CHECK_EQ(0x12345678u, helikon_be32(bytes));
    helikon_put_le64(bytes, 0x1122334455667788ull);
    CHECK(helikon_le64(bytes) == 0x1122334455667788ull);

    CHECK_EQ(0xcbf43926u, helikon_crc32("123456789", 9));

    int32_t one_and_half = helikon_fixed_mul(helikon_fixed_from_int(3),
                                             HELIKON_FIXED_ONE / 2);
    CHECK_EQ(3 * 65536 / 2, one_and_half);
    CHECK_EQ(2, helikon_fixed_to_int(
                    helikon_fixed_div(helikon_fixed_from_int(3),
                                      helikon_fixed_from_int(3) / 2)));

    char text[16];
    CHECK_EQ(5, helikon_format_u32(text, sizeof(text), 12345u));
    CHECK(helikon_strcmp(text, "12345") == 0);
    CHECK_EQ(3, helikon_format_u32(text, 4, 12345u));
    CHECK(helikon_strcmp(text, "123") == 0);
    CHECK_EQ(3, helikon_format_i32(text, sizeof(text), -42));
    CHECK(helikon_strcmp(text, "-42") == 0);
    CHECK_EQ(8, helikon_format_hex32(text, 16, 0xdeadbeefu, 8));
    CHECK(helikon_strcmp(text, "deadbeef") == 0);
    CHECK_EQ(3, helikon_format_hex32(text, 4, 0xabcdefu, 6));

    return helikon_test_report("lib");
}
