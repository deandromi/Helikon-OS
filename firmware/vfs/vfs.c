/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "vfs.h"

#include <string.h>

/*
 * Stale closed handles refuse with HELIKON_ERR_ARGUMENT: the slot generation
 * no longer matches, so a use-after-close cannot read through a freed
 * descriptor. The generation wraps harmlessly because `used` gates validity.
 */

static int path_valid(const char *path)
{
    if (path == NULL || path[0] != '/')
        return 0;
    size_t length = strlen(path);
    if (length == 1u)
        return 1; /* the root */
    if (length >= VFS_PATH_MAX || path[length - 1u] == '/')
        return 0;
    size_t start = 1u;
    for (size_t i = 1u; i <= length; ++i) {
        if (i != length && path[i] != '/')
            continue;
        size_t component = i - start;
        if (component == 0u)
            return 0; /* "//" or trailing slash */
        if (component == 1u && path[start] == '.')
            return 0;
        if (component == 2u && path[start] == '.' && path[start + 1u] == '.')
            return 0;
        start = i + 1u;
    }
    return 1;
}

static const struct vfs_file *file_slot_const(const struct vfs *vfs,
                                              struct vfs_file_handle handle)
{
    if (vfs == NULL || handle.slot >= VFS_MAX_FILES)
        return NULL;
    const struct vfs_file *file = &vfs->files[handle.slot];
    if (file->used == 0u || file->generation != handle.generation)
        return NULL;
    return file;
}

static struct vfs_file *file_slot(struct vfs *vfs,
                                  struct vfs_file_handle handle)
{
    return (struct vfs_file *)file_slot_const(vfs, handle);
}

static struct vfs_dir *dir_slot(struct vfs *vfs, struct vfs_dir_handle handle)
{
    if (vfs == NULL || handle.slot >= VFS_MAX_DIRS)
        return NULL;
    struct vfs_dir *dir = &vfs->dirs[handle.slot];
    if (dir->used == 0u || dir->generation != handle.generation)
        return NULL;
    return dir;
}

static int entry_map(const struct hfs_entry *hfs, struct vfs_entry *entry)
{
    if (hfs == NULL || entry == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (hfs->kind == VFS_KIND_DIR || hfs->kind == VFS_KIND_FILE) {
        entry->kind = hfs->kind;
        entry->unsupported = hfs->unsupported;
    } else {
        entry->kind = 0u;
        entry->unsupported = 1u;
    }
    entry->id = hfs->id;
    entry->size = (hfs->kind == VFS_KIND_FILE) ? hfs->data.size : 0u;
    memcpy(entry->name, hfs->name, sizeof(entry->name));
    entry->name[sizeof(entry->name) - 1u] = '\0';
    return HELIKON_OK;
}

static int inline_complete(const struct hfs_fork *fork)
{
    uint32_t blocks = 0u;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        blocks += fork->extents[i].count;
    return blocks == fork->blocks;
}

/*
 * Ensure the extents B-tree is open before an overflow read. A volume whose
 * extents file is empty (all forks inline) never needs it; a malformed real
 * extents file fails the mount immediately, and a lazy open that fails is
 * sticky so the next read reports the same error.
 */
static int extents_ready(struct vfs *vfs)
{
    if (vfs->extents_ready != 0u)
        return vfs->extents_failed != 0u ? HELIKON_ERR_FORMAT : HELIKON_OK;
    int rc = hfs_extents_open(&vfs->extents, &vfs->volume, vfs->extent_scratch,
                              vfs->extent_scratch_size);
    vfs->extents_ready = 1u;
    if (rc != HELIKON_OK) {
        vfs->extents_failed = 1u;
        return rc;
    }
    return HELIKON_OK;
}

int vfs_mount(struct vfs *vfs, hfs_read_fn read, void *context,
              uint64_t base, uint64_t length, void *catalog_scratch,
              size_t catalog_scratch_size, void *extent_scratch,
              size_t extent_scratch_size)
{
    if (vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    memset(vfs, 0, sizeof(*vfs));
    int rc = hfs_volume_probe_readonly(&vfs->volume, read, context, base,
                                       length);
    if (rc != HELIKON_OK)
        return rc;
    vfs->journal_dirty = vfs->volume.journal_dirty;
    rc = hfs_catalog_open(&vfs->catalog, &vfs->volume, catalog_scratch,
                          catalog_scratch_size);
    if (rc != HELIKON_OK)
        return rc;
    vfs->catalog_scratch = catalog_scratch;
    vfs->catalog_scratch_size = catalog_scratch_size;
    vfs->extent_scratch = extent_scratch;
    vfs->extent_scratch_size = extent_scratch_size;
    if (vfs->volume.extents.blocks != 0u) {
        rc = extents_ready(vfs);
        if (rc != HELIKON_OK)
            return rc;
    }
    return HELIKON_OK;
}

int vfs_stat(struct vfs *vfs, const char *path, struct vfs_entry *entry)
{
    if (vfs == NULL || entry == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (!path_valid(path))
        return HELIKON_ERR_NAME_INVALID;
    struct hfs_entry hfs;
    int rc = hfs_catalog_lookup(&vfs->catalog, path, &hfs);
    if (rc != HELIKON_OK)
        return rc;
    return entry_map(&hfs, entry);
}

int vfs_file_open(struct vfs *vfs, const char *path,
                  struct vfs_file_handle *out)
{
    if (vfs == NULL || out == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (!path_valid(path))
        return HELIKON_ERR_NAME_INVALID;
    struct hfs_entry hfs;
    int rc = hfs_catalog_lookup(&vfs->catalog, path, &hfs);
    if (rc != HELIKON_OK)
        return rc;
    if (hfs.kind == VFS_KIND_DIR)
        return HELIKON_ERR_IS_DIR;
    if (hfs.kind != VFS_KIND_FILE || hfs.unsupported != 0u)
        return HELIKON_ERR_UNSUPPORTED;

    struct vfs_file *file = NULL;
    uint16_t slot = 0u;
    while (slot < VFS_MAX_FILES) {
        if (vfs->files[slot].used == 0u) {
            file = &vfs->files[slot];
            break;
        }
        ++slot;
    }
    if (file == NULL)
        return HELIKON_ERR_BUSY;

    file->used = 1u;
    file->generation += 1u;
    file->entry = hfs;
    file->position = 0u;
    out->slot = slot;
    out->generation = file->generation;
    return HELIKON_OK;
}

int vfs_file_read(struct vfs *vfs, struct vfs_file_handle handle,
                  void *buffer, size_t length, size_t *done)
{
    if (done == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    *done = 0u;
    struct vfs_file *file = file_slot(vfs, handle);
    if (file == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (file->position >= file->entry.data.size)
        return HELIKON_OK;
    uint64_t remaining = file->entry.data.size - file->position;
    size_t count = length;
    if ((uint64_t)count > remaining)
        count = (size_t)remaining;
    if (!inline_complete(&file->entry.data)) {
        int orc = extents_ready(vfs);
        if (orc != HELIKON_OK)
            return orc;
        size_t read_done = 0u;
        int rc = hfs_extents_read(&vfs->extents, &file->entry.data,
                                  file->entry.id, file->position, buffer, count,
                                  &read_done);
        if (rc != HELIKON_OK)
            return rc;
        /*
         * An overflow read has to advance the handle and report the byte count
         * like the inline path: returning the raw reader result left done == 0
         * and the position unchanged, so a caller looped forever on the same
         * bytes.
         */
        *done = read_done;
        file->position += read_done;
        return HELIKON_OK;
    }
    size_t read_done = 0u;
    int rc = hfs_fork_read(&vfs->volume, &file->entry.data, file->position,
                           buffer, count);
    if (rc != HELIKON_OK)
        return rc;
    read_done = count;
    *done = read_done;
    file->position += read_done;
    return HELIKON_OK;
}

int vfs_file_seek(struct vfs *vfs, struct vfs_file_handle handle,
                  int whence, int64_t offset, uint64_t *position)
{
    struct vfs_file *file = file_slot(vfs, handle);
    if (file == NULL || position == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (offset < 0)
        return HELIKON_ERR_ARGUMENT;
    uint64_t target;
    if (whence == VFS_SEEK_SET)
        target = (uint64_t)offset;
    else if (whence == VFS_SEEK_CUR || whence == VFS_SEEK_END) {
        uint64_t origin = (whence == VFS_SEEK_CUR) ? file->position
                                                   : file->entry.data.size;
        target = origin + (uint64_t)offset;
    } else
        return HELIKON_ERR_ARGUMENT;
    if (target > file->entry.data.size)
        return HELIKON_ERR_ARGUMENT;
    file->position = target;
    *position = file->position;
    return HELIKON_OK;
}

uint64_t vfs_file_position(const struct vfs *vfs,
                           struct vfs_file_handle handle)
{
    const struct vfs_file *file = file_slot_const(vfs, handle);
    if (file == NULL)
        return 0u;
    return file->position;
}

int vfs_file_write(struct vfs *vfs, struct vfs_file_handle handle,
                   const void *buffer, size_t length, size_t *done)
{
    (void)vfs;
    (void)handle;
    (void)buffer;
    (void)length;
    if (done != NULL)
        *done = 0u;
    return HELIKON_ERR_READ_ONLY;
}

int vfs_file_close(struct vfs *vfs, struct vfs_file_handle handle)
{
    struct vfs_file *file = file_slot(vfs, handle);
    if (file == NULL)
        return HELIKON_ERR_ARGUMENT;
    file->generation += 1u;
    file->used = 0u;
    return HELIKON_OK;
}

int vfs_dir_open(struct vfs *vfs, const char *path,
                 struct vfs_dir_handle *out)
{
    if (vfs == NULL || out == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (!path_valid(path))
        return HELIKON_ERR_NAME_INVALID;
    uint32_t parent = 2u; /* the kHFSRootParentID folder thread root */
    if (path[1] != '\0') {
        struct hfs_entry hfs;
        int rc = hfs_catalog_lookup(&vfs->catalog, path, &hfs);
        if (rc != HELIKON_OK)
            return rc;
        if (hfs.kind != VFS_KIND_DIR)
            return HELIKON_ERR_NOT_DIR;
        if (hfs.unsupported != 0u)
            return HELIKON_ERR_UNSUPPORTED;
        parent = hfs.id;
    }

    struct vfs_dir *dir = NULL;
    uint16_t slot = 0u;
    while (slot < VFS_MAX_DIRS) {
        if (vfs->dirs[slot].used == 0u) {
            dir = &vfs->dirs[slot];
            break;
        }
        ++slot;
    }
    if (dir == NULL)
        return HELIKON_ERR_BUSY;

    int rc = hfs_catalog_iter_init(&vfs->catalog, parent, &dir->iterator);
    if (rc != HELIKON_OK)
        return rc;
    dir->used = 1u;
    dir->primed = 1u;
    dir->generation += 1u;
    out->slot = slot;
    out->generation = dir->generation;
    return HELIKON_OK;
}

int vfs_dir_next(struct vfs *vfs, struct vfs_dir_handle handle,
                 struct vfs_entry *entry)
{
    if (entry == NULL)
        return HELIKON_ERR_ARGUMENT;
    struct vfs_dir *dir = dir_slot(vfs, handle);
    if (dir == NULL)
        return HELIKON_ERR_ARGUMENT;
    struct hfs_entry hfs;
    int rc = hfs_catalog_iter_next(&vfs->catalog, &dir->iterator, &hfs);
    if (rc == 0)
        return 0;
    if (rc < 0)
        return rc;
    rc = entry_map(&hfs, entry);
    if (rc != HELIKON_OK)
        return rc;
    return 1;
}

int vfs_dir_close(struct vfs *vfs, struct vfs_dir_handle handle)
{
    struct vfs_dir *dir = dir_slot(vfs, handle);
    if (dir == NULL)
        return HELIKON_ERR_ARGUMENT;
    dir->generation += 1u;
    dir->used = 0u;
    return HELIKON_OK;
}
