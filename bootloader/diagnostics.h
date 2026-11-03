/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_BOOTLOADER_DIAGNOSTICS_H
#define HELIKON_BOOTLOADER_DIAGNOSTICS_H

#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

enum boot_diag_status {
    BOOT_DIAG_PASS,
    BOOT_DIAG_FAIL,
    BOOT_DIAG_NOT_TESTED,
    BOOT_DIAG_NOT_IMPLEMENTED
};

enum boot_diag_journal {
    BOOT_DIAG_JOURNAL_NOT_PRESENT,
    BOOT_DIAG_JOURNAL_CLEAN,
    BOOT_DIAG_JOURNAL_DIRTY,
    BOOT_DIAG_JOURNAL_UNKNOWN
};

struct boot_diag_result {
    enum boot_diag_status volume;
    enum boot_diag_status catalog;
    enum boot_diag_status read;
    enum boot_diag_status write;
    enum boot_diag_status splash;
    enum boot_diag_journal journal;
    int volume_error;
    int catalog_error;
    int read_error;
    uint32_t block_size;
    uint32_t blocks;
    uint64_t bytes_read;
};

/*
 * Read-only diagnostic. It never writes to the volume. `write` is always
 * BOOT_DIAG_NOT_IMPLEMENTED until the HFS+ transaction engine exists.
 *
 * `probe_path` may be NULL to skip the file-read test. `catalog_scratch` is
 * required; `extents_scratch` is required only for a non-inline probe file.
 */
int boot_diag_run(hfs_read_fn read, void *context, uint64_t base,
                  uint64_t length, const char *probe_path,
                  void *catalog_scratch, size_t catalog_scratch_size,
                  void *extents_scratch, size_t extents_scratch_size,
                  struct boot_diag_result *result);

const char *boot_diag_status_name(enum boot_diag_status status);
const char *boot_diag_journal_name(enum boot_diag_journal journal);

#endif
