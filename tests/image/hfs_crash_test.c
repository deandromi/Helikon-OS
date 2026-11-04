/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Journal crash-injection test for the native HFS+ writer.
 *
 * This is a disposable-image test. It prepares a known file, injects a write
 * failure at one point of a catalog/data operation, then a second process
 * reopens the image, replays any committed journal and verifies the resulting
 * state. tests/image/run-crash-macos.sh runs fsck_hfs after every recovery.
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
#define RENAMED_NAME "Crash Renamed"
#define DATA_SIZE 4096u
#define DATA_PATTERN 0x5au
#define OVERWRITE_SIZE 512u
#define OVERWRITE_PATTERN 0xc3u

enum scenario {
    SCENARIO_CREATE,
    SCENARIO_APPEND,
    SCENARIO_TRUNCATE,
    SCENARIO_DELETE,
    SCENARIO_RENAME,
    SCENARIO_OVERWRITE
};

struct io {
    int fd;
    unsigned long writes;
    unsigned long fail_at;
    unsigned long flushes;
    unsigned long fail_flush_at;
    unsigned long partial_at;
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
    if (io->partial_at != 0 && io->writes + 1u >= io->partial_at) {
        size_t part = length / 2u;
        if (part == 0)
            part = 1;
        (void)pwrite(io->fd, buffer, part, (off_t)offset);
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
    ++io->flushes;
    if (io->fail_flush_at != 0 && io->flushes >= io->fail_flush_at) {
        io->failed = 1;
        return -1;
    }
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

static int parse_scenario(const char *name, enum scenario *scenario)
{
    if (strcmp(name, "create") == 0) {
        *scenario = SCENARIO_CREATE;
    } else if (strcmp(name, "append") == 0) {
        *scenario = SCENARIO_APPEND;
    } else if (strcmp(name, "truncate") == 0) {
        *scenario = SCENARIO_TRUNCATE;
    } else if (strcmp(name, "delete") == 0) {
        *scenario = SCENARIO_DELETE;
    } else if (strcmp(name, "rename") == 0) {
        *scenario = SCENARIO_RENAME;
    } else if (strcmp(name, "overwrite") == 0) {
        *scenario = SCENARIO_OVERWRITE;
    } else {
        return HFS_ERR_ARGUMENT;
    }
    return HFS_OK;
}

static int lookup_name(struct image *image, const char *name,
                       struct hfs_entry *entry)
{
    return hfs_catalog_lookup_ascii(&image->catalog, 2, name, entry);
}

static int lookup(struct image *image, struct hfs_entry *entry)
{
    return lookup_name(image, CRASH_NAME, entry);
}

static const uint8_t *renamed_name(size_t *units)
{
    static const uint8_t name[] = {
        0, 'C', 0, 'r', 0, 'a', 0, 's', 0, 'h', 0, ' ', 0, 'R', 0, 'e',
        0, 'n', 0, 'a', 0, 'm', 0, 'e', 0, 'd'
    };
    *units = sizeof(name) / 2u;
    return name;
}

static int read_data(struct image *image, const struct hfs_entry *entry)
{
    if (entry->data.size != DATA_SIZE)
        return HFS_ERR_FORMAT;
    uint8_t data[DATA_SIZE];
    size_t done = 0;
    int rc = hfs_catalog_read_file(&image->catalog, entry, 0, data,
                                   sizeof(data), &done);
    if (rc != HFS_OK || done != sizeof(data))
        return rc != HFS_OK ? rc : HFS_ERR_FORMAT;
    for (size_t i = 0; i < sizeof(data); ++i) {
        if (data[i] != DATA_PATTERN)
            return HFS_ERR_FORMAT;
    }
    return HFS_OK;
}

static int prepare(struct image *image, enum scenario scenario)
{
    size_t units = 0;
    const uint8_t *name = crash_name(&units);
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;

    if (scenario == SCENARIO_CREATE || scenario == SCENARIO_DELETE ||
        scenario == SCENARIO_RENAME) {
        uint32_t cnid = 0;
        rc = hfs_catalog_create_file(&image->catalog, &tx, 2, name, units,
                                     0644, hfs_catalog_key_compare, NULL,
                                     &cnid);
    } else if (scenario == SCENARIO_APPEND) {
        uint8_t data[DATA_SIZE];
        memset(data, DATA_PATTERN, sizeof(data));
        rc = hfs_catalog_append_file(&image->catalog, &tx, 2, name, units, 0,
                                     data, sizeof(data),
                                     hfs_catalog_key_compare, NULL);
    } else {
        uint8_t data[DATA_SIZE];
        memset(data, DATA_PATTERN, sizeof(data));
        rc = hfs_catalog_append_file(&image->catalog, &tx, 2, name, units, 0,
                                     data, sizeof(data),
                                     hfs_catalog_key_compare, NULL);
    }

    if (rc == HFS_OK)
        return hfs_txn_commit(&tx);
    hfs_txn_abort(&tx);
    return rc;
}

static int inject(struct image *image, enum scenario scenario,
                  unsigned long fail_at, int failure_mode)
{
    size_t units = 0;
    const uint8_t *name = crash_name(&units);
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;

    if (scenario == SCENARIO_CREATE) {
        uint32_t cnid = 0;
        rc = hfs_catalog_create_file(&image->catalog, &tx, 2, name, units,
                                     0644, hfs_catalog_key_compare, NULL,
                                     &cnid);
    } else if (scenario == SCENARIO_APPEND) {
        uint8_t data[DATA_SIZE];
        memset(data, DATA_PATTERN, sizeof(data));
        rc = hfs_catalog_append_file(&image->catalog, &tx, 2, name, units, 0,
                                     data, sizeof(data),
                                     hfs_catalog_key_compare, NULL);
    } else if (scenario == SCENARIO_TRUNCATE) {
        rc = hfs_catalog_truncate_file(&image->catalog, &tx, 2, name, units,
                                       hfs_catalog_key_compare, NULL);
    } else if (scenario == SCENARIO_OVERWRITE) {
        uint8_t data[OVERWRITE_SIZE];
        memset(data, OVERWRITE_PATTERN, sizeof(data));
        rc = hfs_catalog_overwrite_file(&image->catalog, &tx, 2, name, units,
                                        0, data, sizeof(data),
                                        hfs_catalog_key_compare, NULL);
    } else if (scenario == SCENARIO_RENAME) {
        struct hfs_entry entry;
        rc = lookup(image, &entry);
        if (rc == HFS_OK) {
            size_t new_units = 0;
            const uint8_t *new_name = renamed_name(&new_units);
            rc = hfs_catalog_rename(&image->catalog, &tx, 2, name, units,
                                    entry.id, 2, new_name, new_units,
                                    hfs_catalog_key_compare, NULL);
        }
    } else {
        struct hfs_entry entry;
        rc = lookup(image, &entry);
        if (rc == HFS_OK) {
            rc = hfs_catalog_delete_file(&image->catalog, &tx, 2, name, units,
                                         entry.id, hfs_catalog_key_compare,
                                         NULL);
        }
    }

    if (rc != HFS_OK) {
        hfs_txn_abort(&tx);
        return rc;
    }

    if (failure_mode == 1)
        image->io.fail_flush_at = fail_at;
    else if (failure_mode == 2)
        image->io.partial_at = fail_at;
    else
        image->io.fail_at = fail_at;
    rc = hfs_txn_commit(&tx);
    if (rc == HFS_OK) {
        printf("commit-ok writes=%lu flushes=%lu\n", image->io.writes,
               image->io.flushes);
        return HFS_OK;
    }
    if (rc == HFS_ERR_IO && image->io.failed) {
        printf("%s-cut writes=%lu flushes=%lu\n",
               failure_mode == 1 ? "flush" :
               failure_mode == 2 ? "partial" : "commit",
               image->io.writes, image->io.flushes);
        return HFS_OK;
    }
    return rc;
}

static int recover(struct image *image, enum scenario scenario)
{
    if (scenario == SCENARIO_RENAME) {
        struct hfs_entry old_entry;
        struct hfs_entry new_entry;
        int old_rc = lookup_name(image, CRASH_NAME, &old_entry);
        int new_rc = lookup_name(image, RENAMED_NAME, &new_entry);
        if (old_rc == HFS_OK && new_rc == HFS_ERR_NOT_FOUND) {
            if (old_entry.kind != 2)
                return HFS_ERR_FORMAT;
            printf("recovered old=1 new=0\n");
            return HFS_OK;
        }
        if (old_rc == HFS_ERR_NOT_FOUND && new_rc == HFS_OK) {
            if (new_entry.kind != 2)
                return HFS_ERR_FORMAT;
            printf("recovered old=0 new=1\n");
            return HFS_OK;
        }
        return HFS_ERR_FORMAT;
    }

    struct hfs_entry entry;
    int rc = lookup(image, &entry);
    if (rc == HFS_ERR_NOT_FOUND) {
        if (scenario != SCENARIO_CREATE && scenario != SCENARIO_DELETE)
            return HFS_ERR_FORMAT;
        printf("recovered present=0\n");
        return HFS_OK;
    }

    if (scenario == SCENARIO_OVERWRITE) {
        if (rc != HFS_OK || entry.kind != 2 ||
            entry.data.size != DATA_SIZE)
            return HFS_ERR_FORMAT;
        uint8_t data[DATA_SIZE];
        size_t done = 0;
        rc = hfs_catalog_read_file(&image->catalog, &entry, 0, data,
                                   sizeof(data), &done);
        if (rc != HFS_OK || done != sizeof(data))
            return rc != HFS_OK ? rc : HFS_ERR_FORMAT;

        int old = 1;
        int replacement = 1;
        for (size_t i = 0; i < sizeof(data); ++i) {
            if (data[i] != DATA_PATTERN)
                old = 0;
            uint8_t expected = i < OVERWRITE_SIZE ? OVERWRITE_PATTERN
                                                  : DATA_PATTERN;
            if (data[i] != expected)
                replacement = 0;
        }
        if (old == replacement)
            return HFS_ERR_FORMAT;
        printf("recovered old=%d new=%d\n", old, replacement);
        return HFS_OK;
    }
    if (rc != HFS_OK)
        return rc;

    if (scenario == SCENARIO_CREATE || scenario == SCENARIO_DELETE ||
        scenario == SCENARIO_RENAME) {
        if (entry.kind != 2 || entry.data.size != 0)
            return HFS_ERR_FORMAT;
        printf("recovered present=1 size=0\n");
        return HFS_OK;
    }

    if (entry.kind != 2)
        return HFS_ERR_FORMAT;
    if (entry.data.size == 0) {
        printf("recovered present=1 size=0\n");
        return HFS_OK;
    }
    rc = read_data(image, &entry);
    if (rc != HFS_OK)
        return rc;
    printf("recovered present=1 size=%u\n", DATA_SIZE);
    return HFS_OK;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s IMAGE setup SCENARIO | crash SCENARIO CUT "
                "[flush|partial] | recover SCENARIO\n", argv[0]);
        return 2;
    }

    const char *mode = argv[2];
    enum scenario scenario;
    if (parse_scenario(argv[3], &scenario) != HFS_OK) {
        fprintf(stderr, "unknown scenario: %s\n", argv[3]);
        return 2;
    }

    struct image image;
    int rc;
    if (strcmp(mode, "setup") == 0) {
        if (argc != 4)
            return 2;
        rc = open_image(argv[1], &image, 1);
        if (rc == HFS_OK)
            rc = prepare(&image, scenario);
    } else if (strcmp(mode, "crash") == 0) {
        if (argc != 5 && argc != 6)
            return 2;
        int failure_mode = 0;
        if (argc == 6) {
            if (strcmp(argv[5], "flush") == 0)
                failure_mode = 1;
            else if (strcmp(argv[5], "partial") == 0)
                failure_mode = 2;
            else
                return 2;
        }
        unsigned long cut = strtoul(argv[4], NULL, 10);
        rc = open_image(argv[1], &image, 1);
        if (rc == HFS_OK)
            rc = inject(&image, scenario, cut, failure_mode);
    } else if (strcmp(mode, "recover") == 0) {
        if (argc != 4)
            return 2;
        rc = open_image(argv[1], &image, 1);
        if (rc == HFS_OK) {
            rc = recover(&image, scenario);
            if (rc == HFS_OK && !hfs_journal_is_clean(&image.journal))
                rc = HFS_ERR_DIRTY;
        }
    } else {
        fprintf(stderr, "unknown mode: %s\n", mode);
        return 2;
    }

    close_image(&image);
    if (rc != HFS_OK) {
        fprintf(stderr, "%s %s: %s\n", mode, argv[3], hfs_strerror(rc));
        return 1;
    }
    return 0;
}
