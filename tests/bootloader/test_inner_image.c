/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "inner_image.h"
#include "test_check.h"

#include <stdint.h>
#include <string.h>

#define DEVICE_SIZE 65536u
#define BLOCK 512u

static uint8_t device[DEVICE_SIZE];
static unsigned int flush_count;

static int device_read(void *context, uint64_t offset, void *buffer,
                       size_t length)
{
    (void)context;
    if (offset > DEVICE_SIZE || length > DEVICE_SIZE - offset)
        return -1;
    memcpy(buffer, device + offset, length);
    return 0;
}

static int device_write(void *context, uint64_t offset, const void *buffer,
                        size_t length)
{
    (void)context;
    if (offset > DEVICE_SIZE || length > DEVICE_SIZE - offset)
        return -1;
    memcpy(device + offset, buffer, length);
    return 0;
}

static int device_flush(void *context)
{
    (void)context;
    ++flush_count;
    return 0;
}

static void make_image(struct inner_image *image)
{
    memset(image, 0, sizeof(*image));
    image->outer_read = device_read;
    image->outer_write = device_write;
    image->outer_flush = device_flush;
    image->outer_block_size = BLOCK;
    image->size = 4u * BLOCK;
    image->extent_count = 2;
    image->extents[0].start = 10;
    image->extents[0].count = 2;
    image->extents[1].start = 20;
    image->extents[1].count = 2;
    image->logical[0] = 0;
    image->logical[1] = 2u * BLOCK;
}

static int test_cross_extent(void)
{
    memset(device, 0xa5, sizeof(device));
    struct inner_image image;
    make_image(&image);

    uint8_t data[4u * BLOCK];
    for (size_t i = 0; i < sizeof(data); ++i)
        data[i] = (uint8_t)(i + 1u);
    CHECK(inner_image_write(&image, 0, data, sizeof(data)) == HFS_OK);
    CHECK(memcmp(device + 10u * BLOCK, data, 2u * BLOCK) == 0);
    CHECK(memcmp(device + 20u * BLOCK, data + 2u * BLOCK,
                 2u * BLOCK) == 0);
    CHECK(device[9u * BLOCK + BLOCK - 1u] == 0xa5);
    CHECK(device[22u * BLOCK] == 0xa5);

    uint8_t readback[4u * BLOCK];
    memset(readback, 0, sizeof(readback));
    CHECK(inner_image_read(&image, 0, readback, sizeof(readback)) == HFS_OK);
    CHECK(memcmp(readback, data, sizeof(data)) == 0);

    uint8_t partial[BLOCK + 128u];
    memset(partial, 0x5a, sizeof(partial));
    CHECK(inner_image_write(&image, 3u * BLOCK / 2u, partial,
                            sizeof(partial)) == HFS_OK);
    CHECK(memcmp(device + 11u * BLOCK + BLOCK / 2u, partial, BLOCK / 2u) == 0);
    CHECK(memcmp(device + 20u * BLOCK, partial + BLOCK / 2u, 128u) == 0);
    return 0;
}

static int test_bounds(void)
{
    struct inner_image image;
    make_image(&image);
    uint8_t byte = 0;
    uint8_t big[4u * BLOCK];
    CHECK(inner_image_read(&image, image.size, &byte, 1) == HFS_ERR_FORMAT);
    CHECK(inner_image_write(&image, image.size, &byte, 1) == HFS_ERR_FORMAT);
    CHECK(inner_image_read(&image, 0, big, sizeof(big)) == HFS_OK);
    return 0;
}

static int test_flush(void)
{
    struct inner_image image;
    make_image(&image);
    flush_count = 0;
    CHECK(inner_image_flush(&image) == HFS_OK);
    CHECK(flush_count == 1);
    image.outer_flush = NULL;
    CHECK(inner_image_flush(&image) == HFS_OK);
    return 0;
}

static int test_guard(void)
{
    struct inner_image image;
    make_image(&image);
    struct hfs_volume outer;
    memset(&outer, 0, sizeof(outer));
    outer.length = DEVICE_SIZE;
    outer.block_size = BLOCK;
    outer.blocks = DEVICE_SIZE / BLOCK;
    outer.catalog.extents[0].start = 40;
    outer.catalog.extents[0].count = 1;
    outer.catalog.blocks = 1;
    CHECK(inner_image_guard_metadata(&image, &outer) == HFS_OK);

    outer.catalog.extents[0].start = 20;
    CHECK(inner_image_guard_metadata(&image, &outer) == HFS_ERR_FORMAT);

    outer.catalog.extents[0].start = 40;
    outer.journal_offset = 10u * BLOCK;
    outer.journal_size = BLOCK;
    CHECK(inner_image_guard_metadata(&image, &outer) == HFS_ERR_FORMAT);

    outer.journal_size = 0;
    outer.catalog.blocks = 2;
    CHECK(inner_image_guard_metadata(&image, &outer) == HFS_ERR_UNSUPPORTED);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "cross extent", test_cross_extent },
        { "bounds", test_bounds },
        { "flush", test_flush },
        { "metadata guard", test_guard }
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
