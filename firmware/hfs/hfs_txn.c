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

int hfs_txn_stage_replace(struct hfs_txn *tx, uint64_t sector,
                          const void *data, uint32_t size)
{
    if (tx == NULL || !tx->active || data == NULL || size == 0)
        return HFS_ERR_ARGUMENT;
    if (size > HFS_TXN_BLOCK_MAX || size % tx->journal->sector_size != 0)
        return HFS_ERR_ARGUMENT;

    for (size_t i = 0; i < tx->count; ++i) {
        if (tx->blocks[i].sector == sector) {
            if (tx->blocks[i].size != size)
                return HFS_ERR_BUSY;
            memcpy(tx->blocks[i].data, data, size);
            return HFS_OK;
        }
    }
    return hfs_txn_stage(tx, sector, data, size);
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
