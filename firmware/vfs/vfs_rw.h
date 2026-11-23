/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The write half of the VFS (T-065): create, mkdir, unlink, rmdir, rename,
 * write and truncate, each one a journaled transaction.
 *
 * The read-only VFS and the HFS+ writer already exist and are tested apart.
 * This file is the layer that turns "the writer can create a file if you call
 * it with a parent CNID, a UTF-16 name and a transaction" into "create this
 * path", which is what a player, a playlist importer or a settings file
 * actually needs. It owns nothing new: a transaction engine, a journal, a
 * catalog and a path parser, in that order.
 *
 * Rules, because a half-applied filesystem is worse than a refused call:
 *
 *   - One transaction per call. A call that fails to stage or to commit leaves
 *     the volume as it was: the transaction is aborted, nothing is written.
 *   - A commit that fails at the journal leaves the volume dirty and this
 *     object **stops writing** (`writable = 0`). Every later write answers
 *     READ_ONLY until the caller re-mounts and the replay succeeds. There is no
 *     "try once more" path: a failed commit means the journal's state is
 *     unknown, and I-02 forbids guessing.
 *   - A dirty journal at mount is replayed first (D-04: replay is ours, not
 *     macOS's job), then the volume is re-probed and the catalog re-opened,
 *     because a replay can move every tree. A replay that fails leaves this
 *     object read-only and returns the error.
 *   - A volume without a journal is refused (UNSUPPORTED). There is no
 *     non-journaled writer in Helikon-OS (I-02), not even behind a flag.
 *   - Paths are absolute, exact UTF-8, one existing parent component, no
 *     trailing slash, no `.` or `..`, no empty component. The leaf is encoded
 *     to UTF-16BE with the HFS+ rules; the catalog writer normalizes it.
 *   - **Every open file and directory handle is invalidated by a successful
 *     commit.** A handle caches the fork it read, and a write changes forks and
 *     B-trees underneath it; reusing it would read a stale extent list. The
 *     caller re-opens.
 *   - A write longer than `VFS_RW_IO_MAX` is shortened, and `written` says how
 *     much was applied: that is a short write to continue, not an error. A
 *     range that does not fit this volume's transaction budget returns
 *     `NO_SPACE` with nothing applied, and the caller asks for less.
 */
#ifndef HELIKON_VFS_RW_H
#define HELIKON_VFS_RW_H

#include <stddef.h>
#include <stdint.h>

#include "hfs_catalog_write.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "vfs.h"

/* One call's data budget: a few blocks of staging plus the metadata record. */
#define VFS_RW_IO_MAX 8192u
/* HFS+ names are at most 255 UTF-16 code units. */
#define VFS_RW_NAME_UNITS 255u

struct vfs_rw {
    struct vfs *vfs;
    struct hfs_journal journal;
    struct hfs_txn txn;
    hfs_write_fn write;
    hfs_flush_fn flush;
    void *context;
    uint8_t writable;
};

struct vfs_rw_path {
    uint32_t parent;                        /* CNID of the containing folder */
    uint8_t name[2u * VFS_RW_NAME_UNITS];   /* UTF-16BE code units */
    size_t units;
};

/*
 * Opens the volume for writing: requires a journaled, clean-or-replayable
 * volume already mounted read-only through `vfs`. Sets `writable` on success.
 * `write`/`flush` are the same device the read callback came from; `context` is
 * theirs.
 */
int vfs_rw_mount(struct vfs_rw *rw, struct vfs *vfs, hfs_write_fn write,
                 hfs_flush_fn flush, void *context);

/* Drops the writable flag and the transaction. Leaks nothing; allocates none. */
void vfs_rw_close(struct vfs_rw *rw);

/*
 * Splits an absolute path into its parent CNID and UTF-16 leaf. Public because
 * a caller that writes a file usually wants the same parse for its own checks,
 * and because the test asserts the refusals on it directly.
 */
int vfs_rw_split(struct vfs *vfs, const char *path,
                 struct vfs_rw_path *out);

int vfs_rw_create(struct vfs_rw *rw, const char *path, uint32_t *id);
int vfs_rw_mkdir(struct vfs_rw *rw, const char *path, uint32_t *id);
/* Refuses a folder; the CNID and kind come from the catalog, not the caller. */
int vfs_rw_unlink(struct vfs_rw *rw, const char *path);
/* Refuses a file and a non-empty folder (NOT_EMPTY). */
int vfs_rw_rmdir(struct vfs_rw *rw, const char *path);
/* Rename or move, same volume; a name that collides is BUSY. */
int vfs_rw_rename(struct vfs_rw *rw, const char *from, const char *to);

/*
 * Writes `length` bytes at `offset`, growing the file when the range ends past
 * its size (the gap is zero-filled by the writer). At most `VFS_RW_IO_MAX`
 * bytes are applied per call; `written` receives the count.
 */
int vfs_rw_write(struct vfs_rw *rw, const char *path, uint64_t offset,
                 const void *data, size_t length, size_t *written);

/*
 * Truncates an existing file to `size`. Growing through truncate is ARGUMENT:
 * a caller that wants a longer file writes to it, which is the call that knows
 * what the new bytes are.
 */
int vfs_rw_truncate(struct vfs_rw *rw, const char *path, uint64_t size);

#endif
