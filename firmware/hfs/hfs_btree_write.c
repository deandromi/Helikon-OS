/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_btree_write.h"

#include <string.h>

#define HFS_BTREE_DESCRIPTOR 14u
#define HFS_BTREE_MAX_NODE HFS_MAX_NODE_SIZE

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static int validate_node(const uint8_t *node, uint16_t node_size,
                         uint16_t *count, uint16_t *free_offset)
{
    if (node == NULL || node_size < 64u || node_size > HFS_BTREE_MAX_NODE ||
        (node_size & (node_size - 1u)) != 0)
        return HFS_ERR_ARGUMENT;

    uint16_t records = be16(node + 10);
    if (records > (node_size - HFS_BTREE_DESCRIPTOR) / 2u - 1u)
        return HFS_ERR_FORMAT;

    uint16_t table = (uint16_t)(node_size - 2u * (records + 1u));
    uint16_t previous = HFS_BTREE_DESCRIPTOR;
    for (unsigned int i = 0; i <= records; ++i) {
        uint16_t offset = be16(node + node_size - 2u * (i + 1u));
        if (offset < previous || offset > table || (offset & 1u) != 0)
            return HFS_ERR_FORMAT;
        if (i == 0 && offset != HFS_BTREE_DESCRIPTOR)
            return HFS_ERR_FORMAT;
        previous = offset;
    }

    *count = records;
    *free_offset = be16(node + node_size - 2u * (records + 1u));
    return HFS_OK;
}

static uint16_t record_start(const uint8_t *node, uint16_t node_size,
                             uint16_t index)
{
    return be16(node + node_size - 2u * (index + 1u));
}

static size_t record_length(const uint8_t *node, uint16_t node_size,
                            uint16_t count, uint16_t free_offset,
                            uint16_t index)
{
    uint16_t start = record_start(node, node_size, index);
    uint16_t end = index + 1u < count ?
                   record_start(node, node_size, (uint16_t)(index + 1u)) :
                   free_offset;
    return (size_t)(end - start);
}

int hfs_btree_node_free_space(const uint8_t *node, uint16_t node_size,
                              size_t *free_bytes)
{
    uint16_t count;
    uint16_t free_offset;
    if (free_bytes == NULL)
        return HFS_ERR_ARGUMENT;
    int rc = validate_node(node, node_size, &count, &free_offset);
    if (rc != HFS_OK)
        return rc;
    uint16_t table = (uint16_t)(node_size - 2u * (count + 1u));
    if (free_offset > table)
        return HFS_ERR_FORMAT;
    *free_bytes = (size_t)(table - free_offset);
    return HFS_OK;
}

int hfs_btree_node_insert(uint8_t *node, uint16_t node_size, uint16_t index,
                          const void *record, size_t length)
{
    uint16_t count;
    uint16_t free_offset;
    if (record == NULL || length == 0 || (length & 1u) != 0)
        return HFS_ERR_ARGUMENT;

    int rc = validate_node(node, node_size, &count, &free_offset);
    if (rc != HFS_OK)
        return rc;
    if (index > count)
        return HFS_ERR_ARGUMENT;

    size_t free_bytes;
    rc = hfs_btree_node_free_space(node, node_size, &free_bytes);
    if (rc != HFS_OK)
        return rc;
    if (free_bytes < length + 2u)
        return HFS_ERR_NO_SPACE;

    uint16_t insert_offset = index == 0 ? HFS_BTREE_DESCRIPTOR :
                             index == count ? free_offset :
                             (uint16_t)(record_start(node, node_size,
                                                     (uint16_t)(index - 1u)) +
                                        record_length(node, node_size, count,
                                                      free_offset,
                                                      (uint16_t)(index - 1u)));

    static uint8_t temp[HFS_BTREE_MAX_NODE];
    memcpy(temp, node, node_size);
    memmove(temp + insert_offset + length, temp + insert_offset,
            (size_t)(free_offset - insert_offset));
    memcpy(temp + insert_offset, record, length);

    uint16_t new_count = (uint16_t)(count + 1u);
    put16(temp + 10, new_count);
    uint16_t pos = HFS_BTREE_DESCRIPTOR;
    for (uint16_t i = 0; i < new_count; ++i) {
        put16(temp + node_size - 2u * (i + 1u), pos);
        size_t record_size;
        if (i < index)
            record_size = record_length(node, node_size, count, free_offset, i);
        else if (i == index)
            record_size = length;
        else
            record_size = record_length(node, node_size, count, free_offset,
                                        (uint16_t)(i - 1u));
        pos = (uint16_t)(pos + record_size);
    }
    if (pos != free_offset + length)
        return HFS_ERR_FORMAT;
    put16(temp + node_size - 2u * (new_count + 1u), pos);

    memcpy(node, temp, node_size);
    return HFS_OK;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

int hfs_btree_node_split(uint8_t *left, uint8_t *right, uint16_t node_size,
                         uint16_t split_index, uint32_t left_node,
                         uint32_t right_node, const uint8_t **separator,
                         size_t *separator_length)
{
    uint16_t count;
    uint16_t free_offset;
    if (left == NULL || right == NULL || separator == NULL ||
        separator_length == NULL)
        return HFS_ERR_ARGUMENT;
    int rc = validate_node(left, node_size, &count, &free_offset);
    if (rc != HFS_OK)
        return rc;
    if (split_index == 0 || split_index >= count)
        return HFS_ERR_ARGUMENT;

    static uint8_t old[HFS_BTREE_MAX_NODE];
    memcpy(old, left, node_size);

    memcpy(right, left, HFS_BTREE_DESCRIPTOR);
    put16(right + 10, 0);
    put16(right + node_size - 2, HFS_BTREE_DESCRIPTOR);
    put32(right, be32(left));
    put32(right + 4, left_node);

    for (uint16_t i = split_index; i < count; ++i) {
        uint16_t start = record_start(old, node_size, i);
        size_t length = record_length(old, node_size, count, free_offset, i);
        rc = hfs_btree_node_insert(right, node_size,
                                   be16(right + 10), old + start, length);
        if (rc != HFS_OK)
            return rc;
    }
    for (uint16_t i = count; i > split_index; --i) {
        rc = hfs_btree_node_delete(left, node_size, (uint16_t)(i - 1u));
        if (rc != HFS_OK)
            return rc;
    }
    put32(left, right_node);

    uint16_t right_count;
    uint16_t right_free;
    rc = validate_node(right, node_size, &right_count, &right_free);
    if (rc != HFS_OK)
        return rc;
    if (right_count == 0)
        return HFS_ERR_FORMAT;
    uint16_t start = record_start(right, node_size, 0);
    size_t first_length = record_length(right, node_size, right_count,
                                        right_free, 0);
    const uint8_t *first = right + start;
    uint16_t key_length = be16(first);
    if ((size_t)key_length + 2u > first_length)
        return HFS_ERR_FORMAT;
    *separator = first + 2;
    *separator_length = key_length;
    return HFS_OK;
}

int hfs_btree_node_delete(uint8_t *node, uint16_t node_size, uint16_t index)
{
    uint16_t count;
    uint16_t free_offset;
    int rc = validate_node(node, node_size, &count, &free_offset);
    if (rc != HFS_OK)
        return rc;
    if (index >= count)
        return HFS_ERR_ARGUMENT;

    uint16_t start = record_start(node, node_size, index);
    uint16_t end = index + 1u < count ?
                   record_start(node, node_size, (uint16_t)(index + 1u)) :
                   free_offset;
    size_t length = (size_t)(end - start);

    static uint8_t temp[HFS_BTREE_MAX_NODE];
    memcpy(temp, node, node_size);
    memmove(temp + start, temp + end, (size_t)(free_offset - end));

    uint16_t new_count = (uint16_t)(count - 1u);
    put16(temp + 10, new_count);
    uint16_t pos = HFS_BTREE_DESCRIPTOR;
    for (uint16_t i = 0; i < new_count; ++i) {
        put16(temp + node_size - 2u * (i + 1u), pos);
        size_t record_size = record_length(node, node_size, count, free_offset,
                                           i < index ? i :
                                           (uint16_t)(i + 1u));
        pos = (uint16_t)(pos + record_size);
    }
    if (pos != free_offset - length)
        return HFS_ERR_FORMAT;
    put16(temp + node_size - 2u * (new_count + 1u), pos);

    memcpy(node, temp, node_size);
    return HFS_OK;
}
