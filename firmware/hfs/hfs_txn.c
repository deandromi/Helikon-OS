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
