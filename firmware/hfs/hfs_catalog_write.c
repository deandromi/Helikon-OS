/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog_write.h"

#include "hfs_bitmap.h"
#include "hfs_btree_tree.h"
#include "hfs_extents.h"
#include "hfs_extents_write.h"
#include "hfs_fork_io.h"
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
/*
 * Logical extent map of one data fork: the eight inline extents plus the
 * extents-overflow entries an overwrite is willing to resolve. A fork more
 * fragmented than this refuses the write instead of staging a partial range.
 */
#define CATALOG_FORK_MAP_MAX (HFS_EXTENT_COUNT + HFS_OVERFLOW_EXTENT_MAX)

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

static void put64(uint8_t *p, uint64_t value)
{
    put32(p, (uint32_t)(value >> 32));
    put32(p + 4, (uint32_t)value);
}

static void set_record_dates(uint8_t *record, uint32_t now)
{
    if (now == 0)
        return;
    put32(record + 12, now); /* createDate */
    put32(record + 16, now); /* contentModDate */
    put32(record + 20, now); /* attributeModDate */
    put32(record + 24, now); /* accessDate */
    /* backupDate stays 0 until a backup tool records one. */
}

static void touch_content(uint8_t *record, uint32_t now)
{
    if (now == 0)
        return;
    put32(record + 16, now); /* contentModDate */
}

static void touch_attribute(uint8_t *record, uint32_t now)
{
    if (now == 0)
        return;
    put32(record + 20, now); /* attributeModDate */
}

static void touch_record(uint8_t *record, uint32_t now)
{
    touch_content(record, now);
    touch_attribute(record, now);
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
    static uint8_t key[CATALOG_MAX_RECORD];
    static uint8_t record[CATALOG_MAX_RECORD];
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

    static uint8_t folder_key[CATALOG_MAX_RECORD];
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
    touch_record(folder, catalog->now);

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

    static uint8_t canonical[255u * 2u];
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
    /*
     * CNIDs 0 and 1 are reserved and 2 is the root folder. A volume header that
     * says anything below 3 has been corrupted or hand-built, and using it would
     * write a record under a reserved ID - so refuse instead of guessing.
     */
    if (next_cnid < 3u || next_cnid == 0xffffffffu)
        return HFS_ERR_FORMAT;
    if (file_count == 0xffffffffu || folder_count == 0xffffffffu)
        return HFS_ERR_FORMAT;

    size_t record_size = kind == CATALOG_FOLDER_TYPE ?
                         CATALOG_FOLDER_RECORD : CATALOG_FILE_RECORD;
    uint16_t mode_type = kind == CATALOG_FOLDER_TYPE ?
                         CATALOG_FOLDER_MODE : CATALOG_FILE_MODE;
    uint16_t thread_type = kind == CATALOG_FOLDER_TYPE ?
                           CATALOG_FOLDER_THREAD_TYPE : CATALOG_FILE_THREAD_TYPE;

    static uint8_t record[CATALOG_MAX_RECORD];
    size_t key_length = build_key(record, parent, canonical, canonical_units);
    size_t data_offset = 2u + key_length;
    memset(record + data_offset, 0, record_size);
    put16(record + data_offset, kind);
    put32(record + data_offset + 8, next_cnid);
    put16(record + data_offset + 42,
          (uint16_t)(mode_type | (mode & 0777u)));
    set_record_dates(record + data_offset, catalog->now);
    size_t record_length = data_offset + record_size;

    rc = hfs_btree_insert(&catalog->btree, tx, record, record_length,
                          compare, compare_context);
    if (rc != HFS_OK)
        return rc;

    static uint8_t thread[CATALOG_MAX_RECORD];
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
    update.set_modify_date = catalog->now != 0;
    update.modify_date = catalog->now;
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
    static uint8_t canonical[255u * 2u];
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

    static uint8_t key[CATALOG_MAX_RECORD];
    size_t key_length = build_key(key, parent, canonical, canonical_units);

    if (kind == CATALOG_FOLDER_TYPE) {
        if (folder_count == 0)
            return HFS_ERR_FORMAT;
        static uint8_t record[CATALOG_MAX_RECORD];
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

    if (kind == CATALOG_FILE_TYPE) {
        /*
         * Free the data fork and every overflow record before the catalog
         * record goes away: deleting the record alone would leak each block the
         * file owned (the allocation bitmap would keep them marked used with no
         * object referencing them).
         */
        rc = hfs_catalog_truncate_file(catalog, tx, parent, canonical,
                                       canonical_units, compare,
                                       compare_context);
        if (rc != HFS_OK)
            return rc;
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
    update.set_modify_date = catalog->now != 0;
    update.modify_date = catalog->now;
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
        static uint8_t key[CATALOG_MAX_RECORD];
        size_t key_length = build_key(key, current, NULL, 0);
        static uint8_t record[CATALOG_MAX_RECORD];
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

    static uint8_t old_canonical[255u * 2u];
    static uint8_t new_canonical[255u * 2u];
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

    static uint8_t record[CATALOG_MAX_RECORD];
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

    static uint8_t new_record[CATALOG_MAX_RECORD];
    size_t new_key_length = build_key(new_record, new_parent, new_canonical,
                                      new_canonical_units);
    size_t new_data_offset = 2u + new_key_length;
    memcpy(new_record + new_data_offset, data, object_size);
    touch_attribute(new_record + new_data_offset, catalog->now);
    rc = hfs_btree_insert(&catalog->btree, tx, new_record,
                          new_data_offset + object_size, compare,
                          compare_context);
    if (rc != HFS_OK)
        return rc;

    static uint8_t thread[CATALOG_MAX_RECORD];
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
        if (rc != HFS_OK)
            return rc;
    }

    if (catalog->now != 0) {
        struct hfs_volume_update update;
        memset(&update, 0, sizeof(update));
        update.set_modify_date = 1;
        update.modify_date = catalog->now;
        rc = hfs_volume_stage(tx, catalog->btree.volume, &update);
    }
    return rc;
}

static int find_file_record(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name, size_t units,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context, uint8_t *record,
                            size_t *record_length, size_t *key_length)
{
    static uint8_t key[CATALOG_MAX_RECORD];
    *key_length = build_key(key, parent, name, units);
    return hfs_btree_find(&catalog->btree, tx, key + 2, *key_length,
                          compare, compare_context, record,
                          CATALOG_MAX_RECORD, record_length);
}

/*
 * The last overflow record of a fork, i.e. the record an append extends.
 * Walking the records costs one B-tree read each and keeps only the tail, so a
 * fork with any number of extents can be appended to without an in-memory copy
 * of the whole list (the previous list-based approach capped a fork at 32
 * extents). `start_block` is the record's logical-block key.
 */
static int find_last_overflow_record(struct hfs_volume *volume,
                                     struct hfs_txn *tx, uint32_t file_id,
                                     uint32_t inline_blocks,
                                     uint32_t fork_blocks,
                                     uint32_t *start_block,
                                     struct hfs_extent *extents,
                                     size_t *count)
{
    if (fork_blocks < inline_blocks)
        return HFS_ERR_FORMAT;
    uint32_t remaining = fork_blocks - inline_blocks;
    uint32_t logical = inline_blocks;
    if (remaining == 0)
        return HFS_ERR_NOT_FOUND;

    for (unsigned int guard = 0; guard < HFS_OVERFLOW_EXTENT_MAX;
         ++guard) {
        struct hfs_extent record[HFS_EXTENT_COUNT];
        size_t got = 0;
        int rc = hfs_extents_overflow_get(volume, tx, file_id, logical, record,
                                          HFS_EXTENT_COUNT, &got);
        if (rc != HFS_OK)
            return rc;
        if (got == 0)
            return HFS_ERR_FORMAT;
        uint32_t record_blocks = 0;
        for (size_t i = 0; i < got; ++i) {
            if (record[i].count > remaining - record_blocks)
                return HFS_ERR_FORMAT;
            record_blocks += record[i].count;
        }
        if (record_blocks == 0)
            return HFS_ERR_FORMAT;
        if (record_blocks == remaining) {
            for (size_t i = 0; i < got; ++i)
                extents[i] = record[i];
            *start_block = logical;
            *count = got;
            return HFS_OK;
        }
        logical += record_blocks;
        remaining -= record_blocks;
    }
    return HFS_ERR_UNSUPPORTED;
}

/*
 * The mutable tail of a fork during one append: the eight inline extents plus
 * the one overflow record the new extents land in. Only that record is staged,
 * so an append to a large fragmented file stays inside the transaction budget.
 */
struct fork_tail {
    struct hfs_extent inline_extents[HFS_EXTENT_COUNT];
    uint32_t inline_count;
    uint32_t inline_blocks;
    int have_record;
    uint32_t record_start;
    uint32_t record_blocks;
    struct hfs_extent record[HFS_EXTENT_COUNT];
    uint32_t record_count;
    int record_dirty;
};

static int flush_tail_record(struct hfs_volume *volume, struct hfs_txn *tx,
                             uint32_t file_id, struct fork_tail *tail)
{
    if (!tail->record_dirty)
        return HFS_OK;
    int rc = hfs_extents_overflow_set(volume, tx, file_id,
                                      tail->record_start, tail->record,
                                      tail->record_count);
    if (rc != HFS_OK)
        return rc;
    tail->record_dirty = 0;
    return HFS_OK;
}

/*
 * Add one allocated block to the tail: extend the last extent when it is
 * contiguous, otherwise add an extent to the inline fork or to the tail record,
 * opening a new record (and flushing the full one) when the record is full.
 */
static int append_tail_extent(struct hfs_volume *volume, struct hfs_txn *tx,
                              uint32_t file_id, struct fork_tail *tail,
                              uint32_t start_block)
{
    struct hfs_extent *last = NULL;
    if (tail->have_record && tail->record_count != 0)
        last = &tail->record[tail->record_count - 1u];
    else if (tail->inline_count != 0)
        last = &tail->inline_extents[tail->inline_count - 1u];
    if (last != NULL && last->start + last->count == start_block) {
        ++last->count;
        /* The block count is what the key of the next overflow record is
         * derived from, so extending an extent has to move it too. Leaving it
         * behind wrote the next record under the wrong start block: the fork
         * then claimed a block no record covered, which is a lost block and,
         * for the file, a hole nothing can read. */
        if (tail->have_record) {
            ++tail->record_blocks;
            tail->record_dirty = 1;
        } else {
            ++tail->inline_blocks;
        }
        return HFS_OK;
    }

    if (!tail->have_record && tail->inline_count < HFS_EXTENT_COUNT) {
        tail->inline_extents[tail->inline_count].start = start_block;
        tail->inline_extents[tail->inline_count].count = 1;
        tail->inline_count++;
        tail->inline_blocks++;
        return HFS_OK;
    }

    if (!tail->have_record) {
        /* The inline fork is full: this is the fork's ninth extent. */
        tail->have_record = 1;
        tail->record_start = tail->inline_blocks;
        tail->record_blocks = 0;
        tail->record_count = 0;
    } else if (tail->record_count == HFS_EXTENT_COUNT) {
        int rc = flush_tail_record(volume, tx, file_id, tail);
        if (rc != HFS_OK)
            return rc;
        tail->record_start += tail->record_blocks;
        tail->record_blocks = 0;
        tail->record_count = 0;
    }

    tail->record[tail->record_count].start = start_block;
    tail->record[tail->record_count].count = 1;
    tail->record_count++;
    tail->record_blocks++;
    tail->record_dirty = 1;
    return HFS_OK;
}

static int fork_map(struct hfs_catalog *catalog, struct hfs_txn *tx,
                    const uint8_t *fork_bytes, uint32_t file_id,
                    struct hfs_extent *out, size_t capacity, size_t *count);

/*
 * Append `length` bytes at the current end of the file. `data` may be NULL,
 * which stages a run of zeros: a grow uses that to fill the gap before a write
 * at an offset, without a caller buffer the size of the gap.
 */
static int append_range(struct hfs_catalog *catalog, struct hfs_txn *tx,
                        uint32_t parent, const uint8_t *name, size_t units,
                        uint64_t offset, const void *data, size_t length,
                        hfs_btree_key_compare_fn compare,
                        void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL ||
        compare == NULL || units == 0 || units > 255u || parent < 2u ||
        length == 0)
        return HFS_ERR_ARGUMENT;

    static uint8_t canonical[255u * 2u];
    size_t canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, canonical,
                                        sizeof(canonical), &canonical_units);
    if (rc != HFS_OK)
        return rc;

    static uint8_t record[CATALOG_MAX_RECORD];
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
    struct hfs_fork current;
    rc = hfs_fork_parse(catalog->btree.volume, fork, &current);
    if (rc != HFS_OK)
        return rc;
    uint64_t old_size = current.size;
    uint32_t old_blocks = current.blocks;
    if (offset != old_size)
        return HFS_ERR_ARGUMENT;

    uint32_t file_id = be32(record + data_offset + 8);
    struct hfs_volume *volume = catalog->btree.volume;
    uint32_t block_size = volume->block_size;
    if (block_size == 0 || block_size > 32768u ||
        block_size % tx->journal->sector_size != 0)
        return HFS_ERR_UNSUPPORTED;

    /*
     * A file whose size is not a block multiple ends inside its last block.
     * Appending straight into fresh blocks would leave that tail zero and put
     * the new bytes past the size the record reports, so fill it first.
     */
    uint32_t partial = (uint32_t)(old_size % block_size);
    if (partial != 0) {
        uint32_t fill = block_size - partial;
        if (length < fill)
            fill = (uint32_t)length;
        static struct hfs_extent map[CATALOG_FORK_MAP_MAX];
        size_t map_count = 0;
        static uint8_t zeros[32768];
        memset(zeros, 0, fill);
        rc = fork_map(catalog, tx, fork, file_id, map, CATALOG_FORK_MAP_MAX,
                      &map_count);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_fork_stage_map(tx, volume, map, map_count,
                                (uint64_t)current.blocks * block_size,
                                old_size,
                                data != NULL ? (const uint8_t *)data : zeros,
                                fill);
        if (rc != HFS_OK)
            return rc;
        if (data != NULL)
            data = (const uint8_t *)data + fill;
        old_size += fill;
        length -= fill;
    }

    struct fork_tail tail;
    memset(&tail, 0, sizeof(tail));
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        tail.inline_extents[i] = current.extents[i];
        if (current.extents[i].count != 0) {
            tail.inline_count = i + 1u;
            tail.inline_blocks += current.extents[i].count;
        }
    }
    if (current.blocks > tail.inline_blocks) {
        size_t record_count = 0;
        rc = find_last_overflow_record(catalog->btree.volume, tx, file_id,
                                       tail.inline_blocks, current.blocks,
                                       &tail.record_start, tail.record,
                                       &record_count);
        if (rc != HFS_OK)
            return rc;
        tail.have_record = 1;
        tail.record_count = (uint32_t)record_count;
        for (size_t i = 0; i < record_count; ++i)
            tail.record_blocks += tail.record[i].count;
    }

    uint32_t blocks = (uint32_t)((length + block_size - 1u) / block_size);

    struct hfs_bitmap bitmap;
    rc = hfs_bitmap_open(&bitmap, volume);
    if (rc != HFS_OK)
        return rc;
    bitmap.modify_date = catalog->now;
    touch_record(record + data_offset, catalog->now);
    if (blocks != 0 && blocks + 5u > HFS_TXN_MAX_BLOCKS)
        return HFS_ERR_NO_SPACE;
    uint32_t firsts[32];
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        rc = hfs_bitmap_find(&bitmap, tx, cursor, 1, &firsts[i]);
        if (rc != HFS_OK)
            return rc;
        cursor = firsts[i] + 1u;
    }

    static uint8_t block[32768];
    size_t done = 0;
    for (uint32_t i = 0; i < blocks; ++i) {
        memset(block, 0, block_size);
        size_t chunk = length - done;
        if (chunk > block_size)
            chunk = block_size;
        if (data != NULL)
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
        rc = append_tail_extent(volume, tx, file_id, &tail, firsts[i]);
        if (rc != HFS_OK)
            return rc;
    }
    rc = flush_tail_record(volume, tx, file_id, &tail);
    if (rc != HFS_OK)
        return rc;

    if (old_size > UINT64_MAX - length ||
        old_blocks > UINT32_MAX - blocks)
        return HFS_ERR_NO_SPACE;
    put64(fork, old_size + length);
    put32(fork + 12, old_blocks + blocks);
    memset(fork + 16, 0, 64);
    for (uint32_t i = 0; i < tail.inline_count; ++i) {
        put32(fork + 16 + i * 8, tail.inline_extents[i].start);
        put32(fork + 20 + i * 8, tail.inline_extents[i].count);
    }

    return hfs_btree_update(&catalog->btree, tx, record, record_length,
                            compare, compare_context);
}

int hfs_catalog_append_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint64_t offset,
                            const void *data, size_t length,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context)
{
    if (data == NULL)
        return HFS_ERR_ARGUMENT;
    return append_range(catalog, tx, parent, name, units, offset, data, length,
                        compare, compare_context);
}

/*
 * Write `length` bytes at `offset`, growing the file when the write ends past
 * its current size. A range inside the file is an in-place overwrite; a range
 * that starts past the end first fills the gap with zeros, so a file never holds
 * uninitialised space. One call is bounded by the transaction budget: a gap
 * larger than the remaining block budget returns HFS_ERR_NO_SPACE, and the
 * caller commits what it has and continues.
 */
int hfs_catalog_write_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                           uint32_t parent, const uint8_t *name, size_t units,
                           uint64_t offset, const void *data, size_t length,
                           hfs_btree_key_compare_fn compare,
                           void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL || data == NULL ||
        compare == NULL || units == 0 || units > 255u || parent < 2u ||
        length == 0)
        return HFS_ERR_ARGUMENT;
    if (offset > UINT64_MAX - length)
        return HFS_ERR_ARGUMENT;
    uint64_t end = offset + length;
    if (end > UINT32_MAX)
        return HFS_ERR_UNSUPPORTED;

    static uint8_t canonical[255u * 2u];
    size_t canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, canonical,
                                        sizeof(canonical), &canonical_units);
    if (rc != HFS_OK)
        return rc;

    static uint8_t record[CATALOG_MAX_RECORD];
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
    struct hfs_fork fork;
    rc = hfs_fork_parse(catalog->btree.volume, record + data_offset + 88u,
                        &fork);
    if (rc != HFS_OK)
        return rc;

    const uint8_t *source = data;
    uint64_t position = offset;
    size_t remaining = length;

    /* The part of the range that already has bytes: write it in place. */
    if (position < fork.size) {
        size_t head = (size_t)(fork.size - position);
        if (head > remaining)
            head = remaining;
        rc = hfs_catalog_overwrite_file(catalog, tx, parent, name, units,
                                        position, source, head, compare,
                                        compare_context);
        if (rc != HFS_OK)
            return rc;
        source += head;
        position += head;
        remaining -= head;
    }

    /*
     * A write past the end leaves a gap. Fill it with zeros in bounded chunks
     * so no caller has to hold a buffer the size of the hole, and so the
     * transaction budget stays the real limit.
     */
    while (remaining != 0 && position > fork.size) {
        uint64_t gap = position - fork.size;
        size_t chunk = gap > 4096u ? 4096u : (size_t)gap;
        rc = append_range(catalog, tx, parent, name, units, fork.size, NULL,
                          chunk, compare, compare_context);
        if (rc != HFS_OK)
            return rc;
        fork.size += chunk;
    }

    if (remaining != 0) {
        rc = append_range(catalog, tx, parent, name, units, fork.size, source,
                          remaining, compare, compare_context);
        if (rc != HFS_OK)
            return rc;
    }
    return HFS_OK;
}

/*
 * Build the logical extent map of a data fork: the inline extents first, then
 * the extents-overflow records in key order. The map is read through the
 * transaction, so an overwrite sees a fork this transaction already changed,
 * and the walk stops at the fork's block count instead of trusting a record
 * count. Returns HFS_ERR_UNSUPPORTED when the fork needs more than `capacity`
 * entries.
 */
static int fork_map(struct hfs_catalog *catalog, struct hfs_txn *tx,
                    const uint8_t *fork_bytes, uint32_t file_id,
                    struct hfs_extent *out, size_t capacity, size_t *count)
{
    uint32_t blocks = be32(fork_bytes + 12);
    uint32_t inline_blocks = 0;
    size_t used = 0;

    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        uint32_t start = be32(fork_bytes + 16 + i * 8);
        uint32_t extent_count = be32(fork_bytes + 20 + i * 8);
        if (extent_count == 0) {
            if (start != 0)
                return HFS_ERR_FORMAT;
            continue;
        }
        if (start == 0)
            return HFS_ERR_FORMAT;
        if (extent_count > UINT32_MAX - inline_blocks)
            return HFS_ERR_FORMAT;
        inline_blocks += extent_count;
        if (used >= capacity)
            return HFS_ERR_UNSUPPORTED;
        out[used].start = start;
        out[used].count = extent_count;
        ++used;
    }
    if (blocks < inline_blocks)
        return HFS_ERR_FORMAT;

    uint32_t logical = inline_blocks;
    for (unsigned int guard = 0; logical < blocks; ++guard) {
        if (guard >= HFS_OVERFLOW_EXTENT_MAX)
            return HFS_ERR_UNSUPPORTED;
        struct hfs_extent record[HFS_EXTENT_COUNT];
        size_t got = 0;
        int rc = hfs_extents_overflow_get(catalog->btree.volume, tx, file_id,
                                          logical, record, HFS_EXTENT_COUNT,
                                          &got);
        if (rc != HFS_OK)
            return rc;
        if (got == 0)
            return HFS_ERR_FORMAT;
        uint32_t record_blocks = 0;
        for (size_t i = 0; i < got; ++i) {
            if (record[i].count > blocks - logical - record_blocks)
                return HFS_ERR_FORMAT;
            record_blocks += record[i].count;
        }
        if (record_blocks == 0)
            return HFS_ERR_FORMAT;
        for (size_t i = 0; i < got; ++i) {
            if (used >= capacity)
                return HFS_ERR_UNSUPPORTED;
            out[used++] = record[i];
        }
        logical += record_blocks;
    }

    *count = used;
    return HFS_OK;
}

int hfs_catalog_overwrite_file(struct hfs_catalog *catalog,
                               struct hfs_txn *tx, uint32_t parent,
                               const uint8_t *name, size_t units,
                               uint64_t offset, const void *data,
                               size_t length,
                               hfs_btree_key_compare_fn compare,
                               void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL || data == NULL ||
        compare == NULL || units == 0u || units > 255u || parent < 2u ||
        length == 0u)
        return HFS_ERR_ARGUMENT;

    static uint8_t canonical[255u * 2u];
    size_t canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, canonical,
                                        sizeof(canonical), &canonical_units);
    if (rc != HFS_OK)
        return rc;

    static uint8_t record[CATALOG_MAX_RECORD];
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

    uint8_t *fork_bytes = record + data_offset + 88u;
    struct hfs_fork fork;
    rc = hfs_fork_parse(catalog->btree.volume, fork_bytes, &fork);
    if (rc != HFS_OK)
        return rc;
    if (offset > fork.size || length > fork.size - offset)
        return HFS_ERR_ARGUMENT;

    /*
     * The fork may continue in the extents tree. Its map is built from the
     * record just read plus the overflow records, so an in-place overwrite no
     * longer stops at the eight inline extents.
     */
    static struct hfs_extent map[CATALOG_FORK_MAP_MAX];
    size_t map_count = 0;
    rc = fork_map(catalog, tx, fork_bytes, be32(record + data_offset + 8),
                  map, CATALOG_FORK_MAP_MAX, &map_count);
    if (rc != HFS_OK)
        return rc;

    rc = hfs_fork_stage_map(tx, catalog->btree.volume, map, map_count,
                            fork.size, offset, data, length);
    if (rc != HFS_OK)
        return rc;

    touch_record(record + data_offset, catalog->now);
    return hfs_btree_update(&catalog->btree, tx, record, record_length,
                            compare, compare_context);
}

int hfs_catalog_truncate_to(struct hfs_catalog *catalog, struct hfs_txn *tx,
                            uint32_t parent, const uint8_t *name,
                            size_t units, uint64_t new_size,
                            hfs_btree_key_compare_fn compare,
                            void *compare_context)
{
    if (catalog == NULL || tx == NULL || name == NULL || compare == NULL ||
        units == 0 || units > 255u || parent < 2u)
        return HFS_ERR_ARGUMENT;

    static uint8_t canonical[255u * 2u];
    size_t canonical_units = 0;
    int rc = hfs_unicode_normalize_name(name, units, canonical,
                                        sizeof(canonical), &canonical_units);
    if (rc != HFS_OK)
        return rc;

    static uint8_t record[CATALOG_MAX_RECORD];
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
    uint8_t *fork_bytes = record + data_offset + 88u;

    struct hfs_fork fork;
    rc = hfs_fork_parse(catalog->btree.volume, fork_bytes, &fork);
    if (rc != HFS_OK)
        return rc;
    if (new_size > fork.size)
        return HFS_ERR_ARGUMENT;
    if (new_size == fork.size)
        return HFS_OK;
    if (fork.blocks == 0)
        return HFS_ERR_FORMAT;

    uint32_t block_size = catalog->btree.volume->block_size;
    uint64_t keep_blocks = (new_size + block_size - 1u) / block_size;
    if (keep_blocks > UINT32_MAX)
        return HFS_ERR_UNSUPPORTED;

    uint32_t file_id = be32(record + data_offset + 8);

    /*
     * The whole fork, not just its inline half: a truncate that stopped at
     * eight extents left every overflow block allocated and its record behind.
     */
    static struct hfs_extent map[CATALOG_FORK_MAP_MAX];
    size_t map_count = 0;
    rc = fork_map(catalog, tx, fork_bytes, file_id, map, CATALOG_FORK_MAP_MAX,
                  &map_count);
    if (rc != HFS_OK)
        return rc;

    uint32_t inline_blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        if (fork.extents[i].count > UINT32_MAX - inline_blocks)
            return HFS_ERR_FORMAT;
        inline_blocks += fork.extents[i].count;
    }

    /* The records that exist now; their keys are needed to clear them. */
    static uint32_t old_starts[HFS_OVERFLOW_EXTENT_MAX];
    size_t old_start_count = 0;
    uint32_t logical = inline_blocks;
    while (logical < fork.blocks && old_start_count < HFS_OVERFLOW_EXTENT_MAX) {
        struct hfs_extent record_extents[HFS_EXTENT_COUNT];
        size_t got = 0;
        rc = hfs_extents_overflow_get(catalog->btree.volume, tx, file_id,
                                      logical, record_extents, HFS_EXTENT_COUNT,
                                      &got);
        if (rc != HFS_OK)
            return rc;
        if (got == 0)
            return HFS_ERR_FORMAT;
        uint32_t record_blocks = 0;
        for (size_t i = 0; i < got; ++i) {
            if (record_extents[i].count > fork.blocks - logical -
                                             record_blocks)
                return HFS_ERR_FORMAT;
            record_blocks += record_extents[i].count;
        }
        if (record_blocks == 0)
            return HFS_ERR_FORMAT;
        old_starts[old_start_count++] = logical;
        logical += record_blocks;
    }

    struct hfs_bitmap bitmap;
    rc = hfs_bitmap_open(&bitmap, catalog->btree.volume);
    if (rc != HFS_OK)
        return rc;
    bitmap.modify_date = catalog->now;
    touch_record(record + data_offset, catalog->now);

    /*
     * Zero the bytes past the new end of the last kept block before they stop
     * being visible: a later grow must not expose what the file used to hold.
     *
     * The range stops at the old size. Bytes between it and the end of the block
     * were never part of the file — a fork's last block is only full when its
     * size is a block multiple — and staging them would ask the fork writer for
     * a range past EOF, which it refuses: that turned "shave a few bytes off
     * the end" into an argument error whenever both sizes fell in the same
     * block. A later grow fills a gap with zeros anyway.
     */
    uint64_t block_end = (uint64_t)keep_blocks * block_size;
    uint64_t zero_end = fork.size < block_end ? fork.size : block_end;
    if (zero_end > new_size) {
        uint32_t tail = (uint32_t)(zero_end - new_size);
        static uint8_t zeros[32768];
        if (tail > sizeof(zeros))
            return HFS_ERR_UNSUPPORTED;
        memset(zeros, 0, tail);
        rc = hfs_fork_stage_map(tx, catalog->btree.volume, map, map_count,
                                fork.size, new_size, zeros, tail);
        if (rc != HFS_OK)
            return rc;
    }

    /* Free the blocks past the new end and compact the retained prefix. */
    logical = 0;
    size_t kept = 0;
    for (size_t i = 0; i < map_count; ++i) {
        uint32_t count = map[i].count;
        if (logical >= keep_blocks) {
            rc = hfs_bitmap_free(&bitmap, tx, map[i].start, count);
            if (rc != HFS_OK)
                return rc;
        } else if ((uint64_t)logical + count > keep_blocks) {
            uint32_t kept_count = (uint32_t)(keep_blocks - logical);
            rc = hfs_bitmap_free(&bitmap, tx, map[i].start + kept_count,
                                 count - kept_count);
            if (rc != HFS_OK)
                return rc;
            map[kept].start = map[i].start;
            map[kept].count = kept_count;
            ++kept;
        } else {
            map[kept++] = map[i];
        }
        logical += count;
    }

    /* Rewrite the inline extents from the retained prefix. */
    memset(fork_bytes + 16, 0, HFS_EXTENT_COUNT * 8u);
    size_t inline_kept = kept < HFS_EXTENT_COUNT ? kept : HFS_EXTENT_COUNT;
    for (size_t i = 0; i < inline_kept; ++i) {
        put32(fork_bytes + 16 + i * 8, map[i].start);
        put32(fork_bytes + 20 + i * 8, map[i].count);
    }
    put32(fork_bytes + 0, (uint32_t)(new_size >> 32));
    put32(fork_bytes + 4, (uint32_t)new_size);
    put32(fork_bytes + 12, (uint32_t)keep_blocks); /* clumpSize is left alone */

    /*
     * Clear every overflow record, then write the retained tail back in groups
     * of eight. Clearing first keeps the rewrite independent of how the append
     * path happened to group the records it found.
     */
    for (size_t i = 0; i < old_start_count; ++i) {
        rc = hfs_extents_overflow_clear(catalog->btree.volume, tx, file_id,
                                        old_starts[i]);
        if (rc != HFS_OK)
            return rc;
    }
    if (kept > HFS_EXTENT_COUNT) {
        size_t overflow_kept = kept - HFS_EXTENT_COUNT;
        uint32_t start_block = inline_blocks;
        for (size_t index = 0; index < overflow_kept; index += HFS_EXTENT_COUNT) {
            size_t group = overflow_kept - index;
            if (group > HFS_EXTENT_COUNT)
                group = HFS_EXTENT_COUNT;
            uint32_t group_blocks = 0;
            for (size_t i = 0; i < group; ++i)
                group_blocks += map[HFS_EXTENT_COUNT + index + i].count;
            rc = hfs_extents_overflow_set(catalog->btree.volume, tx, file_id,
                                          start_block,
                                          &map[HFS_EXTENT_COUNT + index],
                                          group);
            if (rc != HFS_OK)
                return rc;
            start_block += group_blocks;
        }
    }

    return hfs_btree_update(&catalog->btree, tx, record, record_length,
                            compare, compare_context);
}

int hfs_catalog_truncate_file(struct hfs_catalog *catalog, struct hfs_txn *tx,
                              uint32_t parent, const uint8_t *name,
                              size_t units,
                              hfs_btree_key_compare_fn compare,
                              void *compare_context)
{
    return hfs_catalog_truncate_to(catalog, tx, parent, name, units, 0,
                                   compare, compare_context);
}
