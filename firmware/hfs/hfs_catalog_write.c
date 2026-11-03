/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog_write.h"

#include "hfs_bitmap.h"
#include "hfs_btree_tree.h"
#include "hfs_volume_write.h"

#include <string.h>

#define CATALOG_FILE_RECORD 248u
#define CATALOG_FILE_TYPE 2u
#define CATALOG_FILE_THREAD_TYPE 4u
#define CATALOG_KEY_HEADER 6u
#define CATALOG_FILE_MODE 0100000u
#define CATALOG_MAX_RECORD 1024u

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint64_t be64(const uint8_t *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static void put64(uint8_t *p, uint64_t value)
{
    put32(p, (uint32_t)(value >> 32));
    put32(p + 4, (uint32_t)value);
}


static size_t build_key(uint8_t *buffer, uint32_t parent,
                        const uint8_t *name, size_t units)
{
    put16(buffer, (uint16_t)(CATALOG_KEY_HEADER + units * 2u));
    put32(buffer + 2, parent);
    put16(buffer + 6, (uint16_t)units);
    if (units != 0)
        memcpy(buffer + 8, name, units * 2u);
    return CATALOG_KEY_HEADER + units * 2u;
}

static int update_valence(struct hfs_catalog *catalog, struct hfs_txn *tx,
                          uint32_t parent, int delta,
                          hfs_btree_key_compare_fn compare,
                          void *compare_context)
{
    uint8_t key[CATALOG_MAX_RECORD];
    uint8_t record[CATALOG_MAX_RECORD];
    size_t record_length;
    size_t key_length = build_key(key, parent, NULL, 0);
    int rc = hfs_btree_find(&catalog->btree, tx, key + 2, key_length,
                            compare, compare_context, record,
                            sizeof(record), &record_length);
    if (rc != HFS_OK)
        return rc;
    if (record_length < 2u + key_length + 10u)
        return HFS_ERR_FORMAT;
    const uint8_t *thread = record + 2u + key_length;
    if (be16(thread) != 3u)
        return HFS_ERR_FORMAT;
    uint32_t folder_parent = be32(thread + 4);
    uint16_t units = be16(thread + 8);
    if ((size_t)units * 2u + 10u > record_length - 2u - key_length)
        return HFS_ERR_FORMAT;

    uint8_t folder_key[CATALOG_MAX_RECORD];
    size_t folder_key_length = build_key(folder_key, folder_parent,
                                         thread + 10, units);
    rc = hfs_btree_find(&catalog->btree, tx, folder_key + 2,
                        folder_key_length, compare, compare_context, record,
                        sizeof(record), &record_length);
    if (rc != HFS_OK)
        return rc;
    size_t data_offset = 2u + folder_key_length;
    if (record_length < data_offset + 8u)
        return HFS_ERR_FORMAT;
    uint8_t *folder = record + data_offset;
    if (be16(folder) != 1u)
        return HFS_ERR_FORMAT;
    uint32_t valence = be32(folder + 4);
    if (delta < 0 && valence == 0)
        return HFS_ERR_FORMAT;
    valence = (uint32_t)((int)valence + delta);
    if (valence > 32767u)
        return HFS_ERR_NO_SPACE;
    put32(folder + 4, valence);

    rc = hfs_btree_delete(&catalog->btree, tx, folder_key + 2,
                          folder_key_length, compare, compare_context);
    if (rc != HFS_OK)
        return rc;
    return hfs_btree_insert(&catalog->btree, tx, record, record_length,
                            compare, compare_context);
}

static int read_counts(const struct hfs_volume *volume, uint32_t *file_count,
                       uint32_t *next_cnid)
{
    uint8_t header[512];
    if (volume->read(volume->context,
                     volume->base + HFS_VOLUME_HEADER_OFFSET, header,
                     sizeof(header)))
        return HFS_ERR_IO;
    *file_count = be32(header + 32);
    *next_cnid = be32(header + 64);
    return HFS_OK;
}

int hfs_catalog_create_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint16_t mode,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context, uint32_t *cnid)
{
    if (catalog == NULL || tx == NULL || name == NULL || compare == NULL ||
        cnid == NULL || units == 0 || units > 255u || parent < 2u)
        return HFS_ERR_ARGUMENT;

    struct hfs_volume *volume = catalog->btree.volume;
    uint32_t file_count;
    uint32_t next_cnid;
    int rc = read_counts(volume, &file_count, &next_cnid);
    if (rc != HFS_OK)
        return rc;

    uint8_t record[CATALOG_MAX_RECORD];
    size_t key_length = build_key(record, parent, name, units);
    size_t data_offset = 2u + key_length;
    memset(record + data_offset, 0, CATALOG_FILE_RECORD);
    put16(record + data_offset, CATALOG_FILE_TYPE);
    put32(record + data_offset + 8, next_cnid);
    put16(record + data_offset + 42,
          (uint16_t)(CATALOG_FILE_MODE | (mode & 0777u)));
    size_t record_length = data_offset + CATALOG_FILE_RECORD;

    rc = hfs_btree_insert(&catalog->btree, tx, record, record_length,
                          compare, compare_context);
    if (rc != HFS_OK)
        return rc;

    uint8_t thread[CATALOG_MAX_RECORD];
    size_t thread_key = build_key(thread, next_cnid, NULL, 0);
    size_t thread_data = 2u + thread_key;
    size_t thread_length = 10u + units * 2u;
    memset(thread + thread_data, 0, thread_length);
    put16(thread + thread_data, CATALOG_FILE_THREAD_TYPE);
    put32(thread + thread_data + 4, parent);
    put16(thread + thread_data + 8, (uint16_t)units);
    memcpy(thread + thread_data + 10, name, units * 2u);
    size_t thread_total = thread_data + thread_length;

    rc = hfs_btree_insert(&catalog->btree, tx, thread, thread_total,
                          compare, compare_context);
    if (rc != HFS_OK)
        return rc;
    rc = update_valence(catalog, tx, parent, 1, compare, compare_context);
    if (rc != HFS_OK)
        return rc;

    struct hfs_volume_update update;
    memset(&update, 0, sizeof(update));
    update.set_file_count = 1;
    update.file_count = file_count + 1u;
    update.set_next_cnid = 1;
    update.next_cnid = next_cnid + 1u;
    rc = hfs_volume_stage(tx, volume, &update);
    if (rc != HFS_OK)
        return rc;

    *cnid = next_cnid;
    return HFS_OK;
}

int hfs_catalog_delete_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint32_t cnid,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL || compare == NULL ||
        units == 0 || units > 255u || parent < 2u || cnid < 16u)
        return HFS_ERR_ARGUMENT;

    struct hfs_volume *volume = catalog->btree.volume;
    uint32_t file_count;
    uint32_t next_cnid;
    int rc = read_counts(volume, &file_count, &next_cnid);
    if (rc != HFS_OK)
        return rc;
    if (file_count == 0)
        return HFS_ERR_FORMAT;

    uint8_t key[CATALOG_MAX_RECORD];
    size_t key_length = build_key(key, parent, name, units);
    rc = hfs_btree_delete(&catalog->btree, tx, key + 2,
                          key_length, compare, compare_context);
    if (rc != HFS_OK)
        return rc;

    size_t thread_key = build_key(key, cnid, NULL, 0);
    rc = hfs_btree_delete(&catalog->btree, tx, key + 2,
                          thread_key, compare, compare_context);
    if (rc != HFS_OK)
        return rc;
    rc = update_valence(catalog, tx, parent, -1, compare, compare_context);
    if (rc != HFS_OK)
        return rc;

    struct hfs_volume_update update;
    memset(&update, 0, sizeof(update));
    update.set_file_count = 1;
    update.file_count = file_count - 1u;
    return hfs_volume_stage(tx, volume, &update);
}

static int find_file_record(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name, size_t units,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context, uint8_t *record,
                            size_t *record_length, size_t *key_length)
{
    uint8_t key[CATALOG_MAX_RECORD];
    *key_length = build_key(key, parent, name, units);
    return hfs_btree_find(&catalog->btree, tx, key + 2, *key_length,
                          compare, compare_context, record,
                          CATALOG_MAX_RECORD, record_length);
}

int hfs_catalog_append_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint64_t offset,
                            const void *data, size_t length,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL || data == NULL ||
        compare == NULL || units == 0 || units > 255u || parent < 2u ||
        length == 0)
        return HFS_ERR_ARGUMENT;

    uint8_t record[CATALOG_MAX_RECORD];
    size_t record_length;
    size_t key_length;
    int rc = find_file_record(catalog, tx, parent, name, units, compare,
                              compare_context, record, &record_length,
                              &key_length);
    if (rc != HFS_OK)
        return rc;
    size_t data_offset = 2u + key_length;
    if (record_length < data_offset + 88u + 80u)
        return HFS_ERR_FORMAT;
    uint8_t *fork = record + data_offset + 88u;
    if (offset != 0 || be64(fork) != 0 || be32(fork + 12) != 0)
        return HFS_ERR_UNSUPPORTED;

    struct hfs_volume *volume = catalog->btree.volume;
    uint32_t block_size = volume->block_size;
    if (block_size == 0 || block_size > 32768u ||
        block_size % tx->journal->sector_size != 0)
        return HFS_ERR_UNSUPPORTED;
    uint32_t blocks = (uint32_t)((length + block_size - 1u) / block_size);
    if (blocks == 0)
        return HFS_ERR_ARGUMENT;

    struct hfs_bitmap bitmap;
    rc = hfs_bitmap_open(&bitmap, volume);
    if (rc != HFS_OK)
        return rc;
    uint32_t first = 0;
    rc = hfs_bitmap_find(&bitmap, 0, blocks, &first);
    if (rc != HFS_OK)
        return rc;

    uint8_t block[32768];
    size_t done = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        memset(block, 0, block_size);
        size_t chunk = length - done;
        if (chunk > block_size)
            chunk = block_size;
        memcpy(block, (const uint8_t *)data + done, chunk);
        uint64_t sector = (uint64_t)(first + i) *
                          (block_size / tx->journal->sector_size);
        rc = hfs_txn_stage_replace(tx, sector, block, block_size);
        if (rc != HFS_OK)
            return rc;
        done += chunk;
    }

    uint32_t allocated = 0;
    rc = hfs_bitmap_alloc(&bitmap, tx, first, blocks, &allocated);
    if (rc != HFS_OK)
        return rc;
    if (allocated != first)
        return HFS_ERR_BUSY;

    put64(fork, length);
    put32(fork + 12, blocks);
    put32(fork + 16, first);
    put32(fork + 20, blocks);
    for (unsigned int i = 1; i < 8; ++i) {
        put32(fork + 16 + i * 8, 0);
        put32(fork + 20 + i * 8, 0);
    }

    uint8_t key[CATALOG_MAX_RECORD];
    size_t delete_key = build_key(key, parent, name, units);
    rc = hfs_btree_delete(&catalog->btree, tx, key + 2, delete_key,
                          compare, compare_context);
    if (rc != HFS_OK)
        return rc;
    return hfs_btree_insert(&catalog->btree, tx, record, record_length,
                            compare, compare_context);
}

int hfs_catalog_truncate_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                              uint32_t parent, const uint8_t *name,
                              size_t units,
                              hfs_btree_key_compare_fn compare,
                              void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL || compare == NULL ||
        units == 0 || units > 255u || parent < 2u)
        return HFS_ERR_ARGUMENT;

    uint8_t record[CATALOG_MAX_RECORD];
    size_t record_length;
    size_t key_length;
    int rc = find_file_record(catalog, tx, parent, name, units, compare,
                              compare_context, record, &record_length,
                              &key_length);
    if (rc != HFS_OK)
        return rc;
    size_t data_offset = 2u + key_length;
    if (record_length < data_offset + 88u + 80u)
        return HFS_ERR_FORMAT;
    uint8_t *fork = record + data_offset + 88u;
    uint32_t blocks = be32(fork + 12);
    if (blocks == 0)
        return HFS_OK;
    uint32_t start = be32(fork + 16);
    uint32_t count = be32(fork + 20);
    if (count != blocks || start == 0)
        return HFS_ERR_UNSUPPORTED;
    for (unsigned int i = 1; i < 8; ++i) {
        if (be32(fork + 16 + i * 8) != 0 || be32(fork + 20 + i * 8) != 0)
            return HFS_ERR_UNSUPPORTED;
    }

    struct hfs_bitmap bitmap;
    rc = hfs_bitmap_open(&bitmap, catalog->btree.volume);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_bitmap_free(&bitmap, tx, start, blocks);
    if (rc != HFS_OK)
        return rc;

    memset(fork, 0, 80);

    uint8_t key[CATALOG_MAX_RECORD];
    size_t delete_key = build_key(key, parent, name, units);
    rc = hfs_btree_delete(&catalog->btree, tx, key + 2, delete_key,
                          compare, compare_context);
    if (rc != HFS_OK)
        return rc;
    return hfs_btree_insert(&catalog->btree, tx, record, record_length,
                            compare, compare_context);
}
