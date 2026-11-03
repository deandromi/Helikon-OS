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
 * HFS_ERR_UNSUPPORTED.
 */
int hfs_fork_stage(struct hfs_txn *tx, const struct hfs_volume *volume,
                   const struct hfs_fork *fork, uint64_t offset,
                   const void *data, size_t length);

#endif
