/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "mount.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"

/*
 * The VFS asks for byte ranges inside the volume; the adapter asks the block
 * device for them; this is the one line that adds the partition's own offset
 * back in, because the volume's zero is not the disk's zero.
 */
static int storage_read(void *context, uint64_t offset, void *buffer,
                        size_t length)
{
    struct helikon_storage *storage = context;
    if (storage == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (storage->base > 0xFFFFFFFFFFFFFFFFu - offset)
        return HELIKON_ERR_ARGUMENT;
    return helikon_hfs_block_read(&storage->adapter, storage->base + offset,
                                  buffer, length);
}

/*
 * The write callbacks put the partition offset back in, exactly as the read one
 * does: the journal and the transaction engine speak volume-relative offsets and
 * the disk does not.
 */
static int storage_write(void *context, uint64_t offset, const void *buffer,
                         size_t length)
{
    struct helikon_storage *storage = context;
    if (storage == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (storage->base > 0xFFFFFFFFFFFFFFFFu - offset)
        return HELIKON_ERR_ARGUMENT;
    return helikon_hfs_block_write(&storage->adapter, storage->base + offset,
                                   buffer, length);
}

static int storage_flush(void *context)
{
    struct helikon_storage *storage = context;
    if (storage == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_hfs_block_flush(&storage->adapter);
}

int helikon_storage_mount(struct helikon_storage *storage,
                          const struct helikon_block_device *device,
                          uint32_t timeout_ms)
{
    if (storage == NULL || device == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(storage, 0, sizeof(*storage));
    storage->device = device;

    /*
     * The scratch holds one sector for the partition scan and for the adapter's
     * aligned reads. A card whose sector is bigger than this buffer cannot be
     * mounted honestly, so it is refused instead of half-read.
     */
    if (device->info.sector_size > sizeof(storage->sector_scratch))
        return HELIKON_ERR_BUFFER;

    int rc = helikon_hfs_block_adapter_init(&storage->adapter, device,
                                            storage->sector_scratch,
                                            sizeof(storage->sector_scratch),
                                            timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    unsigned int count = 0u;
    rc = helikon_partition_scan(device, storage->partitions,
                                HELIKON_STORAGE_PARTITION_MAX,
                                storage->sector_scratch,
                                sizeof(storage->sector_scratch), timeout_ms,
                                &count);
    if (rc < 0)
        return rc;
    storage->partition_count = count;
    if (count == 0u)
        return HELIKON_ERR_NOT_FOUND;

    /*
     * Try each partition in map order and keep the first that mounts as HFS+.
     * A 6G disk carries firmware partitions that are not HFS+, so picking by
     * index would work on one disk and fail on the next; picking by "it mounts"
     * works on both. The last error is what the caller sees when none does.
     */
    int last = HELIKON_ERR_UNSUPPORTED;
    for (unsigned int index = 0u; index < count; ++index) {
        storage->base = storage->partitions[index].offset;
        storage->length = storage->partitions[index].length;
        rc = vfs_mount(&storage->vfs, storage_read, storage, 0u,
                       storage->length, storage->catalog_scratch,
                       sizeof(storage->catalog_scratch),
                       storage->extent_scratch,
                       sizeof(storage->extent_scratch));
        if (rc == HELIKON_OK) {
            storage->mounted_index = index;
            storage->mounted = 1u;
            return HELIKON_OK;
        }
        last = rc;
    }
    storage->base = 0u;
    storage->length = 0u;
    return last;
}

int helikon_storage_enable_write(struct helikon_storage *storage)
{
    if (storage == NULL || storage->mounted == 0u)
        return HELIKON_ERR_ARGUMENT;
    if (storage->writable != 0u)
        return HELIKON_OK;
    /* A device that says it cannot write is believed before a path is read. */
    if ((storage->device->info.flags & HELIKON_BLOCK_FLAG_READ_ONLY) != 0u)
        return HELIKON_ERR_READ_ONLY;

    /*
     * The opt-in is a file on the volume, not a build flag: it survives a
     * reflash, it is written by the one path the owner deliberately used
     * (Disk Mode or the companion), and removing it is how writing is turned off
     * again. A folder with that name is a typo, and a typo is not consent.
     */
    struct vfs_entry flag;
    int rc = vfs_stat(&storage->vfs, HELIKON_STORAGE_WRITE_FLAG, &flag);
    if (rc != HELIKON_OK)
        return rc;
    if (flag.kind != VFS_KIND_FILE)
        return HELIKON_ERR_IS_DIR;

    /*
     * `vfs_rw_mount` is the whole gate: it refuses a volume without a journal
     * (I-02), replays a dirty one and re-probes the volume before returning
     * (D-04), and clears `rw->writable` when it fails. Nothing here relaxes it.
     */
    rc = vfs_rw_mount(&storage->rw, &storage->vfs, storage_write,
                      storage_flush, storage);
    if (rc != HELIKON_OK)
        return rc;

    storage->writable = 1u;
    return HELIKON_OK;
}

int helikon_storage_open_catalog(struct helikon_storage *storage)
{
    if (storage == NULL || storage->mounted == 0u)
        return HELIKON_ERR_ARGUMENT;

    /* A second call replaces the first: the reader's I/O is copied into `db`. */
    if (storage->catalog_open != 0u) {
        hdb_vfs_close(&storage->catalog_io);
        storage->catalog_open = 0u;
    }

    struct vfs_entry entry;
    int rc = vfs_stat(&storage->vfs, HELIKON_STORAGE_CATALOG_PATH, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if (entry.kind != VFS_KIND_FILE)
        return HELIKON_ERR_IS_DIR;

    rc = hdb_vfs_open(&storage->catalog_io, &storage->vfs,
                      HELIKON_STORAGE_CATALOG_PATH);
    if (rc != HELIKON_OK)
        return rc;

    struct hdb_io io;
    hdb_vfs_io_init(&storage->catalog_io, &io);
    rc = hdb_open(&storage->db, &io);
    if (rc != HELIKON_OK) {
        hdb_vfs_close(&storage->catalog_io);
        return rc;
    }

    storage->catalog_size = entry.size;
    storage->catalog_open = 1u;
    return HELIKON_OK;
}

void helikon_storage_unmount(struct helikon_storage *storage)
{
    if (storage == NULL)
        return;
    if (storage->catalog_open != 0u)
        hdb_vfs_close(&storage->catalog_io);
    /* Abort any staged transaction before the object goes away: a scope left
     * open is the one state a caller could carry into the next mount. */
    if (storage->writable != 0u)
        vfs_rw_close(&storage->rw);
    helikon_memset(storage, 0, sizeof(*storage));
}
