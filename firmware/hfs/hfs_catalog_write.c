/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog_write.h"

#include "hfs_bitmap.h"
#include "hfs_btree_tree.h"
#include "hfs_extents_write.h"
#include "hfs_unicode.h"
#include "hfs_volume_write.h"

#include <string.h>

#define CATALOG_FILE_RECORD 248u
#define CATALOG_FOLDER_RECORD 88u
#define CATALOG_FILE_TYPE 2u
#define CATALOG_FOLDER_TYPE 1u
#define CATALOG_FILE_THREAD_TYPE 4u
#define CATALOG_FOLDER_THREAD_TYPE 3u
#define CATALOG_KEY_HEADER 6u
#define CATALOG_FILE_MODE 0100000u
#define CATALOG_FOLDER_MODE 0040000u
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
                       uint32_t *folder_count, uint32_t *next_cnid)
{
    uint8_t header[512];
    if (volume->read(volume->context,
                     volume->base + HFS_VOLUME_HEADER_OFFSET, header,
                     sizeof(header)))
        return HFS_ERR_IO;
    *file_count = be32(header + 32);
    *folder_count = be32(header + 36);
    *next_cnid = be32(header + 64);
    return HFS_OK;
}

static int create_object(struct hfs_catalog *catalog, struct hfs_txn *tx,
                         uint32_t parent, const uint8_t *name, size_t units,
                         uint16_t mode, uint16_t kind,
                         hfs_btree_key_compare_fn compare,
                         void *compare_context, uint32_t *cnid)
{
    if (kind != CATALOG_FILE_TYPE && kind != CATALOG_FOLDER_TYPE)
        return HFS_ERR_ARGUMENT;

    uint8_t canonical[255u * 2u];
    size_t canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, canonical,
                                        sizeof(canonical), &canonical_units);
    if (rc != HFS_OK)
        return rc;

    struct hfs_volume *volume = catalog->btree.volume;
    uint32_t file_count;
    uint32_t folder_count;
    uint32_t next_cnid;
    rc = read_counts(volume, &file_count, &folder_count, &next_cnid);
    if (rc != HFS_OK)
        return rc;

    size_t record_size = kind == CATALOG_FOLDER_TYPE ?
                         CATALOG_FOLDER_RECORD : CATALOG_FILE_RECORD;
    uint16_t mode_type = kind == CATALOG_FOLDER_TYPE ?
                         CATALOG_FOLDER_MODE : CATALOG_FILE_MODE;
    uint16_t thread_type = kind == CATALOG_FOLDER_TYPE ?
                           CATALOG_FOLDER_THREAD_TYPE : CATALOG_FILE_THREAD_TYPE;

    uint8_t record[CATALOG_MAX_RECORD];
    size_t key_length = build_key(record, parent, canonical, canonical_units);
    size_t data_offset = 2u + key_length;
    memset(record + data_offset, 0, record_size);
    put16(record + data_offset, kind);
    put32(record + data_offset + 8, next_cnid);
    put16(record + data_offset + 42,
          (uint16_t)(mode_type | (mode & 0777u)));
    size_t record_length = data_offset + record_size;

    rc = hfs_btree_insert(&catalog->btree, tx, record, record_length,
                          compare, compare_context);
    if (rc != HFS_OK)
        return rc;

    uint8_t thread[CATALOG_MAX_RECORD];
    size_t thread_key = build_key(thread, next_cnid, NULL, 0);
    size_t thread_data = 2u + thread_key;
    size_t thread_length = 10u + canonical_units * 2u;
    memset(thread + thread_data, 0, thread_length);
    put16(thread + thread_data, thread_type);
    put32(thread + thread_data + 4, parent);
    put16(thread + thread_data + 8, (uint16_t)canonical_units);
    memcpy(thread + thread_data + 10, canonical, canonical_units * 2u);
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
    if (kind == CATALOG_FOLDER_TYPE) {
        update.set_folder_count = 1;
        update.folder_count = folder_count + 1u;
    } else {
        update.set_file_count = 1;
        update.file_count = file_count + 1u;
    }
    update.set_next_cnid = 1;
    update.next_cnid = next_cnid + 1u;
    rc = hfs_volume_stage(tx, volume, &update);
    if (rc != HFS_OK)
        return rc;

    *cnid = next_cnid;
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
    return create_object(catalog, tx, parent, name, units, mode,
                         CATALOG_FILE_TYPE, compare, compare_context, cnid);
}

int hfs_catalog_create_folder(struct hfs_catalog *catalog, struct hfs_txn *tx,
                              uint32_t parent, const uint8_t *name,
                              size_t units, uint16_t mode,
                              hfs_btree_key_compare_fn compare,
                              void *compare_context, uint32_t *cnid)
{
    if (catalog == NULL || tx == NULL || name == NULL || compare == NULL ||
        cnid == NULL || units == 0 || units > 255u || parent < 2u)
        return HFS_ERR_ARGUMENT;
    return create_object(catalog, tx, parent, name, units, mode,
                         CATALOG_FOLDER_TYPE, compare, compare_context, cnid);
}

static int delete_object(struct hfs_catalog *catalog, struct hfs_txn *tx,
                         uint32_t parent, const uint8_t *name, size_t units,
                         uint32_t cnid, uint16_t kind,
                         hfs_btree_key_compare_fn compare,
                         void *compare_context)
{
    uint8_t canonical[255u * 2u];
    size_t canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, canonical,
                                        sizeof(canonical), &canonical_units);
    if (rc != HFS_OK)
        return rc;

    struct hfs_volume *volume = catalog->btree.volume;
    uint32_t file_count;
    uint32_t folder_count;
    uint32_t next_cnid;
    rc = read_counts(volume, &file_count, &folder_count, &next_cnid);
    (void)next_cnid;
    if (rc != HFS_OK)
        return rc;

    uint8_t key[CATALOG_MAX_RECORD];
    size_t key_length = build_key(key, parent, canonical, canonical_units);

    if (kind == CATALOG_FOLDER_TYPE) {
        if (folder_count == 0)
            return HFS_ERR_FORMAT;
        uint8_t record[CATALOG_MAX_RECORD];
        size_t record_length = 0;
        rc = hfs_btree_find(&catalog->btree, tx, key + 2, key_length,
                            compare, compare_context, record,
                            sizeof(record), &record_length);
        if (rc != HFS_OK)
            return rc;
        size_t data_offset = 2u + key_length;
        if (record_length < data_offset + 88u ||
            be16(record + data_offset) != CATALOG_FOLDER_TYPE)
            return HFS_ERR_FORMAT;
        if (be32(record + data_offset + 4) != 0)
            return HFS_ERR_NOT_EMPTY;
    } else if (file_count == 0) {
        return HFS_ERR_FORMAT;
    }

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
    if (kind == CATALOG_FOLDER_TYPE) {
        update.set_folder_count = 1;
        update.folder_count = folder_count - 1u;
    } else {
        update.set_file_count = 1;
        update.file_count = file_count - 1u;
    }
    return hfs_volume_stage(tx, volume, &update);
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
    return delete_object(catalog, tx, parent, name, units, cnid,
                         CATALOG_FILE_TYPE, compare, compare_context);
}

int hfs_catalog_delete_folder(struct hfs_catalog *catalog, struct hfs_txn *tx,
                              uint32_t parent, const uint8_t *name,
                              size_t units, uint32_t cnid,
                              hfs_btree_key_compare_fn compare,
                              void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL || compare == NULL ||
        units == 0 || units > 255u || parent < 2u || cnid < 16u)
        return HFS_ERR_ARGUMENT;
    return delete_object(catalog, tx, parent, name, units, cnid,
                         CATALOG_FOLDER_TYPE, compare, compare_context);
}

static int find_file_record(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name, size_t units,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context, uint8_t *record,
                            size_t *record_length, size_t *key_length);

static int folder_is_ancestor(struct hfs_catalog *catalog, struct hfs_txn *tx,
                              uint32_t ancestor, uint32_t start,
                              hfs_btree_key_compare_fn compare,
                              void *compare_context)
{
    uint32_t current = start;
    for (unsigned int depth = 0; depth < 256u; ++depth) {
        if (current == ancestor)
            return 1;
        if (current < 2u)
            return 0;
        uint8_t key[CATALOG_MAX_RECORD];
        size_t key_length = build_key(key, current, NULL, 0);
        uint8_t record[CATALOG_MAX_RECORD];
        size_t record_length = 0;
        int rc = hfs_btree_find(&catalog->btree, tx, key + 2, key_length,
                                compare, compare_context, record,
                                sizeof(record), &record_length);
        if (rc != HFS_OK)
            return rc;
        size_t data_offset = 2u + key_length;
        if (record_length < data_offset + 10u ||
            be16(record + data_offset) != 3u)
            return HFS_ERR_FORMAT;
        uint32_t parent = be32(record + data_offset + 4);
        uint16_t units = be16(record + data_offset + 8);
        if ((size_t)units * 2u + 10u > record_length - data_offset)
            return HFS_ERR_FORMAT;
        current = parent;
    }
    return HFS_ERR_FORMAT;
}

int hfs_catalog_rename(struct hfs_catalog *catalog, struct hfs_txn *tx,
                       uint32_t parent, const uint8_t *name, size_t units,
                       uint32_t cnid, uint32_t new_parent,
                       const uint8_t *new_name, size_t new_units,
                       hfs_btree_key_compare_fn compare,
                       void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL || new_name == NULL ||
        compare == NULL || units == 0 || units > 255u ||
        new_units == 0 || new_units > 255u || parent < 2u ||
        new_parent < 2u || cnid < 16u)
        return HFS_ERR_ARGUMENT;

    uint8_t old_canonical[255u * 2u];
    uint8_t new_canonical[255u * 2u];
    size_t old_units = 0;
    size_t new_canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, old_canonical,
                                        sizeof(old_canonical), &old_units);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_unicode_normalize_name(new_name, new_units, new_canonical,
                                    sizeof(new_canonical),
                                    &new_canonical_units);
    if (rc != HFS_OK)
        return rc;

    uint8_t record[CATALOG_MAX_RECORD];
    size_t record_length = 0;
    size_t key_length = 0;
    rc = find_file_record(catalog, tx, parent, old_canonical, old_units,
                          compare, compare_context, record, &record_length,
                          &key_length);
    if (rc != HFS_OK)
        return rc;

    size_t data_offset = 2u + key_length;
    if (record_length < data_offset + 2u)
        return HFS_ERR_FORMAT;
    uint16_t kind = be16(record + data_offset);
    size_t object_size = 0;
    uint16_t thread_type = 0;
    if (kind == 1u) {
        object_size = 88u;
        thread_type = 3u;
    } else if (kind == 2u) {
        object_size = 248u;
        thread_type = 4u;
    } else {
        return HFS_ERR_UNSUPPORTED;
    }
    if (record_length < data_offset + object_size)
        return HFS_ERR_FORMAT;

    if (kind == 1u && new_parent != parent) {
        int cycle = folder_is_ancestor(catalog, tx, cnid, new_parent,
                                       compare, compare_context);
        if (cycle < 0)
            return cycle;
        if (cycle == 1)
            return HFS_ERR_ARGUMENT;
    }

    uint8_t data[248];
    memcpy(data, record + data_offset, object_size);

    rc = hfs_btree_delete(&catalog->btree, tx, record + 2, key_length,
                          compare, compare_context);
    if (rc != HFS_OK)
        return rc;

    uint8_t new_record[CATALOG_MAX_RECORD];
    size_t new_key_length = build_key(new_record, new_parent, new_canonical,
                                      new_canonical_units);
    size_t new_data_offset = 2u + new_key_length;
    memcpy(new_record + new_data_offset, data, object_size);
    rc = hfs_btree_insert(&catalog->btree, tx, new_record,
                          new_data_offset + object_size, compare,
                          compare_context);
    if (rc != HFS_OK)
        return rc;

    uint8_t thread[CATALOG_MAX_RECORD];
    size_t thread_key = build_key(thread, cnid, NULL, 0);
    rc = hfs_btree_delete(&catalog->btree, tx, thread + 2, thread_key,
                          compare, compare_context);
    if (rc != HFS_OK)
        return rc;

    size_t thread_data = 2u + thread_key;
    size_t thread_length = 10u + new_canonical_units * 2u;
    memset(thread + thread_data, 0, thread_length);
    put16(thread + thread_data, thread_type);
    put32(thread + thread_data + 4, new_parent);
    put16(thread + thread_data + 8, (uint16_t)new_canonical_units);
    memcpy(thread + thread_data + 10, new_canonical,
           new_canonical_units * 2u);
    rc = hfs_btree_insert(&catalog->btree, tx, thread,
                          thread_data + thread_length, compare,
                          compare_context);
    if (rc != HFS_OK)
        return rc;

    if (parent != new_parent) {
        rc = update_valence(catalog, tx, parent, -1, compare,
                            compare_context);
        if (rc != HFS_OK)
            return rc;
        rc = update_valence(catalog, tx, new_parent, 1, compare,
                            compare_context);
    }
    return rc;
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

    uint8_t canonical[255u * 2u];
    size_t canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, canonical,
                                        sizeof(canonical), &canonical_units);
    if (rc != HFS_OK)
        return rc;

    uint8_t record[CATALOG_MAX_RECORD];
    size_t record_length;
    size_t key_length;
    rc = find_file_record(catalog, tx, parent, canonical, canonical_units,
                          compare, compare_context, record, &record_length,
                          &key_length);
    if (rc != HFS_OK)
        return rc;
    size_t data_offset = 2u + key_length;
    if (record_length < data_offset + 88u + 80u)
        return HFS_ERR_FORMAT;
    uint8_t *fork = record + data_offset + 88u;
    uint64_t old_size = be64(fork);
    uint32_t old_blocks = be32(fork + 12);
    if (offset != old_size)
        return HFS_ERR_ARGUMENT;

    uint32_t extent_starts[32];
    uint32_t extent_counts[32];
    uint32_t extent_count = 0;
    uint32_t extent_blocks = 0;
    for (unsigned int i = 0; i < 8; ++i) {
        uint32_t start = be32(fork + 16 + i * 8);
        uint32_t count = be32(fork + 20 + i * 8);
        if (count == 0) {
            if (start != 0)
                return HFS_ERR_FORMAT;
            continue;
        }
        if (start == 0 || extent_count >= 32u)
            return HFS_ERR_UNSUPPORTED;
        extent_starts[extent_count] = start;
        extent_counts[extent_count] = count;
        ++extent_count;
        if (count > UINT32_MAX - extent_blocks)
            return HFS_ERR_FORMAT;
        extent_blocks += count;
    }
    if (extent_blocks != old_blocks)
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
    if (blocks + 5u > HFS_TXN_MAX_BLOCKS)
        return HFS_ERR_NO_SPACE;
    uint32_t firsts[32];
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        rc = hfs_bitmap_find(&bitmap, cursor, 1, &firsts[i]);
        if (rc != HFS_OK)
            return rc;
        cursor = firsts[i] + 1u;
    }

    uint8_t block[32768];
    size_t done = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        memset(block, 0, block_size);
        size_t chunk = length - done;
        if (chunk > block_size)
            chunk = block_size;
        memcpy(block, (const uint8_t *)data + done, chunk);
        uint64_t sector = (uint64_t)firsts[i] *
                          (block_size / tx->journal->sector_size);
        rc = hfs_txn_stage_replace(tx, sector, block, block_size);
        if (rc != HFS_OK)
            return rc;
        done += chunk;
    }

    for (uint32_t i = 0; i < blocks; ++i) {
        uint32_t allocated = 0;
        rc = hfs_bitmap_alloc(&bitmap, tx, firsts[i], 1, &allocated);
        if (rc != HFS_OK)
            return rc;
        if (allocated != firsts[i])
            return HFS_ERR_BUSY;
    }

    for (uint32_t i = 0; i < blocks; ++i) {
        if (extent_count != 0 &&
            extent_starts[extent_count - 1u] +
                extent_counts[extent_count - 1u] == firsts[i]) {
            ++extent_counts[extent_count - 1u];
        } else {
            if (extent_count >= 32u)
                return HFS_ERR_UNSUPPORTED;
            extent_starts[extent_count] = firsts[i];
            extent_counts[extent_count] = 1;
            ++extent_count;
        }
    }
    if (old_size > UINT64_MAX - length ||
        old_blocks > UINT32_MAX - blocks)
        return HFS_ERR_NO_SPACE;
    put64(fork, old_size + length);
    put32(fork + 12, old_blocks + blocks);
    memset(fork + 16, 0, 64);
    uint32_t inline_count = extent_count > 8u ? 8u : extent_count;
    for (uint32_t i = 0; i < inline_count; ++i) {
        put32(fork + 16 + i * 8, extent_starts[i]);
        put32(fork + 20 + i * 8, extent_counts[i]);
    }
    if (extent_count > 8u) {
        uint32_t start_block = 0;
        for (uint32_t i = 0; i < 8u; ++i)
            start_block += extent_counts[i];
        uint32_t file_id = be32(record + data_offset + 8);
        uint32_t index = 8;
        while (index < extent_count) {
            uint32_t chunk = extent_count - index;
            if (chunk > 8u)
                chunk = 8u;
            struct hfs_extent overflow[8];
            for (uint32_t i = 0; i < chunk; ++i) {
                overflow[i].start = extent_starts[index + i];
                overflow[i].count = extent_counts[index + i];
            }
            rc = hfs_extents_overflow_set(volume, tx, file_id, start_block,
                                          overflow, chunk);
            if (rc != HFS_OK)
                return rc;
            for (uint32_t i = 0; i < chunk; ++i)
                start_block += extent_counts[index + i];
            index += chunk;
        }
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

    uint8_t canonical[255u * 2u];
    size_t canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, canonical,
                                        sizeof(canonical), &canonical_units);
    if (rc != HFS_OK)
        return rc;

    uint8_t record[CATALOG_MAX_RECORD];
    size_t record_length;
    size_t key_length;
    rc = find_file_record(catalog, tx, parent, canonical, canonical_units,
                          compare, compare_context, record, &record_length,
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

    struct hfs_bitmap bitmap;
    rc = hfs_bitmap_open(&bitmap, catalog->btree.volume);
    if (rc != HFS_OK)
        return rc;

    uint32_t inline_blocks = 0;
    for (unsigned int i = 0; i < 8; ++i) {
        uint32_t start_block = be32(fork + 16 + i * 8);
        uint32_t extent_count = be32(fork + 20 + i * 8);
        if (extent_count == 0) {
            if (start_block != 0)
                return HFS_ERR_FORMAT;
            continue;
        }
        if (start_block == 0)
            return HFS_ERR_FORMAT;
        if (extent_count > UINT32_MAX - inline_blocks)
            return HFS_ERR_FORMAT;
        inline_blocks += extent_count;
    }
    if (blocks < inline_blocks)
        return HFS_ERR_FORMAT;

    uint32_t file_id = be32(record + data_offset + 8);
    if (blocks > inline_blocks) {
        uint32_t start_block = inline_blocks;
        uint32_t remaining = blocks - inline_blocks;
        unsigned int guard = 0;
        while (remaining > 0) {
            if (guard++ >= 8u)
                return HFS_ERR_UNSUPPORTED;
            struct hfs_extent overflow[8];
            size_t overflow_count = 0;
            rc = hfs_extents_overflow_get(catalog->btree.volume, tx, file_id,
                                          start_block, overflow, 8,
                                          &overflow_count);
            if (rc != HFS_OK || overflow_count == 0)
                return rc != HFS_OK ? rc : HFS_ERR_FORMAT;
            uint32_t overflow_blocks = 0;
            for (size_t i = 0; i < overflow_count; ++i) {
                if (overflow[i].count > remaining - overflow_blocks)
                    return HFS_ERR_FORMAT;
                overflow_blocks += overflow[i].count;
            }
            if (overflow_blocks == 0 || overflow_blocks > remaining)
                return HFS_ERR_FORMAT;
            for (size_t i = 0; i < overflow_count; ++i) {
                rc = hfs_bitmap_free(&bitmap, tx, overflow[i].start,
                                     overflow[i].count);
                if (rc != HFS_OK)
                    return rc;
            }
            rc = hfs_extents_overflow_clear(catalog->btree.volume, tx,
                                            file_id, start_block);
            if (rc != HFS_OK)
                return rc;
            start_block += overflow_blocks;
            remaining -= overflow_blocks;
        }
    }

    for (unsigned int i = 0; i < 8; ++i) {
        uint32_t start_block = be32(fork + 16 + i * 8);
        uint32_t extent_count = be32(fork + 20 + i * 8);
        if (extent_count == 0)
            continue;
        rc = hfs_bitmap_free(&bitmap, tx, start_block, extent_count);
        if (rc != HFS_OK)
            return rc;
    }

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
