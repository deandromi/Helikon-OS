/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_loader.h"
#include "loader_error.h"
#include "test_check.h"
#include "test_fs_image.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SCRATCH 512u

static int test_inline_read(void)
{
    test_fs_image_make();
    struct hfs_volume volume;
    CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0,
                           TEST_IMAGE_SIZE) == HFS_OK);
    uint8_t scratch[SCRATCH];
    struct hfs_catalog catalog;
    CHECK(hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch)) ==
          HFS_OK);
    struct helikon_hfs_loader loader = { &catalog, NULL };
    uint8_t data[4];
    size_t done = 0;
    CHECK(helikon_hfs_read_file(&loader, "/.firmware/current.img", 0, data,
                                sizeof(data), &done) == HELIKON_LOADER_OK);
    CHECK(done == sizeof(data));
    CHECK(memcmp(data, "HELO", 4) == 0);
    CHECK(helikon_hfs_read_file(&loader, "/.firmware/current.img", 2, data,
                                2, &done) == HELIKON_LOADER_OK);
    CHECK(done == 2 && memcmp(data, "LO", 2) == 0);
    return 0;
}

static int test_missing_and_bounds(void)
{
    test_fs_image_make();
    struct hfs_volume volume;
    CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0,
                           TEST_IMAGE_SIZE) == HFS_OK);
    uint8_t scratch[SCRATCH];
    struct hfs_catalog catalog;
    CHECK(hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch)) ==
          HFS_OK);
    struct helikon_hfs_loader loader = { &catalog, NULL };
    uint8_t data[8];
    size_t done = 0;
    CHECK(helikon_hfs_read_file(&loader, "/.firmware/missing.img", 0, data,
                                sizeof(data), &done) ==
          HELIKON_LOADER_ERR_NOT_FOUND);
    CHECK(helikon_hfs_read_file(&loader, "/.firmware/current.img", 3, data,
                                sizeof(data), &done) == HELIKON_LOADER_ERR_IO);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "inline HFS read", test_inline_read },
        { "missing and bounds", test_missing_and_bounds }
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
