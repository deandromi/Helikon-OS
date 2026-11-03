/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_btree_tree.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BLOCK_SIZE 512u
#define TEST_NODE_SIZE 512u
#define TEST_NODES 16u
#define TEST_CATALOG_BLOCK 20u
#define TEST_KEY_BYTES 64u
#define TEST_RECORDS 20u

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint8_t *node_ptr(uint32_t node)
{
    return test_image + TEST_CATALOG_BLOCK * TEST_BLOCK_SIZE +
           node * TEST_NODE_SIZE;
}

static void init_node(uint32_t node, int8_t kind, uint8_t height)
{
    uint8_t *p = node_ptr(node);
    memset(p, 0, TEST_NODE_SIZE);
    p[8] = (uint8_t)kind;
    p[9] = height;
    put_be16(p + 10, 0);
    put_be16(p + TEST_NODE_SIZE - 2, 14);
}

static void make_image(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4,
                            TEST_ATTR_UNMOUNTED, TEST_BLOCK_SIZE,
                            TEST_IMAGE_SIZE / TEST_BLOCK_SIZE,
                            0);

    uint8_t *fork = test_image + 1024 + 272;
    test_image_put_be64(fork, (uint64_t)TEST_NODES * TEST_NODE_SIZE);
    test_image_put_be32(fork + 12, TEST_NODES);
    test_image_put_be32(fork + 16, TEST_CATALOG_BLOCK);
    test_image_put_be32(fork + 20, TEST_NODES);

    uint8_t *header = node_ptr(0);
    memset(header, 0, TEST_NODE_SIZE);
    header[8] = 1;
    header[9] = 0;
    put_be16(header + 10, 3);
    put_be16(header + 14, 1);       /* depth */
    put_be32(header + 16, 1);       /* root */
    put_be32(header + 20, 0);       /* leaf records */
    put_be32(header + 24, 1);       /* first leaf */
    put_be32(header + 28, 1);       /* last leaf */
    put_be16(header + 32, TEST_NODE_SIZE);
    put_be16(header + 34, 68);
    put_be32(header + 36, TEST_NODES);
    put_be32(header + 40, TEST_NODES - 2);
    put_be32(header + 56, 0);
    header[60] = 0;
    header[61] = 0;
    put_be32(header + 62, 0);

    put_be16(header + 510, 14);
    put_be16(header + 508, 120);
    put_be16(header + 506, 248);
    put_be16(header + 504, 504);
    /* Header node map: nodes 0 and 1 used. */
    header[248] = 0xc0;

    init_node(1, -1, 1);
}

static int compare_keys(void *context, const uint8_t *a, size_t a_length,
                        const uint8_t *b, size_t b_length)
{
    (void)context;
    if (a_length != b_length)
        return a_length < b_length ? -1 : 1;
    return memcmp(a, b, a_length);
}

static void make_record(uint8_t *record, uint32_t index)
{
    memset(record, 0, 2u + TEST_KEY_BYTES);
    put_be16(record, TEST_KEY_BYTES);
    put_be32(record + 2, index);
}

static void apply_txn(const struct hfs_txn *tx)
{
    for (size_t i = 0; i < tx->count; ++i) {
        memcpy(test_image + tx->blocks[i].sector * TEST_BLOCK_SIZE,
               tx->blocks[i].data, tx->blocks[i].size);
    }
}

static int test_insert_many(void)
{
    make_image();
    struct hfs_volume volume;
    CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0,
                           TEST_IMAGE_SIZE) == HFS_OK);
    struct hfs_btree btree;
    memset(&btree, 0, sizeof(btree));
    btree.volume = &volume;
    btree.fork = &volume.catalog;
    btree.node_size = TEST_NODE_SIZE;
    btree.total_nodes = TEST_NODES;
    btree.free_nodes = TEST_NODES - 2;
    btree.depth = 1;
    btree.root_node = 1;
    btree.first_leaf = 1;
    btree.last_leaf = 1;
    btree.leaf_records = 0;

    struct hfs_journal journal;
    struct hfs_txn tx;
    for (uint32_t i = 0; i < TEST_RECORDS; ++i) {
        memset(&journal, 0, sizeof(journal));
        journal.sector_size = TEST_BLOCK_SIZE;
        memset(&tx, 0, sizeof(tx));
        tx.journal = &journal;
        tx.active = 1;

        uint8_t record[2u + TEST_KEY_BYTES];
        make_record(record, i + 10u);
        int rc = hfs_btree_insert(&btree, &tx, record, sizeof(record),
                                  compare_keys, NULL);
        if (rc != HFS_OK) {
            fprintf(stderr, "insert %u failed: %d\n", i, rc);
            CHECK(rc == HFS_OK);
        }
        apply_txn(&tx);

        uint8_t *header_now = node_ptr(0);
        uint32_t scan = get_be32(header_now + 24);
        uint32_t prev = 0;
        int have_prev = 0;
        while (scan != 0) {
            uint8_t *leaf_now = node_ptr(scan);
            uint16_t records_now = get_be16(leaf_now + 10);
            for (uint16_t r = 0; r < records_now; ++r) {
                uint8_t *record_now = leaf_now + get_be16(
                    leaf_now + TEST_NODE_SIZE - 2u * (r + 1));
                uint32_t key_now = get_be32(record_now + 2);
                if (have_prev && key_now < prev) {
                    fprintf(stderr, "insert %u first disorder leaf %u key %u prev %u\n",
                            i, scan, key_now, prev);
                    CHECK(key_now >= prev);
                }
                prev = key_now;
                have_prev = 1;
            }
            scan = get_be32(leaf_now);
        }
    }

    /* A key below the tree minimum would require updating the parent chain;
     * that case must fail closed, not corrupt the tree. */
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = TEST_BLOCK_SIZE;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    uint8_t low_record[2u + TEST_KEY_BYTES];
    make_record(low_record, 5);
    CHECK(hfs_btree_insert(&btree, &tx, low_record, sizeof(low_record),
                           compare_keys, NULL) == HFS_ERR_UNSUPPORTED);

    /* Walk the leaf chain and count/order every record. */
    uint8_t *header = node_ptr(0);
    CHECK(get_be16(header + 14) > 1);
    CHECK(get_be32(header + 20) == TEST_RECORDS);
    uint32_t node = get_be32(header + 24);
    uint32_t count = 0;
    uint32_t last = 0;
    uint32_t previous_key = 0;
    while (node != 0) {
        uint8_t *leaf = node_ptr(node);
        CHECK(leaf[8] == 0xff);
        if (count != 0) {
            /* Keys were inserted in ascending order; each leaf must be sorted
             * and the chain must continue ascending. */
            uint8_t first[2u + TEST_KEY_BYTES];
            memcpy(first, leaf + 14, sizeof(first));
            CHECK(get_be32(first + 2) >= previous_key);
        }
        uint16_t records = get_be16(leaf + 10);
        for (uint16_t r = 0; r < records; ++r) {
            uint8_t *record = leaf + get_be16(leaf + TEST_NODE_SIZE -
                                              2u * (r + 1));
            CHECK(get_be16(record) == TEST_KEY_BYTES);
            uint32_t key = get_be32(record + 2);
            if (count != 0)
                CHECK(key >= previous_key);
            previous_key = key;
            ++count;
        }
        last = node;
        node = get_be32(leaf);
    }
    CHECK(count == TEST_RECORDS);
    CHECK(last == get_be32(header + 28));
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "insert many and split", test_insert_many }
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        int rc = tests[i].run();
        if (rc == 0) {
            printf("PASS %s\n", tests[i].name);
        } else {
            printf("FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("%zu tests passed\n", sizeof(tests) / sizeof(tests[0]));
    return 0;
}
