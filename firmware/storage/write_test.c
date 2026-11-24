/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The first write the firmware can make, and the proof that it landed.
 * See `write_test.h` for the contract; this file is the sequence.
 *
 * The read-back goes through the read-only VFS on purpose. Reading the bytes
 * back with the writer would prove that the writer's buffers agree with
 * themselves, which is not the question. The question is whether an ordinary
 * reader, the same one the Music hub uses, sees the pattern on the volume.
 */
#include "write_test.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"

uint8_t helikon_write_pattern(uint32_t offset)
{
    return (uint8_t)(offset * 31u + 7u);
}

const char *helikon_write_stage_name(uint8_t stage)
{
    switch (stage) {
    case HELIKON_WRITE_STAGE_MOUNT:
        return "MOUNT";
    case HELIKON_WRITE_STAGE_GATE:
        return "GATE";
    case HELIKON_WRITE_STAGE_WRITE:
        return "WRITE";
    case HELIKON_WRITE_STAGE_READBACK:
        return "READBACK";
    case HELIKON_WRITE_STAGE_VERIFY:
        return "VERIFY";
    case HELIKON_WRITE_STAGE_DONE:
        return "DONE";
    default:
        return "NONE";
    }
}

/* Fills `test->actual` with the pattern; the same buffer is handed to the
 * writer, then reused for the read-back, so a stale byte cannot survive. */
static void stage_pattern(struct helikon_write_test *test)
{
    uint32_t index = 0u;
    while (index < test->length) {
        test->actual[index] = helikon_write_pattern(index);
        ++index;
    }
}

/* One transaction: begin, replace, commit. A refusal anywhere aborts, so a
 * half-staged file cannot stay on the medium. */
static int write_pattern(struct helikon_write_test *test,
                         struct helikon_storage *storage)
{
    int rc = vfs_rw_txn_begin(&storage->rw);
    if (rc != HELIKON_OK) {
        vfs_rw_txn_abort(&storage->rw);
        return rc;
    }
    rc = vfs_rw_txn_replace(&storage->rw, HELIKON_WRITE_TEST_PATH, test->actual,
                            (size_t)test->length);
    if (rc != HELIKON_OK) {
        vfs_rw_txn_abort(&storage->rw);
        return rc;
    }
    rc = vfs_rw_txn_commit(&storage->rw);
    if (rc != HELIKON_OK)
        vfs_rw_txn_abort(&storage->rw);
    return rc;
}

/* Reads the file back into the same buffer and records the size the volume
 * reports, which is the value a human would see through Disk Mode. */
static int read_back(struct helikon_write_test *test,
                     struct helikon_storage *storage)
{
    struct vfs_entry entry;
    int rc = vfs_stat(&storage->vfs, HELIKON_WRITE_TEST_PATH, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if (entry.kind != VFS_KIND_FILE)
        return HELIKON_ERR_FORMAT;
    test->file_size = (uint32_t)entry.size;

    struct vfs_file_handle handle;
    rc = vfs_file_open(&storage->vfs, HELIKON_WRITE_TEST_PATH, &handle);
    if (rc != HELIKON_OK)
        return rc;

    size_t done = 0u;
    size_t total = 0u;
    rc = HELIKON_OK;
    while (total < test->length) {
        rc = vfs_file_read(&storage->vfs, handle, test->actual + total,
                           (size_t)(test->length - total), &done);
        if (rc != HELIKON_OK)
            break;
        if (done == 0u)
            break; /* a short file is a VERIFY failure, not a read error */
        total += done;
    }
    (void)vfs_file_close(&storage->vfs, handle);
    if (rc != HELIKON_OK)
        return rc;
    if (total != (size_t)test->length || test->file_size != test->length)
        return HELIKON_ERR_FORMAT;
    return HELIKON_OK;
}

static int compare_pattern(const struct helikon_write_test *test)
{
    uint32_t index = 0u;
    while (index < test->length) {
        if (test->actual[index] != helikon_write_pattern(index))
            return HELIKON_ERR_FORMAT;
        ++index;
    }
    return HELIKON_OK;
}

int helikon_write_test_run(struct helikon_write_test *test,
                           struct helikon_storage *storage, uint32_t length)
{
    int rc = HELIKON_OK;

    if (test == NULL)
        return HELIKON_ERR_ARGUMENT;
    helikon_memset(test, 0, sizeof(*test));
    test->length = length;
    test->stage = HELIKON_WRITE_STAGE_MOUNT;

    if (storage == NULL || storage->mounted == 0u || storage->device == NULL) {
        test->error = HELIKON_ERR_ARGUMENT;
        return test->error;
    }
    if (length == 0u || length > HELIKON_WRITE_TEST_MAX) {
        test->error = HELIKON_ERR_ARGUMENT;
        return test->error;
    }

    /* A mount but no opt-in ends here: nothing has been written, and the
     * gate's own error says why. */
    test->stage = HELIKON_WRITE_STAGE_GATE;
    rc = helikon_storage_enable_write(storage);
    if (rc != HELIKON_OK) {
        test->error = rc;
        return rc;
    }

    test->stage = HELIKON_WRITE_STAGE_WRITE;
    stage_pattern(test);
    rc = write_pattern(test, storage);
    if (rc != HELIKON_OK) {
        test->error = rc;
        return rc;
    }

    test->stage = HELIKON_WRITE_STAGE_READBACK;
    rc = read_back(test, storage);
    if (rc != HELIKON_OK) {
        test->error = rc;
        return rc;
    }

    test->stage = HELIKON_WRITE_STAGE_VERIFY;
    rc = compare_pattern(test);
    if (rc != HELIKON_OK) {
        test->error = rc;
        return rc;
    }

    test->stage = HELIKON_WRITE_STAGE_DONE;
    test->error = HELIKON_OK;
    test->passed = 1u;
    return HELIKON_OK;
}
