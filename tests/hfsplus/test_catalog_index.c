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

static size_t put_index(uint8_t *record, uint32_t parent, uint32_t child)
{
    size_t key = put_key(record, parent, "");
    test_image_put_be32(record + key + 2, child);
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
    test_image_put_be32(header + 6, 3);      /* leaf records */
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
    size_t index0 = put_index(index + 14, 2, 2);
    size_t index1 = put_index(index + 14 + index0, 4, 3);
    test_image_put_be16(index + 510, 14);
    test_image_put_be16(index + 508, (uint16_t)(14 + index0));
    test_image_put_be16(index + 506, (uint16_t)(14 + index0 + index1));

    uint8_t *leaf2 = test_image + (CATALOG_BLOCK + 2) * NODE_SIZE;
    memset(leaf2, 0, NODE_SIZE);
    leaf2[8] = 0xff;
    leaf2[9] = 1;
    test_image_put_be16(leaf2 + 10, 2);
    test_image_put_be32(leaf2, 3);           /* next leaf */
    test_image_put_be32(leaf2 + 4, 0);       /* previous leaf */
    size_t folder = put_folder(leaf2 + 14, 2, ".firmware", 3);
    size_t file = put_file(leaf2 + 14 + folder, 3, "current.img", 4);
    test_image_put_be16(leaf2 + 510, 14);
    test_image_put_be16(leaf2 + 508, (uint16_t)(14 + folder));
    test_image_put_be16(leaf2 + 506, (uint16_t)(14 + folder + file));

    uint8_t *leaf3 = test_image + (CATALOG_BLOCK + 3) * NODE_SIZE;
    memset(leaf3, 0, NODE_SIZE);
    leaf3[8] = 0xff;
    leaf3[9] = 1;
    test_image_put_be16(leaf3 + 10, 1);
    test_image_put_be32(leaf3, 0);
    test_image_put_be32(leaf3 + 4, 2);
    size_t dummy = put_folder(leaf3 + 14, 4, "dummy", 5);
    test_image_put_be16(leaf3 + 510, 14);
    test_image_put_be16(leaf3 + 508, (uint16_t)(14 + dummy));

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
    CHECK(hfs_catalog_lookup_ascii(&catalog, 4, "dummy", &entry) == HFS_OK);
    CHECK(entry.id == 5);
    CHECK(hfs_catalog_lookup_ascii(&catalog, 5, "current.img", &entry) ==
          HFS_ERR_NOT_FOUND);
    CHECK(hfs_catalog_lookup(&catalog, "/.firmware/missing", &entry) ==
          HFS_ERR_NOT_FOUND);
    return 0;
}



static void make_deep_catalog(void)
{
    const uint32_t nodes = 8;
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4,
                           TEST_ATTR_UNMOUNTED, 512, 128, 64);

    uint8_t *fork = test_image + 1024 + 272;
    test_image_put_be64(fork, (uint64_t)nodes * NODE_SIZE);
    test_image_put_be32(fork + 12, nodes);
    test_image_put_be32(fork + 16, CATALOG_BLOCK);
    test_image_put_be32(fork + 20, nodes);

    uint8_t *header_node = test_image + CATALOG_BLOCK * NODE_SIZE;
    memset(header_node, 0, NODE_SIZE);
    header_node[8] = 1;
    header_node[9] = 0;
    test_image_put_be16(header_node + 10, 3);
    uint8_t *header = header_node + 14;
    test_image_put_be16(header, 3);          /* depth */
    test_image_put_be32(header + 2, 1);      /* root index */
    test_image_put_be32(header + 6, 5);      /* leaf records */
    test_image_put_be32(header + 10, 4);     /* first leaf */
    test_image_put_be32(header + 14, 7);     /* last leaf */
    test_image_put_be16(header + 18, NODE_SIZE);
    test_image_put_be16(header + 20, 516);
    test_image_put_be32(header + 22, nodes);
    test_image_put_be32(header + 26, 0);
    test_image_put_be32(header + 38, 6);
    header[36] = 0;
    header[37] = 0xcf;
    test_image_put_be16(header_node + 504, 504);
    test_image_put_be16(header_node + 506, 248);
    test_image_put_be16(header_node + 508, 120);
    test_image_put_be16(header_node + 510, 14);

    /* Root index: two level-1 index nodes. */
    uint8_t *root = test_image + (CATALOG_BLOCK + 1) * NODE_SIZE;
    memset(root, 0, NODE_SIZE);
    root[8] = 0;
    root[9] = 2;
    test_image_put_be16(root + 10, 2);
    size_t r0 = put_index(root + 14, 2, 2);
    size_t r1 = put_index(root + 14 + r0, 100, 3);
    test_image_put_be16(root + 510, 14);
    test_image_put_be16(root + 508, (uint16_t)(14 + r0));
    test_image_put_be16(root + 506, (uint16_t)(14 + r0 + r1));

    /* Level-1 index node 2: leaves 4 and 5. */
    uint8_t *idx2 = test_image + (CATALOG_BLOCK + 2) * NODE_SIZE;
    memset(idx2, 0, NODE_SIZE);
    idx2[8] = 0;
    idx2[9] = 1;
    test_image_put_be16(idx2 + 10, 2);
    size_t i20 = put_index(idx2 + 14, 2, 4);
    size_t i21 = put_index(idx2 + 14 + i20, 20, 5);
    test_image_put_be16(idx2 + 510, 14);
    test_image_put_be16(idx2 + 508, (uint16_t)(14 + i20));
    test_image_put_be16(idx2 + 506, (uint16_t)(14 + i20 + i21));

    /* Level-1 index node 3: leaves 6 and 7. */
    uint8_t *idx3 = test_image + (CATALOG_BLOCK + 3) * NODE_SIZE;
    memset(idx3, 0, NODE_SIZE);
    idx3[8] = 0;
    idx3[9] = 1;
    test_image_put_be16(idx3 + 10, 2);
    size_t i30 = put_index(idx3 + 14, 100, 6);
    size_t i31 = put_index(idx3 + 14 + i30, 200, 7);
    test_image_put_be16(idx3 + 510, 14);
    test_image_put_be16(idx3 + 508, (uint16_t)(14 + i30));
    test_image_put_be16(idx3 + 506, (uint16_t)(14 + i30 + i31));

    /* Leaves 4..7, globally linked. */
    const uint32_t parents[4] = { 2, 20, 100, 200 };
    const char *names[4] = { ".firmware", "twenty", "hundred",
                             "twohundred" };
    const uint32_t ids[4] = { 3, 20, 100, 200 };
    for (unsigned int i = 0; i < 4; ++i) {
        uint8_t *leaf = test_image + (CATALOG_BLOCK + 4 + i) * NODE_SIZE;
        memset(leaf, 0, NODE_SIZE);
        leaf[8] = 0xff;
        leaf[9] = 1;
        test_image_put_be16(leaf + 10, i == 0 ? 2 : 1);
        test_image_put_be32(leaf, i < 3 ? 5 + i : 0);
        test_image_put_be32(leaf + 4, i == 0 ? 0 : 3 + i);
        size_t used = put_folder(leaf + 14, parents[i], names[i], ids[i]);
        if (i == 0) {
            size_t child = put_file(leaf + 14 + used, 3, "current.img", 4);
            test_image_put_be16(leaf + 508, (uint16_t)(14 + used));
            test_image_put_be16(leaf + 506, (uint16_t)(14 + used + child));
            test_image_put_be16(leaf + 510, 14);
        } else {
            test_image_put_be16(leaf + 508, (uint16_t)(14 + used));
            test_image_put_be16(leaf + 510, 14);
        }
    }
    memcpy(test_image + FILE_DATA_BLOCK * NODE_SIZE, "HELO", 4);
}

static int test_deep_index_lookup(void)
{
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_entry entry;
    uint8_t scratch[NODE_SIZE];

    make_deep_catalog();
    CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0,
                           TEST_IMAGE_SIZE) == HFS_OK);
    CHECK(hfs_catalog_open(&catalog, &volume, scratch,
                           sizeof(scratch)) == HFS_OK);

    CHECK(hfs_catalog_lookup_ascii(&catalog, 2, ".firmware", &entry) ==
          HFS_OK);
    CHECK(entry.id == 3);
    CHECK(hfs_catalog_lookup_ascii(&catalog, 3, "current.img", &entry) == HFS_OK);
    CHECK(entry.id == 4);
    CHECK(hfs_catalog_lookup_ascii(&catalog, 20, "twenty", &entry) == HFS_OK);
    CHECK(entry.id == 20);
    CHECK(hfs_catalog_lookup_ascii(&catalog, 100, "hundred", &entry) == HFS_OK);
    CHECK(entry.id == 100);
    CHECK(hfs_catalog_lookup_ascii(&catalog, 200, "twohundred", &entry) == HFS_OK);
    CHECK(entry.id == 200);
    CHECK(hfs_catalog_lookup_ascii(&catalog, 50, "twenty", &entry) ==
          HFS_ERR_NOT_FOUND);
    CHECK(hfs_catalog_lookup_ascii(&catalog, 201, "twohundred", &entry) ==
          HFS_ERR_NOT_FOUND);
    return 0;
}

static uint32_t fuzz_state = 0x12345678u;

static uint32_t fuzz_next(void)
{
    fuzz_state = fuzz_state * 1664525u + 1013904223u;
    return fuzz_state;
}

static int test_malformed_index_fuzz(void)
{
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_entry entry;
    uint8_t scratch[NODE_SIZE];
    const size_t catalog_start = CATALOG_BLOCK * NODE_SIZE;
    const size_t catalog_size = TOTAL_NODES * NODE_SIZE;

    for (unsigned int iteration = 0; iteration < 1000; ++iteration) {
        make_catalog();

        /* Mutate catalog bytes or the catalog fork, not the volume header. */
        unsigned int mutations = 1u + fuzz_next() % 3u;
        for (unsigned int m = 0; m < mutations; ++m) {
            size_t offset;
            if (fuzz_next() & 1u)
                offset = 1024u + 272u + fuzz_next() % 80u;
            else
                offset = catalog_start + fuzz_next() % catalog_size;
            test_image[offset] ^= (uint8_t)(1u << (fuzz_next() & 7u));
        }

        if (hfs_volume_probe(&volume, test_image_read, NULL, 0,
                             TEST_IMAGE_SIZE) != HFS_OK)
            continue;
        if (hfs_catalog_open(&catalog, &volume, scratch,
                             sizeof(scratch)) != HFS_OK)
            continue;

        (void)hfs_catalog_lookup_ascii(&catalog, 2, ".firmware", &entry);
        (void)hfs_catalog_lookup_ascii(&catalog, 3, "current.img", &entry);
        (void)hfs_catalog_lookup_ascii(&catalog, 4, "dummy", &entry);
        (void)hfs_catalog_lookup_ascii(&catalog, 5, "missing", &entry);
        (void)hfs_catalog_lookup(&catalog, "/.firmware/current.img", &entry);
    }
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "indexed nested lookup", test_indexed_nested_lookup },
        { "deep index lookup", test_deep_index_lookup },
        { "malformed index fuzz", test_malformed_index_fuzz }
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
