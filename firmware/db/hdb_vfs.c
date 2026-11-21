/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "hdb_vfs.h"

#include <stddef.h>
#include <stdint.h>

#include "error.h"

static int hdb_vfs_read(void *context, uint32_t offset, void *buffer,
                        uint32_t length)
{
    struct hdb_vfs_io *io = context;
    if (io == NULL || !io->open || buffer == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (offset > io->size || length > io->size - offset)
        return HELIKON_ERR_FORMAT;

    uint64_t position = 0u;
    int rc = vfs_file_seek(io->vfs, io->handle, VFS_SEEK_SET, (int64_t)offset,
                           &position);
    if (rc != HELIKON_OK)
        return rc;

    size_t done = 0u;
    uint8_t *bytes = buffer;
    while (done < length) {
        size_t got = 0u;
        rc = vfs_file_read(io->vfs, io->handle, bytes + done,
                           (size_t)length - done, &got);
        if (rc != HELIKON_OK)
            return rc;
        if (got == 0u)
            return HELIKON_ERR_IO;
        done += got;
    }
    return HELIKON_OK;
}

int hdb_vfs_open(struct hdb_vfs_io *io, struct vfs *vfs, const char *path)
{
    if (io == NULL || vfs == NULL || path == NULL)
        return HELIKON_ERR_ARGUMENT;

    io->vfs = NULL;
    io->size = 0u;
    io->open = 0;
    io->handle.generation = 0u;
    io->handle.slot = 0u;

    struct vfs_entry entry;
    int rc = vfs_stat(vfs, path, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if (entry.kind != VFS_KIND_FILE)
        return HELIKON_ERR_IS_DIR;
    if (entry.unsupported != 0u)
        return HELIKON_ERR_UNSUPPORTED;
    /* The reader addresses the file with 32-bit offsets and lengths. */
    if (entry.size > 0xffffffffu)
        return HELIKON_ERR_BUFFER;

    rc = vfs_file_open(vfs, path, &io->handle);
    if (rc != HELIKON_OK)
        return rc;

    io->vfs = vfs;
    io->size = (uint32_t)entry.size;
    io->open = 1;
    return HELIKON_OK;
}

void hdb_vfs_io_init(struct hdb_vfs_io *io, struct hdb_io *out)
{
    if (out == NULL)
        return;
    out->read = hdb_vfs_read;
    out->context = io;
}

void hdb_vfs_close(struct hdb_vfs_io *io)
{
    if (io == NULL || !io->open || io->vfs == NULL)
        return;
    (void)vfs_file_close(io->vfs, io->handle);
    io->open = 0;
    io->vfs = NULL;
    io->size = 0u;
}
