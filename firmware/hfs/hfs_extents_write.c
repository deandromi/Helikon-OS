/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_extents_write.h"

#include "hfs_btree.h"
#include "hfs_btree_tree.h"

#include <string.h>

#define EXTENTS_KEY_LENGTH 10u
#define EXTENTS_KEY_RECORD 12u
#define EXTENTS_DATA 64u
#define EXTENTS_RECORD_SIZE (EXTENTS_KEY_RECORD + EXTENTS_DATA)

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

static void build_key(uint8_t *record, uint32_t file_id,
                      uint32_t start_block)
{
    record[0] = 0;          /* key length high */
    record[1] = EXTENTS_KEY_LENGTH;
    record[2] = 0;          /* data fork */
    record[3] = 0;          /* pad */
    put32(record + 4, file_id);
    put32(record + 8, start_block);
}

static int compare_extents(void *context, const uint8_t *a, size_t a_length,
                           const uint8_t *b, size_t b_length)
{
    (void)context;
    if (a_length < EXTENTS_KEY_LENGTH || b_length < EXTENTS_KEY_LENGTH)
        return a_length < b_length ? -1 : 1;
    if (a[0] != b[0])
        return a[0] < b[0] ? -1 : 1;
    uint32_t file_a = be32(a + 2);
    uint32_t file_b = be32(b + 2);
    if (file_a != file_b)
        return file_a < file_b ? -1 : 1;
    uint32_t start_a = be32(a + 6);
    uint32_t start_b = be32(b + 6);
    if (start_a != start_b)
        return start_a < start_b ? -1 : 1;
    return 0;
}

static int open_extents(struct hfs_volume *volume, struct hfs_btree *btree,
                        uint8_t *scratch)
{
    return hfs_btree_open(btree, volume, &volume->extents, scratch, 32768u);
}

int hfs_extents_overflow_set(struct hfs_volume *volume, struct hfs_txn *tx,
                             uint32_t file_id, uint32_t start_block,
                             const struct hfs_extent *extents, size_t count)
{
    if (volume == NULL || tx == NULL || extents == NULL || count == 0 ||
        count > 8u)
        return HFS_ERR_ARGUMENT;
    static uint8_t scratch[32768];
    struct hfs_btree btree;
    int rc = open_extents(volume, &btree, scratch);
    if (rc != HFS_OK)
        return rc;

    uint8_t record[EXTENTS_RECORD_SIZE];
    memset(record, 0, sizeof(record));
    build_key(record, file_id, start_block);
    uint8_t *data = record + EXTENTS_KEY_RECORD;
    for (size_t i = 0; i < count; ++i) {
        if (extents[i].start == 0 || extents[i].count == 0)
            return HFS_ERR_ARGUMENT;
        put32(data + i * 8, extents[i].start);
        put32(data + i * 8 + 4, extents[i].count);
    }

    rc = hfs_btree_delete(&btree, tx, record + 2, EXTENTS_KEY_LENGTH,
                          compare_extents, NULL);
    if (rc != HFS_OK && rc != HFS_ERR_NOT_FOUND)
        return rc;
    return hfs_btree_insert(&btree, tx, record, sizeof(record),
                            compare_extents, NULL);
}

int hfs_extents_overflow_get(struct hfs_volume *volume, struct hfs_txn *tx,
                             uint32_t file_id, uint32_t start_block,
                             struct hfs_extent *extents, size_t capacity,
                             size_t *count)
{
    if (volume == NULL || tx == NULL || extents == NULL || count == NULL ||
        capacity == 0)
        return HFS_ERR_ARGUMENT;
    static uint8_t scratch[32768];
    struct hfs_btree btree;
    int rc = open_extents(volume, &btree, scratch);
    if (rc != HFS_OK)
        return rc;

    uint8_t key[EXTENTS_KEY_RECORD];
    build_key(key, file_id, start_block);
    uint8_t record[512];
    size_t record_length = 0;
    rc = hfs_btree_find(&btree, tx, key + 2, EXTENTS_KEY_LENGTH,
                        compare_extents, NULL, record, sizeof(record),
                        &record_length);
    if (rc != HFS_OK)
        return rc;
    if (record_length < EXTENTS_RECORD_SIZE)
        return HFS_ERR_FORMAT;
    const uint8_t *data = record + EXTENTS_KEY_RECORD;
    size_t used = 0;
    for (size_t i = 0; i < 8u && used < capacity; ++i) {
        uint32_t start = be32(data + i * 8);
        uint32_t blocks = be32(data + i * 8 + 4);
        if (start == 0 && blocks == 0)
            break;
        if (start == 0 || blocks == 0)
            return HFS_ERR_FORMAT;
        extents[used].start = start;
        extents[used].count = blocks;
        ++used;
    }
    *count = used;
    return HFS_OK;
}

int hfs_extents_overflow_clear(struct hfs_volume *volume, struct hfs_txn *tx,
                               uint32_t file_id, uint32_t start_block)
{
    if (volume == NULL || tx == NULL)
        return HFS_ERR_ARGUMENT;
    static uint8_t scratch[32768];
    struct hfs_btree btree;
    int rc = open_extents(volume, &btree, scratch);
    if (rc != HFS_OK)
        return rc;

    uint8_t key[EXTENTS_KEY_RECORD];
    build_key(key, file_id, start_block);
    rc = hfs_btree_delete(&btree, tx, key + 2, EXTENTS_KEY_LENGTH,
                          compare_extents, NULL);
    return rc == HFS_ERR_NOT_FOUND ? HFS_OK : rc;
}
