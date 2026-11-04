/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_loader.h"

#include "loader_error.h"

#include <string.h>

static int fork_inline_complete(const struct hfs_fork *fork)
{
    uint64_t blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        blocks += fork->extents[i].count;
    return blocks == fork->blocks;
}

int helikon_hfs_read_file(void *context, const char *path, uint64_t offset,
                          void *buffer, size_t length, size_t *done)
{
    struct helikon_hfs_loader *loader = context;
    if (loader == NULL || loader->catalog == NULL || path == NULL ||
        (buffer == NULL && length != 0) || done == NULL)
        return HELIKON_LOADER_ERR_ARGUMENT;

    *done = 0;
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup(loader->catalog, path, &entry);
    if (rc == HFS_ERR_NOT_FOUND)
        return HELIKON_LOADER_ERR_NOT_FOUND;
    if (rc != HFS_OK)
        return HELIKON_LOADER_ERR_IO;
    if (entry.kind != 2 || entry.unsupported)
        return HELIKON_LOADER_ERR_UNSUPPORTED;
    if (offset > entry.data.size || length > entry.data.size - offset)
        return HELIKON_LOADER_ERR_IO;

    size_t length_done = 0;
    if (fork_inline_complete(&entry.data)) {
        rc = hfs_catalog_read_file(loader->catalog, &entry, offset, buffer,
                                   length, &length_done);
    } else {
        if (loader->extents == NULL)
            return HELIKON_LOADER_ERR_ARGUMENT;
        rc = hfs_extents_read(loader->extents, &entry.data, entry.id, offset,
                              buffer, length, &length_done);
    }
    if (rc != HFS_OK)
        return HELIKON_LOADER_ERR_IO;
    if (length_done != length)
        return HELIKON_LOADER_ERR_IO;
    *done = length_done;
    return HELIKON_LOADER_OK;
}
