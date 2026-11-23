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
/* Four nodes, so the index-node tests can have a root, an index and two leaves. */
#define CATALOG_BLOCKS 4u
#define EXTENTS_BLOCK 8u
#define EXTENTS_BLOCKS 2u
#define ATTRIBUTES_BLOCK 10u
#define ATTRIBUTES_BLOCKS 2u
#define USED_BLOCKS 12u

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
    /* The header node is in use, so one fewer is free: the free-node map and the
     * count have to agree with the tree's shape, and the ownership check reads
     * both. */
    put_be32(record + 26, total_nodes - 1u);       /* freeNodes */
    record[36] = 0;                                /* btreeType */
    record[37] = 0;                                /* keyCompareType */

    /* Offset table, filled from the end: record 0 runs 14..120, the two user
     * data records are empty and sit at the same place. */
    size_t table = node_size;
    test_image_put_be16(node + table - 2u, 14u);   /* record 0 begins */
    test_image_put_be16(node + table - 4u, 120u);  /* record 0 ends */
    test_image_put_be16(node + table - 6u, 122u);  /* the user record */
    test_image_put_be16(node + table - 8u, 124u);  /* the free-node map */
    /* Map record at 122..124: two bytes, sixteen nodes, header node in use. */
    node[122] = 0x80u;
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
    bitmap[1] = 0xf0u;
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
    if (report.problems != 0u)
        fprintf(stderr, "  note: clean volume problems 0x%08x skipped 0x%08x\n",
                (unsigned)report.problems, (unsigned)report.skipped);
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

/*
 * Catalog records for the leaf tests. A leaf is assembled record by record and
 * installed as node 1 of a tree, so each test can put exactly one thing wrong:
 * a key out of order, a thread key that names something, a body two bytes short.
 */
static uint8_t leaf_data[512];
static uint16_t leaf_count;
static uint16_t leaf_cursor;

/* Starts any node: kind 0xff is a leaf, 0 an index node. */
static void node_start(uint8_t kind, uint16_t height, uint32_t f_link,
                       uint32_t b_link)
{
    memset(leaf_data, 0, sizeof(leaf_data));
    put_be32(leaf_data + 0u, f_link);
    put_be32(leaf_data + 4u, b_link);
    leaf_data[8] = kind;
    leaf_data[9] = (uint8_t)height;
    leaf_cursor = 14u;
    leaf_count = 0u;
}

static void leaf_start(void)
{
    node_start(0xffu, 1u, 0u, 0u);
}

/* Writes the assembled node into the tree at `block`, node index `node`. */
static void node_write(uint32_t block, uint32_t node)
{
    uint8_t *target = test_image + ((size_t)block + node) * BLOCK_SIZE;
    test_image_put_be16(leaf_data + 512u - 2u * (uint16_t)(leaf_count + 1u),
                        leaf_cursor);
    test_image_put_be16(leaf_data + 10u, leaf_count);
    memcpy(target, leaf_data, 512u);
}

/* Marks a node used in the header node's free-node map (record 2, at 122). */
static void tree_map_used(uint32_t block, uint32_t node)
{
    uint8_t *header = test_image + (size_t)block * BLOCK_SIZE;
    header[122u + node / 8u] |= (uint8_t)(0x80u >> (node % 8u));
}

static uint32_t tree_total(uint32_t block)
{
    const uint8_t *record =
        test_image + (size_t)block * BLOCK_SIZE + 14u;
    return ((uint32_t)record[22] << 24) | ((uint32_t)record[23] << 16) |
           ((uint32_t)record[24] << 8) | record[25];
}

/* The free count the map implies, so the two can never disagree by accident. */
static void tree_free_from_map(uint32_t block)
{
    uint8_t *header = test_image + (size_t)block * BLOCK_SIZE;
    const uint32_t total = tree_total(block);
    uint32_t used = 0u;
    for (uint32_t node = 0u; node < total; ++node) {
        if ((header[122u + node / 8u] &
             (uint8_t)(0x80u >> (node % 8u))) != 0u)
            ++used;
    }
    put_be32(header + 14u + 26u, total - used);
}

/* The tree's header record: where the walk starts and what it promises. */
static void tree_header(uint32_t block, uint16_t depth, uint32_t root,
                        uint32_t records, uint32_t first, uint32_t last)
{
    uint8_t *record = test_image + (size_t)block * BLOCK_SIZE + 14u;
    test_image_put_be16(record + 0u, depth);
    put_be32(record + 2u, root);
    put_be32(record + 6u, records);
    put_be32(record + 10u, first);
    put_be32(record + 14u, last);
    /* The header node always exists; the root exists once the tree has one. */
    tree_map_used(block, 0u);
    if (root != 0u)
        tree_map_used(block, root);
    tree_free_from_map(block);
}

static void leaf_add(const uint8_t *record, size_t length)
{
    test_image_put_be16(leaf_data + 512u - 2u * (uint16_t)(leaf_count + 1u),
                        leaf_cursor);
    memcpy(leaf_data + leaf_cursor, record, length);
    /* HFS+ keeps every record on an even offset. */
    leaf_cursor = (uint16_t)(leaf_cursor + ((length + 1u) & ~(size_t)1u));
    leaf_count += 1u;
}

/*
 * Installs the leaf as node 1 and points the tree's header record at it:
 * depth 1, root 1, first and last leaf 1. `leaf_records` is what the header
 * claims, which is not always what the leaf holds — that is the point of the
 * chain test.
 */
static void leaf_install(uint32_t block, uint32_t records)
{
    node_write(block, 1u);
    tree_header(block, 1u, 1u, records, 1u, 1u);
}

/* keyLength, parentID, nameLength, UTF-16BE name; returns the key's byte size. */
static size_t put_catalog_key(uint8_t *out, uint32_t parent, const char *name)
{
    size_t units = strlen(name);
    test_image_put_be16(out, (uint16_t)(6u + 2u * units));
    test_image_put_be32(out + 2u, parent);
    test_image_put_be16(out + 6u, (uint16_t)units);
    for (size_t index = 0u; index < units; ++index) {
        out[8u + 2u * index] = 0u;
        out[9u + 2u * index] = (uint8_t)name[index];
    }
    return 8u + 2u * units;
}

static size_t make_folder_record(uint8_t *out, uint32_t parent, const char *name,
                                 uint32_t folder_id)
{
    size_t key = put_catalog_key(out, parent, name);
    memset(out + key, 0, 88u);
    test_image_put_be16(out + key, 1u);        /* recordType: folder */
    put_be32(out + key + 8u, folder_id);
    return key + 88u;
}

static size_t make_file_record(uint8_t *out, uint32_t parent, const char *name,
                               uint32_t file_id)
{
    size_t key = put_catalog_key(out, parent, name);
    memset(out + key, 0, 248u);
    test_image_put_be16(out + key, 2u);        /* recordType: file */
    put_be32(out + key + 8u, file_id);
    return key + 248u;
}

static size_t make_thread_record(uint8_t *out, uint32_t cnid, uint16_t type,
                                 uint32_t parent, const char *name)
{
    test_image_put_be16(out, 6u);              /* keyLength: parentID only */
    test_image_put_be32(out + 2u, cnid);
    test_image_put_be16(out + 6u, 0u);         /* a thread key never names */
    size_t units = strlen(name);
    memset(out + 8u, 0, 10u + 2u * units);
    test_image_put_be16(out + 8u, type);
    put_be32(out + 12u, parent);
    test_image_put_be16(out + 16u, (uint16_t)units);
    for (size_t index = 0u; index < units; ++index) {
        out[18u + 2u * index] = 0u;
        out[19u + 2u * index] = (uint8_t)name[index];
    }
    return 8u + 10u + 2u * units;
}

static void set_header_counts(uint32_t files, uint32_t folders)
{
    test_image_put_be32(test_image + 1024u + 32u, files);
    test_image_put_be32(test_image + 1024u + 36u, folders);
}

/* One index record: the key and the child node it points at. */
static void index_add(uint32_t parent, const char *name, uint32_t child)
{
    uint8_t record[512];
    size_t key = put_catalog_key(record, parent, name);
    put_be32(record + key, child);
    leaf_add(record, key + 4u);
}

/*
 * A real depth-2 catalog: one index node over two leaves, with one key per leaf
 * -- the first key of that leaf, which is the rule HFS+ indexes by. Every index
 * test starts here and then puts one thing wrong.
 */
static void make_two_level_catalog(void)
{
    uint8_t record[512];
    make_clean_volume();
    set_header_counts(1u, 1u);

    /* Leaf 2: the folder "alpha" under the root folder. */
    node_start(0xffu, 1u, 3u, 0u);
    leaf_add(record, make_folder_record(record, 2u, "alpha", 3u));
    node_write(CATALOG_BLOCK, 2u);

    /* Leaf 3: the file "one.txt" under the same folder. */
    node_start(0xffu, 1u, 0u, 2u);
    leaf_add(record, make_file_record(record, 2u, "one.txt", 16u));
    node_write(CATALOG_BLOCK, 3u);

    /* Index 1: the first key of each leaf, pointing at it. */
    node_start(0x00u, 2u, 0u, 0u);
    index_add(2u, "alpha", 2u);
    index_add(2u, "one.txt", 3u);
    node_write(CATALOG_BLOCK, 1u);

    /* The two leaves are in use: the free-node map has to say so. */
    tree_map_used(CATALOG_BLOCK, 2u);
    tree_map_used(CATALOG_BLOCK, 3u);
    tree_header(CATALOG_BLOCK, 2u, 1u, 2u, 2u, 3u);
}

/*
 * A catalog that a real volume could hold: the root folder, one folder inside
 * it and the folder threads, keys in leaf order and every body the size its
 * record type promises. Nothing here is a finding, and that is the assertion.
 */
static int test_clean_leaf_catalog(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    uint8_t record[512];
    make_clean_volume();
    set_header_counts(0u, 1u);

    leaf_start();
    leaf_add(record, make_folder_record(record, 1u, "Vol", 2u));
    leaf_add(record, make_thread_record(record, 2u, 3u, 1u, "Vol"));
    leaf_add(record, make_folder_record(record, 2u, "alpha", 3u));
    leaf_add(record, make_thread_record(record, 3u, 3u, 2u, "alpha"));
    leaf_install(CATALOG_BLOCK, 4u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK(report.problems == 0u);
    CHECK(report.catalog_folders == 1u);
    CHECK(report.catalog_files == 0u);
    return 0;
}

/* The same, for a file record: a 248-byte body that a reader may trust. */
static int test_clean_file_record(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    uint8_t record[512];
    make_clean_volume();
    set_header_counts(1u, 0u);

    leaf_start();
    leaf_add(record, make_file_record(record, 2u, "one.txt", 16u));
    leaf_add(record, make_thread_record(record, 16u, 4u, 2u, "one.txt"));
    leaf_install(CATALOG_BLOCK, 2u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK(report.problems == 0u);
    CHECK(report.catalog_files == 1u);
    return 0;
}

/* Two sibling folders in the wrong order: the tree's own search would miss one. */
static int test_catalog_order(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    uint8_t record[512];
    make_clean_volume();
    set_header_counts(0u, 2u);

    leaf_start();
    leaf_add(record, make_folder_record(record, 2u, "beta", 3u));
    leaf_add(record, make_folder_record(record, 2u, "alpha", 4u));
    leaf_install(CATALOG_BLOCK, 2u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_CATALOG_ORDER) != 0u);
    return 0;
}

/* A header that claims one record more than the chain holds. */
static int test_catalog_leaf_count(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    uint8_t record[512];
    make_clean_volume();
    set_header_counts(0u, 1u);

    leaf_start();
    leaf_add(record, make_folder_record(record, 2u, "alpha", 3u));
    leaf_install(CATALOG_BLOCK, 2u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_BTREE_CHAIN) != 0u);
    return 0;
}

/* A thread key that carries a name: no real thread record has one. */
static int test_catalog_thread_key(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    uint8_t record[512];
    make_clean_volume();
    set_header_counts(0u, 1u);

    leaf_start();
    size_t length = make_thread_record(record, 3u, 3u, 2u, "alpha");
    /* Give the key the name it must never have: widen the key, shift the body. */
    memmove(record + 10u, record + 8u, length - 8u);
    test_image_put_be16(record + 0u, 8u);
    test_image_put_be16(record + 6u, 1u);
    record[8] = 0u;
    record[9] = 'x';
    leaf_add(record, length + 2u);
    leaf_install(CATALOG_BLOCK, 1u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_CATALOG_BODY) != 0u);
    return 0;
}

/* A file body cut short: everything after it in the node would be misread. */
static int test_catalog_short_body(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    uint8_t record[512];
    make_clean_volume();
    set_header_counts(1u, 0u);

    leaf_start();
    size_t length = make_file_record(record, 2u, "one.txt", 16u);
    leaf_add(record, length - 148u);           /* 100 bytes of the 248 */
    leaf_install(CATALOG_BLOCK, 1u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_CATALOG_BODY) != 0u);
    return 0;
}

/* Extents keys order by fork type, file ID and start block; two swapped is damage. */
static int test_extents_order(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    uint8_t record[512];
    make_clean_volume();

    leaf_start();
    /* Key: keyLength 10, fork type, pad, file ID, start block; eight extents. */
    memset(record, 0, 74u);
    test_image_put_be16(record + 0u, 10u);
    record[2] = 0u;                            /* data fork */
    put_be32(record + 4u, 16u);                /* file ID */
    put_be32(record + 8u, 40u);                /* start block */
    leaf_add(record, 74u);
    put_be32(record + 8u, 20u);                /* the earlier start block, second */
    leaf_add(record, 74u);
    leaf_install(EXTENTS_BLOCK, 2u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_EXTENTS_ORDER) != 0u);
    return 0;
}

/*
 * A node the tree never reaches but the map calls used is a leak: that block can
 * never be handed out again. The one direction that is worse - a node the tree
 * does reach but the map calls free - is the next case.
 */
static int test_leaked_node(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_clean_volume();
    /* An empty tree owns only its header node; node 2 is inside the tree, marked
     * used, and nothing can reach it. */
    tree_map_used(CATALOG_BLOCK, 2u);
    tree_free_from_map(CATALOG_BLOCK);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_BTREE_NODES) != 0u);
    return 0;
}

static int test_reachable_node_marked_free(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_two_level_catalog();
    /* Leaf 3 is reachable through the index; the map says it is free, so the
     * next insert would hand the node out and overwrite its records. */
    uint8_t *header = test_image + (size_t)CATALOG_BLOCK * BLOCK_SIZE;
    header[122u + 3u / 8u] &= (uint8_t)~(0x80u >> (3u % 8u));
    tree_free_from_map(CATALOG_BLOCK);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_BTREE_NODES) != 0u);
    return 0;
}

/* The map is right and the count is wrong: also the same finding. */
static int test_free_count_disagrees(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_two_level_catalog();
    uint8_t *record = test_image + (size_t)CATALOG_BLOCK * BLOCK_SIZE + 14u;
    put_be32(record + 26, 1u); /* the map says zero */

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_BTREE_NODES) != 0u);
    return 0;
}

/* The depth-2 catalog above is valid and must report nothing. */
static int test_index_clean(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_two_level_catalog();

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK(report.problems == 0u);
    CHECK(report.catalog_depth == 2u);
    return 0;
}

/* An index key that sorts after the subtree it points at: those records are lost. */
static int test_index_key_above_child(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_two_level_catalog();

    /* Rewrite the index with "omega" for the leaf whose first key is "alpha". */
    node_start(0x00u, 2u, 0u, 0u);
    index_add(2u, "omega", 2u);
    index_add(2u, "one.txt", 3u);
    node_write(CATALOG_BLOCK, 1u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_BTREE_INDEX) != 0u);
    return 0;
}

/* A child pointer past the last node. */
static int test_index_child_range(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_two_level_catalog();

    /* The second index record's child: 14 bytes of node, 18 of key, then the
     * child pointer. */
    uint8_t *node = test_image + (size_t)(CATALOG_BLOCK + 1u) * BLOCK_SIZE;
    put_be32(node + 14u + 18u + 4u, 9u);

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_BTREE_INDEX) != 0u);
    return 0;
}

/* A leaf that claims to be an index node. */
static int test_index_child_level(void)
{
    struct hfs_volume volume;
    struct hfs_check_report report;
    make_two_level_catalog();

    test_image[(size_t)(CATALOG_BLOCK + 3u) * BLOCK_SIZE + 8u] = 0x00u;

    CHECK(probe_and_check(&volume, &report) == HFS_OK);
    CHECK((report.problems & HFS_CHECK_BTREE_INDEX) != 0u);
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
        { "bad tree index", test_bad_tree_index },
        { "clean leaf catalog", test_clean_leaf_catalog },
        { "clean file record", test_clean_file_record },
        { "catalog keys out of order", test_catalog_order },
        { "leaf chain count", test_catalog_leaf_count },
        { "thread key with a name", test_catalog_thread_key },
        { "file body cut short", test_catalog_short_body },
        { "extents keys out of order", test_extents_order },
        { "clean index node", test_index_clean },
        { "leaked node", test_leaked_node },
        { "reachable node marked free", test_reachable_node_marked_free },
        { "free count disagrees", test_free_count_disagrees },
        { "index key above its child", test_index_key_above_child },
        { "index child out of range", test_index_child_range },
        { "index child at the wrong level", test_index_child_level }
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
