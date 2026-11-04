/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only block device contract for F1.
 *
 * The block layer owns sector validation, bounds and error propagation. It
 * does not own files, paths, caches or retry policy. A flush error is always
 * returned. F1 never writes: the write and flush operations exist so the ABI
 * is stable, and the default implementation returns HELIKON_ERR_READ_ONLY.
 */
#ifndef HELIKON_BLOCK_BLOCK_H
#define HELIKON_BLOCK_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#define HELIKON_BLOCK_FLAG_READ_ONLY 0x00000001u

struct helikon_block_info {
    uint32_t sector_size;   /* logical bytes per sector */
    uint64_t sector_count;
    uint32_t flags;
};

struct helikon_block_ops {
    int (*read)(void *context, uint64_t lba, uint32_t count, void *buffer,
                uint32_t timeout_ms);
    int (*write)(void *context, uint64_t lba, uint32_t count,
                 const void *buffer, uint32_t timeout_ms);
    int (*flush)(void *context, uint32_t timeout_ms);
    int (*power)(void *context, int on, uint32_t timeout_ms);
};

struct helikon_block_device {
    const struct helikon_block_ops *ops;
    void *context;
    struct helikon_block_info info;
};

int helikon_block_init(struct helikon_block_device *device,
                       const struct helikon_block_ops *ops, void *context,
                       const struct helikon_block_info *info);

/* All operations validate the range and return a canonical error code. */
int helikon_block_read(const struct helikon_block_device *device, uint64_t lba,
                       uint32_t count, void *buffer, uint32_t timeout_ms);
int helikon_block_write(const struct helikon_block_device *device, uint64_t lba,
                        uint32_t count, const void *buffer,
                        uint32_t timeout_ms);
int helikon_block_flush(const struct helikon_block_device *device,
                        uint32_t timeout_ms);
int helikon_block_power(const struct helikon_block_device *device, int on,
                        uint32_t timeout_ms);
uint64_t helikon_block_capacity_bytes(const struct helikon_block_device *device);

#endif
