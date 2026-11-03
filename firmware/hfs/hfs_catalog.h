/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_CATALOG_H
#define HELIKON_HFS_CATALOG_H

#include "hfs_btree.h"
#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

#define HFS_NAME_UTF8_MAX 1024u

struct hfs_entry {
    uint32_t id;
    uint32_t parent;
    uint16_t kind;
    uint16_t mode;
    uint8_t unsupported;
    uint8_t name_escaped;
    struct hfs_fork data;
    char name[HFS_NAME_UTF8_MAX];
};

struct hfs_catalog {
    struct hfs_btree btree;
    /* HFS+ timestamp (seconds since 1904); 0 leaves dates untouched. */
    uint32_t now;
};

struct hfs_catalog_iterator {
    uint32_t parent;
    struct hfs_btree_leaf_iterator leaf;
};

int hfs_catalog_open(struct hfs_catalog *catalog, struct hfs_volume *volume,
                     void *scratch, size_t scratch_size);

int hfs_catalog_iter_init(struct hfs_catalog *catalog, uint32_t parent,
                          struct hfs_catalog_iterator *iterator);

/* Returns 1 for an entry, 0 at end, or a negative HFS error. */
int hfs_catalog_iter_next(struct hfs_catalog *catalog,
                          struct hfs_catalog_iterator *iterator,
                          struct hfs_entry *entry);

/* Absolute path; components are exact UTF-8 names, no normalization. */
int hfs_catalog_lookup(struct hfs_catalog *catalog, const char *path,
                       struct hfs_entry *entry);

/*
 * Exact lookup for one ASCII name under `parent`. It compares the raw UTF-16BE
 * catalog key without decoding unrelated names, so a single malformed root
 * entry cannot hide a boot probe. Returns HFS_ERR_NOT_FOUND when absent.
 */
int hfs_catalog_lookup_ascii(struct hfs_catalog *catalog, uint32_t parent,
                             const char *name, struct hfs_entry *entry);

/* Inline file data only for now; non-inline forks return UNSUPPORTED. */
int hfs_catalog_read_file(struct hfs_catalog *catalog,
                          const struct hfs_entry *entry, uint64_t offset,
                          void *buffer, size_t length, size_t *done);

#endif
