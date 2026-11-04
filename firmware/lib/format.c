/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "format.h"

static size_t copy_digits(char *buffer, size_t size, const char *digits,
                          size_t length)
{
    if (size == 0u)
        return 0;
    size_t copy = length < size - 1u ? length : size - 1u;
    for (size_t i = 0; i < copy; ++i)
        buffer[i] = digits[i];
    buffer[copy] = '\0';
    return copy;
}

size_t helikon_format_u64(char *buffer, size_t size, uint64_t value)
{
    char digits[21];
    size_t length = 0;

    if (value == 0u) {
        digits[length++] = '0';
    } else {
        while (value != 0u) {
            digits[length++] = (char)('0' + (char)(value % 10u));
            value /= 10u;
        }
    }
    for (size_t i = 0; i < length / 2u; ++i) {
        char swap = digits[i];
        digits[i] = digits[length - 1u - i];
        digits[length - 1u - i] = swap;
    }
    return copy_digits(buffer, size, digits, length);
}

size_t helikon_format_u32(char *buffer, size_t size, uint32_t value)
{
    return helikon_format_u64(buffer, size, value);
}

size_t helikon_format_i32(char *buffer, size_t size, int32_t value)
{
    if (value >= 0)
        return helikon_format_u64(buffer, size, (uint64_t)value);

    char digits[22];
    digits[0] = '-';
    uint32_t magnitude = (uint32_t)(-(int64_t)value);
    char reversed[10];
    size_t length = 0;
    while (magnitude != 0u) {
        reversed[length++] = (char)('0' + (char)(magnitude % 10u));
        magnitude /= 10u;
    }
    for (size_t i = 0; i < length; ++i)
        digits[1u + i] = reversed[length - 1u - i];
    return copy_digits(buffer, size, digits, length + 1u);
}

size_t helikon_format_hex32(char *buffer, size_t size, uint32_t value,
                            unsigned int digits)
{
    static const char hex[] = "0123456789abcdef";
    char text[9];
    size_t length = 0;

    if (digits > 8u)
        digits = 8u;
    for (unsigned int i = 0; i < digits; ++i)
        text[length++] = hex[(value >> (4u * (digits - 1u - i))) & 0xfu];
    return copy_digits(buffer, size, text, length);
}
