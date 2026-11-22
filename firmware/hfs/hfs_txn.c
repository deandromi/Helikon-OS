/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_txn.h"

#include <string.h>

int hfs_txn_begin(struct hfs_txn *tx, struct hfs_journal *journal)
{
    if (tx == NULL || journal == NULL)
        return HFS_ERR_ARGUMENT;
    if (!hfs_journal_is_clean(journal))
        return HFS_ERR_DIRTY;
    memset(tx, 0, sizeof(*tx));
    tx->journal = journal;
    tx->active = 1;
    return HFS_OK;
}

int hfs_txn_stage(struct hfs_txn *tx, uint64_t sector, const void *data,
                  uint32_t size)
{
    if (tx == NULL || !tx->active || data == NULL || size == 0)
        return HFS_ERR_ARGUMENT;
    if (size > HFS_TXN_BLOCK_MAX || size % tx->journal->sector_size != 0)
        return HFS_ERR_ARGUMENT;
    if (tx->count >= HFS_TXN_MAX_BLOCKS)
        return HFS_ERR_NO_SPACE;

    for (size_t i = 0; i < tx->count; ++i) {
        if (tx->blocks[i].sector == sector)
            return HFS_ERR_BUSY;
    }

    struct hfs_txn_block *block = &tx->blocks[tx->count];
    block->sector = sector;
    block->size = size;
    memcpy(block->data, data, size);
    ++tx->count;
    return HFS_OK;
}

/*
 * Patching a staged range, not appending a second copy of it.
 *
 * The writers stage in their own units: a 4096-byte B-tree node, one 512-byte
 * allocation block, a whole journal block. The journal, underneath, is a list of
 * byte runs, so the transaction should hold the smallest set of runs that
 * describes the change. Charging a slot per sector instead would make a B-tree
 * root split - four nodes plus the header - cost more slots than the budget has
 * on a volume whose allocation block is 512 bytes, so the tree could never grow
 * a level (issue #115).
 *
 * So a stage of `[sector, sector + size)` means "these bytes shall hold this
 * data", and the callers rely on that: `stage_volume_block` composes a partial
 * block with what is already staged, and a node that is written twice in one
 * transaction must end up with the second content. Three cases are handled:
 *
 *  - the range is already covered exactly: the bytes are replaced;
 *  - the range overlaps a staged run - it lies inside one, or covers one, or
 *    crosses the end of one: they are merged into one run, so long as the
 *    result fits in HFS_TXN_BLOCK_MAX;
 *  - two runs that merely touch are not merged. Adjacent blocks are different
 *    objects, and joining them would let one writer's range swallow another's;
 *    callers that stage a node stage it whole (see `hfs_fork_stage_map`).
 *  - the union would be larger than one block entry: BUSY.
 *
 * Entries never overlap after this, which is what `hfs_txn_overlay` and the
 * journal both assume.
 */
static int stage_patch(struct hfs_txn *tx, uint64_t sector, const void *data,
                       uint32_t size)
{
    uint64_t sector_size = tx->journal->sector_size;
    uint64_t start = sector * sector_size;
    uint64_t end = start + size;

    /*
     * Grow the union over every run that touches it. A chain of runs each
     * touching the next ends up as one, which is how a node staged sector by
     * sector becomes a single entry.
     */
    uint64_t union_start = start;
    uint64_t union_end = end;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (size_t i = 0; i < tx->count; ++i) {
            uint64_t other_start = tx->blocks[i].sector * sector_size;
            uint64_t other_end = other_start + tx->blocks[i].size;
            if (other_end <= union_start || other_start >= union_end)
                continue;
            if (other_start < union_start) {
                union_start = other_start;
                changed = 1;
            }
            if (other_end > union_end) {
                union_end = other_end;
                changed = 1;
            }
        }
    }
    if (union_end - union_start > HFS_TXN_BLOCK_MAX)
        return HFS_ERR_BUSY;

    /*
     * Compose the union from the runs it replaces, then lay the caller's bytes
     * over them, so the newest stage always wins.
     */
    static uint8_t merged[HFS_TXN_BLOCK_MAX];
    size_t kept = 0;
    for (size_t i = 0; i < tx->count; ++i) {
        uint64_t other_start = tx->blocks[i].sector * sector_size;
        uint64_t other_end = other_start + tx->blocks[i].size;
        if (other_end <= union_start || other_start >= union_end)
            continue;
        memcpy(merged + (other_start - union_start), tx->blocks[i].data,
               tx->blocks[i].size);
        ++kept;
    }
    (void)kept;
    memcpy(merged + (start - union_start), data, size);

    /* Drop every run the union replaces, then add the merged run. */
    size_t write_index = 0;
    for (size_t i = 0; i < tx->count; ++i) {
        uint64_t other_start = tx->blocks[i].sector * sector_size;
        uint64_t other_end = other_start + tx->blocks[i].size;
        const int folded = !(other_end <= union_start ||
                             other_start >= union_end);
        if (folded)
            continue;
        if (write_index != i)
            tx->blocks[write_index] = tx->blocks[i];
        ++write_index;
    }
    tx->count = write_index;

    if (tx->count >= HFS_TXN_MAX_BLOCKS)
        return HFS_ERR_NO_SPACE;
    struct hfs_txn_block *block = &tx->blocks[tx->count];
    block->sector = union_start / sector_size;
    block->size = (uint32_t)(union_end - union_start);
    memcpy(block->data, merged, block->size);
    ++tx->count;
    return HFS_OK;
}

int hfs_txn_stage_replace(struct hfs_txn *tx, uint64_t sector,
                          const void *data, uint32_t size)
{
    if (tx == NULL || !tx->active || tx->journal == NULL || data == NULL ||
        size == 0)
        return HFS_ERR_ARGUMENT;
    if (size > HFS_TXN_BLOCK_MAX || size % tx->journal->sector_size != 0)
        return HFS_ERR_ARGUMENT;
    return stage_patch(tx, sector, data, size);
}

int hfs_txn_overlay(const struct hfs_txn *tx, uint64_t offset, void *buffer,
                    size_t length)
{
    if (tx == NULL || tx->journal == NULL || buffer == NULL)
        return HFS_ERR_ARGUMENT;
    if (length == 0)
        return 0;
    uint64_t end = offset + length;
    if (end < offset)
        return HFS_ERR_ARGUMENT;
    int overlapped = 0;
    for (size_t i = 0; i < tx->count; ++i) {
        const struct hfs_txn_block *block = &tx->blocks[i];
        uint64_t block_start = block->sector * tx->journal->sector_size;
        uint64_t block_end = block_start + block->size;
        if (block_end <= offset || block_start >= end)
            continue;
        uint64_t from = block_start > offset ? block_start : offset;
        uint64_t to = block_end < end ? block_end : end;
        size_t count = (size_t)(to - from);
        memcpy((uint8_t *)buffer + (from - offset),
               block->data + (from - block_start), count);
        overlapped = 1;
    }
    return overlapped;
}

int hfs_txn_read(struct hfs_txn *tx, const struct hfs_volume *volume,
                 uint64_t offset, void *buffer, uint32_t size)
{
    if (tx == NULL || tx->journal == NULL || volume == NULL || buffer == NULL ||
        size == 0u)
        return HFS_ERR_ARGUMENT;
    if (offset > volume->length || size > volume->length - offset)
        return HFS_ERR_FORMAT;
    if (volume->read(volume->context, volume->base + offset, buffer, size))
        return HFS_ERR_IO;
    int rc = hfs_txn_overlay(tx, offset, buffer, size);
    return rc < 0 ? rc : HFS_OK;
}

int hfs_txn_commit(struct hfs_txn *tx)
{
    if (tx == NULL || !tx->active || tx->journal == NULL)
        return HFS_ERR_ARGUMENT;
    if (tx->count == 0) {
        hfs_txn_abort(tx);
        return HFS_OK;
    }

    struct hfs_journal_entry entries[HFS_TXN_MAX_BLOCKS];
    for (size_t i = 0; i < tx->count; ++i) {
        entries[i].sector = tx->blocks[i].sector;
        entries[i].data = tx->blocks[i].data;
        entries[i].size = tx->blocks[i].size;
    }

    int rc = hfs_journal_append(tx->journal, entries, tx->count);
    if (rc != HFS_OK) {
        hfs_txn_abort(tx);
        return rc;
    }

    rc = hfs_journal_replay(tx->journal);
    if (rc != HFS_OK) {
        /* The journal remains dirty; the caller must stop writing. */
        tx->active = 0;
        tx->count = 0;
        return rc;
    }

    hfs_txn_abort(tx);
    return HFS_OK;
}

void hfs_txn_abort(struct hfs_txn *tx)
{
    if (tx == NULL)
        return;
    memset(tx, 0, sizeof(*tx));
}
