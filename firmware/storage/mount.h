/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Mount the music volume: block device -> partition -> HFS+ -> VFS -> catalog.
 *
 * This is the step the firmware has never had. Everything below it and
 * everything above it exists and is tested — the read-only block layer, the
 * Apple Partition Map scan, the HFS+ read core, the VFS, the HelikonDB over the
 * VFS, the library index, the music source — but at boot the firmware mounts
 * nothing, which is why the Music hub still plays an embedded blob (T-056's
 * device half).
 *
 * What it does, in order:
 *
 *   1. scans the partition map (APM first, MBR and whole-disk HFS+ as the
 *      fallbacks the block layer already supports);
 *   2. tries each partition as HFS+ in order and keeps the first that mounts,
 *      because a real 6G disk carries firmware partitions that are not HFS+ and
 *      guessing by index is how a mount fails on someone else's disk;
 *   3. mounts it read-only through the VFS with one scratch sector, one catalog
 *      node buffer and one extents node buffer;
 *   4. optionally opens `/.helikon/music.hdb` through the VFS and hands the
 *      reader its I/O adapter.
 *
 * It never writes. `vfs.journal_dirty` is reported as it is, not cleared or
 * ignored: the mount is read-only, so a dirty journal costs the ability to write
 * and nothing else, and pretending otherwise would be the silent bypass I-04
 * forbids.
 *
 * Memory: about 25 KiB of `.bss` for the scratches and the structures, on top of
 * the caller's. Nothing is allocated.
 */
#ifndef HELIKON_STORAGE_MOUNT_H
#define HELIKON_STORAGE_MOUNT_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"
#include "hdb.h"
#include "hdb_vfs.h"
#include "hfs_block_adapter.h"
#include "partition.h"
#include "vfs.h"

/* A 6G disk holds a driver partition, firmware and the data volume. */
#define HELIKON_STORAGE_PARTITION_MAX 16u
/* The largest sector the 6G's ATA reports; the scratch has to hold one. */
#define HELIKON_STORAGE_SECTOR_MAX 4096u
/* One catalog node and one extents node: macOS writes 4 KiB or 8 KiB nodes. */
#define HELIKON_STORAGE_CATALOG_SCRATCH 8192u
#define HELIKON_STORAGE_EXTENT_SCRATCH 8192u
/* Where a HelikonDB catalog lives on the data volume. */
#define HELIKON_STORAGE_CATALOG_PATH "/.helikon/music.hdb"

struct helikon_storage {
    const struct helikon_block_device *device;
    struct helikon_hfs_block_adapter adapter;
    struct vfs vfs;
    struct hdb_vfs_io catalog_io;
    struct hdb db;

    struct helikon_partition partitions[HELIKON_STORAGE_PARTITION_MAX];
    uint64_t base;   /* byte offset of the mounted partition */
    uint64_t length; /* its byte length */
    uint64_t catalog_size;
    uint32_t partition_count;
    uint32_t mounted_index;

    uint8_t sector_scratch[HELIKON_STORAGE_SECTOR_MAX]
        __attribute__((aligned(8)));
    uint8_t catalog_scratch[HELIKON_STORAGE_CATALOG_SCRATCH]
        __attribute__((aligned(8)));
    uint8_t extent_scratch[HELIKON_STORAGE_EXTENT_SCRATCH]
        __attribute__((aligned(8)));

    uint8_t mounted;
    uint8_t catalog_open;
};

/*
 * Scans and mounts. Returns HELIKON_OK with `mounted` set, or the last mount
 * error when no partition could be mounted: `UNSUPPORTED` for a map the scanner
 * does not implement (GPT), `FORMAT` for a map whose records disagree, `IO` for
 * a read that failed. A device whose sector size is larger than
 * `HELIKON_STORAGE_SECTOR_MAX` is `BUFFER`, because the scratch is fixed.
 */
int helikon_storage_mount(struct helikon_storage *storage,
                          const struct helikon_block_device *device,
                          uint32_t timeout_ms);

/*
 * Opens `/.helikon/music.hdb` on the mounted volume and validates it. `NOT_FOUND`
 * when the volume has no catalog, `FORMAT` when it has one that does not parse,
 * `IS_DIR` when the path is a folder. The mount stays usable either way, because
 * a volume without a catalog is still a volume to read files from.
 */
int helikon_storage_open_catalog(struct helikon_storage *storage);

/* Closes the catalog and clears everything. Safe twice, and after a failed mount. */
void helikon_storage_unmount(struct helikon_storage *storage);

#endif
