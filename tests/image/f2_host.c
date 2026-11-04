/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host integration test for F2/T-055 bootstate promotion.
 *
 * Opens a disposable journaled HFS+ image, reads /.helikon/bootstate through
 * the HFS+ catalog read path, promotes the TRYING slot to GOOD through
 * hfs_txn, commits, and reads the file back. The shell driver runs fsck_hfs
 * before and after and then asks the real loader host core to select GOOD.
 */
#include "bootstate.h"
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "hfs_volume.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SCRATCH_SIZE 32768u
#define BOOTSTATE_PATH "/.helikon/bootstate"
#define BOOTSTATE_NAME_UNITS 9u
#define TEST_BOARD 6u

struct image {
    int fd;
    struct hfs_volume volume;
    struct hfs_journal journal;
    struct hfs_catalog catalog;
    uint8_t scratch[SCRATCH_SIZE];
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

    rc = hfs_journal_open(&image->journal, &image->volume, write_at,
                          flush_at);
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
    rc = hfs_catalog_open(&image->catalog, &image->volume, image->scratch,
                          sizeof(image->scratch));
    if (rc != HFS_OK)
        return rc;

    time_t now = time(NULL);
    if (now > 0)
        image->catalog.now = (uint32_t)((int64_t)now + 2082844800LL);
    return HFS_OK;
}

static void close_image(struct image *image)
{
    if (image->fd >= 0)
        close(image->fd);
}

static int read_bootstate(struct image *image, struct hfs_entry *entry,
                          uint8_t *data)
{
    int rc = hfs_catalog_lookup(&image->catalog, BOOTSTATE_PATH, entry);
    if (rc != HFS_OK)
        return rc;
    if (entry->kind != 2u || entry->unsupported != 0u ||
        entry->data.size != HELIKON_BOOTSTATE_SIZE)
        return HFS_ERR_FORMAT;

    size_t done = 0;
    rc = hfs_catalog_read_file(&image->catalog, entry, 0, data,
                               HELIKON_BOOTSTATE_SIZE, &done);
    if (rc != HFS_OK)
        return rc;
    return done == HELIKON_BOOTSTATE_SIZE ? HFS_OK : HFS_ERR_IO;
}

static int promote(const char *path)
{
    struct image image;
    int rc = open_image(path, &image);
    if (rc != HFS_OK)
        return rc;

    struct hfs_entry entry;
    uint8_t data[HELIKON_BOOTSTATE_SIZE];
    rc = read_bootstate(&image, &entry, data);
    if (rc != HFS_OK) {
        close_image(&image);
        return rc;
    }

    struct helikon_bootstate bootstate;
    rc = helikon_bootstate_parse(data, sizeof(data), TEST_BOARD, &bootstate);
    if (rc != HFS_OK) {
        close_image(&image);
        return rc;
    }

    int match = -1;
    for (unsigned int i = 0; i < HELIKON_BOOTSTATE_SLOTS; ++i) {
        if (!bootstate.slots[i].valid ||
            bootstate.slots[i].state != HELIKON_BOOTSTATE_TRYING)
            continue;
        if (match < 0 ||
            bootstate.slots[i].sequence >
                bootstate.slots[match].sequence)
            match = (int)i;
    }
    if (match < 0) {
        for (unsigned int i = 0; i < HELIKON_BOOTSTATE_SLOTS; ++i) {
            if (bootstate.slots[i].valid &&
                bootstate.slots[i].state == HELIKON_BOOTSTATE_GOOD) {
                printf("already GOOD sequence %u\n",
                       (unsigned)bootstate.slots[i].sequence);
                close_image(&image);
                return HFS_OK;
            }
        }
        close_image(&image);
        return HFS_ERR_BOOTSTATE;
    }

    const struct helikon_bootstate_slot *slot = &bootstate.slots[match];
    uint32_t sequence = 0;
    int changed = 0;
    rc = helikon_bootstate_promote(data, sizeof(data), TEST_BOARD,
                                   slot->image_version,
                                   slot->load_address,
                                   slot->image_size,
                                   slot->image_sha256,
                                   &sequence, &changed);
    if (rc != HFS_OK) {
        close_image(&image);
        return rc;
    }
    if (!changed) {
        printf("already GOOD sequence %u\n", (unsigned)sequence);
        close_image(&image);
        return HFS_OK;
    }

    static const uint8_t name[BOOTSTATE_NAME_UNITS * 2u] = {
        0, 'b', 0, 'o', 0, 'o', 0, 't', 0, 's', 0, 't', 0, 'a', 0, 't',
        0, 'e'
    };
    static struct hfs_txn tx;
    rc = hfs_txn_begin(&tx, &image.journal);
    if (rc != HFS_OK) {
        close_image(&image);
        return rc;
    }
    rc = hfs_catalog_overwrite_file(&image.catalog, &tx, entry.parent, name,
                                    BOOTSTATE_NAME_UNITS, 0, data,
                                    sizeof(data), hfs_catalog_key_compare,
                                    NULL);
    if (rc != HFS_OK) {
        hfs_txn_abort(&tx);
        close_image(&image);
        return rc;
    }
    rc = hfs_txn_commit(&tx);
    if (rc != HFS_OK) {
        close_image(&image);
        return rc;
    }

    uint8_t readback[HELIKON_BOOTSTATE_SIZE];
    struct hfs_entry readback_entry;
    rc = read_bootstate(&image, &readback_entry, readback);
    if (rc != HFS_OK) {
        close_image(&image);
        return rc;
    }
    if (memcmp(readback, data, sizeof(readback)) != 0) {
        close_image(&image);
        return HFS_ERR_FORMAT;
    }

    struct helikon_bootstate after;
    rc = helikon_bootstate_parse(readback, sizeof(readback), TEST_BOARD,
                                 &after);
    if (rc != HFS_OK) {
        close_image(&image);
        return rc;
    }
    int found = 0;
    for (unsigned int i = 0; i < HELIKON_BOOTSTATE_SLOTS; ++i) {
        if (!after.slots[i].valid ||
            after.slots[i].state != HELIKON_BOOTSTATE_GOOD)
            continue;
        if (after.slots[i].sequence == sequence &&
            memcmp(after.slots[i].image_sha256, slot->image_sha256,
                   HELIKON_SHA256_SIZE) == 0)
            found = 1;
    }
    if (!found) {
        close_image(&image);
        return HFS_ERR_FORMAT;
    }

    printf("promoted sequence %u, readback GOOD\n", (unsigned)sequence);
    close_image(&image);
    return HFS_OK;
}

int main(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[2], "promote") != 0) {
        fprintf(stderr, "usage: %s IMAGE promote\n", argv[0]);
        return 2;
    }
    int rc = promote(argv[1]);
    if (rc != HFS_OK) {
        fprintf(stderr, "promote: %s (%d)\n", hfs_strerror(rc), rc);
        return 1;
    }
    return 0;
}
