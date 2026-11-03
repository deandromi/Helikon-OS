/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_txn.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_VOLUME_SIZE (256u * 1024u)
#define TEST_JOURNAL_OFFSET 4096u
#define TEST_JOURNAL_SIZE (64u * 1024u)
#define TEST_SECTOR 512u
#define TEST_BLOCK_LIST 4096u
#define TEST_TARGET 200u

static uint8_t device[TEST_VOLUME_SIZE];
static unsigned int flush_count;

static int device_read(void *context, uint64_t offset, void *buffer,
                       size_t length)
{
    (void)context;
    if (offset > sizeof(device) || length > sizeof(device) - offset)
        return -1;
    memcpy(buffer, device + offset, length);
    return 0;
}

static int device_write(void *context, uint64_t offset, const void *buffer,
                        size_t length)
{
    (void)context;
    if (offset > sizeof(device) || length > sizeof(device) - offset)
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

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void put_be64(uint8_t *p, uint64_t value)
{
    put_be32(p, (uint32_t)(value >> 32));
    put_be32(p + 4, (uint32_t)value);
}

static uint32_t journal_checksum(const uint8_t *p, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i)
        sum = (sum << 8) ^ (sum + p[i]);
    return ~sum;
}

static void make_journal(uint64_t start, uint64_t end)
{
    memset(device, 0, sizeof(device));
    flush_count = 0;
    uint8_t *h = device + TEST_JOURNAL_OFFSET;
    put_be32(h, 0x4a4e4c78u);
    put_be32(h + 4, 0x12345678u);
    put_be64(h + 8, start);
    put_be64(h + 16, end);
    put_be64(h + 24, TEST_JOURNAL_SIZE);
    put_be32(h + 32, TEST_BLOCK_LIST);
    put_be32(h + 36, 0);
    put_be32(h + 40, TEST_SECTOR);
    put_be32(h + 36, journal_checksum(h, 44));
}

static int open_journal(struct hfs_journal *journal)
{
    struct hfs_volume volume;
    memset(&volume, 0, sizeof(volume));
    volume.read = device_read;
    volume.length = sizeof(device);
    volume.journal_offset = TEST_JOURNAL_OFFSET;
    volume.journal_size = TEST_JOURNAL_SIZE;
    volume.journaled = 1;
    return hfs_journal_open(journal, &volume, device_write, device_flush);
}

static int test_commit_one_block(void)
{
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[TEST_SECTOR];
    memset(data, 0x42, sizeof(data));
    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_journal(&journal) == HFS_OK);
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_OK);
    CHECK(hfs_txn_stage(&tx, TEST_TARGET, data, sizeof(data)) == HFS_OK);
    CHECK(hfs_txn_commit(&tx) == HFS_OK);
    CHECK(hfs_journal_is_clean(&journal));
    CHECK(memcmp(device + TEST_TARGET * TEST_SECTOR, data, sizeof(data)) == 0);
    CHECK(flush_count > 0);
    return 0;
}

static int test_commit_two_blocks(void)
{
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t one[TEST_SECTOR];
    uint8_t two[TEST_SECTOR];
    memset(one, 0x11, sizeof(one));
    memset(two, 0x22, sizeof(two));
    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_journal(&journal) == HFS_OK);
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_OK);
    CHECK(hfs_txn_stage(&tx, TEST_TARGET, one, sizeof(one)) == HFS_OK);
    CHECK(hfs_txn_stage(&tx, TEST_TARGET + 1, two, sizeof(two)) == HFS_OK);
    CHECK(hfs_txn_commit(&tx) == HFS_OK);
    CHECK(memcmp(device + TEST_TARGET * TEST_SECTOR, one, sizeof(one)) == 0);
    CHECK(memcmp(device + (TEST_TARGET + 1) * TEST_SECTOR, two,
                 sizeof(two)) == 0);
    return 0;
}

static int test_abort_leaves_target(void)
{
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[TEST_SECTOR];
    memset(data, 0x99, sizeof(data));
    make_journal(TEST_SECTOR, TEST_SECTOR);
    memset(device + TEST_TARGET * TEST_SECTOR, 0xaa, TEST_SECTOR);
    CHECK(open_journal(&journal) == HFS_OK);
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_OK);
    CHECK(hfs_txn_stage(&tx, TEST_TARGET, data, sizeof(data)) == HFS_OK);
    hfs_txn_abort(&tx);
    CHECK(hfs_journal_is_clean(&journal));
    CHECK(device[TEST_TARGET * TEST_SECTOR] == 0xaa);
    return 0;
}

static int test_duplicate_sector(void)
{
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[TEST_SECTOR];
    memset(data, 0, sizeof(data));
    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_journal(&journal) == HFS_OK);
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_OK);
    CHECK(hfs_txn_stage(&tx, TEST_TARGET, data, sizeof(data)) == HFS_OK);
    CHECK(hfs_txn_stage(&tx, TEST_TARGET, data, sizeof(data)) == HFS_ERR_BUSY);
    return 0;
}

static int test_too_many_blocks(void)
{
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[TEST_SECTOR];
    memset(data, 0, sizeof(data));
    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_journal(&journal) == HFS_OK);
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_OK);
    for (size_t i = 0; i < HFS_TXN_MAX_BLOCKS; ++i)
        CHECK(hfs_txn_stage(&tx, TEST_TARGET + i, data, sizeof(data)) ==
              HFS_OK);
    CHECK(hfs_txn_stage(&tx, TEST_TARGET + HFS_TXN_MAX_BLOCKS, data,
                        sizeof(data)) == HFS_ERR_NO_SPACE);
    return 0;
}

static int test_dirty_journal_refuses_txn(void)
{
    struct hfs_journal journal;
    struct hfs_txn tx;
    make_journal(TEST_SECTOR, TEST_SECTOR + TEST_SECTOR);
    CHECK(open_journal(&journal) == HFS_OK);
    CHECK(!hfs_journal_is_clean(&journal));
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_ERR_DIRTY);
    return 0;
}

static int test_bad_block_size(void)
{
    struct hfs_journal journal;
    struct hfs_txn tx;
    uint8_t data[TEST_SECTOR];
    memset(data, 0, sizeof(data));
    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_journal(&journal) == HFS_OK);
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_OK);
    CHECK(hfs_txn_stage(&tx, TEST_TARGET, data, 513) == HFS_ERR_ARGUMENT);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "commit one block", test_commit_one_block },
        { "commit two blocks", test_commit_two_blocks },
        { "abort leaves target", test_abort_leaves_target },
        { "duplicate sector", test_duplicate_sector },
        { "too many blocks", test_too_many_blocks },
        { "dirty journal refuses txn", test_dirty_journal_refuses_txn },
        { "bad block size", test_bad_block_size }
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
