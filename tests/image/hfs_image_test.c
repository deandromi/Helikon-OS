/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Disposable-image integration test for the native HFS+ writer.
 *
 * This is not a device test and it must only run on an image created by
 * tests/image/run-macos.sh. It opens a journaled HFS+ image, creates a folder
 * and a file, commits through the journal and verifies the result through the
 * read core. The shell driver runs fsck_hfs between steps.
 */
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_journal.h"
#include "hfs_extents.h"
#include "hfs_txn.h"
#include "hfs_volume.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TEST_FOLDER "Helikon Folder"
#define TEST_FOLDER_RENAMED "Helikon Folder Renamed"
#define TEST_NAME "Helikon Test"
#define TEST_NAME_RENAMED "Helikon Test Renamed"
#define TEST_SIZE 4096u
#define TEST_PATTERN 0x5au
#define HOLE_FILES 48u
#define OVERFLOW_BLOCKS 12u
#define OVERFLOW_NAME "Overflow File"
#define OVERFLOW_SIZE (OVERFLOW_BLOCKS * TEST_SIZE)
/*
 * A second, smaller overflowed file that is deleted directly: its fork has one
 * overflow extent, so the delete path has to free inline and overflow blocks
 * and clear the extents tree without a separate truncate call.
 */
#define DIRECT_BLOCKS 9u
#define DIRECT_NAME "Overflow Direct"
#define DIRECT_SIZE (DIRECT_BLOCKS * TEST_SIZE)

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

static const uint8_t *test_folder_name(size_t *units)
{
    static const uint8_t name[] = {
        0, 'H', 0, 'e', 0, 'l', 0, 'i', 0, 'k', 0, 'o', 0, 'n', 0, ' ',
        0, 'F', 0, 'o', 0, 'l', 0, 'd', 0, 'e', 0, 'r'
    };
    *units = sizeof(name) / 2u;
    return name;
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

static const uint8_t *renamed_folder_name(size_t *units)
{
    static const uint8_t name[] = {
        0, 'H', 0, 'e', 0, 'l', 0, 'i', 0, 'k', 0, 'o', 0, 'n', 0, ' ',
        0, 'F', 0, 'o', 0, 'l', 0, 'd', 0, 'e', 0, 'r', 0, ' ', 0, 'R',
        0, 'e', 0, 'n', 0, 'a', 0, 'm', 0, 'e', 0, 'd'
    };
    *units = sizeof(name) / 2u;
    return name;
}

static const uint8_t *renamed_test_name(size_t *units)
{
    static const uint8_t name[] = {
        0, 'H', 0, 'e', 0, 'l', 0, 'i', 0, 'k', 0, 'o', 0, 'n', 0, ' ',
        0, 'T', 0, 'e', 0, 's', 0, 't', 0, ' ', 0, 'R', 0, 'e', 0, 'n',
        0, 'a', 0, 'm', 0, 'e', 0, 'd'
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
    struct stats after_folder;
    struct stats after_file;
    struct stats after_append;
    int rc = read_stats(image, &before);
    if (rc != HFS_OK)
        return rc;

    size_t folder_units = 0;
    size_t units = 0;
    const uint8_t *folder_name = test_folder_name(&folder_units);
    const uint8_t *name = test_name(&units);
    uint32_t folder_cnid = 0;
    uint32_t file_cnid = 0;
    struct hfs_txn tx;

    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_create_folder(&image->catalog, &tx, 2, folder_name,
                                   folder_units, 0755,
                                   hfs_catalog_key_compare, NULL,
                                   &folder_cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;
    if (read_stats(image, &after_folder) != HFS_OK)
        return HFS_ERR_IO;

    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_create_file(&image->catalog, &tx, folder_cnid, name,
                                 units, 0644, hfs_catalog_key_compare, NULL,
                                 &file_cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;
    if (read_stats(image, &after_file) != HFS_OK)
        return HFS_ERR_IO;

    uint8_t data[TEST_SIZE];
    memset(data, TEST_PATTERN, sizeof(data));
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_append_file(&image->catalog, &tx, folder_cnid, name,
                                 units, 0, data, sizeof(data),
                                 hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;
    if (read_stats(image, &after_append) != HFS_OK)
        return HFS_ERR_IO;

    if (after_folder.folder_count != before.folder_count + 1u)
        return HFS_ERR_FORMAT;
    if (after_file.file_count != before.file_count + 1u)
        return HFS_ERR_FORMAT;
    if (after_append.free_blocks != before.free_blocks - 1u)
        return HFS_ERR_FORMAT;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup_ascii(&image->catalog, folder_cnid, TEST_NAME,
                                  &entry);
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

    printf("created folder cnid=%u file cnid=%u file_count=%u "
           "folder_count=%u free_blocks=%u\n",
           folder_cnid, file_cnid, after_append.file_count,
           after_append.folder_count, after_append.free_blocks);
    printf("content verified: %u bytes of 0x%02x\n", TEST_SIZE, TEST_PATTERN);
    return HFS_OK;
}

static int lookup_pair(struct image *image, const char *folder_ascii,
                       const char *file_ascii, struct hfs_entry *folder,
                       struct hfs_entry *file)
{
    int rc = hfs_catalog_lookup_ascii(&image->catalog, 2, folder_ascii,
                                      folder);
    if (rc != HFS_OK)
        return rc;
    return hfs_catalog_lookup_ascii(&image->catalog, folder->id, file_ascii,
                                    file);
}

static int rename_files(struct image *image)
{
    struct hfs_entry folder;
    struct hfs_entry file;
    int rc = lookup_pair(image, TEST_FOLDER, TEST_NAME, &folder, &file);
    if (rc != HFS_OK)
        return rc;

    size_t folder_units = 0;
    size_t units = 0;
    const uint8_t *folder_name = test_folder_name(&folder_units);
    const uint8_t *name = test_name(&units);
    size_t renamed_folder_units = 0;
    size_t renamed_units = 0;
    const uint8_t *renamed_folder = renamed_folder_name(&renamed_folder_units);
    const uint8_t *renamed_name = renamed_test_name(&renamed_units);

    struct hfs_txn tx;
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_rename(&image->catalog, &tx, folder.id, name, units,
                            file.id, folder.id, renamed_name, renamed_units,
                            hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;

    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_rename(&image->catalog, &tx, 2, folder_name,
                            folder_units, folder.id, 2, renamed_folder,
                            renamed_folder_units, hfs_catalog_key_compare,
                            NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;

    printf("renamed folder and file\n");
    return HFS_OK;
}

static int truncate_file(struct image *image)
{
    struct hfs_entry folder;
    struct hfs_entry file;
    int rc = lookup_pair(image, TEST_FOLDER_RENAMED, TEST_NAME_RENAMED,
                         &folder, &file);
    if (rc != HFS_OK)
        return rc;

    struct stats before;
    struct stats after;
    if (read_stats(image, &before) != HFS_OK)
        return HFS_ERR_IO;

    size_t units = 0;
    const uint8_t *name = renamed_test_name(&units);
    struct hfs_txn tx;
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_truncate_file(&image->catalog, &tx, folder.id, name,
                                   units, hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;

    if (read_stats(image, &after) != HFS_OK)
        return HFS_ERR_IO;
    if (after.free_blocks != before.free_blocks + 1u)
        return HFS_ERR_FORMAT;
    if (after.file_count != before.file_count ||
        after.folder_count != before.folder_count)
        return HFS_ERR_FORMAT;

    printf("truncated file_count=%u folder_count=%u free_blocks=%u\n",
           after.file_count, after.folder_count, after.free_blocks);
    return HFS_OK;
}

static int delete_files(struct image *image)
{
    struct hfs_entry folder;
    struct hfs_entry file;
    int rc = lookup_pair(image, TEST_FOLDER_RENAMED, TEST_NAME_RENAMED,
                         &folder, &file);
    if (rc != HFS_OK)
        return rc;
    if (file.data.size != 0)
        return HFS_ERR_NOT_EMPTY;

    struct stats before;
    struct stats after;
    if (read_stats(image, &before) != HFS_OK)
        return HFS_ERR_IO;

    size_t folder_units = 0;
    size_t units = 0;
    const uint8_t *folder_name = renamed_folder_name(&folder_units);
    const uint8_t *name = renamed_test_name(&units);
    struct hfs_txn tx;

    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_delete_file(&image->catalog, &tx, folder.id, name, units,
                                 file.id, hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;

    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_delete_folder(&image->catalog, &tx, 2, folder_name,
                                   folder_units, folder.id,
                                   hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;

    if (read_stats(image, &after) != HFS_OK)
        return HFS_ERR_IO;
    if (after.file_count + 1u != before.file_count)
        return HFS_ERR_FORMAT;
    if (after.folder_count + 1u != before.folder_count)
        return HFS_ERR_FORMAT;
    if (after.free_blocks != before.free_blocks)
        return HFS_ERR_FORMAT;
    if (hfs_catalog_lookup_ascii(&image->catalog, 2, TEST_FOLDER_RENAMED,
                                 &folder) != HFS_ERR_NOT_FOUND)
        return HFS_ERR_FORMAT;

    printf("deleted file_count=%u folder_count=%u free_blocks=%u\n",
           after.file_count, after.folder_count, after.free_blocks);
    return HFS_OK;
}

/* ASCII to UTF-16BE name units, for the create/delete calls. */
static size_t ascii_units(const char *ascii, uint8_t *out, size_t capacity)
{
    size_t length = strlen(ascii);
    if (length == 0 || length > capacity)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        out[i * 2u] = 0;
        out[i * 2u + 1u] = (uint8_t)ascii[i];
    }
    return length;
}

static int create_one_block_file(struct image *image, const char *ascii,
                                 uint8_t pattern, uint32_t *cnid)
{
    uint8_t name[64];
    size_t units = ascii_units(ascii, name, sizeof(name) / 2u);
    if (units == 0)
        return HFS_ERR_ARGUMENT;
    uint8_t block[TEST_SIZE];
    memset(block, pattern, sizeof(block));

    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = commit(&tx, hfs_catalog_create_file(&image->catalog, &tx, 2, name,
                                             units, 0644,
                                             hfs_catalog_key_compare, NULL,
                                             cnid));
    if (rc != HFS_OK)
        return rc;
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    return commit(&tx, hfs_catalog_append_file(&image->catalog, &tx, 2, name,
                                               units, 0, block, sizeof(block),
                                               hfs_catalog_key_compare, NULL));
}

static int delete_named_file(struct image *image, const char *ascii,
                             uint32_t cnid)
{
    uint8_t name[64];
    size_t units = ascii_units(ascii, name, sizeof(name) / 2u);
    if (units == 0)
        return HFS_ERR_ARGUMENT;
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    return commit(&tx, hfs_catalog_delete_file(&image->catalog, &tx, 2, name,
                                               units, cnid,
                                               hfs_catalog_key_compare, NULL));
}

/*
 * Create a file and append one block per transaction, so each append takes the
 * lowest free single block and the fork collects one extent per block.
 */
static int append_fragmented_file(struct image *image, const char *ascii,
                                  uint32_t blocks, uint8_t pattern_base)
{
    uint8_t name[64];
    size_t units = ascii_units(ascii, name, sizeof(name) / 2u);
    if (units == 0)
        return HFS_ERR_ARGUMENT;

    uint32_t cnid = 0;
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = commit(&tx, hfs_catalog_create_file(&image->catalog, &tx, 2, name,
                                             units, 0644,
                                             hfs_catalog_key_compare, NULL,
                                             &cnid));
    if (rc != HFS_OK)
        return rc;

    static uint8_t block[TEST_SIZE];
    for (uint32_t i = 0; i < blocks; ++i) {
        memset(block, (int)(pattern_base + i), sizeof(block));
        rc = hfs_txn_begin(&tx, &image->journal);
        if (rc != HFS_OK)
            return rc;
        rc = commit(&tx, hfs_catalog_append_file(
                            &image->catalog, &tx, 2, name, units,
                            (uint64_t)i * TEST_SIZE, block, sizeof(block),
                            hfs_catalog_key_compare, NULL));
        if (rc != HFS_OK) {
            fprintf(stderr, "%s append block %u failed: %d\n", ascii, i, rc);
            return rc;
        }
    }
    return HFS_OK;
}

/*
 * Force a data fork past its eight inline extents: create one-block files,
 * delete every other one, then append twelve blocks to a new file. The
 * allocator fills the holes, so the fork needs four overflow extents — the
 * first record the volume's empty extents B-tree will ever hold, which is the
 * create-root path on a real macOS-formatted volume.
 */
static int create_overflow_file(struct image *image)
{
    uint32_t holes[HOLE_FILES];
    char ascii[24];
    struct stats start_stats;
    if (read_stats(image, &start_stats) != HFS_OK)
        return HFS_ERR_IO;
    for (uint32_t i = 0; i < HOLE_FILES; ++i) {
        snprintf(ascii, sizeof(ascii), "Hole %02u", i);
        int rc = create_one_block_file(image, ascii, (uint8_t)(0x10u + i),
                                       &holes[i]);
        if (rc != HFS_OK) {
            fprintf(stderr, "hole %u create failed: %d\n", i, rc);
            return rc;
        }
    }
    struct stats after_holes;
    if (read_stats(image, &after_holes) != HFS_OK)
        return HFS_ERR_IO;
    if (after_holes.free_blocks + HOLE_FILES != start_stats.free_blocks)
        return HFS_ERR_FORMAT;
    for (uint32_t i = 1; i < HOLE_FILES; i += 2u) {
        snprintf(ascii, sizeof(ascii), "Hole %02u", i);
        int rc = delete_named_file(image, ascii, holes[i]);
        if (rc != HFS_OK) {
            fprintf(stderr, "hole %u delete failed: %d\n", i, rc);
            return rc;
        }
    }

    /*
     * Deleting the hole files has to return their blocks to the bitmap; a
     * delete that only removes the catalog record leaks every block it owned.
     */
    struct stats after_deletes;
    if (read_stats(image, &after_deletes) != HFS_OK)
        return HFS_ERR_IO;
    if (after_deletes.free_blocks != after_holes.free_blocks + HOLE_FILES / 2u)
        return HFS_ERR_FORMAT;
    static uint8_t payload[OVERFLOW_SIZE];
    for (uint32_t block = 0; block < OVERFLOW_BLOCKS; ++block) {
        memset(payload + block * TEST_SIZE, (int)(0x40u + block), TEST_SIZE);
    }
    /*
     * One block per call: the allocator takes the lowest free run each time, so
     * twelve single-block appends land in the twelve holes and the fork needs
     * twelve extents instead of one contiguous run.
     */
    int rc = append_fragmented_file(image, OVERFLOW_NAME, OVERFLOW_BLOCKS,
                                    0x40u);
    if (rc != HFS_OK)
        return rc;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup_ascii(&image->catalog, 2, OVERFLOW_NAME, &entry);
    if (rc != HFS_OK)
        return rc;
    if (entry.kind != 2 || entry.data.size != OVERFLOW_SIZE ||
        entry.data.blocks != OVERFLOW_BLOCKS) {
        fprintf(stderr, "overflow entry size=%llu blocks=%u\n",
                (unsigned long long)entry.data.size, entry.data.blocks);
        return HFS_ERR_FORMAT;
    }

    static uint8_t readback[OVERFLOW_SIZE];
    size_t done = 0;
    rc = hfs_catalog_read_file(&image->catalog, &entry, 0, readback,
                               sizeof(readback), &done);
    if (rc != HFS_OK || done != sizeof(readback))
        return rc != HFS_OK ? rc : HFS_ERR_FORMAT;
    if (memcmp(readback, payload, sizeof(payload)) != 0)
        return HFS_ERR_FORMAT;

    /*
     * The fork must sit exactly on the boundary the exercise targets: eight
     * inline extents, the rest in the overflow tree. hfs_extents_collect()
     * returns only the overflow extents, so the two counts have to add up to
     * the fork's block count.
     */
    uint32_t inline_blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        inline_blocks += entry.data.extents[i].count;

    static uint8_t extent_scratch[32768];
    struct hfs_extents extents;
    rc = hfs_extents_open(&extents, &image->volume, extent_scratch,
                          sizeof(extent_scratch));
    if (rc != HFS_OK)
        return rc;
    struct hfs_extent list[32];
    size_t count = 0;
    rc = hfs_extents_collect(&extents, entry.id, inline_blocks,
                             entry.data.blocks, list, 32, &count);
    if (rc != HFS_OK)
        return rc;
    uint32_t overflow_blocks = 0;
    for (size_t i = 0; i < count; ++i)
        overflow_blocks += list[i].count;
    if (inline_blocks != 8u || count == 0 ||
        inline_blocks + overflow_blocks != entry.data.blocks) {
        fprintf(stderr,
                "overflow extents inline=%u overflow=%u blocks=%u extents=%zu\n",
                inline_blocks, overflow_blocks, entry.data.blocks, count);
        return HFS_ERR_FORMAT;
    }

    fprintf(stderr,
            "overflow file cnid=%u size=%u blocks=%u inline=%u overflow=%u\n",
            entry.id, OVERFLOW_SIZE, entry.data.blocks, inline_blocks,
            overflow_blocks);

    /*
     * A second overflowed file stays on the volume so the cleanup stage can
     * delete a fork that still owns overflow extents, without truncating it
     * first.
     */
    rc = append_fragmented_file(image, DIRECT_NAME, DIRECT_BLOCKS, 0x60u);
    if (rc != HFS_OK)
        return rc;
    struct hfs_entry direct;
    rc = hfs_catalog_lookup_ascii(&image->catalog, 2, DIRECT_NAME, &direct);
    if (rc != HFS_OK)
        return rc;
    uint32_t direct_inline = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        direct_inline += direct.data.extents[i].count;
    struct hfs_extent direct_list[8];
    size_t direct_count = 0;
    rc = hfs_extents_collect(&extents, direct.id, direct_inline,
                             direct.data.blocks, direct_list, 8,
                             &direct_count);
    if (rc != HFS_OK)
        return rc;
    if (direct.data.blocks != DIRECT_BLOCKS || direct_inline != 8u ||
        direct_count != 1u || direct_list[0].count != 1u) {
        fprintf(stderr,
                "direct file blocks=%u inline=%u extents=%zu\n",
                direct.data.blocks, direct_inline, direct_count);
        return HFS_ERR_FORMAT;
    }
    return HFS_OK;
}

static int clear_overflow_file(struct image *image)
{
    uint8_t name[64];
    size_t units = ascii_units(OVERFLOW_NAME, name, sizeof(name) / 2u);
    if (units == 0)
        return HFS_ERR_ARGUMENT;

    /*
     * Delete a fork that still owns an overflow extent, with no truncate in
     * front of it: the delete path itself has to free the inline and overflow
     * blocks and clear the extents record.
     */
    struct stats before_direct;
    if (read_stats(image, &before_direct) != HFS_OK)
        return HFS_ERR_IO;
    struct hfs_entry direct;
    int drc = hfs_catalog_lookup_ascii(&image->catalog, 2, DIRECT_NAME,
                                       &direct);
    if (drc != HFS_OK)
        return drc;
    drc = delete_named_file(image, DIRECT_NAME, direct.id);
    if (drc != HFS_OK)
        return drc;
    struct stats after_direct;
    if (read_stats(image, &after_direct) != HFS_OK)
        return HFS_ERR_IO;
    if (after_direct.free_blocks != before_direct.free_blocks + DIRECT_BLOCKS) {
        fprintf(stderr, "direct delete freed %u blocks, expected %u\n",
                after_direct.free_blocks - before_direct.free_blocks,
                (unsigned int)DIRECT_BLOCKS);
        return HFS_ERR_FORMAT;
    }
    if (hfs_catalog_lookup_ascii(&image->catalog, 2, DIRECT_NAME, &direct) !=
        HFS_ERR_NOT_FOUND) {
        fprintf(stderr, "direct file survived its delete\n");
        return HFS_ERR_FORMAT;
    }

    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = commit(&tx, hfs_catalog_truncate_file(&image->catalog, &tx, 2, name,
                                               units,
                                               hfs_catalog_key_compare, NULL));
    if (rc != HFS_OK)
        return rc;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup_ascii(&image->catalog, 2, OVERFLOW_NAME, &entry);
    if (rc != HFS_OK)
        return rc;
    if (entry.data.size != 0u || entry.data.blocks != 0u)
        return HFS_ERR_FORMAT;

    static uint8_t extent_scratch[32768];
    struct hfs_extents extents;
    rc = hfs_extents_open(&extents, &image->volume, extent_scratch,
                          sizeof(extent_scratch));
    if (rc != HFS_OK)
        return rc;
    struct hfs_extent list[8];
    size_t count = 0;
    rc = hfs_extents_collect(&extents, entry.id, 0, 0, list, 8, &count);
    if (rc != HFS_OK || count != 0u)
        return rc != HFS_OK ? rc : HFS_ERR_FORMAT;
    if (extents.btree.empty == 0u) {
        /* The overflow records are gone; the header may keep free nodes, but
         * the tree must be back to its empty state so it can be reopened. */
        fprintf(stderr, "extents tree is not empty after truncate\n");
        return HFS_ERR_FORMAT;
    }

    char ascii[24];
    for (uint32_t i = 0; i < HOLE_FILES; ++i) {
        if ((i & 1u) != 0u)
            continue;
        snprintf(ascii, sizeof(ascii), "Hole %02u", i);
        struct hfs_entry hole;
        rc = hfs_catalog_lookup_ascii(&image->catalog, 2, ascii, &hole);
        if (rc != HFS_OK)
            return rc;
        rc = delete_named_file(image, ascii, hole.id);
        if (rc != HFS_OK)
            return rc;
    }
    struct hfs_entry overflow;
    rc = hfs_catalog_lookup_ascii(&image->catalog, 2, OVERFLOW_NAME, &overflow);
    if (rc != HFS_OK)
        return rc;
    rc = delete_named_file(image, OVERFLOW_NAME, overflow.id);
    if (rc != HFS_OK)
        return rc;

    printf("overflow file truncated and removed, extents tree empty again\n");
    return HFS_OK;
}

static int create_unicode_files(struct image *image)
{
    static const uint8_t cafe[] = { 0, 'C', 0, 'a', 0, 'f', 0, 0xe9 };
    static const uint8_t cjk[] = { 0x4e, 0x2d, 0x65, 0x87 };
    static const uint8_t emoji[] = { 0xd8, 0x3d, 0xde, 0x00 };
    static const uint8_t hangul[] = { 0xd5, 0x5c, 0xae, 0x00 };
    static const uint8_t combining[] = { 0, 'A', 0x03, 0x01 };
    static const uint8_t ohm[] = { 0x21, 0x26 };
    static const uint8_t compatibility[] = { 0xf9, 0x00 };
    static const uint8_t slash[] = {
        0, 's', 0, 'l', 0, 'a', 0, 's', 0, 'h', 0, '/', 0, 'n', 0, 'a',
        0, 'm', 0, 'e'
    };
    static const uint8_t colon[] = {
        0, 'c', 0, 'o', 0, 'l', 0, 'o', 0, 'n', 0, ':', 0, 'n', 0, 'a',
        0, 'm', 0, 'e'
    };
    static const struct {
        const uint8_t *name;
        size_t units;
    } names[] = {
        { cafe, 4 }, { cjk, 2 }, { emoji, 2 }, { hangul, 2 },
        { combining, 2 }, { ohm, 1 }, { compatibility, 1 },
        { slash, 10 }, { colon, 10 }
    };

    struct hfs_txn tx;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        uint32_t cnid = 0;
        int rc = hfs_txn_begin(&tx, &image->journal);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_catalog_create_file(&image->catalog, &tx, 2, names[i].name,
                                     names[i].units, 0644,
                                     hfs_catalog_key_compare, NULL, &cnid);
        rc = commit(&tx, rc);
        if (rc != HFS_OK)
            return rc;
    }

    static uint8_t long_name[255u * 2u];
    for (size_t i = 0; i < 255u; ++i) {
        long_name[i * 2u] = 0;
        long_name[i * 2u + 1u] = 'a';
    }
    uint32_t cnid = 0;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_create_file(&image->catalog, &tx, 2, long_name, 255,
                                 0644, hfs_catalog_key_compare, NULL, &cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK)
        return rc;

    static uint8_t too_long[256u * 2u];
    memset(too_long, 'a', sizeof(too_long));
    for (size_t i = 0; i < 256u; ++i)
        too_long[i * 2u] = 0;
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_catalog_create_file(&image->catalog, &tx, 2, too_long, 256,
                                 0644, hfs_catalog_key_compare, NULL, &cnid);
    if (rc == HFS_OK) {
        hfs_txn_abort(&tx);
        return HFS_ERR_FORMAT;
    }
    hfs_txn_abort(&tx);

    printf("created Unicode corpus and rejected a 256-unit name\n");
    return HFS_OK;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s IMAGE create|rename|truncate|delete|unicode\n",
                argv[0]);
        return 2;
    }
    struct image image;
    int rc = open_image(argv[1], &image);
    if (rc != HFS_OK) {
        fprintf(stderr, "open: %s\n", hfs_strerror(rc));
        return 1;
    }
    if (strcmp(argv[2], "overflow") == 0) {
        rc = create_overflow_file(&image);
    } else if (strcmp(argv[2], "overflow-cleanup") == 0) {
        rc = clear_overflow_file(&image);
    } else if (strcmp(argv[2], "create") == 0) {
        rc = create_and_append(&image);
    } else if (strcmp(argv[2], "rename") == 0) {
        rc = rename_files(&image);
    } else if (strcmp(argv[2], "truncate") == 0) {
        rc = truncate_file(&image);
    } else if (strcmp(argv[2], "delete") == 0) {
        rc = delete_files(&image);
    } else if (strcmp(argv[2], "unicode") == 0) {
        rc = create_unicode_files(&image);
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
