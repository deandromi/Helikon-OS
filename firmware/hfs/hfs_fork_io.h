/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_FORK_IO_H
#define HELIKON_HFS_FORK_IO_H

#include "hfs_txn.h"
#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Stage a byte range of a fork into a transaction. The range is copied into
 * the fork's physical allocation blocks; partial blocks are read-modify-write.
 * Only the eight inline extents are supported; an overflowed fork returns
 * HFS_ERR_UNSUPPORTED. Callers that resolve the extents-overflow records use
 * hfs_fork_stage_map() instead.
 */
int hfs_fork_stage(struct hfs_txn *tx, const struct hfs_volume *volume,
                   const struct hfs_fork *fork, uint64_t offset,
                   const void *data, size_t length);

/*
 * Stage a byte range into a fork described by its logical extent list: the
 * inline extents followed by the extents-overflow records, in logical order.
 * `fork_size` bounds the write; `extents` supplies the physical placement.
 * A range that no extent covers returns HFS_ERR_UNSUPPORTED, so a hole is
 * never written silently. The block budget of the transaction still bounds
 * one call.
 */
int hfs_fork_stage_map(struct hfs_txn *tx, const struct hfs_volume *volume,
                       const struct hfs_extent *extents, size_t count,
                       uint64_t fork_size, uint64_t offset,
                       const void *data, size_t length);

/* Overlay staged volume blocks onto a fork byte range. Returns 1 if any
 * staged block overlapped, 0 if none, or a negative error. */
int hfs_fork_overlay(const struct hfs_txn *tx,
                     const struct hfs_volume *volume,
                     const struct hfs_fork *fork, uint64_t offset,
                     void *buffer, size_t length);

#endif
