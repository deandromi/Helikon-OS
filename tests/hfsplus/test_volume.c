/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_volume.h"
#include "test_image.h"
#include "test_check.h"

#include <stdint.h>

static void make_valid_header(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED, 512, 128, 64);
}

static int test_valid_header(void)
{
    struct hfs_volume volume;
    make_valid_header();

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    CHECK(volume.block_size == 512);
    CHECK(volume.blocks == 128);
    CHECK(volume.free_blocks == 64);
    CHECK(volume.length == TEST_IMAGE_SIZE);
    CHECK(volume.journaled == 0);
    CHECK(volume.recovery == 0);
    return 0;
}

static int test_not_hfs(void)
{
    struct hfs_volume volume;
    test_image_clear();

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_NOT_HFS);
    return 0;
}

static int test_hfsx_rejected(void)
{
    struct hfs_volume volume;
    make_valid_header();
    test_image_make_header(test_image + 1024, 0x4858u, 5, TEST_ATTR_UNMOUNTED, 512, 128, 64);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_UNSUPPORTED);
    return 0;
}

static int test_dirty_volume(void)
{
    struct hfs_volume volume;
    make_valid_header();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED | TEST_ATTR_INCONSISTENT,
                512, 128, 64);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_DIRTY);
    return 0;
}

static int test_bad_block_size(void)
{
    struct hfs_volume volume;
    make_valid_header();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED, 1000, 64, 32);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_blocks_exceed_partition(void)
{
    struct hfs_volume volume;
    make_valid_header();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED, 512, 256, 128);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_free_blocks_exceed_total(void)
{
    struct hfs_volume volume;
    make_valid_header();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED, 512, 128, 129);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_alternate_header_recovery(void)
{
    struct hfs_volume volume;
    test_image_clear();
    test_image_make_header(test_image + TEST_IMAGE_SIZE - 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED, 512,
                128, 64);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    CHECK(volume.recovery == 1);
    CHECK(volume.length == TEST_IMAGE_SIZE);
    return 0;
}

static int test_journal_external_rejected(void)
{
    struct hfs_volume volume;
    make_valid_header();
    uint8_t *header = test_image + 1024;
    test_image_make_header(header, 0x482bu, 4, TEST_ATTR_UNMOUNTED | TEST_ATTR_JOURNALED, 512,
                128, 64);
    test_image_put_be32(header + 8, 0x4846534au); /* HFSJ */
    test_image_put_be32(header + 12, 4);
    test_image_put_be32(test_image + 4 * 512, 2); /* journal on another device */

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_UNSUPPORTED);
    return 0;
}

static int test_journal_needs_init(void)
{
    struct hfs_volume volume;
    make_valid_header();
    uint8_t *header = test_image + 1024;
    test_image_make_header(header, 0x482bu, 4, TEST_ATTR_UNMOUNTED | TEST_ATTR_JOURNALED, 512,
                128, 64);
    test_image_put_be32(header + 8, 0x4846534au);
    test_image_put_be32(header + 12, 4);
    test_image_put_be32(test_image + 4 * 512, 1u | 4u); /* in-FS journal needs init */

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_DIRTY);
    return 0;
}

static int test_journal_missing_info(void)
{
    struct hfs_volume volume;
    make_valid_header();
    uint8_t *header = test_image + 1024;
    test_image_make_header(header, 0x482bu, 4, TEST_ATTR_UNMOUNTED | TEST_ATTR_JOURNALED, 512,
                128, 64);
    test_image_put_be32(header + 8, 0x4846534au);
    test_image_put_be32(header + 12, 0);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_FORMAT);
    return 0;
}

static int test_bad_arguments(void)
{
    struct hfs_volume volume;
    make_valid_header();

    CHECK(hfs_volume_probe(NULL, test_image_read, NULL, 0, TEST_IMAGE_SIZE) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_volume_probe(&volume, NULL, NULL, 0, TEST_IMAGE_SIZE) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0, 1024) ==
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
        {"valid volume header", test_valid_header},
        {"not HFS+", test_not_hfs},
        {"HFSX rejected", test_hfsx_rejected},
        {"dirty volume", test_dirty_volume},
        {"bad block size", test_bad_block_size},
        {"blocks exceed partition", test_blocks_exceed_partition},
        {"free blocks exceed total", test_free_blocks_exceed_total},
        {"alternate header recovery", test_alternate_header_recovery},
        {"external journal rejected", test_journal_external_rejected},
        {"journal needs init", test_journal_needs_init},
        {"journal missing info", test_journal_missing_info},
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
