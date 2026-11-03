/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_btree_tree.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK 512u
#define NODE 512u
#define CATALOG_BLOCK 20u
#define BITMAP_BLOCK 18u

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void make_image(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4,
                            TEST_ATTR_UNMOUNTED, BLOCK,
                            TEST_IMAGE_SIZE / BLOCK, 1);

    uint8_t *catalog = test_image + 1024 + 272;
    test_image_put_be64(catalog, 2u * NODE);
    test_image_put_be32(catalog + 12, 2);
    test_image_put_be32(catalog + 16, CATALOG_BLOCK);
    test_image_put_be32(catalog + 20, 2);

    uint8_t *allocation = test_image + 1024 + 112;
    test_image_put_be64(allocation, 16);
    test_image_put_be32(allocation + 12, 1);
    test_image_put_be32(allocation + 16, BITMAP_BLOCK);
    test_image_put_be32(allocation + 20, 1);
    uint8_t *bitmap = test_image + BITMAP_BLOCK * BLOCK;
    memset(bitmap, 0xff, 16);
    bitmap[41 / 8] &= (uint8_t)~(0x80u >> (41 % 8));

    memcpy(test_image + TEST_IMAGE_SIZE - 1024, test_image + 1024, 512);

    uint8_t *header = test_image + CATALOG_BLOCK * BLOCK;
    memset(header, 0, NODE);
    header[8] = 1;
    put16(header + 10, 3);
    put16(header + 14, 1);
    put32(header + 16, 1);
    put32(header + 20, 1);
    put32(header + 24, 1);
    put32(header + 28, 1);
    put16(header + 32, NODE);
    put16(header + 34, 10);
    put32(header + 36, 2);
    put32(header + 40, 0);
    put16(header + 510, 14);
    put16(header + 508, 120);
    put16(header + 506, 248);
    put16(header + 504, 504);
    header[248] = 0xc0;

    uint8_t *leaf = test_image + (CATALOG_BLOCK + 1) * BLOCK;
    memset(leaf, 0, NODE);
    leaf[8] = 0xff;
    leaf[9] = 1;
    put16(leaf + 10, 1);
    put16(leaf + 14, 4);
    memcpy(leaf + 16, "DUMMY", 5);
    put16(leaf + 510, 14);
    put16(leaf + 508, 14 + 4);
}

static int test_grow(void)
{
    make_image();
    struct hfs_volume volume;
    CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0,
                           TEST_IMAGE_SIZE) == HFS_OK);
    struct hfs_btree btree;
    memset(&btree, 0, sizeof(btree));
    btree.volume = &volume;
    btree.fork = &volume.catalog;
    btree.node_size = NODE;
    btree.total_nodes = 2;
    btree.free_nodes = 0;
    btree.depth = 1;
    btree.root_node = 1;
    btree.first_leaf = 1;
    btree.last_leaf = 1;

    struct hfs_journal journal;
    struct hfs_txn tx;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = BLOCK;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;

    CHECK(hfs_btree_grow(&btree, &tx) == HFS_OK);
    for (size_t i = 0; i < tx.count; ++i) {
        memcpy(test_image + tx.blocks[i].sector * BLOCK,
               tx.blocks[i].data, tx.blocks[i].size);
    }

    CHECK(btree.total_nodes == 3);
    CHECK(btree.free_nodes == 1);
    uint8_t *catalog = test_image + 1024 + 272;
    CHECK(get32(catalog) == 0);
    CHECK(get32(catalog + 4) == 1536);
    CHECK(get32(catalog + 12) == 3);
    CHECK(get32(catalog + 16) == CATALOG_BLOCK);
    CHECK(get32(catalog + 20) == 2);
    CHECK(get32(catalog + 24) == 41);
    CHECK(get32(catalog + 28) == 1);

    uint8_t *header = test_image + CATALOG_BLOCK * BLOCK;
    CHECK(get32(header + 36) == 3);
    CHECK(get32(header + 40) == 1);
    CHECK((header[248 + 2 / 8] & (0x80u >> (2 % 8))) == 0);
    CHECK((test_image[BITMAP_BLOCK * BLOCK + 41 / 8] &
           (0x80u >> (41 % 8))) != 0);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "grow catalog tree", test_grow }
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
