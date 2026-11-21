/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_fork_io.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void make_txn(struct hfs_txn *tx, struct hfs_journal *journal)
{
    memset(journal, 0, sizeof(*journal));
    journal->sector_size = 512;
    memset(tx, 0, sizeof(*tx));
    tx->journal = journal;
    tx->active = 1;
}

static void make_volume(struct hfs_volume *volume, struct hfs_fork *fork)
{
    memset(volume, 0, sizeof(*volume));
    volume->read = test_image_read;
    volume->length = TEST_IMAGE_SIZE;
    volume->block_size = 512;
    volume->blocks = TEST_IMAGE_SIZE / 512;
    memset(fork, 0, sizeof(*fork));
    fork->size = 4u * 512u;
    fork->blocks = 4;
    fork->extents[0].start = 10;
    fork->extents[0].count = 4;
}

static int test_partial_write(void)
{
    struct hfs_volume volume;
    struct hfs_fork fork;
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[100];

    test_image_clear();
    memset(test_image + 10 * 512, 0xaa, 512);
    make_volume(&volume, &fork);
    make_txn(&tx, &journal);
    memset(data, 0x55, sizeof(data));
    CHECK(hfs_fork_stage(&tx, &volume, &fork, 0, data, sizeof(data)) ==
          HFS_OK);
    CHECK(tx.count == 1);
    CHECK(tx.blocks[0].sector == 10);
    CHECK(tx.blocks[0].size == 512);
    CHECK(memcmp(tx.blocks[0].data, data, sizeof(data)) == 0);
    CHECK(tx.blocks[0].data[100] == 0xaa);
    CHECK(tx.blocks[0].data[511] == 0xaa);
    return 0;
}

static int test_full_block(void)
{
    struct hfs_volume volume;
    struct hfs_fork fork;
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[512];

    test_image_clear();
    make_volume(&volume, &fork);
    make_txn(&tx, &journal);
    memset(data, 0x33, sizeof(data));
    CHECK(hfs_fork_stage(&tx, &volume, &fork, 512, data, sizeof(data)) ==
          HFS_OK);
    CHECK(tx.count == 1);
    CHECK(tx.blocks[0].sector == 11);
    CHECK(tx.blocks[0].size == 512);
    CHECK(memcmp(tx.blocks[0].data, data, sizeof(data)) == 0);
    return 0;
}

static int test_bounds(void)
{
    struct hfs_volume volume;
    struct hfs_fork fork;
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[16];

    test_image_clear();
    make_volume(&volume, &fork);
    make_txn(&tx, &journal);
    memset(data, 0, sizeof(data));
    CHECK(hfs_fork_stage(&tx, &volume, &fork, 2048, data, 16) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_fork_stage(&tx, &volume, &fork, 0, data, 0) ==
          HFS_ERR_ARGUMENT);
    return 0;
}

static int test_overflow_unsupported(void)
{
    struct hfs_volume volume;
    struct hfs_fork fork;
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[512];

    test_image_clear();
    make_volume(&volume, &fork);
    fork.size = 4096;
    make_txn(&tx, &journal);
    memset(data, 0, sizeof(data));
    CHECK(hfs_fork_stage(&tx, &volume, &fork, 2048, data, sizeof(data)) ==
          HFS_ERR_UNSUPPORTED);
    return 0;
}

/*
 * A fork that continues in the extents tree: the map form has to place a range
 * that starts in one extent and ends in a later, non-adjacent one.
 */
static int test_map_across_extents(void)
{
    struct hfs_volume volume;
    struct hfs_fork fork;
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[200];
    struct hfs_extent map[4];

    test_image_clear();
    memset(test_image + 10 * 512, 0xaa, 512);
    memset(test_image + 40 * 512, 0xbb, 512);
    make_volume(&volume, &fork);
    make_txn(&tx, &journal);

    /* Logical 0..511 at 10, 512..1023 at 40, 1024..1535 at 41. */
    map[0].start = 10;
    map[0].count = 1;
    map[1].start = 40;
    map[1].count = 2;

    memset(data, 0x55, sizeof(data));
    CHECK(hfs_fork_stage_map(&tx, &volume, map, 2, 1536, 400, data,
                             sizeof(data)) == HFS_OK);
    CHECK(tx.count == 2);
    CHECK(tx.blocks[0].sector == 10);
    CHECK(tx.blocks[0].data[400] == 0x55);
    CHECK(tx.blocks[0].data[399] == 0xaa);
    CHECK(tx.blocks[0].data[511] == 0x55);
    CHECK(tx.blocks[1].sector == 40);
    CHECK(tx.blocks[1].data[0] == 0x55);
    CHECK(tx.blocks[1].data[87] == 0x55);
    CHECK(tx.blocks[1].data[88] == 0xbb);

    /* An extent list that does not reach the end of the range is a hole. */
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_fork_stage_map(&tx, &volume, map, 1, 1536, 400, data,
                             600) == HFS_ERR_UNSUPPORTED);
    CHECK(tx.count == 0);

    /* The size bound is the fork size, not the extent list length. */
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    CHECK(hfs_fork_stage_map(&tx, &volume, map, 2, 1536, 1500, data, 100) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_fork_stage_map(&tx, &volume, map, 2, 1536, 0, data, 0) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_fork_stage_map(&tx, &volume, NULL, 2, 1536, 0, data, 16) ==
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
        { "partial write", test_partial_write },
        { "full block", test_full_block },
        { "bounds", test_bounds },
        { "overflow unsupported", test_overflow_unsupported },
        { "map across extents", test_map_across_extents }
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
