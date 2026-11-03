/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog_write.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BLOCK_SIZE 512u
#define TEST_NODE_SIZE 512u
#define TEST_NODES 16u
#define TEST_CATALOG_BLOCK 20u
#define TEST_NAME_UNITS 5u

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

static void make_image(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4,
                            TEST_ATTR_UNMOUNTED, TEST_BLOCK_SIZE,
                            TEST_IMAGE_SIZE / TEST_BLOCK_SIZE, 0);
    put_be32(test_image + 1024 + 32, 1);   /* one root folder */
    put_be32(test_image + 1024 + 64, 16);  /* next CNID */
    memcpy(test_image + TEST_IMAGE_SIZE - 1024, test_image + 1024, 512);

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
    put_be16(header + 14, 1);
    put_be32(header + 16, 1);
    put_be32(header + 20, 0);
    put_be32(header + 24, 1);
    put_be32(header + 28, 1);
    put_be16(header + 32, TEST_NODE_SIZE);
    put_be16(header + 34, 520);
    put_be32(header + 36, TEST_NODES);
    put_be32(header + 40, TEST_NODES - 2);
    put_be16(header + 510, 14);
    put_be16(header + 508, 120);
    put_be16(header + 506, 248);
    put_be16(header + 504, 504);
    header[248] = 0xc0;

    uint8_t *leaf = node_ptr(1);
    memset(leaf, 0, TEST_NODE_SIZE);
    leaf[8] = 0xff;
    leaf[9] = 1;
    put_be16(leaf + 10, 0);
    put_be16(leaf + TEST_NODE_SIZE - 2, 14);
}

static int compare_catalog(void *context, const uint8_t *a, size_t a_length,
                           const uint8_t *b, size_t b_length)
{
    (void)context;
    if (a_length < 6 || b_length < 6)
        return a_length < b_length ? -1 : 1;
    uint32_t parent_a = get_be32(a);
    uint32_t parent_b = get_be32(b);
    if (parent_a != parent_b)
        return parent_a < parent_b ? -1 : 1;
    uint16_t units_a = get_be16(a + 4);
    uint16_t units_b = get_be16(b + 4);
    if (units_a != units_b)
        return units_a < units_b ? -1 : 1;
    size_t bytes = (size_t)units_a * 2u;
    if (bytes > a_length - 6u || bytes > b_length - 6u)
        return 0;
    return memcmp(a + 6, b + 6, bytes);
}

static void apply_txn(const struct hfs_txn *tx)
{
    for (size_t i = 0; i < tx->count; ++i) {
        memcpy(test_image + tx->blocks[i].sector * TEST_BLOCK_SIZE,
               tx->blocks[i].data, tx->blocks[i].size);
    }
}

static int test_create_delete_file(void)
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

    struct hfs_catalog catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.btree = btree;

    static const uint8_t name[TEST_NAME_UNITS * 2] = {
        0, 'H', 0, 'E', 0, 'L', 0, 'L', 0, 'O'
    };
    struct hfs_journal journal;
    struct hfs_txn tx;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = TEST_BLOCK_SIZE;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;

    uint32_t cnid = 0;
    int create_rc = hfs_catalog_create_file(&catalog, &tx, 2, name,
                                             TEST_NAME_UNITS, 0644,
                                             compare_catalog, NULL, &cnid);
    CHECK(create_rc == HFS_OK);
    CHECK(cnid == 16);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 32) == 2);
    CHECK(get_be32(test_image + 1024 + 64) == 17);
    CHECK(get_be32(node_ptr(0) + 20) == 2);

    /* The root leaf has a file record and a thread record. */
    uint8_t *leaf = node_ptr(1);
    CHECK(get_be16(leaf + 10) == 2);
    uint8_t *first = leaf + get_be16(leaf + TEST_NODE_SIZE - 2);
    CHECK(get_be32(first + 2) == 2);
    CHECK(get_be16(first + 6) == TEST_NAME_UNITS);
    CHECK(memcmp(first + 8, name, sizeof(name)) == 0);
    uint8_t *second = leaf + get_be16(leaf + TEST_NODE_SIZE - 4);
    CHECK(get_be32(second + 2) == 16);
    CHECK(get_be16(second + 6) == 0);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_catalog_delete_file(&catalog, &tx, 2, name, TEST_NAME_UNITS,
                                  cnid, compare_catalog, NULL) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 32) == 1);
    CHECK(get_be32(node_ptr(0) + 20) == 0);
    CHECK(get_be16(node_ptr(1) + 10) == 0);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "create and delete empty file", test_create_delete_file }
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
