/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The ATA disk as the block device everything above expects (T-126).
 *
 * `helikon_ata` talks to the controller: commands, the identify data, the
 * sector size it adopted and the LBA mode it selected. `helikon_block_device` is
 * the read-only contract the partition scan, the HFS+ adapter, the VFS and the
 * mount are written against. This file is the ten lines between them, plus the
 * one thing worth checking in code: the arithmetic that turns a sector size and
 * a sector count into a capacity, because the difference between 2^28 and 2^32
 * sectors is the difference between a 128 GB card that works and a 1 TB card
 * that reads the first 128 GB and corrupts the rest.
 *
 * The operations are the ATA driver's own functions: their signatures already
 * match `struct helikon_block_ops`, so there is no wrapper to get wrong. What is
 * *not* shared is the write gate: the device is presented read-only unless the
 * caller says otherwise, and the block layer refuses writes before the driver
 * ever sees them.
 */
#ifndef HELIKON_STORAGE_ATA_BLOCK_H
#define HELIKON_STORAGE_ATA_BLOCK_H

#include <stdint.h>

#include "ata.h"
#include "block.h"

struct helikon_ata_block {
    struct helikon_ata *ata;
    struct helikon_block_device device;
};

/*
 * Fills `info` from the geometry the driver measured. `sector_size` must be a
 * power of two of at least 512 (the CD/DVD 2048 is allowed: an SD adapter can
 * report it), and `total_sectors` must be non-zero. Anything else is `FORMAT`:
 * a disk whose size cannot be written down is not a disk this layer will
 * describe.
 */
int helikon_ata_block_info(uint32_t sector_size, uint64_t total_sectors,
                           struct helikon_block_info *info);

/*
 * Binds `ata` to `block` and builds the block device. `UNSUPPORTED` when the
 * driver has no valid IDENTIFY (init failed or was never called), `FORMAT` when
 * its geometry does not add up, `ARGUMENT` for a null pointer.
 */
int helikon_ata_block_init(struct helikon_ata_block *block,
                           struct helikon_ata *ata, int writable);

/* The block device, for `helikon_storage_mount` and friends. */
const struct helikon_block_device *helikon_ata_block_device(
    const struct helikon_ata_block *block);

#endif
