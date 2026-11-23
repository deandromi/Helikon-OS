/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * VFS destination for the playlist writer. See vfs_playlist.h for the rules.
 */
#include "vfs_playlist.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"

static int playlist_sink_write(void *context, const void *data, size_t length)
{
    struct helikon_vfs_playlist *destination = context;
    const uint8_t *bytes = data;
    size_t done = 0u;

    while (done < length) {
        size_t applied = 0u;
        int rc = vfs_rw_write(destination->rw, destination->path,
                              destination->offset + done, bytes + done,
                              length - done, &applied);
        if (rc != HELIKON_OK)
            return rc;
        if (applied == 0u)
            return HELIKON_ERR_IO;
        done += applied;
    }
    destination->offset += length;
    return HELIKON_OK;
}

int helikon_vfs_playlist_begin(struct helikon_vfs_playlist *destination,
                               struct vfs_rw *rw, const char *path)
{
    if (destination == NULL || rw == NULL || path == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (rw->writable == 0u)
        return HELIKON_ERR_READ_ONLY;

    int rc = vfs_rw_unlink(rw, path);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_NOT_FOUND)
        return rc;
    rc = vfs_rw_create(rw, path, NULL);
    if (rc != HELIKON_OK)
        return rc;

    destination->rw = rw;
    destination->path = path;
    destination->offset = 0u;
    return HELIKON_OK;
}

void helikon_vfs_playlist_sink(struct helikon_vfs_playlist *destination,
                               struct helikon_playlist_sink *sink)
{
    if (destination == NULL || sink == NULL)
        return;
    sink->write = playlist_sink_write;
    sink->context = destination;
}

uint64_t helikon_vfs_playlist_size(const struct helikon_vfs_playlist *destination)
{
    return destination == NULL ? 0u : destination->offset;
}
