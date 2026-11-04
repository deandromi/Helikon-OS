/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_OUTER_DIAG_H
#define HELIKON_OUTER_DIAG_H

#include "hfs_journal.h"
#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

typedef void (*boot_outer_progress_fn)(void *context, const char *stage);

enum boot_outer_status {
    BOOT_OUTER_NOT_TESTED = 0,
    BOOT_OUTER_PASS,
    BOOT_OUTER_FAIL,
    BOOT_OUTER_UNSUPPORTED
};

struct boot_outer_diag_result {
    enum boot_outer_status opt_in;
    enum boot_outer_status outer_volume;
    enum boot_outer_status outer_journal;
    enum boot_outer_status catalog;
    enum boot_outer_status cleanup;
    enum boot_outer_status create_folder;
    enum boot_outer_status create_file;
    enum boot_outer_status append;
    enum boot_outer_status readback;
    enum boot_outer_status rename;
    enum boot_outer_status truncate;
    enum boot_outer_status delete_file;
    enum boot_outer_status delete_folder;
    enum boot_outer_status restored;
    int error;
    uint32_t free_blocks_before;
    uint32_t free_blocks_after;
    uint64_t writes;
};

/*
 * Run the native writer directly on the outer HFS+ volume.
 *
 * This is deliberately narrow: it requires a root-level opt-in file with the
 * exact magic, refuses a dirty journal, and only creates/deletes the dedicated
 * `/helikon-outer-test` folder and its known marker and test files. It does not
 * replay a dirty journal and does not touch any other catalog entry.
 *
 * `writes` is an optional counter maintained by the caller's write callback.
 * The catalog scratch buffer must be at least the largest B-tree node size
 * (32 KiB covers the usual 4 KiB node).
 */
int boot_outer_diag_run(hfs_read_fn read, hfs_write_fn write,
                        hfs_flush_fn flush, void *context,
                        uint64_t base, uint64_t length,
                        void *catalog_scratch, size_t catalog_scratch_size,
                        uint32_t now, uint64_t *writes,
                        boot_outer_progress_fn progress,
                        void *progress_context,
                        struct boot_outer_diag_result *result);

#endif
