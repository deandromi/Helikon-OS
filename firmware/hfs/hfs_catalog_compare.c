/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog_compare.h"

#include "hfs_unicode.h"

#include <string.h>

#define CATALOG_KEY_HEADER 6u

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int raw_compare(const uint8_t *a, size_t a_length,
                       const uint8_t *b, size_t b_length)
{
    if (a == NULL)
        a_length = 0;
    if (b == NULL)
        b_length = 0;
    size_t common = a_length < b_length ? a_length : b_length;
    int result = common == 0 ? 0 : memcmp(a, b, common);
    if (result != 0)
        return result < 0 ? -1 : 1;
    if (a_length != b_length)
        return a_length < b_length ? -1 : 1;
    return 0;
}

int hfs_catalog_name_compare(const uint8_t *a, size_t a_units,
                             const uint8_t *b, size_t b_units)
{
    return hfs_fast_unicode_compare(a, a_units, b, b_units);
}

int hfs_catalog_key_compare(void *context, const uint8_t *a, size_t a_length,
                            const uint8_t *b, size_t b_length)
{
    (void)context;
    if (a == NULL)
        a_length = 0;
    if (b == NULL)
        b_length = 0;
    if (a_length < CATALOG_KEY_HEADER || b_length < CATALOG_KEY_HEADER)
        return raw_compare(a, a_length, b, b_length);

    uint32_t parent_a = be32(a);
    uint32_t parent_b = be32(b);
    if (parent_a != parent_b)
        return parent_a < parent_b ? -1 : 1;

    uint16_t units_a = be16(a + 4);
    uint16_t units_b = be16(b + 4);
    size_t available_a = (a_length - CATALOG_KEY_HEADER) / 2u;
    size_t available_b = (b_length - CATALOG_KEY_HEADER) / 2u;
    if ((size_t)units_a > available_a)
        units_a = (uint16_t)available_a;
    if ((size_t)units_b > available_b)
        units_b = (uint16_t)available_b;

    return hfs_catalog_name_compare(a + CATALOG_KEY_HEADER, units_a,
                                    b + CATALOG_KEY_HEADER, units_b);
}
