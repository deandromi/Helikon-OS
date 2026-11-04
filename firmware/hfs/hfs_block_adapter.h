/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HFS+ read callback adapter.
 *
 * The existing firmware/hfs/ read core takes an `hfs_read_fn` that reads exact
 * byte ranges at absolute disk offsets. This adapter maps those requests onto
 * a read-only helikon_block_device. It does not change the read core's
 * semantics and it never exposes a write path.
 */
#ifndef HELIKON_HFS_BLOCK_ADAPTER_H
#define HELIKON_HFS_BLOCK_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"
#include "hfs_volume.h"

struct helikon_hfs_block_adapter {
    const struct helikon_block_device *device;
    uint8_t *scratch;
    size_t scratch_size;
    uint32_t timeout_ms;
};

int helikon_hfs_block_adapter_init(struct helikon_hfs_block_adapter *adapter,
                                   const struct helikon_block_device *device,
                                   void *scratch, size_t scratch_size,
                                   uint32_t timeout_ms);

/* hfs_read_fn: returns 0 on success, nonzero on any error or short read. */
int helikon_hfs_block_read(void *context, uint64_t offset, void *buffer,
                           size_t length);

#endif
