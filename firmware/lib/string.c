/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded freestanding string helpers. No function reads past the supplied
 * limit. strlcpy always NUL-terminates when `size` is nonzero.
 */
#include "string.h"

#include <stdint.h>

size_t helikon_strlen(const char *text, size_t limit)
{
    size_t length = 0;

    if (text == NULL)
        return 0;
    while (length < limit && text[length] != '\0')
        ++length;
    return length;
}

int helikon_strcmp(const char *left, const char *right)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;

    if (left == NULL || right == NULL)
        return left == right ? 0 : (left == NULL ? -1 : 1);
    while (*a != 0u && *a == *b) {
        ++a;
        ++b;
    }
    return (int)*a - (int)*b;
}

int helikon_strncmp(const char *left, const char *right, size_t limit)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;

    if (limit == 0u)
        return 0;
    if (left == NULL || right == NULL)
        return left == right ? 0 : (left == NULL ? -1 : 1);
    while (limit-- != 0u) {
        if (*a != *b)
            return (int)*a - (int)*b;
        if (*a == 0u)
            return 0;
        ++a;
        ++b;
    }
    return 0;
}

size_t helikon_strlcpy(char *dest, const char *source, size_t size)
{
    size_t length = 0;

    if (source == NULL) {
        if (dest != NULL && size != 0u)
            dest[0] = '\0';
        return 0;
    }
    while (source[length] != '\0')
        ++length;
    if (dest != NULL && size != 0u) {
        size_t copy = length < size - 1u ? length : size - 1u;
        for (size_t i = 0; i < copy; ++i)
            dest[i] = source[i];
        dest[copy] = '\0';
    }
    return length;
}

#ifndef HELIKON_HOST_TEST
size_t strlen(const char *text)
{
    return helikon_strlen(text, (size_t)-1);
}

int strcmp(const char *left, const char *right)
{
    return helikon_strcmp(left, right);
}
#endif
