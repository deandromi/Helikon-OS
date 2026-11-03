/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_unicode.h"

static int put_utf8(char *dest, size_t dest_size, size_t *used, uint32_t cp)
{
    if (cp < 0x80u) {
        if (*used + 1 >= dest_size)
            return HFS_ERR_BUFFER;
        dest[(*used)++] = (char)cp;
    } else if (cp < 0x800u) {
        if (*used + 2 >= dest_size)
            return HFS_ERR_BUFFER;
        dest[(*used)++] = (char)(0xc0u | (cp >> 6));
        dest[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    } else if (cp < 0x10000u) {
        if (*used + 3 >= dest_size)
            return HFS_ERR_BUFFER;
        dest[(*used)++] = (char)(0xe0u | (cp >> 12));
        dest[(*used)++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        dest[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    } else {
        if (*used + 4 >= dest_size)
            return HFS_ERR_BUFFER;
        dest[(*used)++] = (char)(0xf0u | (cp >> 18));
        dest[(*used)++] = (char)(0x80u | ((cp >> 12) & 0x3fu));
        dest[(*used)++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
        dest[(*used)++] = (char)(0x80u | (cp & 0x3fu));
    }
    return HFS_OK;
}

int hfs_utf16be_to_utf8(const uint8_t *source, size_t units, char *dest,
                        size_t dest_size)
{
    if ((source == NULL && units != 0) || dest == NULL || dest_size == 0)
        return HFS_ERR_ARGUMENT;

    size_t used = 0;
    for (size_t i = 0; i < units; ++i) {
        uint32_t cp = ((uint32_t)source[i * 2] << 8) | source[i * 2 + 1];

        if (cp >= 0xd800u && cp <= 0xdbffu) {
            if (i + 1 >= units)
                return HFS_ERR_NAME_INVALID;
            uint32_t low = ((uint32_t)source[(i + 1) * 2] << 8) |
                           source[(i + 1) * 2 + 1];
            if (low < 0xdc00u || low > 0xdfffu)
                return HFS_ERR_NAME_INVALID;
            cp = 0x10000u + ((cp - 0xd800u) << 10) + (low - 0xdc00u);
            ++i;
        } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
            return HFS_ERR_NAME_INVALID;
        }

        if (cp == 0)
            return HFS_ERR_NAME_INVALID;
        int rc = put_utf8(dest, dest_size, &used, cp);
        if (rc != HFS_OK)
            return rc;
    }

    dest[used] = '\0';
    return HFS_OK;
}

static uint16_t fold_unicode(uint16_t value)
{
    uint16_t offset = hfs_unicode_fold_table[value >> 8];
    if (offset == 0)
        return value;
    return hfs_unicode_fold_table[offset + (value & 0xffu)];
}

static uint16_t read_unit(const uint8_t *source, size_t index)
{
    return (uint16_t)(((uint16_t)source[index * 2] << 8) |
                      source[index * 2 + 1]);
}

int hfs_fast_unicode_compare(const uint8_t *a, size_t a_units,
                             const uint8_t *b, size_t b_units)
{
    if (a == NULL)
        a_units = 0;
    if (b == NULL)
        b_units = 0;

    size_t i = 0;
    size_t j = 0;
    for (;;) {
        uint16_t left = 0;
        uint16_t right = 0;
        while (i < a_units && left == 0) {
            left = fold_unicode(read_unit(a, i));
            ++i;
        }
        while (j < b_units && right == 0) {
            right = fold_unicode(read_unit(b, j));
            ++j;
        }
        if (left != right)
            return left < right ? -1 : 1;
        if (left == 0)
            return 0;
    }
}

static int find_decomposition(
    uint16_t value, const struct hfs_unicode_decomposition **result)
{
    size_t low = 0;
    size_t high = hfs_unicode_decomposition_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        uint16_t code_point = hfs_unicode_decompositions[middle].code_point;
        if (value < code_point) {
            high = middle;
        } else if (value > code_point) {
            low = middle + 1u;
        } else {
            *result = &hfs_unicode_decompositions[middle];
            return 1;
        }
    }
    return 0;
}

static uint8_t combining_class(uint16_t value)
{
    size_t low = 0;
    size_t high = hfs_unicode_ccc_range_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        const struct hfs_unicode_ccc_range *range =
            &hfs_unicode_ccc_ranges[middle];
        if (value < range->first) {
            high = middle;
        } else if (value > range->last) {
            low = middle + 1u;
        } else {
            return range->ccc;
        }
    }
    return 0;
}

static void put_unit(uint8_t *dest, size_t index, uint16_t value)
{
    dest[index * 2] = (uint8_t)(value >> 8);
    dest[index * 2 + 1] = (uint8_t)value;
}

static int append_units(uint8_t *dest, size_t capacity, size_t *used,
                        const uint16_t *units, size_t count)
{
    if (count > 255u - *used)
        return HFS_ERR_NAME_INVALID;
    if (count > capacity - *used)
        return HFS_ERR_BUFFER;
    for (size_t i = 0; i < count; ++i)
        put_unit(dest, (*used)++, units[i]);
    return HFS_OK;
}

static int append_unit(uint8_t *dest, size_t capacity, size_t *used,
                       uint16_t value)
{
    return append_units(dest, capacity, used, &value, 1);
}

static void canonical_order(uint8_t *dest, size_t units)
{
    for (size_t i = 1; i < units; ++i) {
        uint16_t value = read_unit(dest, i);
        uint8_t ccc = combining_class(value);
        if (ccc == 0)
            continue;
        size_t j = i;
        while (j > 0) {
            uint16_t previous = read_unit(dest, j - 1u);
            uint8_t previous_ccc = combining_class(previous);
            if (previous_ccc == 0 || previous_ccc <= ccc)
                break;
            put_unit(dest, j, previous);
            put_unit(dest, j - 1u, value);
            --j;
        }
    }
}

int hfs_unicode_normalize_name(const uint8_t *name, size_t units,
                               uint8_t *dest, size_t dest_capacity,
                               size_t *dest_units)
{
    if (name == NULL || dest == NULL || dest_units == NULL ||
        units == 0 || units > 255u)
        return HFS_ERR_NAME_INVALID;
    size_t capacity = dest_capacity / 2u;
    if (capacity == 0)
        return HFS_ERR_BUFFER;

    size_t used = 0;
    int significant = 0;
    for (size_t i = 0; i < units; ++i) {
        uint16_t value = read_unit(name, i);
        if (value == 0)
            return HFS_ERR_NAME_INVALID;

        if (value >= 0xd800u && value <= 0xdbffu) {
            if (i + 1u >= units)
                return HFS_ERR_NAME_INVALID;
            uint16_t low = read_unit(name, i + 1u);
            if (low < 0xdc00u || low > 0xdfffu)
                return HFS_ERR_NAME_INVALID;
            uint16_t pair[2] = { value, low };
            int rc = append_units(dest, capacity, &used, pair, 2);
            if (rc != HFS_OK)
                return rc;
            significant = 1;
            ++i;
            continue;
        }
        if (value >= 0xdc00u && value <= 0xdfffu)
            return HFS_ERR_NAME_INVALID;

        if (value >= 0xac00u && value <= 0xd7a3u) {
            uint32_t index = (uint32_t)value - 0xac00u;
            uint16_t jamo[3];
            jamo[0] = (uint16_t)(0x1100u + index / 588u);
            jamo[1] = (uint16_t)(0x1161u + (index % 588u) / 28u);
            jamo[2] = (uint16_t)(0x11a7u + index % 28u);
            size_t count = jamo[2] == 0x11a7u ? 2u : 3u;
            int rc = append_units(dest, capacity, &used, jamo, count);
            if (rc != HFS_OK)
                return rc;
            significant = 1;
            continue;
        }

        const struct hfs_unicode_decomposition *decomposition = NULL;
        if (find_decomposition(value, &decomposition)) {
            int rc = append_units(dest, capacity, &used, decomposition->units,
                                  decomposition->length);
            if (rc != HFS_OK)
                return rc;
            significant = 1;
            continue;
        }

        int rc = append_unit(dest, capacity, &used, value);
        if (rc != HFS_OK)
            return rc;
        if (fold_unicode(value) != 0)
            significant = 1;
    }

    if (!significant)
        return HFS_ERR_NAME_INVALID;
    canonical_order(dest, used);
    *dest_units = used;
    return HFS_OK;
}
