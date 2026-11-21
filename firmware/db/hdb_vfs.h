/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HelikonDB over the read-only VFS (T-067 slice).
 *
 * The HDB reader asks for absolute byte ranges through `struct hdb_io`. This
 * adapter answers those calls from one open VFS file by seeking and reading,
 * which is the same path the firmware uses for a storage-backed catalog: mount
 * the volume read-only, open `/.helikon/music.hdb`, hand the reader this
 * adapter instead of an embedded blob. It never writes and it never allocates.
 */
#ifndef HELIKON_MUSICDB_HDB_VFS_H
#define HELIKON_MUSICDB_HDB_VFS_H

#include <stdint.h>

#include "hdb.h"
#include "vfs.h"

struct hdb_vfs_io {
    struct vfs *vfs;
    struct vfs_file_handle handle;
    uint32_t size; /* data fork size reported by the catalog */
    uint8_t open;
};

/*
 * Stats `path` and opens it. Returns HELIKON_OK, HELIKON_ERR_NOT_FOUND when the
 * path does not exist, HELIKON_ERR_IS_DIR for a folder, HELIKON_ERR_BUFFER when
 * the file cannot fit the reader's 32-bit offset model, or another taxonomy
 * error from the VFS.
 */
int hdb_vfs_open(struct hdb_vfs_io *io, struct vfs *vfs, const char *path);

/* Fills `out` with this adapter's read callback. */
void hdb_vfs_io_init(struct hdb_vfs_io *io, struct hdb_io *out);

/* Closes the file if it is open. Safe to call twice. */
void hdb_vfs_close(struct hdb_vfs_io *io);

#endif
