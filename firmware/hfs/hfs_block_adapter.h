/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HFS+ read callback adapter.
 *
 * The existing firmware/hfs/ core takes exact byte-range callbacks at absolute
 * disk offsets. This adapter maps them onto a helikon_block_device. F1 uses
 * only the read callback; F2 adds write and flush after the board's write gate
 * is opened.
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

/*
 * hfs_write_fn / hfs_flush_fn for the F2 writer. They are only usable after
 * the board's write gate is opened; the block layer still enforces that flag.
 */
int helikon_hfs_block_write(void *context, uint64_t offset,
                            const void *buffer, size_t length);
int helikon_hfs_block_flush(void *context);

#endif
