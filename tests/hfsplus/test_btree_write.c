/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_btree_write.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NODE_SIZE 512u

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void make_node(uint8_t *node, uint16_t count, uint16_t free_offset)
{
    memset(node, 0, NODE_SIZE);
    node[8] = 0xff;
    node[9] = 1;
    put16(node + 10, count);
    put16(node + NODE_SIZE - 2u * (count + 1u), free_offset);
}

static void set_record(uint8_t *node, uint16_t index, uint16_t offset)
{
    put16(node + NODE_SIZE - 2u * (index + 1u), offset);
}

static int test_insert_middle(void)
{
    uint8_t node[NODE_SIZE];
    make_node(node, 2, 22);
    memcpy(node + 14, "AAAA", 4);
    memcpy(node + 18, "BBBB", 4);
    set_record(node, 0, 14);
    set_record(node, 1, 18);

    CHECK(hfs_btree_node_insert(node, NODE_SIZE, 1, "CCCCCC", 6) == HFS_OK);
    CHECK(get16(node + 10) == 3);
    CHECK(memcmp(node + 14, "AAAA", 4) == 0);
    CHECK(memcmp(node + 18, "CCCCCC", 6) == 0);
    CHECK(memcmp(node + 24, "BBBB", 4) == 0);
    CHECK(get16(node + NODE_SIZE - 2) == 14);
    CHECK(get16(node + NODE_SIZE - 4) == 18);
    CHECK(get16(node + NODE_SIZE - 6) == 24);
    CHECK(get16(node + NODE_SIZE - 8) == 28);
    return 0;
}

static int test_delete_middle(void)
{
    uint8_t node[NODE_SIZE];
    make_node(node, 2, 22);
    memcpy(node + 14, "AAAA", 4);
    memcpy(node + 18, "BBBB", 4);
    set_record(node, 0, 14);
    set_record(node, 1, 18);

    CHECK(hfs_btree_node_delete(node, NODE_SIZE, 0) == HFS_OK);
    CHECK(get16(node + 10) == 1);
    CHECK(memcmp(node + 14, "BBBB", 4) == 0);
    CHECK(get16(node + NODE_SIZE - 2) == 14);
    CHECK(get16(node + NODE_SIZE - 4) == 18);
    return 0;
}

static int test_insert_then_delete(void)
{
    uint8_t node[NODE_SIZE];
    make_node(node, 2, 22);
    memcpy(node + 14, "AAAA", 4);
    memcpy(node + 18, "BBBB", 4);
    set_record(node, 0, 14);
    set_record(node, 1, 18);

    CHECK(hfs_btree_node_insert(node, NODE_SIZE, 2, "CCCC", 4) == HFS_OK);
    CHECK(hfs_btree_node_delete(node, NODE_SIZE, 2) == HFS_OK);
    CHECK(get16(node + 10) == 2);
    CHECK(memcmp(node + 14, "AAAA", 4) == 0);
    CHECK(memcmp(node + 18, "BBBB", 4) == 0);
    return 0;
}

static int test_no_space(void)
{
    uint8_t node[NODE_SIZE];
    make_node(node, 1, 494);
    memset(node + 14, 0x11, 480);
    set_record(node, 0, 14);

    CHECK(hfs_btree_node_insert(node, NODE_SIZE, 1, "CCCCCCCCCCCCCC", 14) ==
          HFS_ERR_NO_SPACE);
    return 0;
}

static int test_bad_arguments(void)
{
    uint8_t node[NODE_SIZE];
    make_node(node, 0, 14);
    CHECK(hfs_btree_node_insert(node, NODE_SIZE, 0, "AA", 2) == HFS_OK);
    make_node(node, 0, 14);
    CHECK(hfs_btree_node_insert(node, NODE_SIZE, 1, "AA", 2) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_btree_node_insert(node, NODE_SIZE, 0, "AA", 3) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_btree_node_delete(node, NODE_SIZE, 0) == HFS_ERR_ARGUMENT);
    return 0;
}

static int test_split_leaf(void)
{
    uint8_t left[NODE_SIZE];
    uint8_t right[NODE_SIZE];
    const uint8_t *separator = NULL;
    size_t separator_length = 0;
    const uint8_t rec_a[4] = { 0x00, 0x02, 'A', 'A' };
    const uint8_t rec_b[4] = { 0x00, 0x02, 'B', 'B' };
    const uint8_t rec_c[4] = { 0x00, 0x02, 'C', 'C' };
    const uint8_t rec_d[4] = { 0x00, 0x02, 'D', 'D' };

    make_node(left, 4, 30);
    memcpy(left + 14, rec_a, 4);
    memcpy(left + 18, rec_b, 4);
    memcpy(left + 22, rec_c, 4);
    memcpy(left + 26, rec_d, 4);
    set_record(left, 0, 14);
    set_record(left, 1, 18);
    set_record(left, 2, 22);
    set_record(left, 3, 26);
    memset(right, 0, sizeof(right));

    CHECK(hfs_btree_node_split(left, right, NODE_SIZE, 2, 1, 2,
                               &separator, &separator_length) == HFS_OK);
    CHECK(get16(left + 10) == 2);
    CHECK(get16(right + 10) == 2);
    CHECK(get32(left + 0) == 2);
    CHECK(get32(right + 4) == 1);
    CHECK(memcmp(left + 14, rec_a, 4) == 0);
    CHECK(memcmp(left + 18, rec_b, 4) == 0);
    CHECK(memcmp(right + 14, rec_c, 4) == 0);
    CHECK(memcmp(right + 18, rec_d, 4) == 0);
    CHECK(separator_length == 2);
    CHECK(memcmp(separator, "CC", 2) == 0);
    return 0;
}

static int test_split_arguments(void)
{
    uint8_t left[NODE_SIZE];
    uint8_t right[NODE_SIZE];
    const uint8_t *separator = NULL;
    size_t separator_length = 0;

    make_node(left, 2, 22);
    memcpy(left + 14, "AAAA", 4);
    memcpy(left + 18, "BBBB", 4);
    set_record(left, 0, 14);
    set_record(left, 1, 18);
    CHECK(hfs_btree_node_split(left, right, NODE_SIZE, 0, 1, 2,
                               &separator, &separator_length) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_btree_node_split(left, right, NODE_SIZE, 2, 1, 2,
                               &separator, &separator_length) ==
          HFS_ERR_ARGUMENT);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "insert middle", test_insert_middle },
        { "delete middle", test_delete_middle },
        { "insert then delete", test_insert_then_delete },
        { "no space", test_no_space },
        { "split leaf", test_split_leaf },
        { "split arguments", test_split_arguments },
        { "bad arguments", test_bad_arguments }
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
