/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_TXN_H
#define HELIKON_HFS_TXN_H

#include "hfs_journal.h"

#include <stddef.h>
#include <stdint.h>

#define HFS_TXN_MAX_BLOCKS 32u
#define HFS_TXN_BLOCK_MAX 8192u

struct hfs_txn_block {
    uint64_t sector;
    uint32_t size;
    uint8_t data[HFS_TXN_BLOCK_MAX];
};

/*
 * A bounded write set. The transaction engine stages metadata blocks here,
 * commits them through the Apple HFS+ journal, and applies them via replay.
 * Catalog/bitmap/extents code must use this interface; it must not write
 * metadata blocks directly.
 */
struct hfs_txn {
    struct hfs_journal *journal;
    struct hfs_txn_block blocks[HFS_TXN_MAX_BLOCKS];
    size_t count;
    int active;
};

int hfs_txn_begin(struct hfs_txn *tx, struct hfs_journal *journal);
int hfs_txn_stage(struct hfs_txn *tx, uint64_t sector, const void *data,
                  uint32_t size);
/* Stage or replace an already-staged block of the same size. */
int hfs_txn_stage_replace(struct hfs_txn *tx, uint64_t sector,
                          const void *data, uint32_t size);
/* Overlay staged blocks on a volume-relative byte range. Returns 1 if any
 * staged block overlapped, 0 if none, or a negative error. */
int hfs_txn_overlay(const struct hfs_txn *tx, uint64_t offset, void *buffer,
                    size_t length);
/*
 * Read volume bytes with every already-staged range overlaid on the on-disk
 * content. The volume header is touched by more than one updater in a single
 * transaction (the bitmap records free_blocks, the catalog records the
 * file/folder counts), so an updater must see the earlier stages or it would
 * drop them.
 */
int hfs_txn_read(struct hfs_txn *tx, const struct hfs_volume *volume,
                 uint64_t offset, void *buffer, uint32_t size);
int hfs_txn_commit(struct hfs_txn *tx);
void hfs_txn_abort(struct hfs_txn *tx);

#endif
