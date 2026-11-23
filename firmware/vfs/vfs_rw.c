/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Write half of the VFS. See vfs_rw.h for the rules this file implements.
 */
#include "vfs_rw.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_extents.h"
#include "hfs_unicode.h"
#include "mem.h"

/* The root folder's CNID; the catalog thread root is CNID 2. */
#define VFS_RW_ROOT_CNID 2u

#define VFS_RW_FILE_MODE 0644u
#define VFS_RW_FOLDER_MODE 0755u

static int rw_begin(struct vfs_rw *rw)
{
    if (rw == NULL || rw->vfs == NULL || rw->writable == 0u)
        return HELIKON_ERR_READ_ONLY;
    return hfs_txn_begin(&rw->txn, &rw->journal);
}

/*
 * Re-opens the trees against what is now on disk and invalidates every open
 * handle. A handle caches its fork; after a commit that fork may name blocks
 * that were reallocated, and a stale extent list is exactly the read this
 * project refuses to serve.
 */
static int rw_refresh(struct vfs_rw *rw)
{
    struct vfs *vfs = rw->vfs;
    for (size_t index = 0u; index < VFS_MAX_FILES; ++index) {
        vfs->files[index].generation += 1u;
        vfs->files[index].used = 0u;
    }
    for (size_t index = 0u; index < VFS_MAX_DIRS; ++index) {
        vfs->dirs[index].generation += 1u;
        vfs->dirs[index].used = 0u;
    }

    int rc = hfs_catalog_open(&vfs->catalog, &vfs->volume,
                              vfs->catalog_scratch, vfs->catalog_scratch_size);
    if (rc != HELIKON_OK)
        return rc;
    if (vfs->extents_ready != 0u) {
        rc = hfs_extents_open(&vfs->extents, &vfs->volume, vfs->extent_scratch,
                              vfs->extent_scratch_size);
        if (rc != HELIKON_OK) {
            vfs->extents_ready = 0u;
            vfs->extents_failed = 1u;
        }
    }
    return HELIKON_OK;
}

/*
 * Re-reads the volume header and re-opens the trees.
 *
 * A transaction stages into memory as well as into the txn: the allocation
 * bitmap keeps a running free-block count and the B-tree handles keep header
 * counters and node state. A commit refreshes them from what is now on disk, and
 * an abort must do the same — otherwise the next write, or the structural check,
 * compares a counter the medium never saw. This is the abort-side counterpart of
 * rw_refresh(), and it exists because the transaction-scope test caught exactly
 * that (HFS_CHECK_FREE_MISMATCH after an aborted delete).
 */
static int rw_reload(struct vfs_rw *rw)
{
    struct vfs *vfs = rw->vfs;
    hfs_read_fn read = vfs->volume.read;
    void *context = vfs->volume.context;
    uint64_t base = vfs->volume.base;
    uint64_t length = vfs->volume.length;
    int rc = hfs_volume_probe_readonly(&vfs->volume, read, context, base,
                                       length);
    if (rc != HELIKON_OK)
        return rc;
    return rw_refresh(rw);
}

static int rw_commit(struct vfs_rw *rw)
{
    if (hfs_txn_commit(&rw->txn) != HELIKON_OK) {
        /*
         * The journal's state is unknown now; keep the volume open for reading
         * but never write it again from this object.
         */
        rw->writable = 0u;
        return HELIKON_ERR_IO;
    }
    int rc = rw_refresh(rw);
    if (rc != HELIKON_OK) {
        rw->writable = 0u;
        return rc;
    }
    return HELIKON_OK;
}

static int rw_abort(struct vfs_rw *rw, int error)
{
    hfs_txn_abort(&rw->txn);
    return error;
}

/* ------------------------------------------------------------------------- */
/* Paths                                                                     */
/* ------------------------------------------------------------------------- */

/*
 * Absolute, exact, one level: `/a`, `/dir/file`. Refused here rather than
 * normalized, because a writer that silently reinterprets "a/../b" writes a
 * file nobody asked for. Unicode is not the parser's business: the leaf goes to
 * the HFS+ encoder, which owns the rules.
 */
int vfs_rw_split(struct vfs *vfs, const char *path,
                 struct vfs_rw_path *out)
{
    if (vfs == NULL || path == NULL || out == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (path[0] != '/')
        return HELIKON_ERR_ARGUMENT;

    size_t length = 0u;
    while (path[length] != '\0') {
        if (length + 1u >= VFS_PATH_MAX)
            return HELIKON_ERR_NAME_INVALID;
        ++length;
    }
    if (length < 2u || path[length - 1u] == '/')
        return HELIKON_ERR_ARGUMENT;

    /*
     * Syntax first, resolution second. Every component is checked before any
     * lookup, so `/a//b` is ARGUMENT whether or not `/a` exists: an error code
     * that depends on what is on the volume is an error code a caller cannot
     * branch on.
     *
     * `.` and `..` are refused anywhere, not just as the leaf. A writer that
     * silently reinterprets "a/../b" writes a file nobody asked for, and this
     * layer has no notion of a current directory to resolve them against.
     */
    for (size_t index = 1u; index < length; ++index) {
        if (path[index] != '/')
            continue;
        size_t component_start = 0u;
        for (size_t back = index; back > 0u; --back) {
            if (path[back - 1u] == '/') {
                component_start = back;
                break;
            }
        }
        size_t component_length = index - component_start;
        const char *component = path + component_start;
        if (component_length == 0u)
            return HELIKON_ERR_ARGUMENT; /* "a//b" */
        if ((component_length == 1u && component[0] == '.') ||
            (component_length == 2u && component[0] == '.' &&
             component[1] == '.'))
            return HELIKON_ERR_ARGUMENT;
    }
    {
        size_t leaf_start = 0u;
        for (size_t back = length; back > 0u; --back) {
            if (path[back - 1u] == '/') {
                leaf_start = back;
                break;
            }
        }
        size_t leaf_length = length - leaf_start;
        const char *leaf = path + leaf_start;
        if (leaf_length == 0u)
            return HELIKON_ERR_ARGUMENT;
        if ((leaf_length == 1u && leaf[0] == '.') ||
            (leaf_length == 2u && leaf[0] == '.' && leaf[1] == '.'))
            return HELIKON_ERR_ARGUMENT;
    }

    /*
     * Walk the intermediate components, one catalog lookup each, and hand back
     * the leaf. The walk is what a player's path actually looks like
     * (`/Music/Album/Track.mp3`) and what the loader's own layout needs
     * (`/.helikon/update/firmware.img`); refusing nested paths was a limit of
     * the first slice, not a rule of the format. Every intermediate component
     * must exist and must be a folder: this layer creates nothing implicitly.
     */
    const char *leaf = path + 1;
    uint32_t parent = VFS_RW_ROOT_CNID;
    for (size_t index = 1u; index < length; ++index) {
        if (path[index] != '/')
            continue;

        char parent_path[VFS_PATH_MAX];
        size_t parent_length = index; /* includes the leading '/' */
        if (parent_length + 1u >= sizeof(parent_path))
            return HELIKON_ERR_NAME_INVALID;
        helikon_memcpy(parent_path, path, parent_length);
        parent_path[parent_length] = '\0';

        struct hfs_entry entry;
        int rc = hfs_catalog_lookup(&vfs->catalog, parent_path, &entry);
        if (rc != HELIKON_OK)
            return rc;
        if (entry.kind != VFS_KIND_DIR)
            return HELIKON_ERR_NOT_DIR;
        parent = entry.id;
        leaf = path + index + 1u;
    }

    size_t leaf_length = length - (size_t)(leaf - path);
    size_t units = 0u;
    int rc = hfs_utf8_to_utf16be(leaf, leaf_length, out->name,
                                 sizeof(out->name), &units);
    if (rc != HELIKON_OK)
        return rc;
    if (units == 0u || units > VFS_RW_NAME_UNITS)
        return HELIKON_ERR_NAME_INVALID;

    out->parent = parent;
    out->units = units;
    return HELIKON_OK;
}

int vfs_rw_mount(struct vfs_rw *rw, struct vfs *vfs, hfs_write_fn write,
                 hfs_flush_fn flush, void *context)
{
    if (rw == NULL || vfs == NULL || write == NULL || flush == NULL)
        return HELIKON_ERR_ARGUMENT;
    helikon_memset(rw, 0, sizeof(*rw));
    rw->vfs = vfs;
    rw->write = write;
    rw->flush = flush;
    rw->context = context;

    /* No journal, no writes: I-02 has no non-journaled mode to fall back to. */
    if (vfs->volume.journaled == 0u)
        return HELIKON_ERR_UNSUPPORTED;

    int rc = hfs_journal_open(&rw->journal, &vfs->volume, write, flush);
    if (rc != HELIKON_OK)
        return rc;

    if (!hfs_journal_is_clean(&rw->journal)) {
        rc = hfs_journal_replay(&rw->journal);
        if (rc != HELIKON_OK)
            return rc;
        /* Replay can move every tree; re-probe before trusting the catalog. */
        rc = hfs_volume_probe(&vfs->volume, vfs->volume.read,
                              vfs->volume.context, vfs->volume.base,
                              vfs->volume.length);
        if (rc != HELIKON_OK)
            return rc;
        vfs->journal_dirty = 0u;
        rc = hfs_catalog_open(&vfs->catalog, &vfs->volume,
                              vfs->catalog_scratch,
                              vfs->catalog_scratch_size);
        if (rc != HELIKON_OK)
            return rc;
    }

    rw->writable = 1u;
    return HELIKON_OK;
}

void vfs_rw_close(struct vfs_rw *rw)
{
    if (rw == NULL)
        return;
    hfs_txn_abort(&rw->txn);
    rw->writable = 0u;
    rw->vfs = NULL;
}

/* ------------------------------------------------------------------------- */
/* Create                                                                    */
/* ------------------------------------------------------------------------- */

static int rw_create(struct vfs_rw *rw, const char *path, int folder,
                     uint32_t *id)
{
    struct vfs_rw_path target;
    int rc = vfs_rw_split(rw->vfs, path, &target);
    if (rc != HELIKON_OK)
        return rc;
    rc = rw_begin(rw);
    if (rc != HELIKON_OK)
        return rc;

    uint32_t cnid = 0u;
    if (folder != 0) {
        rc = hfs_catalog_create_folder(&rw->vfs->catalog, &rw->txn,
                                       target.parent, target.name,
                                       target.units, VFS_RW_FOLDER_MODE,
                                       hfs_catalog_key_compare, NULL, &cnid);
    } else {
        rc = hfs_catalog_create_file(&rw->vfs->catalog, &rw->txn,
                                     target.parent, target.name, target.units,
                                     VFS_RW_FILE_MODE, hfs_catalog_key_compare,
                                     NULL, &cnid);
    }
    if (rc != HELIKON_OK)
        return rw_abort(rw, rc);
    rc = rw_commit(rw);
    if (rc != HELIKON_OK)
        return rc;
    if (id != NULL)
        *id = cnid;
    return HELIKON_OK;
}

int vfs_rw_create(struct vfs_rw *rw, const char *path, uint32_t *id)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope != 0u)
        return HELIKON_ERR_BUSY;
    return rw_create(rw, path, 0, id);
}

int vfs_rw_mkdir(struct vfs_rw *rw, const char *path, uint32_t *id)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope != 0u)
        return HELIKON_ERR_BUSY;
    return rw_create(rw, path, 1, id);
}

/* ------------------------------------------------------------------------- */
/* Delete                                                                    */
/* ------------------------------------------------------------------------- */

static int rw_delete(struct vfs_rw *rw, const char *path, int want_folder)
{
    struct vfs_rw_path target;
    int rc = vfs_rw_split(rw->vfs, path, &target);
    if (rc != HELIKON_OK)
        return rc;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup(&rw->vfs->catalog, path, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if ((entry.kind == VFS_KIND_DIR) != (want_folder != 0))
        return HELIKON_ERR_ARGUMENT;

    rc = rw_begin(rw);
    if (rc != HELIKON_OK)
        return rc;
    if (want_folder != 0) {
        rc = hfs_catalog_delete_folder(&rw->vfs->catalog, &rw->txn,
                                       target.parent, target.name,
                                       target.units, entry.id,
                                       hfs_catalog_key_compare, NULL);
    } else {
        rc = hfs_catalog_delete_file(&rw->vfs->catalog, &rw->txn,
                                     target.parent, target.name, target.units,
                                     entry.id, hfs_catalog_key_compare, NULL);
    }
    if (rc != HELIKON_OK)
        return rw_abort(rw, rc);
    return rw_commit(rw);
}

int vfs_rw_unlink(struct vfs_rw *rw, const char *path)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope != 0u)
        return HELIKON_ERR_BUSY;
    return rw_delete(rw, path, 0);
}

int vfs_rw_rmdir(struct vfs_rw *rw, const char *path)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope != 0u)
        return HELIKON_ERR_BUSY;
    return rw_delete(rw, path, 1);
}

/* ------------------------------------------------------------------------- */
/* Rename                                                                    */
/* ------------------------------------------------------------------------- */

int vfs_rw_rename(struct vfs_rw *rw, const char *from, const char *to)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope != 0u)
        return HELIKON_ERR_BUSY;

    struct vfs_rw_path source;
    struct vfs_rw_path destination;
    int rc = vfs_rw_split(rw->vfs, from, &source);
    if (rc != HELIKON_OK)
        return rc;
    rc = vfs_rw_split(rw->vfs, to, &destination);
    if (rc != HELIKON_OK)
        return rc;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup(&rw->vfs->catalog, from, &entry);
    if (rc != HELIKON_OK)
        return rc;

    rc = rw_begin(rw);
    if (rc != HELIKON_OK)
        return rc;
    rc = hfs_catalog_rename(&rw->vfs->catalog, &rw->txn, source.parent,
                            source.name, source.units, entry.id,
                            destination.parent, destination.name,
                            destination.units, hfs_catalog_key_compare, NULL);
    if (rc != HELIKON_OK)
        return rw_abort(rw, rc);
    return rw_commit(rw);
}

/* ------------------------------------------------------------------------- */
/* Data                                                                      */
/* ------------------------------------------------------------------------- */

int vfs_rw_write(struct vfs_rw *rw, const char *path, uint64_t offset,
                 const void *data, size_t length, size_t *written)
{
    if (written != NULL)
        *written = 0u;
    if (rw == NULL || rw->vfs == NULL || data == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (length == 0u)
        return HELIKON_OK;

    struct vfs_rw_path target;
    int rc = vfs_rw_split(rw->vfs, path, &target);
    if (rc != HELIKON_OK)
        return rc;

    size_t chunk = length > VFS_RW_IO_MAX ? VFS_RW_IO_MAX : length;
    rc = rw_begin(rw);
    if (rc != HELIKON_OK)
        return rc;
    rc = hfs_catalog_write_file(&rw->vfs->catalog, &rw->txn, target.parent,
                                target.name, target.units, offset, data, chunk,
                                hfs_catalog_key_compare, NULL);
    if (rc != HELIKON_OK)
        return rw_abort(rw, rc);
    rc = rw_commit(rw);
    if (rc != HELIKON_OK)
        return rc;
    if (written != NULL)
        *written = chunk;
    return HELIKON_OK;
}

int vfs_rw_truncate(struct vfs_rw *rw, const char *path, uint64_t size)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope != 0u)
        return HELIKON_ERR_BUSY;

    struct vfs_rw_path target;
    int rc = vfs_rw_split(rw->vfs, path, &target);
    if (rc != HELIKON_OK)
        return rc;

    struct hfs_entry entry;
    rc = hfs_catalog_lookup(&rw->vfs->catalog, path, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if (entry.kind != VFS_KIND_FILE)
        return HELIKON_ERR_ARGUMENT;

    rc = rw_begin(rw);
    if (rc != HELIKON_OK)
        return rc;
    rc = hfs_catalog_truncate_to(&rw->vfs->catalog, &rw->txn, target.parent,
                                 target.name, target.units, size,
                                 hfs_catalog_key_compare, NULL);
    if (rc != HELIKON_OK)
        return rw_abort(rw, rc);
    return rw_commit(rw);
}

/* ------------------------------------------------------------------------- */
/* One commit, several operations (T-064)                                    */
/* ------------------------------------------------------------------------- */

int vfs_rw_txn_begin(struct vfs_rw *rw)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->writable == 0u)
        return HELIKON_ERR_READ_ONLY;
    if (rw->txn_scope != 0u)
        return HELIKON_ERR_BUSY;

    int rc = hfs_txn_begin(&rw->txn, &rw->journal);
    if (rc != HELIKON_OK)
        return rc;
    rw->txn_scope = 1u;
    return HELIKON_OK;
}

int vfs_rw_txn_commit(struct vfs_rw *rw)
{
    if (rw == NULL || rw->vfs == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope == 0u)
        return HELIKON_ERR_ARGUMENT;

    rw->txn_scope = 0u;
    return rw_commit(rw);
}

void vfs_rw_txn_abort(struct vfs_rw *rw)
{
    if (rw == NULL || rw->txn_scope == 0u)
        return;
    hfs_txn_abort(&rw->txn);
    rw->txn_scope = 0u;
    /* The staged operations had already touched in-memory counters. */
    (void)rw_reload(rw);
}

/* A refused operation ends the scope: the caller begins a fresh one. */
static int rw_scope_fail(struct vfs_rw *rw, int error)
{
    hfs_txn_abort(&rw->txn);
    rw->txn_scope = 0u;
    (void)rw_reload(rw);
    return error;
}

int vfs_rw_txn_delete(struct vfs_rw *rw, const char *path)
{
    if (rw == NULL || rw->vfs == NULL || path == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope == 0u)
        return HELIKON_ERR_ARGUMENT;

    struct vfs_rw_path target;
    int rc = vfs_rw_split(rw->vfs, path, &target);
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);

    struct hfs_entry entry;
    rc = hfs_catalog_lookup(&rw->vfs->catalog, path, &entry);
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);

    if (entry.kind == VFS_KIND_DIR) {
        rc = hfs_catalog_delete_folder(&rw->vfs->catalog, &rw->txn,
                                       target.parent, target.name,
                                       target.units, entry.id,
                                       hfs_catalog_key_compare, NULL);
    } else {
        rc = hfs_catalog_delete_file(&rw->vfs->catalog, &rw->txn,
                                     target.parent, target.name, target.units,
                                     entry.id, hfs_catalog_key_compare, NULL);
    }
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);
    return HELIKON_OK;
}

int vfs_rw_txn_rename(struct vfs_rw *rw, const char *from, const char *to)
{
    if (rw == NULL || rw->vfs == NULL || from == NULL || to == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope == 0u)
        return HELIKON_ERR_ARGUMENT;

    struct vfs_rw_path source;
    struct vfs_rw_path destination;
    int rc = vfs_rw_split(rw->vfs, from, &source);
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);
    rc = vfs_rw_split(rw->vfs, to, &destination);
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);

    struct hfs_entry entry;
    rc = hfs_catalog_lookup(&rw->vfs->catalog, from, &entry);
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);

    rc = hfs_catalog_rename(&rw->vfs->catalog, &rw->txn, source.parent,
                            source.name, source.units, entry.id,
                            destination.parent, destination.name,
                            destination.units, hfs_catalog_key_compare, NULL);
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);
    return HELIKON_OK;
}

int vfs_rw_txn_replace(struct vfs_rw *rw, const char *path, const void *data,
                       size_t length)
{
    if (rw == NULL || rw->vfs == NULL || path == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->txn_scope == 0u)
        return HELIKON_ERR_ARGUMENT;
    if (data == NULL || length == 0u || length > VFS_RW_IO_MAX)
        return rw_scope_fail(rw, HELIKON_ERR_ARGUMENT);

    struct vfs_rw_path target;
    int rc = vfs_rw_split(rw->vfs, path, &target);
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);

    /*
     * The size before the write is read here, on the medium: `hfs_catalog_lookup`
     * has no transaction parameter, so it does not see what this scope has
     * already staged. That is also why this function looks each path up exactly
     * once, at the point where the on-disk record is still the whole truth.
     */
    struct hfs_entry entry;
    uint64_t previous = 0u;
    int existed = 0;
    rc = hfs_catalog_lookup(&rw->vfs->catalog, path, &entry);
    if (rc == HELIKON_ERR_NOT_FOUND) {
        uint32_t cnid = 0u;
        rc = hfs_catalog_create_file(&rw->vfs->catalog, &rw->txn,
                                     target.parent, target.name, target.units,
                                     VFS_RW_FILE_MODE, hfs_catalog_key_compare,
                                     NULL, &cnid);
        if (rc != HELIKON_OK)
            return rw_scope_fail(rw, rc);
    } else if (rc != HELIKON_OK) {
        return rw_scope_fail(rw, rc);
    } else if (entry.kind != VFS_KIND_FILE) {
        return rw_scope_fail(rw, HELIKON_ERR_ARGUMENT);
    } else {
        existed = 1;
        previous = entry.data.size;
    }

    /*
     * Write first, then shrink: the writer grows a short file and a long one
     * keeps its stale tail until the truncate below returns those blocks.
     */
    rc = hfs_catalog_write_file(&rw->vfs->catalog, &rw->txn, target.parent,
                                target.name, target.units, 0u, data, length,
                                hfs_catalog_key_compare, NULL);
    if (rc != HELIKON_OK)
        return rw_scope_fail(rw, rc);

    if (existed != 0 && previous > (uint64_t)length) {
        rc = hfs_catalog_truncate_to(&rw->vfs->catalog, &rw->txn,
                                     target.parent, target.name, target.units,
                                     (uint64_t)length, hfs_catalog_key_compare,
                                     NULL);
        if (rc != HELIKON_OK)
            return rw_scope_fail(rw, rc);
    }
    return HELIKON_OK;
}
