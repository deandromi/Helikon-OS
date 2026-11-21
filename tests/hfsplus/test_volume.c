/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_volume.h"
#include "test_image.h"
#include "test_check.h"

#include <stdint.h>
#include <string.h>

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

static int test_journal_uninitialized_readonly(void)
{
    struct hfs_volume volume;
    make_valid_header();
    uint8_t *header = test_image + 1024;
    test_image_make_header(header, 0x482bu, 4,
                           TEST_ATTR_UNMOUNTED | TEST_ATTR_JOURNALED, 512,
                           128, 64);
    /* The alternate header uses "10.0"; the primary is authoritative. */
    test_image_put_be32(header + 8, 0x31302e30u); /* "10.0" */
    test_image_put_be32(header + 12, 4);
    uint8_t *info = test_image + 4 * 512;
    test_image_put_be32(info, 1u); /* in-FS journal */
    test_image_put_be64(info + 36, 8u * 512u);
    test_image_put_be64(info + 44, 8u * 512u);
    /* Journal header at 8*512 remains zero: uninitialized, but clean. */

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    CHECK(volume.journaled == 1);
    CHECK(volume.journal_uninitialized == 1);
    return 0;
}

static int test_journal_dirty_readonly(void)
{
    struct hfs_volume volume;
    make_valid_header();
    uint8_t *header = test_image + 1024;
    test_image_make_header(header, 0x482bu, 4,
                           TEST_ATTR_UNMOUNTED | TEST_ATTR_JOURNALED, 512,
                           128, 64);
    test_image_put_be32(header + 8, 0x4846534au);
    test_image_put_be32(header + 12, 4);
    uint8_t *info = test_image + 4 * 512;
    test_image_put_be32(info, 1u);
    test_image_put_be64(info + 36, 8u * 512u);
    test_image_put_be64(info + 44, 8u * 512u);

    uint8_t *journal = test_image + 8 * 512;
    test_image_put_be32(journal, 0x4a4e4c78u);
    test_image_put_be32(journal + 4, 0x12345678u);
    test_image_put_be64(journal + 8, 512u);       /* start */
    test_image_put_be64(journal + 16, 1024u);     /* end: dirty */
    test_image_put_be64(journal + 24, 4096u);     /* journal size */
    test_image_put_be32(journal + 32, 512u);      /* block-list header */
    test_image_put_be32(journal + 40, 512u);      /* header size */
    test_image_put_be32(journal + 36, 0u);
    uint32_t sum = 0;
    for (size_t i = 0; i < 44; ++i)
        sum = (sum << 8) ^ (sum + journal[i]);
    test_image_put_be32(journal + 36, ~sum);

    int rc = hfs_volume_probe(&volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_DIRTY);

    rc = hfs_volume_probe_readonly(&volume, test_image_read, NULL, 0,
                                   TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    CHECK(volume.journaled == 1);
    CHECK(volume.journal_dirty == 1);
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

/*
 * A volume that starts above 128 GiB. The reader is handed a base offset and
 * must fold it into every read: this callback refuses any offset below the base,
 * so a 32-bit truncation in the adapter shows up as an immediate I/O error
 * rather than as a mount that reads the wrong sectors.
 */
static uint64_t base_read_base;

static int base_read(void *context, uint64_t offset, void *buffer, size_t length)
{
    (void)context;
    if (offset < base_read_base)
        return HFS_ERR_IO;
    uint64_t relative = offset - base_read_base;
    if (relative > TEST_IMAGE_SIZE || length > TEST_IMAGE_SIZE - relative)
        return HFS_ERR_IO;
    memcpy(buffer, test_image + relative, length);
    return HFS_OK;
}

static int test_base_above_128_gib(void)
{
    struct hfs_volume volume;
    make_valid_header();

    /* 128 GiB = 2^37 bytes: above every 32-bit byte offset. */
    base_read_base = 0x2000000000ull;
    int rc = hfs_volume_probe(&volume, base_read, NULL, base_read_base,
                              TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    CHECK(volume.base == base_read_base);
    CHECK(volume.block_size == 512);
    CHECK(volume.blocks == 128);

    /* The same volume without the base shift still reads: the base is the only
     * difference, so the check above cannot pass by accident. */
    base_read_base = 0u;
    rc = hfs_volume_probe(&volume, base_read, NULL, 0u, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_OK);
    CHECK(volume.base == 0u);

    /* A base whose last byte would wrap is refused, not wrapped. */
    base_read_base = 0u;
    rc = hfs_volume_probe(&volume, base_read, NULL,
                          UINT64_MAX - TEST_IMAGE_SIZE / 2u, TEST_IMAGE_SIZE);
    CHECK(rc == HFS_ERR_ARGUMENT);
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
        {"journal uninitialized read-only", test_journal_uninitialized_readonly},
        {"journal dirty read-only", test_journal_dirty_readonly},
        {"journal missing info", test_journal_missing_info},
        {"base above 128 GiB", test_base_above_128_gib},
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
