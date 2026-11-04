/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_block_adapter.h"

#include "bounce.h"
#include "error.h"

int helikon_hfs_block_adapter_init(struct helikon_hfs_block_adapter *adapter,
                                   const struct helikon_block_device *device,
                                   void *scratch, size_t scratch_size,
                                   uint32_t timeout_ms)
{
    if (adapter == NULL || device == NULL || scratch == NULL ||
        scratch_size < device->info.sector_size)
        return HELIKON_ERR_ARGUMENT;

    adapter->device = device;
    adapter->scratch = scratch;
    adapter->scratch_size = scratch_size;
    adapter->timeout_ms = timeout_ms;
    return HELIKON_OK;
}

int helikon_hfs_block_read(void *context, uint64_t offset, void *buffer,
                           size_t length)
{
    struct helikon_hfs_block_adapter *adapter = context;
    if (adapter == NULL || adapter->device == NULL ||
        (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;

    int rc = helikon_block_read_bytes(adapter->device, offset, buffer, length,
                                      adapter->scratch, adapter->scratch_size,
                                      adapter->timeout_ms);
    return rc == HELIKON_OK ? 0 : rc;
}

int helikon_hfs_block_write(void *context, uint64_t offset,
                            const void *buffer, size_t length)
{
    struct helikon_hfs_block_adapter *adapter = context;
    if (adapter == NULL || adapter->device == NULL ||
        (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;

    int rc = helikon_block_write_bytes(adapter->device, offset, buffer, length,
                                       adapter->scratch, adapter->scratch_size,
                                       adapter->timeout_ms);
    return rc == HELIKON_OK ? 0 : rc;
}

int helikon_hfs_block_flush(void *context)
{
    struct helikon_hfs_block_adapter *adapter = context;
    if (adapter == NULL || adapter->device == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_block_flush(adapter->device, adapter->timeout_ms);
}
