/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_LOADER_H
#define HELIKON_HFS_LOADER_H

#include "firmware_loader.h"
#include "hfs_catalog.h"
#include "hfs_extents.h"

#include <stddef.h>
#include <stdint.h>

struct helikon_hfs_loader {
    struct hfs_catalog *catalog;
    struct hfs_extents *extents;
};

/*
 * `helikon_file_read_fn` adapter over the native HFS+ read core. Reads inline
 * and extents-overflow files. The context must point at a live
 * struct helikon_hfs_loader; the caller owns the catalog/extents scratch.
 */
int helikon_hfs_read_file(void *context, const char *path, uint64_t offset,
                          void *buffer, size_t length, size_t *done);

#endif
