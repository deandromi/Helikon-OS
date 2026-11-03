/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog_write.h"
#include "hfs_extents_write.h"
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
                            TEST_IMAGE_SIZE / TEST_BLOCK_SIZE, 61);
    put_be32(test_image + 1024 + 32, 1);   /* one root folder */
    put_be32(test_image + 1024 + 64, 16);  /* next CNID */

    /* Allocation file: 16 bytes (128 bits), one block at block 18. */
    uint8_t *allocation = test_image + 1024 + 112;
    test_image_put_be64(allocation, 16);
    test_image_put_be32(allocation + 12, 1);
    test_image_put_be32(allocation + 16, 18);
    test_image_put_be32(allocation + 20, 1);
    uint8_t *bitmap = test_image + 18 * TEST_BLOCK_SIZE;
    memset(bitmap, 0xff, 5);
    bitmap[5] = 0xa0;      /* blocks 40 and 42 */
    memset(bitmap + 12, 0xff, 3); /* blocks 96..119, incl. extents tree */
    bitmap[15] = 0x02;     /* block 126, alternate header */

    /* Extents-overflow B-tree: 16 nodes at block 100. */
    uint8_t *extents_fork = test_image + 1024 + 192;
    test_image_put_be64(extents_fork, (uint64_t)TEST_NODES * TEST_NODE_SIZE);
    test_image_put_be32(extents_fork + 12, TEST_NODES);
    test_image_put_be32(extents_fork + 16, 100);
    test_image_put_be32(extents_fork + 20, TEST_NODES);
    uint8_t *eh = test_image + 100 * TEST_BLOCK_SIZE;
    memset(eh, 0, TEST_NODE_SIZE);
    eh[8] = 1;
    put_be16(eh + 10, 3);
    put_be16(eh + 14, 1);
    put_be32(eh + 16, 1);
    put_be32(eh + 20, 1);
    put_be32(eh + 24, 1);
    put_be32(eh + 28, 1);
    put_be16(eh + 32, TEST_NODE_SIZE);
    put_be16(eh + 34, 10);
    put_be32(eh + 36, TEST_NODES);
    put_be32(eh + 40, TEST_NODES - 2);
    eh[60] = 1;
    eh[61] = 0;
    put_be16(eh + 510, 14);
    put_be16(eh + 508, 120);
    put_be16(eh + 506, 248);
    put_be16(eh + 504, 504);
    eh[248] = 0xc0;
    uint8_t *el = test_image + 101 * TEST_BLOCK_SIZE;
    memset(el, 0, TEST_NODE_SIZE);
    el[8] = 0xff;
    el[9] = 1;
    put_be16(el + 10, 1);
    put_be16(el + 14, 10);
    memset(el + 16, 0, 74);
    put_be16(el + 510, 14);
    put_be16(el + 508, 90);
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
                           const uint8_t *b, size_t b_length);

static size_t build_key(uint8_t *buffer, uint32_t parent,
                        const uint8_t *name, size_t units)
{
    put_be16(buffer, (uint16_t)(6u + units * 2u));
    put_be32(buffer + 2, parent);
    put_be16(buffer + 6, (uint16_t)units);
    if (units != 0)
        memcpy(buffer + 8, name, units * 2u);
    return 6u + units * 2u;
}

static int seed_root(struct hfs_btree *btree, struct hfs_txn *tx)
{
    static const uint8_t root_name[8] = {
        0, 'T', 0, 'E', 0, 'S', 0, 'T'
    };
    uint8_t record[512];
    size_t key_length = build_key(record, 1, root_name, 4);
    size_t data_offset = 2u + key_length;
    memset(record + data_offset, 0, 88);
    put_be16(record + data_offset, 1);
    put_be32(record + data_offset + 8, 2);
    put_be32(record + data_offset + 4, 0);
    int rc = hfs_btree_insert(btree, tx, record, data_offset + 88,
                              compare_catalog, NULL);
    if (rc != HFS_OK)
        return rc;

    key_length = build_key(record, 2, NULL, 0);
    data_offset = 2u + key_length;
    memset(record + data_offset, 0, 18);
    put_be16(record + data_offset, 3);
    put_be32(record + data_offset + 4, 1);
    put_be16(record + data_offset + 8, 4);
    memcpy(record + data_offset + 10, root_name, 8);
    return hfs_btree_insert(btree, tx, record, data_offset + 18,
                            compare_catalog, NULL);
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
    CHECK(seed_root(&btree, &tx) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(node_ptr(0) + 20) == 2);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    uint32_t cnid = 0;
    CHECK(hfs_catalog_create_file(&catalog, &tx, 2, name, TEST_NAME_UNITS,
                                  0644, compare_catalog, NULL, &cnid) ==
          HFS_OK);
    CHECK(cnid == 16);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 32) == 2);
    CHECK(get_be32(test_image + 1024 + 64) == 17);
    CHECK(get_be32(node_ptr(0) + 20) == 4);

    /* Verify the file, thread and parent valence through the tree. */
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    uint8_t key[256];
    uint8_t record[1024];
    size_t record_length = 0;
    size_t key_length = build_key(key, 2, name, TEST_NAME_UNITS);
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_OK);
    size_t data_offset = 2u + key_length;
    CHECK(get_be16(record + data_offset) == 2);
    CHECK(get_be32(record + data_offset + 8) == cnid);

    key_length = build_key(key, cnid, NULL, 0);
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_OK);
    data_offset = 2u + key_length;
    CHECK(get_be16(record + data_offset) == 4);
    CHECK(get_be32(record + data_offset + 4) == 2);

    static const uint8_t root_name[8] = {
        0, 'T', 0, 'E', 0, 'S', 0, 'T'
    };
    key_length = build_key(key, 1, root_name, 4);
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_OK);
    data_offset = 2u + key_length;
    CHECK(get_be16(record + data_offset) == 1);
    CHECK(get_be32(record + data_offset + 4) == 1);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    uint8_t payload[1200];
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)(i & 0xffu);
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_catalog_append_file(&catalog, &tx, 2, name, TEST_NAME_UNITS, 0,
                                  payload, sizeof(payload), compare_catalog,
                                  NULL) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 48) == 58);
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    key_length = build_key(key, 2, name, TEST_NAME_UNITS);
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_OK);
    data_offset = 2u + key_length;
    uint8_t *fork = record + data_offset + 88u;
    CHECK(get_be32(fork + 12) == 3);
    CHECK(get_be32(fork + 16) == 41);
    CHECK(get_be32(fork + 20) == 1);
    CHECK(get_be32(fork + 24) == 43);
    CHECK(get_be32(fork + 28) == 2);
    CHECK(memcmp(test_image + 41 * TEST_BLOCK_SIZE, payload, 512) == 0);
    CHECK(memcmp(test_image + 43 * TEST_BLOCK_SIZE, payload + 512, 512) == 0);
    CHECK(memcmp(test_image + 44 * TEST_BLOCK_SIZE, payload + 1024, 176) == 0);

    uint8_t more[600];
    for (size_t i = 0; i < sizeof(more); ++i)
        more[i] = (uint8_t)(0xa0u + (i & 0x0fu));
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_catalog_append_file(&catalog, &tx, 2, name, TEST_NAME_UNITS,
                                  1200, more, sizeof(more), compare_catalog,
                                  NULL) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 48) == 56);
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_OK);
    fork = record + data_offset + 88u;
    CHECK(get_be32(fork + 4) == 1800);
    CHECK(get_be32(fork + 12) == 5);
    CHECK(get_be32(fork + 16) == 41);
    CHECK(get_be32(fork + 20) == 1);
    CHECK(get_be32(fork + 24) == 43);
    CHECK(get_be32(fork + 28) == 4);
    CHECK(get_be32(fork + 32) == 0);
    CHECK(get_be32(fork + 36) == 0);
    CHECK(memcmp(test_image + 45 * TEST_BLOCK_SIZE, more, 512) == 0);
    CHECK(memcmp(test_image + 46 * TEST_BLOCK_SIZE, more + 512, 88) == 0);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_catalog_truncate_file(&catalog, &tx, 2, name, TEST_NAME_UNITS,
                                    compare_catalog, NULL) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 48) == 61);
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_OK);
    fork = record + data_offset + 88u;
    CHECK(get_be32(fork + 12) == 0);
    CHECK(get_be32(fork + 16) == 0);
    CHECK(get_be32(fork + 20) == 0);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_catalog_delete_file(&catalog, &tx, 2, name, TEST_NAME_UNITS,
                                  cnid, compare_catalog, NULL) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 32) == 1);
    CHECK(get_be32(node_ptr(0) + 20) == 2);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    key_length = build_key(key, 2, name, TEST_NAME_UNITS);
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_ERR_NOT_FOUND);
    key_length = build_key(key, cnid, NULL, 0);
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_ERR_NOT_FOUND);
    key_length = build_key(key, 1, root_name, 4);
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_OK);
    data_offset = 2u + key_length;
    CHECK(get_be32(record + data_offset + 4) == 0);

    /* Fragment the bitmap: only 50,52,...,88 are free. */
    uint8_t *bitmap_now = test_image + 18 * TEST_BLOCK_SIZE;
    memset(bitmap_now, 0xff, 16);
    for (uint32_t block = 50; block <= 88; block += 2)
        bitmap_now[block / 8] &= (uint8_t)~(0x80u >> (block % 8));
    put_be32(test_image + 1024 + 48, 20);
    volume.free_blocks = 20;

    static const uint8_t frag_name[8] = {
        0, 'F', 0, 'R', 0, 'A', 0, 'G'
    };
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    uint32_t frag_cnid = 0;
    CHECK(hfs_catalog_create_file(&catalog, &tx, 2, frag_name, 4, 0644,
                                  compare_catalog, NULL, &frag_cnid) ==
          HFS_OK);
    CHECK(frag_cnid == 17);
    apply_txn(&tx);

    uint8_t fragmented[20 * 512];
    for (size_t i = 0; i < sizeof(fragmented); ++i)
        fragmented[i] = (uint8_t)(0x30u + (i & 0x3fu));
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_catalog_append_file(&catalog, &tx, 2, frag_name, 4, 0,
                                  fragmented, sizeof(fragmented),
                                  compare_catalog, NULL) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 48) == 0);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    key_length = build_key(key, 2, frag_name, 4);
    CHECK(hfs_btree_find(&btree, &tx, key + 2, key_length, compare_catalog,
                         NULL, record, sizeof(record), &record_length) ==
          HFS_OK);
    data_offset = 2u + key_length;
    fork = record + data_offset + 88u;
    CHECK(get_be32(fork + 12) == 20);
    for (unsigned int i = 0; i < 8; ++i) {
        CHECK(get_be32(fork + 16 + i * 8) == 50u + i * 2u);
        CHECK(get_be32(fork + 20 + i * 8) == 1);
    }
    struct hfs_extent overflow[8];
    size_t overflow_count = 0;
    CHECK(hfs_extents_overflow_get(&volume, &tx, frag_cnid, 8, overflow, 8,
                                   &overflow_count) == HFS_OK);
    CHECK(overflow_count == 8);
    for (size_t i = 0; i < 8; ++i) {
        CHECK(overflow[i].start == 66u + i * 2u);
        CHECK(overflow[i].count == 1);
    }
    struct hfs_extent overflow2[8];
    size_t overflow2_count = 0;
    CHECK(hfs_extents_overflow_get(&volume, &tx, frag_cnid, 16, overflow2, 8,
                                   &overflow2_count) == HFS_OK);
    CHECK(overflow2_count == 4);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(overflow2[i].start == 82u + i * 2u);
        CHECK(overflow2[i].count == 1);
    }
    CHECK(memcmp(test_image + 50 * TEST_BLOCK_SIZE, fragmented, 512) == 0);
    CHECK(memcmp(test_image + 66 * TEST_BLOCK_SIZE, fragmented + 8 * 512,
                 512) == 0);
    CHECK(memcmp(test_image + 82 * TEST_BLOCK_SIZE, fragmented + 16 * 512,
                 512) == 0);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_catalog_truncate_file(&catalog, &tx, 2, frag_name, 4,
                                    compare_catalog, NULL) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 48) == 20);
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_extents_overflow_get(&volume, &tx, frag_cnid, 8, overflow, 8,
                                   &overflow_count) == HFS_ERR_NOT_FOUND);
    CHECK(hfs_extents_overflow_get(&volume, &tx, frag_cnid, 16, overflow, 8,
                                   &overflow_count) == HFS_ERR_NOT_FOUND);

    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_catalog_delete_file(&catalog, &tx, 2, frag_name, 4, frag_cnid,
                                  compare_catalog, NULL) == HFS_OK);
    apply_txn(&tx);
    CHECK(get_be32(test_image + 1024 + 32) == 1);
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
