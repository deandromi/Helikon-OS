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
 *  - each tree's leaf chain: it ends at the header's last leaf, it holds the
 *    number of records the header claims, and every key is well formed;
 *  - key order: catalog keys strictly increase in leaf order (parent ID, then
 *    the name under Apple's FastUnicodeCompare) and extents keys strictly
 *    increase in leaf order (fork type, file ID, start block), across leaf
 *    boundaries as well as inside one leaf;
 *  - catalog record bodies: a file record carries 248 bytes and a real file ID,
 *    a folder record 88 bytes and a real folder ID, a thread record names its
 *    item and its key names nothing, and only types 1..4 appear;
 *  - index nodes: every child pointer is inside the tree, the child is the level
 *    below (an index node, or a leaf at the last step), and the key that points
 *    at a subtree is not greater than that subtree's first key - the rule a
 *    search depends on. A stale ancestor key is allowed, because HFS+ leaves one
 *    behind after a delete;
 *  - node ownership: every node the tree can reach from its root is marked used
 *    in the free-node map, every node it cannot reach is marked free, and the
 *    header's free count matches the map. A node marked used but unreachable is
 *    a leak - the block is gone forever and `fsck_hfs` reports the free count as
 *    wrong - and a node marked free but reachable would be handed out again and
 *    overwrite live records;
 *  - the file and folder counts the volume header claims against the records
 *    actually there, and the next catalog ID against the two CNIDs the format
 *    reserves and the root's own ID.
 *
 * What it does not check yet: the attributes tree's index keys (its key order is
 * not exercised by any fixture here), the B-tree node's own free-space
 * accounting, quarantine, and the charging/idle scrub policy. Those are the next
 * slices. A tree with more nodes than the node-ownership pass can mark is
 * skipped (`HFS_CHECK_SKIPPED_NODES`), not guessed.
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
/* A leaf chain that does not match its header: wrong end, wrong count, or a
 * break the iterator refused to walk past. */
#define HFS_CHECK_BTREE_CHAIN 0x00008000u
/* Catalog keys that do not strictly increase in leaf order. */
#define HFS_CHECK_CATALOG_ORDER 0x00010000u
/* A catalog record body that is not what its record type promises. */
#define HFS_CHECK_CATALOG_BODY 0x00020000u
/* Extents keys that do not strictly increase, or that do not fit their record. */
#define HFS_CHECK_EXTENTS_ORDER 0x00040000u
/* A free-node map that disagrees with what the tree reaches, or a free count
 * that disagrees with the map. */
#define HFS_CHECK_BTREE_NODES 0x00080000u
/*
 * Not a finding: the tree has more nodes than the ownership pass can mark. Kept
 * out of `problems` for the same reason as the bitmap skip.
 */
#define HFS_CHECK_SKIPPED_NODES 0x00100000u
/* A next-catalog-ID that would hand out a reserved CNID. 0 and 1 are reserved
 * and 2 is the root folder, so the volume header must say 3 or more. */
#define HFS_CHECK_CATALOG_ID 0x00200000u

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
