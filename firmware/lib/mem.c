/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Freestanding memory primitives.
 *
 * The word-copy paths require both pointers to share a 4-byte alignment. Any
 * other case falls back to bytes, so unaligned register and HFS+ structures
 * are safe. No allocation, no blocking, no hosted headers.
 */
#include "mem.h"

#include <stdint.h>

void *helikon_memcpy(void *dest, const void *source, size_t count)
{
    uint8_t *out = dest;
    const uint8_t *in = source;

    if ((((uintptr_t)out | (uintptr_t)in) & 3u) == 0) {
        uint32_t *out32 = (uint32_t *)(void *)out;
        const uint32_t *in32 = (const uint32_t *)(const void *)in;
        while (count >= 4u) {
            *out32++ = *in32++;
            count -= 4u;
        }
        out = (uint8_t *)(void *)out32;
        in = (const uint8_t *)(const void *)in32;
    }

    while (count-- != 0u)
        *out++ = *in++;
    return dest;
}

void *helikon_memmove(void *dest, const void *source, size_t count)
{
    uint8_t *out = dest;
    const uint8_t *in = source;

    if (out == in || count == 0u)
        return dest;

    if (out < in || out >= in + count) {
        /* Forward copy is safe when the ranges do not overlap. */
        if ((((uintptr_t)out | (uintptr_t)in) & 3u) == 0) {
            uint32_t *out32 = (uint32_t *)(void *)out;
            const uint32_t *in32 = (const uint32_t *)(const void *)in;
            while (count >= 4u) {
                *out32++ = *in32++;
                count -= 4u;
            }
            out = (uint8_t *)(void *)out32;
            in = (const uint8_t *)(const void *)in32;
        }
        while (count-- != 0u)
            *out++ = *in++;
    } else {
        out += count;
        in += count;
        while (count-- != 0u)
            *--out = *--in;
    }
    return dest;
}

void *helikon_memset(void *dest, int value, size_t count)
{
    uint8_t *out = dest;
    uint8_t byte = (uint8_t)value;

    if (((uintptr_t)out & 3u) == 0 && count >= 4u) {
        uint32_t word = (uint32_t)byte;
        word |= word << 8;
        word |= word << 16;
        uint32_t *out32 = (uint32_t *)(void *)out;
        while (count >= 4u) {
            *out32++ = word;
            count -= 4u;
        }
        out = (uint8_t *)(void *)out32;
    }

    while (count-- != 0u)
        *out++ = byte;
    return dest;
}

int helikon_memcmp(const void *left, const void *right, size_t count)
{
    const uint8_t *a = left;
    const uint8_t *b = right;

    while (count-- != 0u) {
        if (*a != *b)
            return (int)*a - (int)*b;
        ++a;
        ++b;
    }
    return 0;
}

#ifndef HELIKON_HOST_TEST
void *memcpy(void *dest, const void *source, size_t count)
{
    return helikon_memcpy(dest, source, count);
}

void *memmove(void *dest, const void *source, size_t count)
{
    return helikon_memmove(dest, source, count);
}

void *memset(void *dest, int value, size_t count)
{
    return helikon_memset(dest, value, count);
}

int memcmp(const void *left, const void *right, size_t count)
{
    return helikon_memcmp(left, right, count);
}
#endif
