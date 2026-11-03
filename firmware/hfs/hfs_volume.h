/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_VOLUME_H
#define HELIKON_HFS_VOLUME_H

#include <stddef.h>
#include <stdint.h>

#define HFS_VOLUME_HEADER_OFFSET 1024u
#define HFS_VOLUME_HEADER_SIZE 512u
#define HFS_EXTENT_COUNT 8u
#define HFS_MAX_NODE_SIZE 32768u

enum hfs_error {
    HFS_OK = 0,
    HFS_ERR_IO = -1,
    HFS_ERR_NOT_HFS = -2,
    HFS_ERR_FORMAT = -3,
    HFS_ERR_UNSUPPORTED = -4,
    HFS_ERR_DIRTY = -5,
    HFS_ERR_ARGUMENT = -6,
    HFS_ERR_BUFFER = -7
};

struct hfs_extent {
    uint32_t start;
    uint32_t count;
};

struct hfs_fork {
    uint64_t size;
    uint32_t blocks;
    struct hfs_extent extents[HFS_EXTENT_COUNT];
};

/*
 * The callback must read exactly length bytes and return zero on success.
 * It must not write to the device.
 */
typedef int (*hfs_read_fn)(void *context, uint64_t offset,
                           void *buffer, size_t length);

struct hfs_volume {
    hfs_read_fn read;
    void *context;
    uint64_t base;
    uint64_t length;
    uint32_t block_size;
    uint32_t blocks;
    uint32_t free_blocks;
    uint32_t attributes;
    uint32_t write_count;
    uint32_t modify_date;
    struct hfs_fork allocation;
    struct hfs_fork extents;
    struct hfs_fork catalog;
    struct hfs_fork attributes_file;
    struct hfs_fork startup;
    uint64_t journal_offset;
    uint64_t journal_size;
    uint64_t journal_info_offset;
    uint64_t journal_info_size;
    uint8_t journaled;
    uint8_t recovery;
};

/*
 * Validate the primary (or alternate) HFS+ volume header and its journal
 * metadata. This is a probe, not a full mount: it does not parse the catalog
 * B-tree yet.
 */
int hfs_volume_probe(struct hfs_volume *volume, hfs_read_fn read,
                     void *context, uint64_t base, uint64_t length);

/*
 * Read a fork whose extents are fully inline. Extents-overflow reads are a
 * later module; returning HFS_ERR_UNSUPPORTED is deliberate, not a fallback.
 */
int hfs_fork_read(const struct hfs_volume *volume,
                  const struct hfs_fork *fork, uint64_t offset,
                  void *buffer, size_t length);

const char *hfs_strerror(int error);

#endif
