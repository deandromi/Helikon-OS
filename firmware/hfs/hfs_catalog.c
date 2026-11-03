/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HFS+ catalog record parsing and directory iteration.
 *
 * Derived from the read-only HFS+ parser in HFSPlus Rockboxed iPod
 * (firmware/hfsplus/hfsplus_ro.c, GPL-2.0-or-later). The first slice supports
 * folder/file records, exact UTF-8 path lookup and inline file data. Thread
 * records are validated and skipped; extents overflow is a later module.
 */
#include "hfs_catalog.h"
#include "hfs_unicode.h"

#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int parse_key(const uint8_t *record, size_t length, uint32_t *parent,
                     uint16_t *units, const uint8_t **name)
{
    if (length < 8)
        return HFS_ERR_FORMAT;

    uint16_t key_length = be16(record);
    uint16_t name_units = be16(record + 6);
    if (name_units > 255 || key_length != 6u + name_units * 2u ||
        (size_t)key_length + 2u > length)
        return HFS_ERR_FORMAT;

    *parent = be32(record + 2);
    *units = name_units;
    *name = record + 8;
    return HFS_OK;
}

static int parse_record(const struct hfs_volume *volume, const uint8_t *record,
                        size_t length, struct hfs_entry *entry)
{
    uint32_t parent;
    uint16_t units;
    const uint8_t *name;
    int rc = parse_key(record, length, &parent, &units, &name);
    if (rc != HFS_OK)
        return rc;

    size_t key_length = be16(record);
    const uint8_t *data = record + key_length + 2;
    size_t data_size = length - key_length - 2;
    if (data_size < 2)
        return HFS_ERR_FORMAT;

    uint16_t kind = be16(data);
    if (kind == 3 || kind == 4) {
        if (units != 0)
            return HFS_ERR_FORMAT;
        return 0;
    }
    if (kind != 1 && kind != 2)
        return HFS_ERR_FORMAT;
    if (units == 0 || data_size < (kind == 1 ? 88u : 248u))
        return HFS_ERR_FORMAT;

    memset(entry, 0, sizeof(*entry));
    entry->kind = kind;
    entry->parent = parent;
    entry->id = be32(data + 8);
    entry->mode = be16(data + 42);
    if (parent == 0 || entry->id < 2)
        return HFS_ERR_FORMAT;

    rc = hfs_utf16be_to_utf8(name, units, entry->name, sizeof(entry->name));
    if (rc == HFS_ERR_NAME_INVALID) {
        rc = hfs_utf16be_to_utf8_escaped(name, units, entry->name,
                                         sizeof(entry->name));
        if (rc != HFS_OK)
            return rc;
        entry->name_escaped = 1;
        entry->unsupported = 1;
    } else if (rc != HFS_OK) {
        return rc;
    }

    uint16_t mode_type = entry->mode & 0170000;
    if (kind == 2) {
        rc = hfs_fork_parse(volume, data + 88, &entry->data);
        if (rc != HFS_OK)
            return rc;
        entry->unsupported = entry->unsupported ||
                             (mode_type != 0 && mode_type != 0100000);
    } else {
        entry->unsupported = entry->unsupported ||
                             (mode_type != 0 && mode_type != 0040000);
    }
    return 1;
}

int hfs_catalog_open(struct hfs_catalog *catalog, struct hfs_volume *volume,
                     void *scratch, size_t scratch_size)
{
    if (catalog == NULL || volume == NULL)
        return HFS_ERR_ARGUMENT;
    return hfs_btree_open(&catalog->btree, volume, &volume->catalog, scratch,
                          scratch_size);
}

int hfs_catalog_iter_init(struct hfs_catalog *catalog, uint32_t parent,
                          struct hfs_catalog_iterator *iterator)
{
    if (catalog == NULL || iterator == NULL || parent < 2)
        return HFS_ERR_ARGUMENT;

    memset(iterator, 0, sizeof(*iterator));
    iterator->parent = parent;
    return hfs_btree_leaf_init(&catalog->btree, &iterator->leaf);
}

int hfs_catalog_iter_next(struct hfs_catalog *catalog,
                          struct hfs_catalog_iterator *iterator,
                          struct hfs_entry *entry)
{
    if (catalog == NULL || iterator == NULL || entry == NULL)
        return HFS_ERR_ARGUMENT;

    const uint8_t *record;
    size_t length;
    int rc;
    while ((rc = hfs_btree_leaf_next(&catalog->btree, &iterator->leaf,
                                     &record, &length)) > 0) {
        uint32_t parent;
        uint16_t units;
        const uint8_t *name;
        rc = parse_key(record, length, &parent, &units, &name);
        if (rc != HFS_OK)
            return rc;

        if (parent < iterator->parent)
            continue;
        if (parent > iterator->parent)
            return 0;

        rc = parse_record(catalog->btree.volume, record, length, entry);
        if (rc < 0)
            return rc;
        if (rc == 1 && entry->parent == iterator->parent)
            return 1;
    }
    return rc;
}

int hfs_catalog_lookup(struct hfs_catalog *catalog, const char *path,
                       struct hfs_entry *entry)
{
    char component[HFS_NAME_UTF8_MAX];

    if (catalog == NULL || path == NULL || entry == NULL ||
        path[0] != '/')
        return HFS_ERR_ARGUMENT;

    uint32_t parent = 2;
    const char *p = path;
    while (*p == '/')
        ++p;
    if (*p == '\0')
        return HFS_ERR_NOT_FOUND;

    while (*p != '\0') {
        const char *start = p;
        while (*p != '\0' && *p != '/')
            ++p;
        size_t length = (size_t)(p - start);
        if (length == 0 || length >= sizeof(component))
            return HFS_ERR_NAME_INVALID;
        memcpy(component, start, length);
        component[length] = '\0';
        if (strcmp(component, ".") == 0 || strcmp(component, "..") == 0)
            return HFS_ERR_NAME_INVALID;

        struct hfs_catalog_iterator iterator;
        int rc = hfs_catalog_iter_init(catalog, parent, &iterator);
        if (rc != HFS_OK)
            return rc;

        int found = 0;
        struct hfs_entry candidate;
        while ((rc = hfs_catalog_iter_next(catalog, &iterator, &candidate)) > 0) {
            if (strcmp(candidate.name, component) == 0) {
                found = 1;
                break;
            }
        }
        if (rc < 0)
            return rc;
        if (!found)
            return HFS_ERR_NOT_FOUND;

        if (*p == '/') {
            if (candidate.kind != 1)
                return HFS_ERR_NOT_DIR;
            while (*p == '/')
                ++p;
            if (*p == '\0') {
                *entry = candidate;
                return HFS_OK;
            }
            parent = candidate.id;
        } else {
            *entry = candidate;
            return HFS_OK;
        }
    }

    return HFS_ERR_NOT_FOUND;
}

static int ascii_key_matches(const uint8_t *name, uint16_t units,
                             const char *ascii)
{
    size_t length = strlen(ascii);
    if (units != length)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        if (name[i * 2] != 0 || name[i * 2 + 1] != (uint8_t)ascii[i])
            return 0;
    }
    return 1;
}

int hfs_catalog_lookup_ascii(struct hfs_catalog *catalog, uint32_t parent,
                             const char *name, struct hfs_entry *entry)
{
    if (catalog == NULL || name == NULL || entry == NULL || parent < 2)
        return HFS_ERR_ARGUMENT;

    size_t length = strlen(name);
    if (length == 0 || length > 255)
        return HFS_ERR_ARGUMENT;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c >= 0x80u || c == '/')
            return HFS_ERR_ARGUMENT;
    }

    struct hfs_btree_leaf_iterator iterator;
    int rc = hfs_btree_leaf_init(&catalog->btree, &iterator);
    if (rc != HFS_OK)
        return rc;

    const uint8_t *record;
    size_t record_length;
    while ((rc = hfs_btree_leaf_next(&catalog->btree, &iterator, &record,
                                     &record_length)) > 0) {
        uint32_t key_parent;
        uint16_t units;
        const uint8_t *key_name;
        rc = parse_key(record, record_length, &key_parent, &units, &key_name);
        if (rc != HFS_OK)
            return rc;
        if (key_parent < parent)
            continue;
        if (key_parent > parent)
            return HFS_ERR_NOT_FOUND;
        if (!ascii_key_matches(key_name, units, name))
            continue;

        rc = parse_record(catalog->btree.volume, record, record_length, entry);
        if (rc < 0)
            return rc;
        if (rc == 1 && entry->parent == parent)
            return HFS_OK;
    }
    if (rc < 0)
        return rc;
    return HFS_ERR_NOT_FOUND;
}

int hfs_catalog_read_file(struct hfs_catalog *catalog,
                          const struct hfs_entry *entry, uint64_t offset,
                          void *buffer, size_t length, size_t *done)
{
    if (catalog == NULL || entry == NULL || done == NULL ||
        (buffer == NULL && length != 0))
        return HFS_ERR_ARGUMENT;
    *done = 0;
    if (entry->kind == 1)
        return HFS_ERR_IS_DIR;
    if (entry->kind != 2 || entry->unsupported)
        return HFS_ERR_UNSUPPORTED;
    if (offset >= entry->data.size || length == 0)
        return HFS_OK;
    if (length > entry->data.size - offset)
        length = (size_t)(entry->data.size - offset);

    int rc = hfs_fork_read(catalog->btree.volume, &entry->data, offset,
                           buffer, length);
    if (rc != HFS_OK)
        return rc;
    *done = length;
    return HFS_OK;
}
