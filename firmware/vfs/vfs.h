/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only VFS over the HFS+ read core (F3, task T-056).
 *
 * The layer is thin and capability-oriented: lookup, iterate, open, read,
 * seek, close. There is one namespace; a non-HFS+ volume is NOT_HFS. Handles
 * are generation-checked values so a use-after-close is an ARGUMENT error,
 * never a stale read. Unsupported objects (symlinks, hardlinks, private
 * entries, kinds the catalog flags) are surfaced with their flag, never
 * hidden. The VFS owns no B-trees, no registers and no second error
 * taxonomy; a dirty journal is recorded and the volume still mounts
 * read-only, and no write API exists (a stub returns READ_ONLY).
 */
#ifndef HELIKON_VFS_H
#define HELIKON_VFS_H

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "hfs_catalog.h"
#include "hfs_extents.h"
#include "hfs_volume.h"

#define VFS_PATH_MAX 4096u
#define VFS_MAX_FILES 8u
#define VFS_MAX_DIRS 4u

/* hfs_catalog maps kind 1 to folders and kind 2 to files. */
#define VFS_KIND_DIR 1u
#define VFS_KIND_FILE 2u

struct vfs_entry {
    uint32_t id;
    uint16_t kind; /* VFS_KIND_* */
    uint8_t unsupported;
    uint64_t size; /* file data fork size; 0 for directories */
    char name[HFS_NAME_UTF8_MAX];
};

struct vfs_file_handle {
    uint32_t generation;
    uint16_t slot;
};

struct vfs_dir_handle {
    uint32_t generation;
    uint16_t slot;
};

struct vfs_file {
    uint32_t generation;
    uint8_t used;
    struct hfs_entry entry;
    uint64_t position;
};

struct vfs_dir {
    uint32_t generation;
    uint8_t used;
    uint8_t primed;
    struct hfs_catalog_iterator iterator;
};

struct vfs {
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_extents extents;
    uint8_t extents_ready;    /* the extents B-tree was opened (or would fail) */
    uint8_t extents_failed;   /* a lazily opened extents B-tree refuses */
    uint8_t journal_dirty;    /* recorded at mount; never replayed here */
    /*
     * Both scratches are kept, not just the extents one, because a write
     * transaction changes the trees underneath the open catalog: after a commit
     * the catalog and extents handles must be re-opened against the bytes that
     * are now on disk, and re-opening needs the buffers they were built with.
     */
    void *catalog_scratch;
    size_t catalog_scratch_size;
    void *extent_scratch;
    size_t extent_scratch_size;
    struct vfs_file files[VFS_MAX_FILES];
    struct vfs_dir dirs[VFS_MAX_DIRS];
};

/*
 * Mount read-only. A dirty, external or uninitialized journal is recorded and
 * the volume stays readable; it can never write. Returns HELIKON_OK,
 * HELIKON_ERR_NOT_HFS for a foreign volume, or another taxonomy error.
 */
int vfs_mount(struct vfs *vfs, hfs_read_fn read, void *context,
              uint64_t base, uint64_t length, void *catalog_scratch,
              size_t catalog_scratch_size, void *extent_scratch,
              size_t extent_scratch_size);

/* Stat a path. Paths are absolute, exact UTF-8, no traversal. */
int vfs_stat(struct vfs *vfs, const char *path, struct vfs_entry *entry);

int vfs_file_open(struct vfs *vfs, const char *path,
                  struct vfs_file_handle *out);

/*
 * Read up to `length` bytes at the current position; the position advances
 * past what was read. A zero `done` at HELIKON_OK means end of file, not
 * failure. A short read that ends at the file size is normal.
 */
int vfs_file_read(struct vfs *vfs, struct vfs_file_handle handle,
                  void *buffer, size_t length, size_t *done);

/* Whence: VFS_SEEK_SET, VFS_SEEK_CUR, VFS_SEEK_END. Seeking past the size
 * refuses with HELIKON_ERR_ARGUMENT; this VFS is bounded, not POSIX. */
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2
int vfs_file_seek(struct vfs *vfs, struct vfs_file_handle handle,
                  int whence, int64_t offset, uint64_t *position);

uint64_t vfs_file_position(const struct vfs *vfs, struct vfs_file_handle handle);

/* Never granted in the read-only VFS: READ_ONLY, unconditionally. */
int vfs_file_write(struct vfs *vfs, struct vfs_file_handle handle,
                   const void *buffer, size_t length, size_t *done);

int vfs_file_close(struct vfs *vfs, struct vfs_file_handle handle);

int vfs_dir_open(struct vfs *vfs, const char *path,
                 struct vfs_dir_handle *out);

/*
 * Next child of the open directory: 1 with a filled entry, 0 at the end, or a
 * negative taxonomy error. Unsupported or private records are returned with
 * entry->unsupported set so a browser can list them with reasons.
 */
int vfs_dir_next(struct vfs *vfs, struct vfs_dir_handle handle,
                 struct vfs_entry *entry);

int vfs_dir_close(struct vfs *vfs, struct vfs_dir_handle handle);

#endif
