/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_file.h"

#include "error.h"

static int fork_inline_complete(const struct hfs_fork *fork)
{
    uint64_t blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        blocks += fork->extents[i].count;
    return blocks == fork->blocks;
}

int helikon_hfs_read_file(struct hfs_catalog *catalog,
                          struct hfs_extents *extents, const char *path,
                          uint64_t offset, void *buffer, size_t length,
                          size_t *done)
{
    if (catalog == NULL || path == NULL || done == NULL ||
        (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;

    *done = 0;
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup(catalog, path, &entry);
    if (rc != HFS_OK)
        return rc;
    if (entry.kind != 2u || entry.unsupported != 0u)
        return HELIKON_ERR_UNSUPPORTED;
    if (offset > entry.data.size || length > entry.data.size - offset)
        return HELIKON_ERR_ARGUMENT;

    size_t length_done = 0;
    if (fork_inline_complete(&entry.data)) {
        rc = hfs_catalog_read_file(catalog, &entry, offset, buffer, length,
                                   &length_done);
    } else {
        if (extents == NULL)
            return HELIKON_ERR_UNSUPPORTED;
        rc = hfs_extents_read(extents, &entry.data, entry.id, offset, buffer,
                              length, &length_done);
    }
    if (rc != HFS_OK)
        return rc;
    if (length_done != length)
        return HELIKON_ERR_IO;
    *done = length_done;
    return HELIKON_OK;
}
