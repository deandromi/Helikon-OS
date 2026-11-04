/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read one path through the existing HFS+ catalog and extents-overflow core.
 *
 * This is the F1 read adapter, not a VFS: it performs one exact path lookup and
 * one bounded read. Unsupported objects are returned as UNSUPPORTED, never
 * hidden. A missing file is NOT_FOUND. The caller owns the output buffer; the
 * function borrows it for the call and never writes the volume.
 */
#ifndef HELIKON_HFS_FILE_H
#define HELIKON_HFS_FILE_H

#include <stddef.h>
#include <stdint.h>

#include "hfs_catalog.h"
#include "hfs_extents.h"

int helikon_hfs_read_file(struct hfs_catalog *catalog,
                          struct hfs_extents *extents, const char *path,
                          uint64_t offset, void *buffer, size_t length,
                          size_t *done);

#endif
