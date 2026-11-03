/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only boot diagnostics.
 *
 * This is the host-testable core of the diagnostic bootloader. It checks the
 * HFS+ read path and reports the write path as not implemented. Hardware
 * bring-up, the LCD blit and the boot jump remain separate integration work.
 */
#include "diagnostics.h"
#include "hfs_catalog.h"
#include "hfs_extents.h"
#include "splash.h"

#include <string.h>

static void reset_result(struct boot_diag_result *result)
{
    memset(result, 0, sizeof(*result));
    result->volume = BOOT_DIAG_NOT_TESTED;
    result->catalog = BOOT_DIAG_NOT_TESTED;
    result->read = BOOT_DIAG_NOT_TESTED;
    result->write = BOOT_DIAG_NOT_IMPLEMENTED;
    result->splash = BOOT_DIAG_PASS;
    result->journal = BOOT_DIAG_JOURNAL_UNKNOWN;
}

static void diagnose_journal(const struct hfs_volume *volume, int probe_error,
                             struct boot_diag_result *result)
{
    if (probe_error == HFS_ERR_DIRTY) {
        result->journal = BOOT_DIAG_JOURNAL_DIRTY;
    } else if (probe_error == HFS_OK) {
        result->journal = volume->journaled ? BOOT_DIAG_JOURNAL_CLEAN
                                            : BOOT_DIAG_JOURNAL_NOT_PRESENT;
    }
}

static int diagnose_read(struct hfs_catalog *catalog, struct hfs_volume *volume,
                         const char *probe_path, void *extents_scratch,
                         size_t extents_scratch_size,
                         struct boot_diag_result *result)
{
    if (probe_path == NULL) {
        result->read = BOOT_DIAG_NOT_TESTED;
        return HFS_OK;
    }

    struct hfs_entry entry;
    int rc = hfs_catalog_lookup(catalog, probe_path, &entry);
    if (rc != HFS_OK) {
        result->read = BOOT_DIAG_FAIL;
        result->read_error = rc;
        return HFS_OK;
    }
    if (entry.kind != 2 || entry.unsupported) {
        result->read = BOOT_DIAG_FAIL;
        result->read_error = entry.kind == 1 ? HFS_ERR_IS_DIR
                                             : HFS_ERR_UNSUPPORTED;
        return HFS_OK;
    }

    uint8_t probe[32];
    size_t done = 0;
    rc = hfs_catalog_read_file(catalog, &entry, 0, probe, sizeof(probe),
                               &done);
    if (rc == HFS_ERR_UNSUPPORTED) {
        if (extents_scratch == NULL || extents_scratch_size < 512) {
            result->read = BOOT_DIAG_FAIL;
            result->read_error = HFS_ERR_BUFFER;
            return HFS_OK;
        }

        struct hfs_extents extents;
        rc = hfs_extents_open(&extents, volume, extents_scratch,
                              extents_scratch_size);
        if (rc == HFS_OK)
            rc = hfs_extents_read(&extents, &entry.data, entry.id, 0, probe,
                                  sizeof(probe), &done);
    }

    if (rc != HFS_OK) {
        result->read = BOOT_DIAG_FAIL;
        result->read_error = rc;
        return HFS_OK;
    }

    result->read = BOOT_DIAG_PASS;
    result->bytes_read = done;
    return HFS_OK;
}

int boot_diag_run(hfs_read_fn read, void *context, uint64_t base,
                  uint64_t length, const char *probe_path,
                  void *catalog_scratch, size_t catalog_scratch_size,
                  void *extents_scratch, size_t extents_scratch_size,
                  struct boot_diag_result *result)
{
    if (read == NULL || result == NULL || catalog_scratch == NULL ||
        catalog_scratch_size < 512)
        return HFS_ERR_ARGUMENT;

    reset_result(result);

    struct hfs_volume volume;
    int rc = hfs_volume_probe(&volume, read, context, base, length);
    diagnose_journal(&volume, rc, result);
    if (rc != HFS_OK) {
        result->volume = BOOT_DIAG_FAIL;
        result->volume_error = rc;
        return HFS_OK;
    }

    result->volume = BOOT_DIAG_PASS;
    result->block_size = volume.block_size;
    result->blocks = volume.blocks;

    struct hfs_catalog catalog;
    rc = hfs_catalog_open(&catalog, &volume, catalog_scratch,
                          catalog_scratch_size);
    if (rc != HFS_OK) {
        result->catalog = BOOT_DIAG_FAIL;
        result->catalog_error = rc;
        return HFS_OK;
    }
    result->catalog = BOOT_DIAG_PASS;

    (void)diagnose_read(&catalog, &volume, probe_path, extents_scratch,
                        extents_scratch_size, result);
    return HFS_OK;
}

const char *boot_diag_status_name(enum boot_diag_status status)
{
    switch (status) {
    case BOOT_DIAG_PASS: return "PASS";
    case BOOT_DIAG_FAIL: return "FAIL";
    case BOOT_DIAG_NOT_TESTED: return "NOT TESTED";
    case BOOT_DIAG_NOT_IMPLEMENTED: return "NOT IMPLEMENTED";
    default: return "UNKNOWN";
    }
}

const char *boot_diag_journal_name(enum boot_diag_journal journal)
{
    switch (journal) {
    case BOOT_DIAG_JOURNAL_NOT_PRESENT: return "NOT PRESENT";
    case BOOT_DIAG_JOURNAL_CLEAN: return "CLEAN";
    case BOOT_DIAG_JOURNAL_DIRTY: return "DIRTY";
    case BOOT_DIAG_JOURNAL_UNKNOWN: return "UNKNOWN";
    default: return "UNKNOWN";
    }
}
