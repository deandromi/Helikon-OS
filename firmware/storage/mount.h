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
 * It mounts read-only. `vfs.journal_dirty` is reported as it is, not cleared or
 * ignored: on a read-only mount a dirty journal costs the ability to write and
 * nothing else, and pretending otherwise would be the silent bypass I-04
 * forbids.
 *
 * Writing is a second, explicit step: `helikon_storage_enable_write()` opens the
 * volume read-write only when the volume itself carries the opt-in file
 * `/.helikon/write-enabled`. Nothing else turns writes on -- not a build flag,
 * not a button held at boot, not a partition attribute -- because a firmware
 * that can write to a library it was not told to write to is one bad commit away
 * from doing it. With the flag absent the volume stays read-only and answers
 * `NOT_FOUND`, which is the normal state, not an error. With the flag present
 * the writer still refuses a volume that has no journal (I-02) and still
 * replays a dirty one before the first write (D-04), and a replay failure leaves
 * the volume read-only rather than half-writable.
 *
 * Memory: about 287 KiB of `.bss` for the scratches, the structures and the
 * writer -- measured at 293,800 bytes, of which `vfs_rw`'s staged blocks are
 * 262,824 -- on top of the caller's. Nothing is allocated. The writer is part of
 * the object rather than a caller's pointer because one object owning both
 * mount states cannot be half-open; an image that never declares a
 * `struct helikon_storage` pays nothing for it.
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
#include "vfs_rw.h"

/* A 6G disk holds a driver partition, firmware and the data volume. */
#define HELIKON_STORAGE_PARTITION_MAX 16u
/* The largest sector the 6G's ATA reports; the scratch has to hold one. */
#define HELIKON_STORAGE_SECTOR_MAX 4096u
/* One catalog node and one extents node: macOS writes 4 KiB or 8 KiB nodes. */
#define HELIKON_STORAGE_CATALOG_SCRATCH 8192u
#define HELIKON_STORAGE_EXTENT_SCRATCH 8192u
/* Where a HelikonDB catalog lives on the data volume. */
#define HELIKON_STORAGE_CATALOG_PATH "/.helikon/music.hdb"
/* The opt-in: nothing opens the volume for writing without this file. */
#define HELIKON_STORAGE_DIR "/.helikon"
#define HELIKON_STORAGE_WRITE_FLAG "/.helikon/write-enabled"

struct helikon_storage {
    const struct helikon_block_device *device;
    struct helikon_hfs_block_adapter adapter;
    struct vfs vfs;
    struct vfs_rw rw; /* only usable after enable_write() succeeds */
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
    uint8_t writable;
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
 * Opens the mounted volume for writing, but only if it asks to be written:
 * `/.helikon/write-enabled` must exist and must be a file.
 *
 * Returns HELIKON_OK with `writable` set, or
 *   - HELIKON_ERR_ARGUMENT  when nothing is mounted;
 *   - HELIKON_ERR_NOT_FOUND when the flag is absent, which is the normal state
 *     and the reason the volume is read-only;
 *   - HELIKON_ERR_IS_DIR    when the flag is a folder: a typo is not an opt-in;
 *   - HELIKON_ERR_READ_ONLY when the device reports itself read-only;
 *   - HELIKON_ERR_UNSUPPORTED when the volume has no journal (I-02);
 *   - the journal error when a dirty journal could not be replayed.
 *
 * Every failure leaves the volume exactly as it was: `writable` stays 0 and
 * `rw.writable` is cleared by `vfs_rw_mount` itself. Calling it twice is not an
 * error.
 */
int helikon_storage_enable_write(struct helikon_storage *storage);

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
