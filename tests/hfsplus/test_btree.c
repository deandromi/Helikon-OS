/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_btree.h"
#include "hfs_volume.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <string.h>

#define CATALOG_NODE_BLOCK 8u
#define CATALOG_NODE_SIZE 512u
#define CATALOG_TOTAL_NODES 4u

static void make_catalog_fork(uint8_t *header, uint32_t blocks)
{
    uint8_t *fork = header + 272;
    test_image_put_be64(fork, (uint64_t)blocks * CATALOG_NODE_SIZE);
    test_image_put_be32(fork + 12, blocks);
    test_image_put_be32(fork + 16, CATALOG_NODE_BLOCK);
    test_image_put_be32(fork + 20, blocks);
}

static uint8_t *make_btree_fixture(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED, 512,
                           128, 64);
    make_catalog_fork(test_image + 1024, CATALOG_TOTAL_NODES);

    uint8_t *node = test_image + CATALOG_NODE_BLOCK * CATALOG_NODE_SIZE;
    memset(node, 0, CATALOG_NODE_SIZE);

    node[8] = 1; /* header node */
    node[9] = 0; /* height */
    test_image_put_be16(node + 10, 3);

    uint8_t *header = node + 14;
    test_image_put_be16(header, 1);                 /* depth */
    test_image_put_be32(header + 2, 1);             /* root node */
    test_image_put_be32(header + 6, 1);             /* leaf records */
    test_image_put_be32(header + 10, 1);            /* first leaf */
    test_image_put_be32(header + 14, 1);            /* last leaf */
    test_image_put_be16(header + 18, CATALOG_NODE_SIZE);
    test_image_put_be16(header + 20, 516);          /* max key length */
    test_image_put_be32(header + 22, CATALOG_TOTAL_NODES);
    test_image_put_be32(header + 26, 1);            /* free nodes */
    test_image_put_be32(header + 38, 6);            /* catalog attributes */
    header[36] = 0;                                 /* btree type */
    header[37] = 0xcf;                              /* catalog key compare */

    /* HFS+ stores the offset table in reverse: record 0 is the last word. */
    test_image_put_be16(node + 504, 504);
    test_image_put_be16(node + 506, 248);
    test_image_put_be16(node + 508, 120);
    test_image_put_be16(node + 510, 14);
    return node;
}

static int test_valid_btree(void)
{
    struct hfs_volume volume;
    struct hfs_btree btree;
    uint8_t scratch[CATALOG_NODE_SIZE];
    const uint8_t *node = NULL;

    make_btree_fixture();
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                        sizeof(scratch));
    CHECK(rc == HFS_OK);
    CHECK(btree.node_size == CATALOG_NODE_SIZE);
    CHECK(btree.total_nodes == CATALOG_TOTAL_NODES);
    CHECK(btree.root_node == 1);
    CHECK(btree.depth == 1);
    CHECK(btree.key_compare == 0xcf);
    CHECK(btree.btree_type == 0);
    rc = hfs_btree_read_node(&btree, 1, &node);
    CHECK(rc == HFS_OK);
    CHECK(node != NULL);
    return 0;
}

static int test_bad_node_size(void)
{
    struct hfs_volume volume;
    struct hfs_btree btree;
    uint8_t scratch[CATALOG_NODE_SIZE];

    uint8_t *node = make_btree_fixture();
    test_image_put_be16(node + 32, 1000);
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                        sizeof(scratch));
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_fork_size_mismatch(void)
{
    struct hfs_volume volume;
    struct hfs_btree btree;
    uint8_t scratch[CATALOG_NODE_SIZE];

    uint8_t *node = make_btree_fixture();
    test_image_put_be32(node + 14 + 22, CATALOG_TOTAL_NODES + 1);
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                        sizeof(scratch));
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_bad_offset_table(void)
{
    struct hfs_volume volume;
    struct hfs_btree btree;
    uint8_t scratch[CATALOG_NODE_SIZE];

    uint8_t *node = make_btree_fixture();
    test_image_put_be16(node + 506, 10);
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                        sizeof(scratch));
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_wrong_header_kind(void)
{
    struct hfs_volume volume;
    struct hfs_btree btree;
    uint8_t scratch[CATALOG_NODE_SIZE];

    uint8_t *node = make_btree_fixture();
    node[8] = 2;
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                        sizeof(scratch));
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_root_out_of_range(void)
{
    struct hfs_volume volume;
    struct hfs_btree btree;
    uint8_t scratch[CATALOG_NODE_SIZE];

    uint8_t *node = make_btree_fixture();
    test_image_put_be32(node + 16, CATALOG_TOTAL_NODES);
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                        sizeof(scratch));
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_non_inline_fork(void)
{
    struct hfs_volume volume;
    struct hfs_btree btree;
    uint8_t scratch[CATALOG_NODE_SIZE];

    make_btree_fixture();
    test_image_put_be32(test_image + 1024 + 272 + 12,
                        CATALOG_TOTAL_NODES + 1);
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                        sizeof(scratch));
    CHECK(rc == HFS_ERR_UNSUPPORTED);
    return 0;
}

static int test_scratch_too_small(void)
{
    struct hfs_volume volume;
    struct hfs_btree btree;
    uint8_t scratch[256];

    make_btree_fixture();
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_btree_open(&btree, &volume, &volume.catalog, scratch,
                        sizeof(scratch));
    CHECK(rc == HFS_ERR_BUFFER);
    return 0;
}

static int test_bad_arguments(void)
{
    struct hfs_btree btree;
    struct hfs_volume volume;
    uint8_t scratch[CATALOG_NODE_SIZE];

    make_btree_fixture();
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    CHECK(hfs_btree_open(NULL, &volume, &volume.catalog, scratch,
                        sizeof(scratch)) == HFS_ERR_ARGUMENT);
    CHECK(hfs_btree_open(&btree, NULL, &volume.catalog, scratch,
                        sizeof(scratch)) == HFS_ERR_ARGUMENT);
    CHECK(hfs_btree_open(&btree, &volume, NULL, scratch,
                        sizeof(scratch)) == HFS_ERR_ARGUMENT);
    CHECK(hfs_btree_open(&btree, &volume, &volume.catalog, NULL,
                        sizeof(scratch)) == HFS_ERR_ARGUMENT);
    CHECK(hfs_btree_read_node(&btree, 0, NULL) == HFS_ERR_ARGUMENT);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"valid btree", test_valid_btree},
        {"bad node size", test_bad_node_size},
        {"fork size mismatch", test_fork_size_mismatch},
        {"bad offset table", test_bad_offset_table},
        {"wrong header kind", test_wrong_header_kind},
        {"root out of range", test_root_out_of_range},
        {"non-inline fork", test_non_inline_fork},
        {"scratch too small", test_scratch_too_small},
        {"bad arguments", test_bad_arguments}
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
