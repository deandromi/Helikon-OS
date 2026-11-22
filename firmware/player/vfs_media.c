/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "vfs_media.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"

/*
 * Absolute-offset read over a VFS file. End of file is a clean zero-byte
 * read, not an error: every decoder treats it as end of stream. The seek is
 * explicit because the VFS keeps a read position and a codec may jump
 * (seek, then frame headers before the target).
 */
static int vfs_media_read_at(void *context, uint64_t offset, void *buffer,
                             size_t length, size_t *read)
{
    struct helikon_vfs_media *media = context;
    if (media == NULL || read == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;

    *read = 0u;
    if (offset >= media->size)
        return HELIKON_OK;

    size_t want = length;
    const uint64_t available = media->size - offset;
    if (available < (uint64_t)want)
        want = (size_t)available;
    if (want == 0u)
        return HELIKON_OK;

    uint64_t position = 0u;
    int rc = vfs_file_seek(media->vfs, media->handle, VFS_SEEK_SET,
                           (int64_t)offset, &position);
    if (rc != HELIKON_OK)
        return rc;
    return vfs_file_read(media->vfs, media->handle, buffer, want, read);
}

int helikon_vfs_media_init(struct helikon_vfs_media *media, struct vfs *vfs,
                           struct vfs_file_handle handle, uint64_t size,
                           struct helikon_codec_source *source)
{
    if (media == NULL || vfs == NULL || source == NULL)
        return HELIKON_ERR_ARGUMENT;

    media->vfs = vfs;
    media->handle = handle;
    media->size = size;

    source->read_at = vfs_media_read_at;
    source->size = size;
    source->context = media;
    return HELIKON_OK;
}
