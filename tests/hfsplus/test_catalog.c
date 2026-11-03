/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog.h"
#include "hfs_volume.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <string.h>

#define CATALOG_BLOCK 8u
#define CATALOG_NODE_SIZE 512u
#define CATALOG_TOTAL_NODES 4u
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
                       uint32_t id, uint32_t blocks, uint8_t **fork_out)
{
    size_t key = put_key(record, parent, name);
    uint8_t *data = record + key + 2;
    memset(data, 0, 248);
    test_image_put_be16(data, 2);
    test_image_put_be32(data + 8, id);
    test_image_put_be16(data + 42, 0100000);
    test_image_put_be64(data + 88, 4);
    test_image_put_be32(data + 88 + 12, blocks);
    test_image_put_be32(data + 88 + 16, FILE_DATA_BLOCK);
    test_image_put_be32(data + 88 + 20, 1);
    if (fork_out != NULL)
        *fork_out = data + 88;
    return key + 2u + 248u;
}

struct catalog_fixture {
    uint8_t *leaf;
    uint8_t *file_fork;
};

static struct catalog_fixture make_catalog_fixture(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED,
                           512, 128, 64);

    uint8_t *fork = test_image + 1024 + 272;
    test_image_put_be64(fork, CATALOG_TOTAL_NODES * CATALOG_NODE_SIZE);
    test_image_put_be32(fork + 12, CATALOG_TOTAL_NODES);
    test_image_put_be32(fork + 16, CATALOG_BLOCK);
    test_image_put_be32(fork + 20, CATALOG_TOTAL_NODES);

    uint8_t *header_node = test_image + CATALOG_BLOCK * CATALOG_NODE_SIZE;
    memset(header_node, 0, CATALOG_NODE_SIZE);
    header_node[8] = 1;
    header_node[9] = 0;
    test_image_put_be16(header_node + 10, 3);

    uint8_t *header = header_node + 14;
    test_image_put_be16(header, 1);
    test_image_put_be32(header + 2, 1);
    test_image_put_be32(header + 6, 2);
    test_image_put_be32(header + 10, 1);
    test_image_put_be32(header + 14, 1);
    test_image_put_be16(header + 18, CATALOG_NODE_SIZE);
    test_image_put_be16(header + 20, 516);
    test_image_put_be32(header + 22, CATALOG_TOTAL_NODES);
    test_image_put_be32(header + 26, 1);
    test_image_put_be32(header + 38, 6);
    header[36] = 0;
    header[37] = 0xcf;

    test_image_put_be16(header_node + 504, 504);
    test_image_put_be16(header_node + 506, 248);
    test_image_put_be16(header_node + 508, 120);
    test_image_put_be16(header_node + 510, 14);

    uint8_t *leaf = test_image + 9 * CATALOG_NODE_SIZE;
    memset(leaf, 0, CATALOG_NODE_SIZE);
    leaf[8] = 0xff;
    leaf[9] = 1;
    test_image_put_be16(leaf + 10, 2);

    size_t folder_size = put_folder(leaf + 14, 2, ".firmware", 3);
    uint8_t *file_fork = NULL;
    size_t file_size = put_file(leaf + 14 + folder_size, 3, "current.img", 4,
                                1, &file_fork);
    size_t end = 14 + folder_size + file_size;

    test_image_put_be16(leaf + 510, 14);
    test_image_put_be16(leaf + 508, (uint16_t)(14 + folder_size));
    test_image_put_be16(leaf + 506, (uint16_t)end);

    memcpy(test_image + FILE_DATA_BLOCK * 512, "HELO", 4);
    struct catalog_fixture fixture = { leaf, file_fork };
    return fixture;
}

static int test_iterate_and_read(void)
{
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_catalog_iterator iterator;
    struct hfs_entry entry;
    uint8_t scratch[CATALOG_NODE_SIZE];
    char data[8];
    size_t done = 0;

    make_catalog_fixture();
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch));
    CHECK(rc == HFS_OK);

    rc = hfs_catalog_iter_init(&catalog, 2, &iterator);
    CHECK(rc == HFS_OK);
    rc = hfs_catalog_iter_next(&catalog, &iterator, &entry);
    CHECK(rc == 1);
    CHECK(entry.kind == 1);
    CHECK(entry.id == 3);
    CHECK(strcmp(entry.name, ".firmware") == 0);
    CHECK(hfs_catalog_iter_next(&catalog, &iterator, &entry) == 0);

    memset(data, 0, sizeof(data));
    rc = hfs_catalog_lookup(&catalog, "/.firmware/current.img", &entry);
    CHECK(rc == HFS_OK);
    CHECK(entry.kind == 2);
    CHECK(entry.id == 4);
    CHECK(entry.data.size == 4);
    rc = hfs_catalog_read_file(&catalog, &entry, 0, data, 4, &done);
    CHECK(rc == HFS_OK);
    CHECK(done == 4);
    CHECK(memcmp(data, "HELO", 4) == 0);

    rc = hfs_catalog_read_file(&catalog, &entry, 2, data, 4, &done);
    CHECK(rc == HFS_OK);
    CHECK(done == 2);
    CHECK(memcmp(data, "LO", 2) == 0);

    rc = hfs_catalog_read_file(&catalog, &entry, 4, data, 4, &done);
    CHECK(rc == HFS_OK);
    CHECK(done == 0);
    return 0;
}

static int test_lookup_errors(void)
{
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_entry entry;
    uint8_t scratch[CATALOG_NODE_SIZE];

    make_catalog_fixture();
    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch));
    CHECK(rc == HFS_OK);

    CHECK(hfs_catalog_lookup(&catalog, "/.firmware/missing", &entry) ==
          HFS_ERR_NOT_FOUND);
    CHECK(hfs_catalog_lookup(&catalog, "/.firmware/current.img/x", &entry) ==
          HFS_ERR_NOT_DIR);
    CHECK(hfs_catalog_lookup(&catalog, "/.firmware/../current.img", &entry) ==
          HFS_ERR_NAME_INVALID);
    CHECK(hfs_catalog_lookup(&catalog, "relative", &entry) ==
          HFS_ERR_ARGUMENT);
    return 0;
}

static int test_non_inline_file_unsupported(void)
{
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_entry entry;
    uint8_t scratch[CATALOG_NODE_SIZE];
    struct catalog_fixture fixture = make_catalog_fixture();
    char data[4];
    size_t done = 0;

    test_image_put_be32(fixture.file_fork + 12, 2);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    rc = hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch));
    CHECK(rc == HFS_OK);
    CHECK(hfs_catalog_lookup(&catalog, "/.firmware/current.img", &entry) ==
          HFS_OK);
    CHECK(hfs_catalog_read_file(&catalog, &entry, 0, data, 4, &done) ==
          HFS_ERR_UNSUPPORTED);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"iterate and read", test_iterate_and_read},
        {"lookup errors", test_lookup_errors},
        {"non-inline file unsupported", test_non_inline_file_unsupported}
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
