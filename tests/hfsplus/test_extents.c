/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_extents.h"
#include "hfs_volume.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <string.h>

#define EXTENTS_BLOCK 12u
#define EXTENTS_NODE_SIZE 512u
#define EXTENTS_TOTAL_NODES 4u
#define FILE_ID 4u

static void make_extents_fixture(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED,
                           512, 128, 64);

    uint8_t *fork = test_image + 1024 + 192;
    test_image_put_be64(fork, EXTENTS_TOTAL_NODES * EXTENTS_NODE_SIZE);
    test_image_put_be32(fork + 12, EXTENTS_TOTAL_NODES);
    test_image_put_be32(fork + 16, EXTENTS_BLOCK);
    test_image_put_be32(fork + 20, EXTENTS_TOTAL_NODES);

    uint8_t *header_node = test_image + EXTENTS_BLOCK * EXTENTS_NODE_SIZE;
    memset(header_node, 0, EXTENTS_NODE_SIZE);
    header_node[8] = 1;
    header_node[9] = 0;
    test_image_put_be16(header_node + 10, 3);

    uint8_t *header = header_node + 14;
    test_image_put_be16(header, 1);
    test_image_put_be32(header + 2, 1);
    test_image_put_be32(header + 6, 1);
    test_image_put_be32(header + 10, 1);
    test_image_put_be32(header + 14, 1);
    test_image_put_be16(header + 18, EXTENTS_NODE_SIZE);
    test_image_put_be16(header + 20, 10);
    test_image_put_be32(header + 22, EXTENTS_TOTAL_NODES);
    test_image_put_be32(header + 26, 1);
    test_image_put_be32(header + 38, 6);
    header[37] = 0xbc; /* extents key compare */

    test_image_put_be16(header_node + 504, 504);
    test_image_put_be16(header_node + 506, 248);
    test_image_put_be16(header_node + 508, 120);
    test_image_put_be16(header_node + 510, 14);

    uint8_t *leaf = test_image + 13 * EXTENTS_NODE_SIZE;
    memset(leaf, 0, EXTENTS_NODE_SIZE);
    leaf[8] = 0xff;
    leaf[9] = 1;
    test_image_put_be16(leaf + 10, 1);

    uint8_t *record = leaf + 14;
    test_image_put_be16(record, 10);
    record[2] = 0; /* data fork */
    record[3] = 0;
    test_image_put_be32(record + 4, FILE_ID);
    test_image_put_be32(record + 8, 1); /* logical start block */
    test_image_put_be32(record + 12, 20);
    test_image_put_be32(record + 16, 1);

    test_image_put_be16(leaf + 510, 14);
    test_image_put_be16(leaf + 508, 90);

    memset(test_image + 16 * 512, 'A', 512);
    memset(test_image + 20 * 512, 'B', 512);
}

static struct hfs_fork make_file_fork(void)
{
    struct hfs_fork fork;
    memset(&fork, 0, sizeof(fork));
    fork.size = 516;
    fork.blocks = 2;
    fork.extents[0].start = 16;
    fork.extents[0].count = 1;
    return fork;
}

static int test_extents_read(void)
{
    struct hfs_volume volume;
    struct hfs_extents extents;
    struct hfs_fork fork = make_file_fork();
    uint8_t scratch[EXTENTS_NODE_SIZE];
    char data[16];
    size_t done = 0;

    make_extents_fixture();
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_extents_open(&extents, &volume, scratch, sizeof(scratch));
    CHECK(rc == HFS_OK);

    struct hfs_extent collected[4];
    size_t count = 0;
    rc = hfs_extents_collect(&extents, FILE_ID, 1, 2, collected, 4, &count);
    CHECK(rc == HFS_OK);
    CHECK(count == 1);
    CHECK(collected[0].start == 20);
    CHECK(collected[0].count == 1);

    memset(data, 0, sizeof(data));
    rc = hfs_extents_read(&extents, &fork, FILE_ID, 0, data, 8, &done);
    CHECK(rc == HFS_OK);
    CHECK(done == 8);
    CHECK(memcmp(data, "AAAAAAAA", 8) == 0);

    rc = hfs_extents_read(&extents, &fork, FILE_ID, 512, data, 4, &done);
    CHECK(rc == HFS_OK);
    CHECK(done == 4);
    CHECK(memcmp(data, "BBBB", 4) == 0);

    rc = hfs_extents_read(&extents, &fork, FILE_ID, 510, data, 8, &done);
    CHECK(rc == HFS_OK);
    CHECK(done == 6);
    CHECK(memcmp(data, "AABBBB", 6) == 0);

    rc = hfs_extents_read(&extents, &fork, FILE_ID, 516, data, 4, &done);
    CHECK(rc == HFS_OK);
    CHECK(done == 0);
    return 0;
}

static int test_collect_mismatch(void)
{
    struct hfs_volume volume;
    struct hfs_extents extents;
    uint8_t scratch[EXTENTS_NODE_SIZE];
    struct hfs_extent collected[4];
    size_t count = 0;

    make_extents_fixture();
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_extents_open(&extents, &volume, scratch, sizeof(scratch));
    CHECK(rc == HFS_OK);
    CHECK(hfs_extents_collect(&extents, FILE_ID, 1, 3, collected, 4, &count) ==
          HFS_ERR_FORMAT);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"extents read", test_extents_read},
        {"collect mismatch", test_collect_mismatch}
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
