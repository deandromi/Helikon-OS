/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_bitmap.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BLOCK_SIZE 512u
#define TEST_BLOCKS 128u
#define TEST_BITMAP_BLOCK 20u
#define TEST_FREE_BLOCKS 107u

static void make_volume_image(uint32_t free_blocks)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4,
                            TEST_ATTR_UNMOUNTED, TEST_BLOCK_SIZE,
                            TEST_BLOCKS, free_blocks);

    uint8_t *fork = test_image + 1024 + 112;
    test_image_put_be64(fork, 16);
    test_image_put_be32(fork + 12, 1);
    test_image_put_be32(fork + 16, TEST_BITMAP_BLOCK);
    test_image_put_be32(fork + 20, 1);

    /* Blocks 0..20 are used: volume headers, metadata and this bitmap block. */
    uint8_t *bitmap = test_image + TEST_BITMAP_BLOCK * TEST_BLOCK_SIZE;
    bitmap[0] = 0xff;
    bitmap[1] = 0xff;
    bitmap[2] = 0xf8;
}

static int open_bitmap(struct hfs_bitmap *bitmap, struct hfs_volume *volume)
{
    int rc = hfs_volume_probe(volume, test_image_read, NULL, 0,
                              TEST_IMAGE_SIZE);
    if (rc != HFS_OK)
        return rc;
    return hfs_bitmap_open(bitmap, volume);
}

static void make_txn(struct hfs_txn *tx, struct hfs_journal *journal)
{
    memset(journal, 0, sizeof(*journal));
    journal->sector_size = TEST_BLOCK_SIZE;
    memset(tx, 0, sizeof(*tx));
    tx->journal = journal;
    tx->active = 1;
}

static void apply_txn(const struct hfs_txn *tx)
{
    for (size_t i = 0; i < tx->count; ++i) {
        memcpy(test_image + tx->blocks[i].sector * TEST_BLOCK_SIZE,
               tx->blocks[i].data, tx->blocks[i].size);
    }
}

static const struct hfs_txn_block *find_block(const struct hfs_txn *tx,
                                              uint64_t sector)
{
    for (size_t i = 0; i < tx->count; ++i) {
        if (tx->blocks[i].sector == sector)
            return &tx->blocks[i];
    }
    return NULL;
}

static int test_alloc_first_free(void)
{
    struct hfs_volume volume;
    struct hfs_bitmap bitmap;
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint32_t first = 0;

    make_volume_image(TEST_FREE_BLOCKS);
    CHECK(open_bitmap(&bitmap, &volume) == HFS_OK);
    make_txn(&tx, &journal);
    CHECK(hfs_bitmap_alloc(&bitmap, &tx, 0, 1, &first) == HFS_OK);
    CHECK(first == 21);
    CHECK(bitmap.free_blocks == TEST_FREE_BLOCKS - 1);
    CHECK(volume.free_blocks == TEST_FREE_BLOCKS - 1);
    apply_txn(&tx);
    CHECK((test_image[TEST_BITMAP_BLOCK * TEST_BLOCK_SIZE + 21 / 8] &
           (uint8_t)(0x80u >> (21 % 8))) != 0);

    const struct hfs_txn_block *primary = find_block(&tx, 2);
    const struct hfs_txn_block *alternate = find_block(&tx, 126);
    const struct hfs_txn_block *bitmap_block = find_block(&tx, TEST_BITMAP_BLOCK);
    CHECK(primary != NULL);
    CHECK(alternate != NULL);
    CHECK(bitmap_block != NULL);
    if (primary != NULL)
        CHECK(primary->data[48] == 0 && primary->data[49] == 0 &&
              primary->data[50] == 0 && primary->data[51] == 106);
    if (alternate != NULL)
        CHECK(alternate->data[48] == 0 && alternate->data[49] == 0 &&
              alternate->data[50] == 0 && alternate->data[51] == 106);
    return 0;
}

static int test_alloc_two_blocks(void)
{
    struct hfs_volume volume;
    struct hfs_bitmap bitmap;
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint32_t first = 0;

    make_volume_image(TEST_FREE_BLOCKS);
    CHECK(open_bitmap(&bitmap, &volume) == HFS_OK);
    make_txn(&tx, &journal);
    CHECK(hfs_bitmap_alloc(&bitmap, &tx, 0, 2, &first) == HFS_OK);
    CHECK(first == 21);
    CHECK(bitmap.free_blocks == TEST_FREE_BLOCKS - 2);
    apply_txn(&tx);
    CHECK((test_image[TEST_BITMAP_BLOCK * TEST_BLOCK_SIZE + 21 / 8] &
           (uint8_t)(0x80u >> (21 % 8))) != 0);
    CHECK((test_image[TEST_BITMAP_BLOCK * TEST_BLOCK_SIZE + 22 / 8] &
           (uint8_t)(0x80u >> (22 % 8))) != 0);
    return 0;
}

static int test_free_restores_block(void)
{
    struct hfs_volume volume;
    struct hfs_bitmap bitmap;
    struct hfs_journal journal;
    struct hfs_txn tx;

    make_volume_image(TEST_FREE_BLOCKS);
    CHECK(open_bitmap(&bitmap, &volume) == HFS_OK);
    make_txn(&tx, &journal);
    CHECK(hfs_bitmap_free(&bitmap, &tx, 100, 1) == HFS_ERR_FORMAT);

    uint32_t first = 0;
    make_txn(&tx, &journal);
    CHECK(hfs_bitmap_alloc(&bitmap, &tx, 0, 1, &first) == HFS_OK);
    CHECK(first == 21);
    apply_txn(&tx);

    make_txn(&tx, &journal);
    CHECK(hfs_bitmap_free(&bitmap, &tx, first, 1) == HFS_OK);
    CHECK(bitmap.free_blocks == TEST_FREE_BLOCKS);
    apply_txn(&tx);
    CHECK((test_image[TEST_BITMAP_BLOCK * TEST_BLOCK_SIZE + 21 / 8] &
           (uint8_t)(0x80u >> (21 % 8))) == 0);
    return 0;
}

static int test_no_space(void)
{
    struct hfs_volume volume;
    struct hfs_bitmap bitmap;
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint32_t first = 0;

    make_volume_image(TEST_FREE_BLOCKS);
    memset(test_image + TEST_BITMAP_BLOCK * TEST_BLOCK_SIZE, 0xff, 16);
    test_image_put_be32(test_image + 1024 + 48, 0);

    CHECK(open_bitmap(&bitmap, &volume) == HFS_OK);
    make_txn(&tx, &journal);
    CHECK(hfs_bitmap_alloc(&bitmap, &tx, 0, 1, &first) == HFS_ERR_NO_SPACE);
    return 0;
}

static int test_bad_free_range(void)
{
    struct hfs_volume volume;
    struct hfs_bitmap bitmap;
    struct hfs_journal journal;
    struct hfs_txn tx;

    make_volume_image(TEST_FREE_BLOCKS);
    CHECK(open_bitmap(&bitmap, &volume) == HFS_OK);
    make_txn(&tx, &journal);
    CHECK(hfs_bitmap_free(&bitmap, &tx, TEST_BLOCKS, 1) == HFS_ERR_ARGUMENT);
    CHECK(hfs_bitmap_free(&bitmap, &tx, 0, TEST_BLOCKS) == HFS_ERR_FORMAT);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "alloc first free", test_alloc_first_free },
        { "alloc two blocks", test_alloc_two_blocks },
        { "free restores block", test_free_restores_block },
        { "no space", test_no_space },
        { "bad free range", test_bad_free_range }
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
