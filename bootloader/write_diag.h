/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_WRITE_DIAG_H
#define HELIKON_WRITE_DIAG_H

#include "hfs_journal.h"
#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

enum boot_write_status {
    BOOT_WRITE_NOT_TESTED = 0,
    BOOT_WRITE_PASS,
    BOOT_WRITE_FAIL,
    BOOT_WRITE_UNSUPPORTED
};

struct boot_write_diag_result {
    enum boot_write_status outer_volume;
    enum boot_write_status test_file;
    enum boot_write_status inner_volume;
    enum boot_write_status inner_catalog;
    enum boot_write_status inner_journal;
    enum boot_write_status create_folder;
    enum boot_write_status create_file;
    enum boot_write_status append;
    enum boot_write_status readback;
    enum boot_write_status rename;
    enum boot_write_status truncate;
    enum boot_write_status delete;
    int error;
    uint32_t inner_block_size;
    uint32_t inner_blocks;
    uint64_t inner_size;
    uint64_t writes;
};

/*
 * Run the native writer inside a preallocated HFS+ image file on the outer
 * volume. The outer volume is read-only: all writes go through inner_image,
 * which is limited to the test file's data-fork extents. `image_name` is a
 * root-level ASCII name. The outer and inner catalog scratch buffers must each
 * be at least the largest B-tree node size (32 KiB covers the usual 4 KiB).
 */
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
                        struct boot_write_diag_result *result);

#endif
