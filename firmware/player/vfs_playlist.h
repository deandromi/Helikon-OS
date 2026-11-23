/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * VFS destination for the playlist writer (T-065 seam).
 *
 * `firmware/player/playlist.c` knows how a playlist is spelled and nothing about
 * where the bytes go. This adapter is the only place the writer meets the write
 * VFS: it creates the file, turns each sink call into `vfs_rw_write` calls of at
 * most `VFS_RW_IO_MAX` bytes, and reports a short write as the failure it is.
 *
 * Two rules worth stating, because both are choices:
 *
 *   - An existing file is removed first. A playlist that shrinks must not keep
 *     the tail of the one it replaces, and the VFS writer grows files rather than
 *     rewriting them.
 *   - The playlist writer is not atomic across its lines, so a crash mid-export
 *     leaves a partial file. The caller that cares writes a temporary name and
 *     renames it through the same VFS (one transaction), which is the update
 *     client's pattern and the reason `vfs_rw_txn_*` exists.
 */
#ifndef HELIKON_PLAYER_VFS_PLAYLIST_H
#define HELIKON_PLAYER_VFS_PLAYLIST_H

#include <stddef.h>
#include <stdint.h>

#include "playlist.h"
#include "vfs_rw.h"

struct helikon_vfs_playlist {
    struct vfs_rw *rw;
    const char *path;
    uint64_t offset;
};

/*
 * Creates `path` empty and bound to `rw`. Refuses a read-only writer, a path the
 * VFS rejects, and a volume that is not writable; nothing is created on a
 * refusal.
 */
int helikon_vfs_playlist_begin(struct helikon_vfs_playlist *destination,
                               struct vfs_rw *rw, const char *path);

/* Fills `sink` with the callback the playlist writer calls. */
void helikon_vfs_playlist_sink(struct helikon_vfs_playlist *destination,
                               struct helikon_playlist_sink *sink);

/* Bytes written so far; the caller does not have to track them. */
uint64_t helikon_vfs_playlist_size(const struct helikon_vfs_playlist *destination);

#endif
