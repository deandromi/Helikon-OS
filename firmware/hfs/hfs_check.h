/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only structural check of an HFS+ volume.
 *
 * This is the first slice of the anti-corruption work (T-020, D-11): it answers
 * "does this volume's own metadata agree with itself?" without writing anything
 * and without mounting for write. It is deliberately conservative — it reports
 * what it can prove is wrong and skips what it cannot prove, rather than
 * guessing.
 *
 * What it checks:
 *  - the volume header's block size and block count;
 *  - that every inline extent of every fork lies inside the volume;
 *  - that no fork claims more blocks than its inline extents plus overflow can
 *    supply, and that its byte size fits in those blocks;
 *  - that the allocation file is large enough to describe every block;
 *  - the free-block count in the header against the allocation bitmap, when the
 *    whole bitmap is inline (a large volume's bitmap lives in the extents
 *    overflow tree, which this pass does not chase yet: it says so instead of
 *    guessing). The padding bits of the bitmap's last byte are ignored: macOS
 *    leaves them zero, so treating them as corruption would fire on every real
 *    volume;
 *  - the catalog, extents and attributes B-tree headers: node size, total
 *    nodes against the fork, depth, root and leaf node indices;
 *  - the catalog itself: every leaf node's key length, and the file and folder
 *    counts the volume header claims against the records actually there.
 *
 * What it does not check yet: key ordering across leaves, index nodes, record
 * bodies, and overflow extents. Those are the next slice.
 */
#ifndef HELIKON_HFS_HFS_CHECK_H
#define HELIKON_HFS_HFS_CHECK_H

#include "hfs_volume.h"

#include <stdint.h>

/* One bit per finding, so a caller can log a mask and tests can assert on it. */
#define HFS_CHECK_BAD_BLOCK_SIZE 0x00000001u
#define HFS_CHECK_BAD_BLOCK_COUNT 0x00000002u
#define HFS_CHECK_FREE_EXCEEDS_TOTAL 0x00000004u
#define HFS_CHECK_EXTENT_OUTSIDE 0x00000008u
#define HFS_CHECK_FORK_BLOCK_OVERFLOW 0x00000010u
#define HFS_CHECK_FORK_SIZE_MISMATCH 0x00000020u
#define HFS_CHECK_ALLOCATION_TOO_SMALL 0x00000040u
#define HFS_CHECK_FREE_MISMATCH 0x00000080u
/* 0x00000100 was the bitmap-padding finding; macOS writes those bits as
 * zero, so it would fire on every real volume. Reserved. */
#define HFS_CHECK_BTREE_HEADER 0x00000200u
#define HFS_CHECK_BTREE_SIZE 0x00000400u
#define HFS_CHECK_BTREE_INDEX 0x00000800u
/*
 * Not a finding: the check could not look. Kept out of `problems` so a large
 * volume's bitmap, which lives in the extents overflow tree, does not make every
 * scrub "fail". It is reported separately and a caller decides what to say.
 */
#define HFS_CHECK_SKIPPED_BITMAP 0x00001000u
#define HFS_CHECK_CATALOG_COUNT 0x00002000u
#define HFS_CHECK_CATALOG_RECORD 0x00004000u

/*
 * The allocation bitmap is counted up to this size. 8 MiB of bitmap describes
 * 67 million blocks — 275 GB at 4096-byte blocks — so it covers the whole 6G
 * range and then some. Beyond it the check says it skipped rather than spending
 * minutes reading a scratch buffer's worth of bitmap on a device.
 */
#define HFS_CHECK_BITMAP_BYTES 8388608u

struct hfs_check_report {
    uint32_t block_size;
    uint32_t blocks;
    uint32_t free_blocks_claimed;
    uint32_t free_blocks_counted;
    int bitmap_counted;      /* 1 when the bitmap was read and compared */
    uint32_t problems;       /* the HFS_CHECK_* mask, findings only */
    uint32_t skipped;        /* what the check could not look at */

    uint16_t catalog_depth;
    uint16_t extents_depth;
    uint16_t attributes_depth;
    uint32_t catalog_nodes;
    uint32_t extents_nodes;
    uint32_t attributes_nodes;
    uint32_t catalog_files;
    uint32_t catalog_folders;
    uint32_t header_files;
    uint32_t header_folders;
};

/*
 * Fills `report` and returns HFS_OK, or HFS_ERR_ARGUMENT. Inconsistencies are
 * findings, not errors: a volume with problems still returns HFS_OK so the
 * caller can report all of them at once. The volume must already be probed.
 */
int hfs_check_volume(struct hfs_volume *volume, struct hfs_check_report *report);

#endif
