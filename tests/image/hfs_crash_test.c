/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Journal crash-injection test for the native HFS+ writer.
 *
 * This is a disposable-image test. It writes one create transaction with a
 * configurable write-failure point, then a second process reopens the image,
 * replays any committed journal, and verifies the catalog is consistent.
 * tests/image/run-crash-macos.sh runs fsck_hfs after every recovery.
 */
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "hfs_volume.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CRASH_NAME "Crash Test"

struct io {
    int fd;
    unsigned long writes;
    unsigned long fail_at;
    int failed;
};

struct image {
    struct io io;
    struct hfs_volume volume;
    struct hfs_journal journal;
    struct hfs_catalog catalog;
    uint8_t scratch[32768];
};

static int read_at(void *context, uint64_t offset, void *buffer, size_t length)
{
    struct io *io = context;
    ssize_t done = pread(io->fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int write_at(void *context, uint64_t offset, const void *buffer,
                    size_t length)
{
    struct io *io = context;
    if (io->fail_at != 0 && io->writes + 1u >= io->fail_at) {
        io->failed = 1;
        return -1;
    }
    ssize_t done = pwrite(io->fd, buffer, length, (off_t)offset);
    if (done != (ssize_t)length)
        return -1;
    ++io->writes;
    return 0;
}

static int flush_at(void *context)
{
    struct io *io = context;
    return fsync(io->fd) == 0 ? 0 : -1;
}

static int open_image(const char *path, struct image *image, int replay)
{
    memset(image, 0, sizeof(*image));
    image->io.fd = open(path, O_RDWR);
    if (image->io.fd < 0)
        return HFS_ERR_IO;

    off_t end = lseek(image->io.fd, 0, SEEK_END);
    if (end < 0)
        return HFS_ERR_IO;
    int rc = hfs_volume_probe(&image->volume, read_at, &image->io, 0,
                              (uint64_t)end);
    if (rc != HFS_OK && rc != HFS_ERR_DIRTY)
        return rc;
    if (!image->volume.journaled)
        return HFS_ERR_UNSUPPORTED;

    rc = hfs_journal_open(&image->journal, &image->volume, write_at, flush_at);
    if (rc != HFS_OK)
        return rc;
    if (!hfs_journal_is_clean(&image->journal)) {
        if (!replay)
            return HFS_ERR_DIRTY;
        rc = hfs_journal_replay(&image->journal);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_volume_probe(&image->volume, read_at, &image->io, 0,
                              (uint64_t)end);
        if (rc != HFS_OK)
            return rc;
    }
    return hfs_catalog_open(&image->catalog, &image->volume, image->scratch,
                            sizeof(image->scratch));
}

static void close_image(struct image *image)
{
    if (image->io.fd >= 0)
        close(image->io.fd);
}

static const uint8_t *crash_name(size_t *units)
{
    static const uint8_t name[] = {
        0, 'C', 0, 'r', 0, 'a', 0, 's', 0, 'h', 0, ' ', 0, 'T', 0, 'e',
        0, 's', 0, 't'
    };
    *units = sizeof(name) / 2u;
    return name;
}

static int inject_crash(struct image *image, unsigned long fail_at)
{
    size_t units = 0;
    const uint8_t *name = crash_name(&units);
    uint32_t cnid = 0;
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_create_file(&image->catalog, &tx, 2, name, units, 0644,
                                 hfs_catalog_key_compare, NULL, &cnid);
    if (rc != HFS_OK) {
        hfs_txn_abort(&tx);
        return rc;
    }

    image->io.fail_at = fail_at;
    rc = hfs_txn_commit(&tx);
    if (rc == HFS_OK) {
        printf("commit-ok writes=%lu cnid=%u\n", image->io.writes, cnid);
        return HFS_OK;
    }
    if (rc == HFS_ERR_IO && image->io.failed) {
        printf("commit-cut writes=%lu\n", image->io.writes);
        return HFS_OK;
    }
    return rc;
}

static int recover_image(struct image *image)
{
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup_ascii(&image->catalog, 2, CRASH_NAME, &entry);
    if (rc == HFS_ERR_NOT_FOUND) {
        printf("recovered present=0\n");
        return HFS_OK;
    }
    if (rc != HFS_OK)
        return rc;
    if (entry.kind != 2 || entry.data.size != 0)
        return HFS_ERR_FORMAT;
    printf("recovered present=1 size=0\n");
    return HFS_OK;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s IMAGE crash CUT | recover\n", argv[0]);
        return 2;
    }
    struct image image;
    if (strcmp(argv[2], "crash") == 0) {
        if (argc != 4) {
            fprintf(stderr, "usage: %s IMAGE crash CUT\n", argv[0]);
            return 2;
        }
        unsigned long cut = strtoul(argv[3], NULL, 10);
        int rc = open_image(argv[1], &image, 1);
        if (rc != HFS_OK) {
            fprintf(stderr, "open: %s\n", hfs_strerror(rc));
            return 1;
        }
        rc = inject_crash(&image, cut);
        close_image(&image);
        if (rc != HFS_OK) {
            fprintf(stderr, "crash: %s\n", hfs_strerror(rc));
            return 1;
        }
        return 0;
    }
    if (strcmp(argv[2], "recover") == 0) {
        if (argc != 3) {
            fprintf(stderr, "usage: %s IMAGE recover\n", argv[0]);
            return 2;
        }
        int rc = open_image(argv[1], &image, 1);
        if (rc != HFS_OK) {
            fprintf(stderr, "open: %s\n", hfs_strerror(rc));
            return 1;
        }
        rc = recover_image(&image);
        if (rc == HFS_OK && !hfs_journal_is_clean(&image.journal))
            rc = HFS_ERR_DIRTY;
        close_image(&image);
        if (rc != HFS_OK) {
            fprintf(stderr, "recover: %s\n", hfs_strerror(rc));
            return 1;
        }
        return 0;
    }
    fprintf(stderr, "unknown mode: %s\n", argv[2]);
    return 2;
}
