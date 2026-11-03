/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog_write.h"

#include "hfs_btree_tree.h"
#include "hfs_volume_write.h"

#include <string.h>

#define CATALOG_FILE_RECORD 248u
#define CATALOG_FILE_TYPE 2u
#define CATALOG_FILE_THREAD_TYPE 4u
#define CATALOG_KEY_HEADER 6u
#define CATALOG_FILE_MODE 0100000u
#define CATALOG_MAX_RECORD 1024u

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

    struct hfs_volume_update update;
    memset(&update, 0, sizeof(update));
    update.set_file_count = 1;
    update.file_count = file_count - 1u;
    return hfs_volume_stage(tx, volume, &update);
}
