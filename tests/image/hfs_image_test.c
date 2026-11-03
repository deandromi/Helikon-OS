/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Disposable-image integration test for the native HFS+ writer.
 *
 * This is not a device test and it must only run on an image created by
 * tests/image/run-macos.sh. It opens a journaled HFS+ image, performs one
 * catalog/data operation, commits it through the journal and verifies the
 * result through the read core. The shell driver runs fsck_hfs between steps.
 */
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "hfs_volume.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define TEST_NAME "Helikon Test"
#define TEST_SIZE 4096u
#define TEST_PATTERN 0x5au

struct image {
    int fd;
    struct hfs_volume volume;
    struct hfs_journal journal;
    struct hfs_catalog catalog;
    uint8_t scratch[32768];
};

struct stats {
    uint32_t file_count;
    uint32_t folder_count;
    uint32_t free_blocks;
    uint32_t write_count;
};

static int read_at(void *context, uint64_t offset, void *buffer, size_t length)
{
    int fd = *(int *)context;
    ssize_t done = pread(fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int write_at(void *context, uint64_t offset, const void *buffer,
                    size_t length)
{
    int fd = *(int *)context;
    ssize_t done = pwrite(fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int flush_at(void *context)
{
    return fsync(*(int *)context) == 0 ? 0 : -1;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int read_stats(struct image *image, struct stats *stats)
{
    uint8_t header[HFS_VOLUME_HEADER_SIZE];
    if (pread(image->fd, header, sizeof(header),
              HFS_VOLUME_HEADER_OFFSET) != (ssize_t)sizeof(header))
        return HFS_ERR_IO;
    stats->file_count = be32(header + 32);
    stats->folder_count = be32(header + 36);
    stats->free_blocks = be32(header + 48);
    stats->write_count = be32(header + 68);
    return HFS_OK;
}

static int open_image(const char *path, struct image *image)
{
    memset(image, 0, sizeof(*image));
    image->fd = open(path, O_RDWR);
    if (image->fd < 0)
        return HFS_ERR_IO;

    off_t end = lseek(image->fd, 0, SEEK_END);
    if (end < 0)
        return HFS_ERR_IO;
    int rc = hfs_volume_probe(&image->volume, read_at, &image->fd, 0,
                              (uint64_t)end);
    if (rc != HFS_OK && rc != HFS_ERR_DIRTY)
        return rc;
    if (!image->volume.journaled)
        return HFS_ERR_UNSUPPORTED;

    rc = hfs_journal_open(&image->journal, &image->volume, write_at, flush_at);
    if (rc != HFS_OK)
        return rc;
    if (!hfs_journal_is_clean(&image->journal)) {
        rc = hfs_journal_replay(&image->journal);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_volume_probe(&image->volume, read_at, &image->fd, 0,
                              (uint64_t)end);
        if (rc != HFS_OK)
            return rc;
    }
    return hfs_catalog_open(&image->catalog, &image->volume, image->scratch,
                            sizeof(image->scratch));
}

static void close_image(struct image *image)
{
    if (image->fd >= 0)
        close(image->fd);
}

static const uint8_t *test_name(size_t *units)
{
    static const uint8_t name[] = {
        0, 'H', 0, 'e', 0, 'l', 0, 'i', 0, 'k', 0, 'o', 0, 'n', 0, ' ',
        0, 'T', 0, 'e', 0, 's', 0, 't'
    };
    *units = sizeof(name) / 2u;
    return name;
}

static int commit(struct hfs_txn *tx, int rc)
{
    if (rc == HFS_OK)
        return hfs_txn_commit(tx);
    hfs_txn_abort(tx);
    return rc;
}

static int create_and_append(struct image *image)
{
    struct stats before;
    struct stats after_create;
    struct stats after_append;
    int rc = read_stats(image, &before);
    if (rc != HFS_OK)
        return rc;

    size_t units = 0;
    const uint8_t *name = test_name(&units);
    uint32_t cnid = 0;
    struct hfs_txn tx;
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_create_file(&image->catalog, &tx, 2, name, units, 0644,
                                 hfs_catalog_key_compare, NULL, &cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;
    if (read_stats(image, &after_create) != HFS_OK)
        return HFS_ERR_IO;

    uint8_t data[TEST_SIZE];
    memset(data, TEST_PATTERN, sizeof(data));
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_append_file(&image->catalog, &tx, 2, name, units, 0,
                                 data, sizeof(data), hfs_catalog_key_compare,
                                 NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;
    if (read_stats(image, &after_append) != HFS_OK)
        return HFS_ERR_IO;

    if (after_create.file_count != before.file_count + 1u)
        return HFS_ERR_FORMAT;
    if (after_append.free_blocks != before.free_blocks - 1u)
        return HFS_ERR_FORMAT;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup_ascii(&image->catalog, 2, TEST_NAME, &entry);
    if (rc != HFS_OK)
        return rc;
    if (entry.kind != 2 || entry.data.size != TEST_SIZE)
        return HFS_ERR_FORMAT;

    uint8_t readback[TEST_SIZE];
    size_t done = 0;
    rc = hfs_catalog_read_file(&image->catalog, &entry, 0, readback,
                               sizeof(readback), &done);
    if (rc != HFS_OK || done != sizeof(readback))
        return rc != HFS_OK ? rc : HFS_ERR_FORMAT;
    for (size_t i = 0; i < sizeof(readback); ++i) {
        if (readback[i] != TEST_PATTERN)
            return HFS_ERR_FORMAT;
    }

    printf("created cnid=%u file_count=%u free_blocks=%u\n", cnid,
           after_append.file_count, after_append.free_blocks);
    printf("content verified: %u bytes of 0x%02x\n", TEST_SIZE, TEST_PATTERN);
    return HFS_OK;
}

static int delete_file(struct image *image)
{
    struct stats before;
    struct stats after;
    int rc = read_stats(image, &before);
    if (rc != HFS_OK)
        return rc;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup_ascii(&image->catalog, 2, TEST_NAME, &entry);
    if (rc != HFS_OK)
        return rc;

    size_t units = 0;
    const uint8_t *name = test_name(&units);
    struct hfs_txn tx;
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_truncate_file(&image->catalog, &tx, 2, name, units,
                                   hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;

    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_delete_file(&image->catalog, &tx, 2, name, units,
                                 entry.id, hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;

    if (read_stats(image, &after) != HFS_OK)
        return HFS_ERR_IO;
    if (after.file_count + 1u != before.file_count)
        return HFS_ERR_FORMAT;
    if (after.free_blocks != before.free_blocks + 1u)
        return HFS_ERR_FORMAT;
    rc = hfs_catalog_lookup_ascii(&image->catalog, 2, TEST_NAME, &entry);
    if (rc != HFS_ERR_NOT_FOUND)
        return HFS_ERR_FORMAT;

    printf("deleted file_count=%u free_blocks=%u\n", after.file_count,
           after.free_blocks);
    return HFS_OK;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s IMAGE create|delete\n", argv[0]);
        return 2;
    }
    struct image image;
    int rc = open_image(argv[1], &image);
    if (rc != HFS_OK) {
        fprintf(stderr, "open: %s\n", hfs_strerror(rc));
        return 1;
    }
    if (strcmp(argv[2], "create") == 0) {
        rc = create_and_append(&image);
    } else if (strcmp(argv[2], "delete") == 0) {
        rc = delete_file(&image);
    } else {
        fprintf(stderr, "unknown mode: %s\n", argv[2]);
        rc = HFS_ERR_ARGUMENT;
    }
    close_image(&image);
    if (rc != HFS_OK) {
        fprintf(stderr, "%s: %s\n", argv[2], hfs_strerror(rc));
        return 1;
    }
    return 0;
}
