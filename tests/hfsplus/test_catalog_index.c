/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog.h"
#include "hfs_volume.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <string.h>

#define CATALOG_BLOCK 8u
#define NODE_SIZE 512u
#define TOTAL_NODES 4u
#define FILE_DATA_BLOCK 16u

static size_t put_key(uint8_t *record, uint32_t parent, const char *name)
{
    size_t units = strlen(name);
    test_image_put_be16(record, (uint16_t)(6u + units * 2u));
    test_image_put_be32(record + 2, parent);
    test_image_put_be16(record + 6, (uint16_t)units);
    for (size_t i = 0; i < units; ++i)
        test_image_put_be16(record + 8 + i * 2, (uint8_t)name[i]);
    return 6u + units * 2u;
}

static size_t put_folder(uint8_t *record, uint32_t parent, const char *name,
                         uint32_t id)
{
    size_t key = put_key(record, parent, name);
    uint8_t *data = record + key + 2;
    memset(data, 0, 88);
    test_image_put_be16(data, 1);
    test_image_put_be32(data + 8, id);
    test_image_put_be16(data + 42, 0040000);
    return key + 2u + 88u;
}

static size_t put_file(uint8_t *record, uint32_t parent, const char *name,
                       uint32_t id)
{
    size_t key = put_key(record, parent, name);
    uint8_t *data = record + key + 2;
    memset(data, 0, 248);
    test_image_put_be16(data, 2);
    test_image_put_be32(data + 8, id);
    test_image_put_be16(data + 42, 0100000);
    test_image_put_be64(data + 88, 4);
    test_image_put_be32(data + 88 + 12, 1);
    test_image_put_be32(data + 88 + 16, FILE_DATA_BLOCK);
    test_image_put_be32(data + 88 + 20, 1);
    return key + 2u + 248u;
}

static size_t put_index(uint8_t *record, uint32_t parent)
{
    size_t key = put_key(record, parent, "");
    test_image_put_be32(record + key + 2, parent);
    return key + 2u + 4u;
}

static void make_catalog(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4,
                           TEST_ATTR_UNMOUNTED, 512, 128, 64);

    uint8_t *fork = test_image + 1024 + 272;
    test_image_put_be64(fork, TOTAL_NODES * NODE_SIZE);
    test_image_put_be32(fork + 12, TOTAL_NODES);
    test_image_put_be32(fork + 16, CATALOG_BLOCK);
    test_image_put_be32(fork + 20, TOTAL_NODES);

    uint8_t *header_node = test_image + CATALOG_BLOCK * NODE_SIZE;
    memset(header_node, 0, NODE_SIZE);
    header_node[8] = 1;
    header_node[9] = 0;
    test_image_put_be16(header_node + 10, 3);

    uint8_t *header = header_node + 14;
    test_image_put_be16(header, 2);          /* depth */
    test_image_put_be32(header + 2, 1);      /* root index node */
    test_image_put_be32(header + 6, 2);      /* leaf records */
    test_image_put_be32(header + 10, 2);     /* first leaf */
    test_image_put_be32(header + 14, 3);     /* last leaf */
    test_image_put_be16(header + 18, NODE_SIZE);
    test_image_put_be16(header + 20, 516);
    test_image_put_be32(header + 22, TOTAL_NODES);
    test_image_put_be32(header + 26, 1);
    test_image_put_be32(header + 38, 6);
    header[36] = 0;
    header[37] = 0xcf;

    test_image_put_be16(header_node + 504, 504);
    test_image_put_be16(header_node + 506, 248);
    test_image_put_be16(header_node + 508, 120);
    test_image_put_be16(header_node + 510, 14);

    uint8_t *index = test_image + (CATALOG_BLOCK + 1) * NODE_SIZE;
    memset(index, 0, NODE_SIZE);
    index[8] = 0;
    index[9] = 1;
    test_image_put_be16(index + 10, 2);
    size_t index0 = put_index(index + 14, 2);
    size_t index1 = put_index(index + 14 + index0, 3);
    test_image_put_be16(index + 510, 14);
    test_image_put_be16(index + 508, (uint16_t)(14 + index0));
    test_image_put_be16(index + 506, (uint16_t)(14 + index0 + index1));

    uint8_t *leaf2 = test_image + (CATALOG_BLOCK + 2) * NODE_SIZE;
    memset(leaf2, 0, NODE_SIZE);
    leaf2[8] = 0xff;
    leaf2[9] = 1;
    test_image_put_be16(leaf2 + 10, 1);
    test_image_put_be32(leaf2, 3);           /* next leaf */
    test_image_put_be32(leaf2 + 4, 0);       /* previous leaf */
    size_t folder = put_folder(leaf2 + 14, 2, ".firmware", 3);
    test_image_put_be16(leaf2 + 510, 14);
    test_image_put_be16(leaf2 + 508, (uint16_t)(14 + folder));

    uint8_t *leaf3 = test_image + (CATALOG_BLOCK + 3) * NODE_SIZE;
    memset(leaf3, 0, NODE_SIZE);
    leaf3[8] = 0xff;
    leaf3[9] = 1;
    test_image_put_be16(leaf3 + 10, 1);
    test_image_put_be32(leaf3, 0);
    test_image_put_be32(leaf3 + 4, 2);
    size_t file = put_file(leaf3 + 14, 3, "current.img", 4);
    test_image_put_be16(leaf3 + 510, 14);
    test_image_put_be16(leaf3 + 508, (uint16_t)(14 + file));

    memcpy(test_image + FILE_DATA_BLOCK * NODE_SIZE, "HELO", 4);
}

static int test_indexed_nested_lookup(void)
{
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_entry entry;
    uint8_t scratch[NODE_SIZE];
    char data[4];
    size_t done = 0;

    make_catalog();
    CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0,
                           TEST_IMAGE_SIZE) == HFS_OK);
    CHECK(hfs_catalog_open(&catalog, &volume, scratch,
                           sizeof(scratch)) == HFS_OK);

    CHECK(hfs_catalog_lookup(&catalog, "/.firmware/current.img", &entry) ==
          HFS_OK);
    CHECK(entry.kind == 2);
    CHECK(entry.id == 4);
    CHECK(hfs_catalog_read_file(&catalog, &entry, 0, data, sizeof(data),
                                &done) == HFS_OK);
    CHECK(done == 4);
    CHECK(memcmp(data, "HELO", 4) == 0);

    CHECK(hfs_catalog_lookup_ascii(&catalog, 3, "current.img", &entry) ==
          HFS_OK);
    CHECK(entry.id == 4);
    CHECK(hfs_catalog_lookup_ascii(&catalog, 4, "current.img", &entry) ==
          HFS_ERR_NOT_FOUND);
    CHECK(hfs_catalog_lookup(&catalog, "/.firmware/missing", &entry) ==
          HFS_ERR_NOT_FOUND);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "indexed nested lookup", test_indexed_nested_lookup }
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
