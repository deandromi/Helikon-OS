/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_journal.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_VOLUME_SIZE (256u * 1024u)
#define TEST_JOURNAL_OFFSET 4096u
#define TEST_JOURNAL_SIZE (64u * 1024u)
#define TEST_SECTOR 512u
#define TEST_BLOCK_LIST 4096u
#define TEST_TARGET_SECTOR 200u

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

static void make_volume(struct hfs_volume *volume)
{
    memset(volume, 0, sizeof(*volume));
    volume->read = device_read;
    volume->length = sizeof(device);
    volume->journal_offset = TEST_JOURNAL_OFFSET;
    volume->journal_size = TEST_JOURNAL_SIZE;
    volume->journaled = 1;
}

static int open_clean(struct hfs_journal *journal)
{
    struct hfs_volume volume;
    make_volume(&volume);
    return hfs_journal_open(journal, &volume, device_write, device_flush);
}

static int test_clean_open(void)
{
    struct hfs_journal journal;
    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_clean(&journal) == HFS_OK);
    CHECK(hfs_journal_is_clean(&journal));
    CHECK(journal.start == TEST_SECTOR);
    CHECK(journal.end == TEST_SECTOR);
    CHECK(hfs_journal_replay(&journal) == HFS_OK);
    CHECK(flush_count == 0);
    return 0;
}

static int test_append_and_replay(void)
{
    struct hfs_journal journal;
    uint8_t data[TEST_SECTOR];
    memset(data, 0x5a, sizeof(data));

    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_clean(&journal) == HFS_OK);
    struct hfs_journal_entry entry = { TEST_TARGET_SECTOR, data, sizeof(data) };
    CHECK(hfs_journal_append(&journal, &entry, 1) == HFS_OK);
    CHECK(!hfs_journal_is_clean(&journal));
    CHECK(journal.end != journal.start);
    CHECK(flush_count > 0);

    CHECK(hfs_journal_replay(&journal) == HFS_OK);
    CHECK(hfs_journal_is_clean(&journal));
    CHECK(memcmp(device + TEST_TARGET_SECTOR * TEST_SECTOR, data,
                 sizeof(data)) == 0);

    /* Idempotent replay must not change the target. */
    CHECK(hfs_journal_replay(&journal) == HFS_OK);
    CHECK(memcmp(device + TEST_TARGET_SECTOR * TEST_SECTOR, data,
                 sizeof(data)) == 0);
    return 0;
}

static int test_corrupt_block_list_checksum(void)
{
    struct hfs_journal journal;
    uint8_t data[TEST_SECTOR];
    memset(data, 0x33, sizeof(data));

    make_journal(TEST_SECTOR, TEST_SECTOR);
    memset(device + TEST_TARGET_SECTOR * TEST_SECTOR, 0xaa, TEST_SECTOR);
    CHECK(open_clean(&journal) == HFS_OK);
    struct hfs_journal_entry entry = { TEST_TARGET_SECTOR, data, sizeof(data) };
    CHECK(hfs_journal_append(&journal, &entry, 1) == HFS_OK);
    uint64_t header_offset = TEST_JOURNAL_OFFSET + journal.start;
    device[header_offset + 8] ^= 0xffu;

    CHECK(hfs_journal_replay(&journal) == HFS_ERR_FORMAT);
    CHECK(memcmp(device + TEST_TARGET_SECTOR * TEST_SECTOR,
                 "\xaa\xaa\xaa\xaa", 4) == 0);
    CHECK(!hfs_journal_is_clean(&journal));
    return 0;
}

static int test_wrapped_transaction(void)
{
    struct hfs_journal journal;
    uint8_t data[TEST_SECTOR];
    memset(data, 0x77, sizeof(data));
    make_journal(TEST_JOURNAL_SIZE - 1024u, TEST_JOURNAL_SIZE - 1024u);
    CHECK(open_clean(&journal) == HFS_OK);
    struct hfs_journal_entry entry = { TEST_TARGET_SECTOR, data, sizeof(data) };
    CHECK(hfs_journal_append(&journal, &entry, 1) == HFS_OK);
    CHECK(hfs_journal_replay(&journal) == HFS_OK);
    CHECK(hfs_journal_is_clean(&journal));
    CHECK(memcmp(device + TEST_TARGET_SECTOR * TEST_SECTOR, data,
                 sizeof(data)) == 0);
    return 0;
}

static int test_no_space(void)
{
    struct hfs_journal journal;
    uint8_t data[TEST_JOURNAL_SIZE];
    memset(data, 0, sizeof(data));
    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_clean(&journal) == HFS_OK);
    struct hfs_journal_entry entry = { TEST_TARGET_SECTOR, data, sizeof(data) };
    CHECK(hfs_journal_append(&journal, &entry, 1) == HFS_ERR_NO_SPACE);
    return 0;
}

static int test_bad_target(void)
{
    struct hfs_journal journal;
    uint8_t data[TEST_SECTOR];
    memset(data, 0, sizeof(data));
    make_journal(TEST_SECTOR, TEST_SECTOR);
    CHECK(open_clean(&journal) == HFS_OK);
    struct hfs_journal_entry entry = {
        TEST_VOLUME_SIZE / TEST_SECTOR, data, sizeof(data)
    };
    CHECK(hfs_journal_append(&journal, &entry, 1) == HFS_ERR_ARGUMENT);
    return 0;
}

static int test_corrupt_header(void)
{
    struct hfs_journal journal;
    make_journal(TEST_SECTOR, TEST_SECTOR);
    device[TEST_JOURNAL_OFFSET] ^= 0xffu;
    CHECK(open_clean(&journal) == HFS_ERR_FORMAT);
    return 0;
}

static int test_dirty_replay_failure(void)
{
    struct hfs_journal journal;
    make_journal(TEST_SECTOR, TEST_SECTOR + TEST_SECTOR);
    /* No valid block list header at start; replay must fail closed. */
    CHECK(open_clean(&journal) == HFS_OK);
    CHECK(!hfs_journal_is_clean(&journal));
    CHECK(hfs_journal_replay(&journal) == HFS_ERR_FORMAT);
    CHECK(journal.start == TEST_SECTOR);
    CHECK(!hfs_journal_is_clean(&journal));
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "clean open", test_clean_open },
        { "append and replay", test_append_and_replay },
        { "corrupt block list checksum", test_corrupt_block_list_checksum },
        { "wrapped transaction", test_wrapped_transaction },
        { "no space", test_no_space },
        { "bad target", test_bad_target },
        { "corrupt header", test_corrupt_header },
        { "dirty replay failure", test_dirty_replay_failure }
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
