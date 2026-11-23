/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Resume bookmark over the VFS (T-093). See `vfs_bookmark.h` for the contract
 * and the replace-before-rename argument.
 */
#include "vfs_bookmark.h"

#include <stddef.h>

#include "helikon_error.h"
#include "mem.h"

/* A temporary sibling of the record: `<path>` plus ".tmp". */
#define BOOKMARK_SUFFIX ".tmp"
#define BOOKMARK_SUFFIX_LENGTH (sizeof(BOOKMARK_SUFFIX) - 1u)
#define BOOKMARK_PATH_LENGTH 256u

static int path_length(const char *path, size_t *length)
{
    if (path == NULL)
        return HELIKON_ERR_ARGUMENT;
    for (size_t index = 0u; index < BOOKMARK_PATH_LENGTH; ++index) {
        if (path[index] == '\0') {
            *length = index;
            return index == 0u ? HELIKON_ERR_ARGUMENT : HELIKON_OK;
        }
    }
    return HELIKON_ERR_ARGUMENT;
}

int helikon_vfs_bookmark_save(struct vfs_rw *rw, const char *path,
                              const struct helikon_bookmark *bookmark)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    /* Say why before touching a path: a read-only mount is not a missing file. */
    if (rw->writable == 0u)
        return HELIKON_ERR_READ_ONLY;

    size_t length = 0u;
    int rc = path_length(path, &length);
    if (rc != HELIKON_OK)
        return rc;
    if (length + BOOKMARK_SUFFIX_LENGTH + 1u > BOOKMARK_PATH_LENGTH)
        return HELIKON_ERR_ARGUMENT;

    char text[HELIKON_BOOKMARK_TEXT_MAX];
    size_t bytes = helikon_bookmark_write(bookmark, text, sizeof(text));
    if (bytes == 0u)
        return HELIKON_ERR_ARGUMENT;

    char temporary[BOOKMARK_PATH_LENGTH];
    for (size_t index = 0u; index < length; ++index)
        temporary[index] = path[index];
    for (size_t index = 0u; index < BOOKMARK_SUFFIX_LENGTH; ++index)
        temporary[length + index] = BOOKMARK_SUFFIX[index];
    temporary[length + BOOKMARK_SUFFIX_LENGTH] = '\0';

    /* A stale temporary from a cut is ours to remove; anything else is not. */
    rc = vfs_rw_unlink(rw, temporary);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_NOT_FOUND)
        return rc;

    uint32_t id = 0u;
    rc = vfs_rw_create(rw, temporary, &id);
    if (rc != HELIKON_OK)
        return rc;

    size_t written = 0u;
    rc = vfs_rw_write(rw, temporary, 0u, text, bytes, &written);
    if (rc != HELIKON_OK)
        return rc;
    if (written != bytes)
        return HELIKON_ERR_IO;

    /* Read the record back before letting it replace the old one. */
    struct helikon_bookmark check;
    rc = helikon_vfs_bookmark_load(rw->vfs, temporary, &check);
    if (rc != HELIKON_OK)
        return rc;
    if (check.position_ms != bookmark->position_ms ||
        check.duration_ms != bookmark->duration_ms ||
        check.track_id != bookmark->track_id ||
        check.saved_at != bookmark->saved_at) {
        return HELIKON_ERR_IO;
    }
    for (size_t index = 0u; index < HELIKON_BOOKMARK_PATH_MAX; ++index) {
        if (check.path[index] != bookmark->path[index])
            return HELIKON_ERR_IO;
        if (bookmark->path[index] == '\0')
            break;
    }

    rc = vfs_rw_unlink(rw, path);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_NOT_FOUND)
        return rc;
    return vfs_rw_rename(rw, temporary, path);
}

int helikon_vfs_bookmark_load(struct vfs *vfs, const char *path,
                              struct helikon_bookmark *bookmark)
{
    if (vfs == NULL || bookmark == NULL)
        return HELIKON_ERR_ARGUMENT;
    helikon_memset(bookmark, 0, sizeof(*bookmark));

    size_t length = 0u;
    int rc = path_length(path, &length);
    if (rc != HELIKON_OK)
        return rc;

    struct vfs_entry entry;
    rc = vfs_stat(vfs, path, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if (entry.kind != VFS_KIND_FILE)
        return HELIKON_ERR_FORMAT;
    /* One byte past the limit is enough to know it is not our record. */
    if (entry.size > HELIKON_BOOKMARK_TEXT_MAX)
        return HELIKON_ERR_FORMAT;

    struct vfs_file_handle handle;
    rc = vfs_file_open(vfs, path, &handle);
    if (rc != HELIKON_OK)
        return rc;

    char text[HELIKON_BOOKMARK_TEXT_MAX + 1u];
    size_t done = 0u;
    rc = vfs_file_read(vfs, handle, text, sizeof(text), &done);
    if (rc == HELIKON_OK && done > HELIKON_BOOKMARK_TEXT_MAX)
        rc = HELIKON_ERR_FORMAT;
    if (rc == HELIKON_OK && done != (size_t)entry.size)
        rc = HELIKON_ERR_IO;
    int closed = vfs_file_close(vfs, handle);
    if (rc == HELIKON_OK)
        rc = closed;
    if (rc != HELIKON_OK)
        return rc;

    return helikon_bookmark_parse(bookmark, text, done);
}
