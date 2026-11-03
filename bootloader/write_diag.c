/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "write_diag.h"

#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "inner_image.h"

#include <string.h>

#define INNER_MIN_SIZE (16u * 1024u * 1024u)
#define TEST_DATA_SIZE 4096u
#define TEST_PATTERN 0xa5u
#define CUT_CONFIG_NAME "helikon-cut.txt"

static uint8_t test_data[TEST_DATA_SIZE];
static uint8_t readback[TEST_DATA_SIZE];

static const uint8_t folder_name[] = {
    0, 'd', 0, 'i', 0, 'a', 0, 'g'
};
static const uint8_t file_name[] = {
    0, 't', 0, 'e', 0, 's', 0, 't'
};
static const uint8_t renamed_name[] = {
    0, 'r', 0, 'e', 0, 'n', 0, 'a', 0, 'm', 0, 'e', 0, 'd'
};

static void reset_result(struct boot_write_diag_result *result)
{
    memset(result, 0, sizeof(*result));
    result->outer_volume = BOOT_WRITE_NOT_TESTED;
    result->test_file = BOOT_WRITE_NOT_TESTED;
    result->inner_volume = BOOT_WRITE_NOT_TESTED;
    result->inner_catalog = BOOT_WRITE_NOT_TESTED;
    result->inner_journal = BOOT_WRITE_NOT_TESTED;
    result->cleanup = BOOT_WRITE_NOT_TESTED;
    result->create_folder = BOOT_WRITE_NOT_TESTED;
    result->create_file = BOOT_WRITE_NOT_TESTED;
    result->append = BOOT_WRITE_NOT_TESTED;
    result->readback = BOOT_WRITE_NOT_TESTED;
    result->rename = BOOT_WRITE_NOT_TESTED;
    result->truncate = BOOT_WRITE_NOT_TESTED;
    result->delete = BOOT_WRITE_NOT_TESTED;
    result->cut_at = 0;
}

static uint32_t read_cut_at(struct hfs_catalog *outer_catalog)
{
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup_ascii(outer_catalog, 2, CUT_CONFIG_NAME,
                                      &entry);
    if (rc != HFS_OK || entry.kind != 2 || entry.data.size == 0 ||
        entry.data.size > 16)
        return 0;

    char text[17];
    size_t done = 0;
    rc = hfs_catalog_read_file(outer_catalog, &entry, 0, text, sizeof(text) - 1,
                               &done);
    if (rc != HFS_OK)
        return 0;
    text[done] = '\0';

    uint32_t value = 0;
    int digits = 0;
    for (size_t i = 0; i < done; ++i) {
        if (text[i] < '0' || text[i] > '9')
            break;
        if (value > (UINT32_MAX - (uint32_t)(text[i] - '0')) / 10u)
            return 0;
        value = value * 10u + (uint32_t)(text[i] - '0');
        ++digits;
    }
    return digits != 0 ? value : 0;
}

static int commit(struct hfs_txn *tx, int rc)
{
    if (rc == HFS_OK)
        return hfs_txn_commit(tx);
    hfs_txn_abort(tx);
    return rc;
}

static int start_tx(struct hfs_txn *tx, struct hfs_journal *journal)
{
    memset(tx, 0, sizeof(*tx));
    return hfs_txn_begin(tx, journal);
}

static int cleanup_one_file(struct hfs_catalog *catalog,
                           struct hfs_journal *journal, struct hfs_txn *tx,
                           uint32_t folder, const char *ascii,
                           const uint8_t *name, size_t units)
{
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup_ascii(catalog, folder, ascii, &entry);
    if (rc == HFS_ERR_NOT_FOUND)
        return HFS_OK;
    if (rc != HFS_OK)
        return rc;
    if (entry.kind != 2)
        return HFS_ERR_FORMAT;
    if (entry.data.size != 0) {
        rc = start_tx(tx, journal);
        if (rc == HFS_OK)
            rc = hfs_catalog_truncate_file(catalog, tx, folder, name, units,
                                           hfs_catalog_key_compare, NULL);
        rc = commit(tx, rc);
        if (rc != HFS_OK)
            return rc;
    }
    rc = start_tx(tx, journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_delete_file(catalog, tx, folder, name, units,
                                     entry.id, hfs_catalog_key_compare, NULL);
    return commit(tx, rc);
}

static int cleanup_inner(struct hfs_catalog *catalog,
                         struct hfs_journal *journal, struct hfs_txn *tx)
{
    struct hfs_entry folder;
    int rc = hfs_catalog_lookup_ascii(catalog, 2, "diag", &folder);
    if (rc == HFS_ERR_NOT_FOUND)
        return HFS_OK;
    if (rc != HFS_OK)
        return rc;
    if (folder.kind != 1)
        return HFS_ERR_FORMAT;

    rc = cleanup_one_file(catalog, journal, tx, folder.id, "test",
                          file_name, sizeof(file_name) / 2u);
    if (rc != HFS_OK)
        return rc;
    rc = cleanup_one_file(catalog, journal, tx, folder.id, "renamed",
                          renamed_name, sizeof(renamed_name) / 2u);
    if (rc != HFS_OK)
        return rc;

    rc = start_tx(tx, journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_delete_folder(catalog, tx, 2,
                                       (const uint8_t *)"diag", 4,
                                       folder.id, hfs_catalog_key_compare,
                                       NULL);
    return commit(tx, rc);
}

int boot_write_diag_run(hfs_read_fn outer_read, hfs_write_fn outer_write,
                        hfs_flush_fn outer_flush, void *context,
                        uint64_t base, uint64_t length,
                        const char *image_name,
                        void *outer_catalog_scratch,
                        size_t outer_catalog_scratch_size,
                        void *outer_extents_scratch,
                        size_t outer_extents_scratch_size,
                        void *inner_catalog_scratch,
                        size_t inner_catalog_scratch_size,
                        uint32_t now,
                        uint32_t *cut_at,
                        struct boot_write_diag_result *result)
{
    if (outer_read == NULL || outer_write == NULL || outer_flush == NULL ||
        image_name == NULL || outer_catalog_scratch == NULL ||
        outer_extents_scratch == NULL || inner_catalog_scratch == NULL ||
        result == NULL)
        return HFS_ERR_ARGUMENT;

    reset_result(result);

    struct hfs_volume outer;
    int rc = hfs_volume_probe(&outer, outer_read, context, base, length);
    if (rc != HFS_OK) {
        result->outer_volume = BOOT_WRITE_FAIL;
        result->error = rc;
        return HFS_OK;
    }
    result->outer_volume = BOOT_WRITE_PASS;

    struct hfs_catalog outer_catalog;
    rc = hfs_catalog_open(&outer_catalog, &outer, outer_catalog_scratch,
                          outer_catalog_scratch_size);
    if (rc != HFS_OK) {
        result->test_file = BOOT_WRITE_FAIL;
        result->error = rc;
        return HFS_OK;
    }

    static struct inner_image inner;
    rc = inner_image_open(&inner, &outer, &outer_catalog, image_name,
                          outer_write, outer_flush, outer_extents_scratch,
                          outer_extents_scratch_size, INNER_MIN_SIZE);
    if (rc != HFS_OK) {
        result->test_file = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->test_file = BOOT_WRITE_PASS;

    rc = inner_image_guard_metadata(&inner, &outer);
    if (rc != HFS_OK) {
        result->test_file = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }

    struct hfs_volume inner_volume;
    rc = hfs_volume_probe(&inner_volume, inner_image_read, &inner, 0,
                          inner.size);
    if (rc != HFS_OK && rc != HFS_ERR_DIRTY) {
        result->inner_volume = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->inner_volume = BOOT_WRITE_PASS;
    result->inner_block_size = inner_volume.block_size;
    result->inner_blocks = inner_volume.blocks;
    result->inner_size = inner.size;

    if (!inner_volume.journaled) {
        result->inner_journal = BOOT_WRITE_FAIL;
        result->error = HFS_ERR_UNSUPPORTED;
        return HFS_OK;
    }

    struct hfs_journal journal;
    rc = hfs_journal_open(&journal, &inner_volume, inner_image_write,
                          inner_image_flush);
    if (rc != HFS_OK) {
        result->inner_journal = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    if (!hfs_journal_is_clean(&journal)) {
        result->recovered = 1;
        rc = hfs_journal_replay(&journal);
        if (rc != HFS_OK) {
            result->inner_journal = BOOT_WRITE_FAIL;
            result->error = rc;
            return HFS_OK;
        }
        rc = hfs_volume_probe(&inner_volume, inner_image_read, &inner, 0,
                              inner.size);
        if (rc != HFS_OK) {
            result->inner_journal = BOOT_WRITE_FAIL;
            result->error = rc;
            return HFS_OK;
        }
    }
    result->inner_journal = BOOT_WRITE_PASS;

    if (cut_at != NULL) {
        result->cut_at = read_cut_at(&outer_catalog);
        *cut_at = result->cut_at;
    }

    struct hfs_catalog inner_catalog;
    rc = hfs_catalog_open(&inner_catalog, &inner_volume, inner_catalog_scratch,
                          inner_catalog_scratch_size);
    if (rc != HFS_OK) {
        result->inner_catalog = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->inner_catalog = BOOT_WRITE_PASS;
    inner_catalog.now = now;

    /* The transaction object alone is ~256 KiB; keep it out of the 8 KiB
     * bootloader stack. The writer is single-threaded. */
    static struct hfs_txn tx;
    rc = cleanup_inner(&inner_catalog, &journal, &tx);
    if (rc != HFS_OK) {
        result->cleanup = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->cleanup = BOOT_WRITE_PASS;

    uint32_t folder_cnid = 0;
    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_create_folder(&inner_catalog, &tx, 2, folder_name,
                                       sizeof(folder_name) / 2u, 0755,
                                       hfs_catalog_key_compare, NULL,
                                       &folder_cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->create_folder = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->create_folder = BOOT_WRITE_PASS;

    uint32_t file_cnid = 0;
    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_create_file(&inner_catalog, &tx, folder_cnid,
                                     file_name, sizeof(file_name) / 2u, 0644,
                                     hfs_catalog_key_compare, NULL,
                                     &file_cnid);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->create_file = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->create_file = BOOT_WRITE_PASS;

    memset(test_data, TEST_PATTERN, sizeof(test_data));
    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_append_file(&inner_catalog, &tx, folder_cnid,
                                     file_name, sizeof(file_name) / 2u, 0,
                                     test_data, sizeof(test_data),
                                     hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->append = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->append = BOOT_WRITE_PASS;

    static struct hfs_entry entry;
    rc = hfs_catalog_lookup_ascii(&inner_catalog, folder_cnid, "test", &entry);
    if (rc == HFS_OK && entry.kind == 2 && entry.data.size == TEST_DATA_SIZE) {
        size_t done = 0;
        rc = hfs_catalog_read_file(&inner_catalog, &entry, 0, readback,
                                   sizeof(readback), &done);
        if (rc == HFS_OK && done == sizeof(readback)) {
            rc = memcmp(readback, test_data, sizeof(readback)) == 0 ?
                 HFS_OK : HFS_ERR_FORMAT;
        }
    } else if (rc == HFS_OK) {
        rc = HFS_ERR_FORMAT;
    }
    if (rc != HFS_OK) {
        result->readback = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->readback = BOOT_WRITE_PASS;

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_rename(&inner_catalog, &tx, folder_cnid, file_name,
                                sizeof(file_name) / 2u, file_cnid,
                                folder_cnid, renamed_name,
                                sizeof(renamed_name) / 2u,
                                hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->rename = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->rename = BOOT_WRITE_PASS;

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_truncate_file(&inner_catalog, &tx, folder_cnid,
                                       renamed_name,
                                       sizeof(renamed_name) / 2u,
                                       hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->truncate = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->truncate = BOOT_WRITE_PASS;

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_delete_file(&inner_catalog, &tx, folder_cnid,
                                     renamed_name,
                                     sizeof(renamed_name) / 2u, file_cnid,
                                     hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->delete = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }

    rc = start_tx(&tx, &journal);
    if (rc == HFS_OK)
        rc = hfs_catalog_delete_folder(&inner_catalog, &tx, 2, folder_name,
                                       sizeof(folder_name) / 2u, folder_cnid,
                                       hfs_catalog_key_compare, NULL);
    rc = commit(&tx, rc);
    if (rc != HFS_OK) {
        result->delete = BOOT_WRITE_FAIL;
        result->writes = inner.writes;
        result->writes = inner.writes;
        result->error = rc;
        return HFS_OK;
    }
    result->delete = BOOT_WRITE_PASS;
    result->writes = inner.writes;
    return HFS_OK;
}
