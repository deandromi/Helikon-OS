/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only partition scan.
 *
 * Apple Partition Map is the 6G native layout. MBR type 0xaf and a whole-disk
 * HFS+ volume are accepted as fallbacks. GPT is deliberately not implemented
 * in F1 and returns HELIKON_ERR_UNSUPPORTED rather than guessing. The scanner
 * never writes a partition table.
 */
#ifndef HELIKON_BLOCK_PARTITION_H
#define HELIKON_BLOCK_PARTITION_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"

struct helikon_partition {
    uint64_t offset;
    uint64_t length;
    uint32_t unit;
    uint8_t type;
};

/*
 * Fill `parts` in table order. `scratch` must hold at least one 512-byte
 * record. Returns the number of partitions (0 if none), or a negative error.
 */
int helikon_partition_scan(const struct helikon_block_device *device,
                           struct helikon_partition *parts,
                           unsigned int capacity, uint8_t *scratch,
                           size_t scratch_size, uint32_t timeout_ms,
                           unsigned int *count);

#endif
