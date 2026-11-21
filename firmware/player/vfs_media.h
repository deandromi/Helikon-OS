/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * VFS media adapter for the codec source interface (T-056/T-058 seam).
 *
 * The codec asks its source for bytes at an absolute offset; the VFS offers a
 * positioned read. This adapter is the only place the two meet: it seeks and
 * reads through the read-only VFS, reports end of file as a clean zero-byte
 * read, and never exposes a writable path. The caller owns the file handle
 * and the mounted volume.
 */
#ifndef HELIKON_PLAYER_VFS_MEDIA_H
#define HELIKON_PLAYER_VFS_MEDIA_H

#include <stddef.h>
#include <stdint.h>

#include "codec.h"
#include "vfs.h"

struct helikon_vfs_media {
    struct vfs *vfs;
    struct vfs_file_handle handle;
    uint64_t size;
};

/*
 * Binds an open file to a codec source. `size` is the file's data-fork length
 * (from `vfs_stat`), which bounds the reads; a source cannot discover it.
 * The returned source borrows `media`, so both must outlive the open codec.
 */
int helikon_vfs_media_init(struct helikon_vfs_media *media, struct vfs *vfs,
                           struct vfs_file_handle handle, uint64_t size,
                           struct helikon_codec_source *source);

#endif
