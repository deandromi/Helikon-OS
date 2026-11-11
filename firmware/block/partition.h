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

#define HELIKON_PARTITION_MAP_NONE 0u
#define HELIKON_PARTITION_MAP_APM 1u
#define HELIKON_PARTITION_MAP_MBR 2u
#define HELIKON_PARTITION_MAP_HFS 3u

/*
 * Raw partition-map evidence for diagnostics. APM records map_unit as the
 * block size and the block-0 fields as sb_block_size/sb_block_count; only the
 * first PM record carries pm_map_blk_count. MBR records the unit that made its
 * first Apple_HFS entry probe successfully. The hfs_* fields are the raw
 * on-disk start/length in that unit, not bytes.
 */
struct helikon_partition_map {
    uint8_t kind;
    uint32_t unit;
    uint32_t sb_block_size;
    uint32_t sb_block_count;
    uint32_t pm_map_blk_count;
    uint32_t hfs_start;
    uint32_t hfs_blocks;
    uint32_t hfs_unit;
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

/*
 * Same scan with optional raw map evidence for the diagnostic screen.
 * `map` may be NULL. It is filled even when the map is recognised but
 * rejected, so a probe can show why.
 */
int helikon_partition_scan_info(const struct helikon_block_device *device,
                                struct helikon_partition *parts,
                                unsigned int capacity, uint8_t *scratch,
                                size_t scratch_size, uint32_t timeout_ms,
                                unsigned int *count,
                                struct helikon_partition_map *map);

#endif
