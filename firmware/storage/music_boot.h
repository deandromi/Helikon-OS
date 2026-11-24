/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The boot path that gives the Music hub a real library.
 *
 * Everything below this file exists and is tested: the ATA driver as a block
 * device, the partition scan, the HFS+ probe, the VFS, the write gate and the
 * HelikonDB reader. `storage/mount.c` mounts the volume; this is the one object
 * that owns the mount for the whole session and answers the only question the UI
 * asks: *is there a catalog to draw?*
 *
 * Three rules, in order of what they protect:
 *
 *   1. Nothing half-open. A mount that fails, or a catalog that will not open,
 *      leaves `db` NULL and the volume unmounted. The UI falls back to the
 *      embedded catalog it already has; it never draws from a mount that is
 *      half alive.
 *   2. Writing is best effort and never blocks reading. `enable_write` is
 *      attempted because a resume bookmark or an exported playlist needs it, but
 *      its answer is recorded rather than obeyed: a read-only device, a missing
 *      opt-in file (D-57) or a volume with no journal still gets its library.
 *   3. Ownership is explicit. `close` releases the catalog and the mount; the
 *      caller does not have to know the order.
 *
 * Memory: this object contains a `helikon_storage`, so an image that declares
 * one pays its measured 293,800 bytes of `.bss` whether or not a volume is
 * present. An image that does not declare one pays nothing, which is why the
 * declaration belongs to the caller and not to a library-global.
 */
#ifndef HELIKON_STORAGE_MUSIC_BOOT_H
#define HELIKON_STORAGE_MUSIC_BOOT_H

#include <stdint.h>

#include "ata.h"
#include "ata_block.h"
#include "block.h"
#include "hdb.h"
#include "mount.h"

struct helikon_music_boot {
    struct helikon_storage storage;
    struct helikon_ata_block ata_block;
    struct hdb *db; /* the open catalog, or NULL when there is none */
    int error;      /* why `db` is NULL: a taxonomy code, HELIKON_OK otherwise */
    uint8_t writable;
};

/*
 * Mounts `device`, opens `/.helikon/music.hdb` and tries to enable writing.
 *
 * Returns HELIKON_OK with `db` set when a catalog is open. Otherwise returns the
 * reason (`NOT_HFS` for a foreign volume, `NOT_FOUND` for a volume with no
 * catalog, `FORMAT` for a catalog that does not parse, a mount error...), leaves
 * `db` NULL and unmounts everything it mounted.
 */
int helikon_music_boot_open(struct helikon_music_boot *boot,
                            const struct helikon_block_device *device,
                            uint32_t timeout_ms);

/*
 * The same open, from the driver the firmware actually talks to: binds `ata` to
 * a block device and then runs the mount/gate/catalog sequence above.
 *
 * `writable` is the device's own gate and it defaults to read-only at every call
 * site in this tree: a block device that admits writes is a decision the caller
 * makes, and the 6G has not proven one yet. When it is 0 the D-57 gate can only
 * answer `READ_ONLY`, which is the honest answer rather than a lie about what the
 * image can do.
 */
int helikon_music_boot_from_ata(struct helikon_music_boot *boot,
                                struct helikon_ata *ata, int writable,
                                uint32_t timeout_ms);

/* Releases the catalog and the mount. Safe twice and after a failed open. */
void helikon_music_boot_close(struct helikon_music_boot *boot);

#endif
