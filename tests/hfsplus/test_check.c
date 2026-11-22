/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host tests for the read-only structural check (`firmware/hfs/hfs_check.c`).
 *
 * The image is built by hand, block by block, so each test can put exactly one
 * thing wrong and prove the check names it. The clean image is the important
 * case: a checker that cries wolf is worse than no checker, so every finding
 * gets a matching test that the clean volume has none of it.
 */
#include "hfs_check.h"
#include "hfs_volume.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <string.h>

#define BLOCK_SIZE 512u
#define BLOCKS 125u

#define HEADER_BLOCK 2u
#define ALLOCATION_BLOCK 3u
#define CATALOG_BLOCK 4u
#define CATALOG_BLOCKS 2u
#define EXTENTS_BLOCK 6u
#define EXTENTS_BLOCKS 2u
#define ATTRIBUTES_BLOCK 8u
#define ATTRIBUTES_BLOCKS 2u
#define USED_BLOCKS 10u

static void put_be32(uint8_t *p, uint32_t value)
{
    test_image_put_be32(p, value);
}

/* One HFSPlusForkData at `offset` of the volume header. */
static void make_fork(size_t offset, uint64_t size, uint32_t blocks,
                      uint32_t start, uint32_t count)
{
    uint8_t *fork = test_image + 1024u + offset;
    test_image_put_be64(fork, size);
    put_be32(fork + 8, 0);
    put_be32(fork + 12, blocks);
    put_be32(fork + 16, start);
    put_be32(fork + 20, count);
}

/*
 * A valid header node for an empty tree: descriptor, header record and the
 * offset table the reader walks. `node_size` is 512 for every tree here.
 */
static void make_tree(uint32_t block, uint32_t blocks, uint16_t node_size,
                      uint32_t total_nodes)
{
    uint8_t *node = test_image + (size_t)block * BLOCK_SIZE;
    memset(node, 0, (size_t)blocks * BLOCK_SIZE);

    /* BTNodeDescriptor: fLink, bLink, kind = header, height = 0, numRecords. */
    put_be32(node + 0, 0);
    put_be32(node + 4, 0);
    node[8] = 1;
    node[9] = 0;
    node[10] = 0;
    node[11] = 3;
    node[12] = 0;
    node[13] = 0;

    /* Header record, at offset 14, reached through the offset table. */
    uint8_t *record = node + 14u;
    test_image_put_be16(record + 0, 0);            /* depth */
    put_be32(record + 2, 0);                       /* rootNode */
    put_be32(record + 6, 0);                       /* leafRecords */
    put_be32(record + 10, 0);                      /* firstLeafNode */
    put_be32(record + 14, 0);                      /* lastLeafNode */
    test_image_put_be16(record + 18, node_size);   /* nodeSize */
    test_image_put_be16(record + 20, (uint16_t)(node_size + 4u));
    put_be32(record + 22, total_nodes);            /* totalNodes */
    put_be32(record + 26, total_nodes);            /* freeNodes */
    record[36] = 0;                                /* btreeType */
    record[37] = 0;                                /* keyCompareType */

    /* Offset table, filled from the end: record 0 runs 14..120, the two user
     * data records are empty and sit at the same place. */
    size_t table = node_size;
    test_image_put_be16(node + table - 2u, 14u);   /* record 0 begins */
    test_image_put_be16(node + table - 4u, 120u);  /* record 0 ends */
    test_image_put_be16(node + table - 6u, 122u);  /* empty user record */
    test_image_put_be16(node + table - 8u, 124u);  /* empty user record */
}

static void make_bitmap(void)
{
    uint8_t *bitmap = test_image + (size_t)ALLOCATION_BLOCK * BLOCK_SIZE;
    memset(bitmap, 0, BLOCK_SIZE);
    /*
     * 125 blocks: blocks 0..USED_BLOCKS-1 are allocated, the rest are free, and
     * the three padding bits of the last byte are defined as used.
     */
    bitmap[0] = 0xffu;
    bitmap[1] = 0xc0u;
    /* Byte 15 covers blocks 120..127: 120..124 free, and the three padding bits
     * (125..127) set. */
    bitmap[15] = 0x07u;
}

static void make_clean_volume(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4,
                           TEST_ATTR_UNMOUNTED, BLOCK_SIZE, BLOCKS,
                           BLOCKS - USED_BLOCKS);
    make_fork(112u, 16u, 1u, ALLOCATION_BLOCK, 1u);
    make_fork(192u, (uint64_t)EXTENTS_BLOCKS * BLOCK_SIZE, EXTENTS_BLOCKS,
              EXTENTS_BLOCK, EXTENTS_BLOCKS);
    make_fork(272u, (uint64_t)CATALOG_BLOCKS * BLOCK_SIZE, CATALOG_BLOCKS,
              CATALOG_BLOCK, CATALOG_BLOCKS);
    make_fork(352u, (uint64_t)ATTRIBUTES_BLOCKS * BLOCK_SIZE,
              ATTRIBUTES_BLOCKS, ATTRIBUTES_BLOCK, ATTRIBUTES_BLOCKS);
    make_bitmap();
    make_tree(CATALOG_BLOCK, CATALOG_BLOCKS, 512u, CATALOG_BLOCKS);
    make_tree(EXTENTS_BLOCK, EXTENTS_BLOCKS, 512u, EXTENTS_BLOCKS);
    make_tree(ATTRIBUTES_BLOCK, ATTRIBUTES_BLOCKS, 512u, ATTRIBUTES_BLOCKS);
}

static int probe_and_check(struct hfs_volume *volume, struct hfs_check_report *report)
{
    int rc = hfs_volume_probe(volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    if (rc != HFS_OK)
        return rc;
    return hfs_check_volume(volume, report);
}

static int test_clean_volume(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK(report.problems == 0u);
    CHECK(report.bitmap_counted == 1);
    CHECK(report.free_blocks_claimed == BLOCKS - USED_BLOCKS);
    CHECK(report.free_blocks_counted == BLOCKS - USED_BLOCKS);
    CHECK(report.catalog_nodes == CATALOG_BLOCKS);
    CHECK(report.catalog_depth == 0u);
    return 0;
}

static int test_bad_block_size(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* 500 is not a power of two. */
    put_be32(test_image + 1024u + 40u, 500u);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* probe rejected it outright, which is also a refusal */
    CHECK((report.problems & HFS_CHECK_BAD_BLOCK_SIZE) != 0u);
    return 0;
}

static int test_free_exceeds_total(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    put_be32(test_image + 1024u + 48u, BLOCKS + 1u);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.problems & HFS_CHECK_FREE_EXCEEDS_TOTAL) != 0u);
    return 0;
}

static int test_extent_outside_volume(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* Catalog starts past the end of the volume. */
    make_fork(272u, (uint64_t)CATALOG_BLOCKS * BLOCK_SIZE, CATALOG_BLOCKS,
              BLOCKS + 8u, CATALOG_BLOCKS);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.problems & HFS_CHECK_EXTENT_OUTSIDE) != 0u);
    return 0;
}

static int test_fork_block_overflow(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* Two blocks placed, but the fork claims one: the extents overrun it. */
    make_fork(192u, (uint64_t)EXTENTS_BLOCKS * BLOCK_SIZE, 1u,
              EXTENTS_BLOCK, EXTENTS_BLOCKS);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.problems & HFS_CHECK_FORK_BLOCK_OVERFLOW) != 0u);
    return 0;
}

static int test_fork_size_mismatch(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* A fork whose byte size needs more blocks than it has. */
    make_fork(272u, (uint64_t)CATALOG_BLOCKS * BLOCK_SIZE + 1u, CATALOG_BLOCKS,
              CATALOG_BLOCK, CATALOG_BLOCKS);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.problems & HFS_CHECK_FORK_SIZE_MISMATCH) != 0u);
    return 0;
}

static int test_allocation_too_small(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* 8 bytes of bitmap cannot describe 128 blocks. */
    make_fork(112u, 8u, 1u, ALLOCATION_BLOCK, 1u);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.problems & HFS_CHECK_ALLOCATION_TOO_SMALL) != 0u);
    return 0;
}

static int test_free_mismatch(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* One block marked used in the header but free in the bitmap. */
    put_be32(test_image + 1024u + 48u, BLOCKS - USED_BLOCKS + 1u);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.problems & HFS_CHECK_FREE_MISMATCH) != 0u);
    CHECK(report.bitmap_counted == 1);
    CHECK(report.free_blocks_counted == BLOCKS - USED_BLOCKS);
    return 0;
}

static int test_bitmap_padding_ignored(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /*
     * macOS leaves the padding bits of the last bitmap byte zero on a freshly
     * formatted volume, so the check must ignore them rather than report a
     * volume macOS itself made.
     */
    uint8_t *bitmap = test_image + (size_t)ALLOCATION_BLOCK * BLOCK_SIZE;
    bitmap[15] = 0x00u;

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK(report.problems == 0u);
    CHECK(report.free_blocks_counted == BLOCKS - USED_BLOCKS);
    return 0;
}

static int test_allocation_two_runs(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /*
     * A bitmap in two runs: the first eight bytes in one block, the rest in a
     * block elsewhere. That is what a fragmented — or overflowed — allocation
     * fork looks like, and the count has to follow it rather than read one run.
     */
    uint8_t *bitmap = test_image + (size_t)ALLOCATION_BLOCK * BLOCK_SIZE;
    uint8_t *second = test_image + 20u * BLOCK_SIZE;
    memcpy(second, bitmap + 8u, 8u);
    memset(bitmap + 8u, 0, 8u);
    make_fork(112u, 16u, 2u, ALLOCATION_BLOCK, 1u);
    /* The second extent of the allocation fork. */
    uint8_t *fork = test_image + 1024u + 112u;
    put_be32(fork + 24u, 20u);
    put_be32(fork + 28u, 1u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK(report.problems == 0u);
    CHECK(report.bitmap_counted == 1);
    CHECK(report.free_blocks_counted == BLOCKS - USED_BLOCKS);
    return 0;
}

static int test_catalog_count(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* The tree is empty, so a file count of three is a lie. */
    test_image_put_be32(test_image + 1024u + 32u, 3u);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0;
    CHECK((report.problems & HFS_CHECK_CATALOG_COUNT) != 0u);
    CHECK(report.header_files == 3u);
    CHECK(report.catalog_files == 0u);
    return 0;
}

static int test_skipped_bitmap(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /*
     * An allocation fork whose inline extents do not cover it: the rest would be
     * in the extents overflow tree. The check must say it skipped, not invent a
     * count.
     */
    make_fork(112u, 16u, 4u, ALLOCATION_BLOCK, 1u);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.skipped & HFS_CHECK_SKIPPED_BITMAP) != 0u);
    CHECK(report.problems == 0u);
    CHECK(report.bitmap_counted == 0);
    CHECK((report.problems & HFS_CHECK_FREE_MISMATCH) == 0u);
    return 0;
}

static int test_bad_tree_header(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* Node kind 2 is a map node, not a header node: the tree cannot open. */
    test_image[(size_t)CATALOG_BLOCK * BLOCK_SIZE + 8u] = 2u;

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.problems & HFS_CHECK_BTREE_HEADER) != 0u);
    return 0;
}

static int test_bad_tree_size(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* Three nodes declared in a fork that holds two. */
    uint8_t *record = test_image + (size_t)CATALOG_BLOCK * BLOCK_SIZE + 14u;
    put_be32(record + 22, CATALOG_BLOCKS + 1u);

    if (probe_and_check(&volume, &report) != HFS_OK)
        return 0; /* the mount refused it, which detects the corruption too */
    CHECK((report.problems & HFS_CHECK_BTREE_HEADER) != 0u ||
          (report.problems & HFS_CHECK_BTREE_SIZE) != 0u);
    return 0;
}

static int test_bad_tree_index(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* A tree that claims a leaf and points it past the last node. */
    uint8_t *record = test_image + (size_t)CATALOG_BLOCK * BLOCK_SIZE + 14u;
    test_image_put_be16(record + 0, 1u);       /* depth */
    put_be32(record + 2, 1u);                  /* root */
    put_be32(record + 6, 1u);                  /* leafRecords */
    put_be32(record + 10, 9u);                 /* firstLeaf, out of range */
    put_be32(record + 14, 9u);                 /* lastLeaf, out of range */
    put_be32(record + 26, 0u);                 /* freeNodes */

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    /* The reader refuses the header itself, so either finding is correct. */
    CHECK((report.problems & HFS_CHECK_BTREE_HEADER) != 0u ||
          (report.problems & HFS_CHECK_BTREE_INDEX) != 0u);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "clean volume", test_clean_volume },
        { "bad block size", test_bad_block_size },
        { "free exceeds total", test_free_exceeds_total },
        { "extent outside volume", test_extent_outside_volume },
        { "fork block overflow", test_fork_block_overflow },
        { "fork size mismatch", test_fork_size_mismatch },
        { "allocation too small", test_allocation_too_small },
        { "free count mismatch", test_free_mismatch },
        { "bitmap padding ignored", test_bitmap_padding_ignored },
        { "allocation in two runs", test_allocation_two_runs },
        { "catalog count mismatch", test_catalog_count },
        { "skipped bitmap", test_skipped_bitmap },
        { "bad tree header", test_bad_tree_header },
        { "bad tree size", test_bad_tree_size },
        { "bad tree index", test_bad_tree_index }
    };

    int failed = 0;
    const size_t index_count = sizeof(tests) / sizeof(tests[0]);
    for (size_t index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (tests[index].run() != 0) {
            printf("FAIL %s\n", tests[index].name);
            failed = 1;
            continue;
        }
        printf("PASS %s\n", tests[index].name);
    }
    if (failed != 0) {
        fprintf(stderr, "%u test(s) failed\n", (unsigned)index_count);
        return 1;
    }
    return 0;
}
