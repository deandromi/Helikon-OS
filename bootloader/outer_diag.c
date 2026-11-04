/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Outer-volume write diagnostic.
 *
 * This writes the real outer HFS+ volume. It is deliberately narrower than the
 * writer: an opt-in root file must exist, the journal must be clean, and only
 * the dedicated /helikon-outer-test folder and its known files are touched.
 */
#include "outer_diag.h"

#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"

#include <string.h>

#define OPT_IN_NAME "helikon-outer-ok.txt"
#define OPT_IN_MAGIC "HELIKON-OUTER-WRITE-OK"
#define TEST_FOLDER "helikon-outer-test"
#define TEST_FILE "test"
#define RENAMED_FILE "renamed"
#define MARKER_FILE "marker"
#define MARKER_MAGIC "HELIKON-OUTER-TEST"
#define TEST_DATA_SIZE 4096u
#define TEST_PATTERN 0xa5u

static uint8_t test_data[TEST_DATA_SIZE];
static uint8_t readback[TEST_DATA_SIZE];

static void reset_result(struct boot_outer_diag_result *result)
{
    memset(result, 0, sizeof(*result));
    result->opt_in = BOOT_OUTER_NOT_TESTED;
    result->outer_volume = BOOT_OUTER_NOT_TESTED;
    result->outer_journal = BOOT_OUTER_NOT_TESTED;
    result->catalog = BOOT_OUTER_NOT_TESTED;
    result->cleanup = BOOT_OUTER_NOT_TESTED;
    result->create_folder = BOOT_OUTER_NOT_TESTED;
    result->create_file = BOOT_OUTER_NOT_TESTED;
    result->append = BOOT_OUTER_NOT_TESTED;
    result->readback = BOOT_OUTER_NOT_TESTED;
    result->rename = BOOT_OUTER_NOT_TESTED;
    result->truncate = BOOT_OUTER_NOT_TESTED;
    result->delete_file = BOOT_OUTER_NOT_TESTED;
    result->delete_folder = BOOT_OUTER_NOT_TESTED;
    result->restored = BOOT_OUTER_NOT_TESTED;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int fork_inline_complete(const struct hfs_fork *fork)
{
    uint64_t blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        blocks += fork->extents[i].count;
    return blocks == fork->blocks;
}

static int to_utf16(const char *ascii, uint8_t *out, size_t out_size,
                    size_t *units)
{
    size_t length = strlen(ascii);
    if (out == NULL || units == NULL || length == 0 || length > 255u ||
        out_size < length * 2u)
        return HFS_ERR_ARGUMENT;
    for (size_t i = 0; i < length; ++i) {
        out[i * 2u] = 0;
        out[i * 2u + 1u] = (uint8_t)ascii[i];
    }
    *units = length;
    return HFS_OK;
}

static int magic_matches(const uint8_t *data, size_t length,
                         const char *magic)
{
    size_t magic_length = strlen(magic);
    if (length == magic_length &&
        memcmp(data, magic, magic_length) == 0)
        return 1;
    if (length == magic_length + 1u &&
        memcmp(data, magic, magic_length) == 0 &&
        (data[magic_length] == '\n' || data[magic_length] == '\r'))
        return 1;
    if (length == magic_length + 2u &&
        memcmp(data, magic, magic_length) == 0 &&
        data[magic_length] == '\r' && data[magic_length + 1u] == '\n')
        return 1;
    return 0;
}

static int read_file(struct hfs_catalog *catalog,
                     const struct hfs_entry *entry, uint8_t *buffer,
                     size_t capacity, size_t *done)
{
    if (entry->data.size > capacity)
        return HFS_ERR_BUFFER;
    size_t length = 0;
    int rc = hfs_catalog_read_file(catalog, entry, 0, buffer, capacity,
                                   &length);
    if (rc != HFS_OK)
        return rc;
    if (length != (size_t)entry->data.size)
        return HFS_ERR_FORMAT;
    *done = length;
    return HFS_OK;
}

static int check_opt_in(struct hfs_catalog *catalog)
{
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup_ascii(catalog, 2, OPT_IN_NAME, &entry);
    if (rc != HFS_OK)
        return rc;
    if (entry.kind != 2 || entry.unsupported)
        return HFS_ERR_FORMAT;

    uint8_t content[64];
    size_t length = 0;
    rc = read_file(catalog, &entry, content, sizeof(content), &length);
    if (rc != HFS_OK)
        return rc;
    return magic_matches(content, length, OPT_IN_MAGIC) ?
           HFS_OK : HFS_ERR_FORMAT;
}

static int start_tx(struct hfs_txn *tx, struct hfs_journal *journal)
{
    memset(tx, 0, sizeof(*tx));
    return hfs_txn_begin(tx, journal);
}

static int commit(struct hfs_txn *tx, int rc)
{
    if (rc == HFS_OK)
        return hfs_txn_commit(tx);
    hfs_txn_abort(tx);
    return rc;
}

static int cleanup_test_folder(struct hfs_catalog *catalog)
{
    struct hfs_entry folder;
    int rc = hfs_catalog_lookup_ascii(catalog, 2, TEST_FOLDER, &folder);
    if (rc == HFS_ERR_NOT_FOUND)
        return HFS_OK;
    if (rc != HFS_OK)
        return rc;
    /*
     * A pre-existing folder may be user data, or a leftover from a failed
     * run. Do not delete it automatically: refusing here keeps unknown data
     * and any extended attributes intact. The operator removes it on macOS
     * after inspecting it.
     */
    (void)folder;
    return HFS_ERR_BUSY;
}

static int read_counts(struct hfs_volume *volume, uint32_t *files,
                       uint32_t *folders)
{
    uint8_t header[512];
    if (volume->read(volume->context,
                     volume->base + HFS_VOLUME_HEADER_OFFSET, header,
                     sizeof(header)))
        return HFS_ERR_IO;
    *files = be32(header + 32);
    *folders = be32(header + 36);
    return HFS_OK;
}

int boot_outer_diag_run(hfs_read_fn read, hfs_write_fn write,
                        hfs_flush_fn flush, void *context,
                        uint64_t base, uint64_t length,
                        void *catalog_scratch, size_t catalog_scratch_size,
                        uint32_t now, uint64_t *writes,
                        struct boot_outer_diag_result *result)
{
    if (read == NULL || write == NULL || flush == NULL ||
        catalog_scratch == NULL || result == NULL)
        return HFS_ERR_ARGUMENT;

    reset_result(result);
    if (writes != NULL)
        result->writes = *writes;

    struct hfs_volume outer;
    int rc = hfs_volume_probe(&outer, read, context, base, length);
    if (rc != HFS_OK) {
        result->outer_volume = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }
    if (outer.recovery) {
        result->outer_volume = BOOT_OUTER_FAIL;
        result->error = HFS_ERR_UNSUPPORTED;
        return HFS_OK;
    }
    if ((outer.attributes & ((1u << 7) | (1u << 15))) != 0) {
        result->outer_volume = BOOT_OUTER_FAIL;
        result->error = HFS_ERR_READ_ONLY;
        return HFS_OK;
    }
    if (!fork_inline_complete(&outer.allocation) ||
        !fork_inline_complete(&outer.extents) ||
        !fork_inline_complete(&outer.catalog)) {
        result->outer_volume = BOOT_OUTER_FAIL;
        result->error = HFS_ERR_UNSUPPORTED;
        return HFS_OK;
    }
    if (outer.block_size > HFS_TXN_BLOCK_MAX) {
        result->outer_volume = BOOT_OUTER_FAIL;
        result->error = HFS_ERR_UNSUPPORTED;
        return HFS_OK;
    }
    result->outer_volume = BOOT_OUTER_PASS;
    result->free_blocks_before = outer.free_blocks;

    if (!outer.journaled) {
        result->outer_journal = BOOT_OUTER_UNSUPPORTED;
        result->error = HFS_ERR_UNSUPPORTED;
        return HFS_OK;
    }

    struct hfs_catalog catalog;
    rc = hfs_catalog_open(&catalog, &outer, catalog_scratch,
                          catalog_scratch_size);
    if (rc != HFS_OK) {
        result->catalog = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }
    result->catalog = BOOT_OUTER_PASS;

    rc = check_opt_in(&catalog);
    if (rc != HFS_OK) {
        result->opt_in = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }
    result->opt_in = BOOT_OUTER_PASS;

    struct hfs_journal journal;
    rc = hfs_journal_open(&journal, &outer, write, flush);
    if (rc != HFS_OK) {
        result->outer_journal = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }
    if (!hfs_journal_is_clean(&journal)) {
        result->outer_journal = BOOT_OUTER_FAIL;
        result->error = HFS_ERR_DIRTY;
        return HFS_OK;
    }
    if (outer.block_size < journal.sector_size ||
        outer.block_size % journal.sector_size != 0) {
        result->outer_journal = BOOT_OUTER_UNSUPPORTED;
        result->error = HFS_ERR_UNSUPPORTED;
        return HFS_OK;
    }
    result->outer_journal = BOOT_OUTER_PASS;

    uint32_t files_before = 0;
    uint32_t folders_before = 0;
    rc = read_counts(&outer, &files_before, &folders_before);
    if (rc != HFS_OK) {
        result->restored = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }

    catalog.now = now;

    /* The transaction object alone is ~256 KiB; keep it out of the 8 KiB
     * bootloader stack. The writer is single-threaded. */
    static struct hfs_txn tx;
    rc = cleanup_test_folder(&catalog);
    if (rc != HFS_OK) {
        result->cleanup = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->cleanup = BOOT_OUTER_PASS;

    static uint8_t folder_key[512];
    static uint8_t file_key[512];
    static uint8_t renamed_key[512];
    static uint8_t marker_key[512];
    size_t folder_units = 0;
    size_t file_units = 0;
    size_t renamed_units = 0;
    size_t marker_units = 0;
    rc = to_utf16(TEST_FOLDER, folder_key, sizeof(folder_key), &folder_units);
    if (rc != HFS_OK) {
        result->create_folder = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }
    rc = to_utf16(TEST_FILE, file_key, sizeof(file_key), &file_units);
    if (rc != HFS_OK) {
        result->create_file = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }
    rc = to_utf16(RENAMED_FILE, renamed_key, sizeof(renamed_key),
                  &renamed_units);
    if (rc != HFS_OK) {
        result->rename = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }
    rc = to_utf16(MARKER_FILE, marker_key, sizeof(marker_key), &marker_units);
    if (rc != HFS_OK) {
        result->create_file = BOOT_OUTER_FAIL;
        result->error = rc;
        return HFS_OK;
    }

    uint32_t folder_cnid = 0;
    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_create_folder(&catalog, &tx, 2, folder_key,
                                       folder_units, 0755,
                                       hfs_catalog_key_compare, NULL,
                                       &folder_cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->create_folder = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->create_folder = BOOT_OUTER_PASS;

    uint32_t marker_cnid = 0;
    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_create_file(&catalog, &tx, folder_cnid, marker_key,
                                     marker_units, 0644,
                                     hfs_catalog_key_compare, NULL,
                                     &marker_cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->create_file = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_append_file(&catalog, &tx, folder_cnid, marker_key,
                                     marker_units, 0, MARKER_MAGIC,
                                     strlen(MARKER_MAGIC),
                                     hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->create_file = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }

    uint32_t file_cnid = 0;
    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_create_file(&catalog, &tx, folder_cnid, file_key,
                                     file_units, 0644,
                                     hfs_catalog_key_compare, NULL,
                                     &file_cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->create_file = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->create_file = BOOT_OUTER_PASS;

    memset(test_data, TEST_PATTERN, sizeof(test_data));
    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_append_file(&catalog, &tx, folder_cnid, file_key,
                                     file_units, 0, test_data,
                                     sizeof(test_data),
                                     hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->append = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->append = BOOT_OUTER_PASS;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup_ascii(&catalog, folder_cnid, TEST_FILE, &entry);
    if (rc == HFS_OK && entry.kind == 2 &&
        entry.data.size == TEST_DATA_SIZE) {
        size_t done = 0;
        rc = hfs_catalog_read_file(&catalog, &entry, 0, readback,
                                   sizeof(readback), &done);
        if (rc == HFS_OK && done == sizeof(readback))
            rc = memcmp(readback, test_data, sizeof(readback)) == 0 ?
                 HFS_OK : HFS_ERR_FORMAT;
    } else if (rc == HFS_OK) {
        rc = HFS_ERR_FORMAT;
    }
    if (rc != HFS_OK) {
        result->readback = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->readback = BOOT_OUTER_PASS;

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_rename(&catalog, &tx, folder_cnid, file_key,
                                file_units, file_cnid, folder_cnid,
                                renamed_key, renamed_units,
                                hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->rename = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->rename = BOOT_OUTER_PASS;

    rc = hfs_catalog_lookup_ascii(&catalog, folder_cnid, RENAMED_FILE, &entry);
    if (rc != HFS_OK || entry.kind != 2 ||
        entry.data.size != TEST_DATA_SIZE) {
        result->readback = BOOT_OUTER_FAIL;
        result->error = rc == HFS_OK ? HFS_ERR_FORMAT : rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_truncate_file(&catalog, &tx, folder_cnid,
                                       renamed_key, renamed_units,
                                       hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->truncate = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->truncate = BOOT_OUTER_PASS;

    rc = hfs_catalog_lookup_ascii(&catalog, folder_cnid, RENAMED_FILE, &entry);
    if (rc != HFS_OK || entry.kind != 2 || entry.data.size != 0) {
        result->truncate = BOOT_OUTER_FAIL;
        result->error = rc == HFS_OK ? HFS_ERR_FORMAT : rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_delete_file(&catalog, &tx, folder_cnid,
                                     renamed_key, renamed_units, file_cnid,
                                     hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->delete_file = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->delete_file = BOOT_OUTER_PASS;

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_truncate_file(&catalog, &tx, folder_cnid,
                                       marker_key, marker_units,
                                       hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->delete_file = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_delete_file(&catalog, &tx, folder_cnid, marker_key,
                                     marker_units, marker_cnid,
                                     hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->delete_file = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_delete_folder(&catalog, &tx, 2, folder_key,
                                       folder_units, folder_cnid,
                                       hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->delete_folder = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    result->delete_folder = BOOT_OUTER_PASS;

    if (hfs_catalog_lookup_ascii(&catalog, 2, TEST_FOLDER, &entry) !=
        HFS_ERR_NOT_FOUND) {
        result->delete_folder = BOOT_OUTER_FAIL;
        result->error = HFS_ERR_FORMAT;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }

    struct hfs_volume after;
    rc = hfs_volume_probe(&after, read, context, base, length);
    if (rc != HFS_OK) {
        result->restored = BOOT_OUTER_FAIL;
        result->error = rc;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    uint32_t files_after = 0;
    uint32_t folders_after = 0;
    rc = read_counts(&after, &files_after, &folders_after);
    if (rc != HFS_OK || files_after != files_before ||
        folders_after != folders_before) {
        result->restored = BOOT_OUTER_FAIL;
        result->error = rc != HFS_OK ? rc : HFS_ERR_FORMAT;
        if (writes != NULL)
            result->writes = *writes;
        return HFS_OK;
    }
    /*
     * A catalog fork may grow by a node during the sequence and is not shrunk
     * on delete. That is a legitimate allocation, not a leak; report the free
     * block delta but require only the root file/folder counts to match.
     */
    result->free_blocks_after = after.free_blocks;
    result->restored = BOOT_OUTER_PASS;

    if (writes != NULL)
        result->writes = *writes;
    return HFS_OK;
}
