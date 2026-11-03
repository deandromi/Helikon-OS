/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_INNER_IMAGE_H
#define HELIKON_INNER_IMAGE_H

#include "hfs_journal.h"
#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

#define HFS_INNER_EXTENT_MAX 128u

struct hfs_catalog;

/*
 * A disposable HFS+ image stored as one regular file on the outer volume.
 * The diagnostic maps only the file's data-fork extents; reads and writes
 * outside those extents fail. The outer volume itself stays read-only.
 */
struct inner_image {
    hfs_read_fn outer_read;
    hfs_write_fn outer_write;
    hfs_flush_fn outer_flush;
    void *outer_context;
    uint64_t outer_base;
    uint32_t outer_block_size;
    struct hfs_extent extents[HFS_INNER_EXTENT_MAX];
    uint64_t logical[HFS_INNER_EXTENT_MAX];
    size_t extent_count;
    uint64_t size;
    uint64_t writes;
};

/*
 * Build the mapping from the outer catalog. `name` is a root-level ASCII file
 * name. `extents_scratch` must hold the outer extents B-tree node (32 KiB for
 * the usual 4 KiB node size). The file must be a regular file and have at
 * least `minimum_size` bytes.
 */
int inner_image_open(struct inner_image *image, struct hfs_volume *outer,
                     struct hfs_catalog *outer_catalog, const char *name,
                     hfs_write_fn outer_write, hfs_flush_fn outer_flush,
                     void *extents_scratch, size_t extents_scratch_size,
                     uint64_t minimum_size);

/*
 * Reject a mapping whose extents overlap outer metadata, the journal, the
 * volume headers or the catalog/extents/allocation forks. Call after
 * inner_image_open and before any write.
 */
int inner_image_guard_metadata(struct inner_image *image,
                               const struct hfs_volume *outer);

int inner_image_read(void *context, uint64_t offset, void *buffer,
                     size_t length);
int inner_image_write(void *context, uint64_t offset, const void *buffer,
                      size_t length);
int inner_image_flush(void *context);

#endif
