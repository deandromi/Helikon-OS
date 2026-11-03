/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_catalog.h"
#include "hfs_volume.h"
#include "test_check.h"
#include "test_fs_image.h"
#include "test_image.h"

#include <stdint.h>
#include <string.h>

static int test_iterate_and_read(void)
{
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_catalog_iterator iterator;
    struct hfs_entry entry;
    uint8_t scratch[TEST_FS_NODE_SIZE];
    char data[8];
    size_t done = 0;

    test_fs_image_make();
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
    uint8_t scratch[TEST_FS_NODE_SIZE];

    test_fs_image_make();
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
    uint8_t scratch[TEST_FS_NODE_SIZE];
    struct test_fs_image fixture = test_fs_image_make();
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
