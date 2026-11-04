/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bootstate.h"
#include "error.h"
#include "sha256.h"

#include <string.h>

#include "test_check.h"

#define TEST_BOARD 6u
#define TEST_VERSION 2u
#define TEST_LOAD 0x08000000u
#define TEST_SIZE 39108u

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void make_slot(uint8_t *slot, uint32_t sequence, uint32_t board,
                      uint32_t version, uint32_t load, uint32_t size,
                      uint8_t fill, uint8_t state)
{
    memset(slot, 0, HELIKON_BOOTSTATE_SLOT_SIZE);
    memcpy(slot, HELIKON_BOOTSTATE_MAGIC, 4u);
    put32(slot + 4, HELIKON_BOOTSTATE_VERSION);
    put32(slot + 8, sequence);
    put32(slot + 12, board);
    put32(slot + 16, version);
    put32(slot + 20, load);
    put32(slot + 24, size);
    for (size_t i = 0; i < HELIKON_SHA256_SIZE; ++i)
        slot[28u + i] = (uint8_t)(fill + i);
    slot[60] = state;
    helikon_sha256(slot, 64u, slot + 64u);
}

static void make_bootstate(uint8_t *data, uint8_t state)
{
    memset(data, 0, HELIKON_BOOTSTATE_SIZE);
    make_slot(data, 5u, TEST_BOARD, TEST_VERSION, TEST_LOAD, TEST_SIZE,
              0x40u, state);
}

static void expected_hash(uint8_t *hash)
{
    for (size_t i = 0; i < HELIKON_SHA256_SIZE; ++i)
        hash[i] = (uint8_t)(0x40u + i);
}

static int slot_is_good(const uint8_t *data, int index)
{
    struct helikon_bootstate bootstate;
    int rc = helikon_bootstate_parse(data, HELIKON_BOOTSTATE_SIZE,
                                     TEST_BOARD, &bootstate);
    if (rc != HELIKON_OK)
        return 0;
    return bootstate.slots[index].valid &&
           bootstate.slots[index].state == HELIKON_BOOTSTATE_GOOD;
}

static int test_sha256_vector(void)
{
    static const uint8_t abc[] = { 'a', 'b', 'c' };
    static const uint8_t expected[HELIKON_SHA256_SIZE] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    uint8_t digest[HELIKON_SHA256_SIZE];
    helikon_sha256(abc, sizeof(abc), digest);
    CHECK(memcmp(digest, expected, sizeof(digest)) == 0);
    return 0;
}

static int test_parse_valid(void)
{
    uint8_t data[HELIKON_BOOTSTATE_SIZE];
    make_bootstate(data, HELIKON_BOOTSTATE_TRYING);

    struct helikon_bootstate bootstate;
    CHECK_EQ(HELIKON_OK,
             helikon_bootstate_parse(data, sizeof(data), TEST_BOARD,
                                     &bootstate));
    CHECK_EQ(1, bootstate.slots[0].valid);
    CHECK_EQ(HELIKON_BOOTSTATE_TRYING, bootstate.slots[0].state);
    CHECK_EQ(5, bootstate.slots[0].sequence);
    CHECK_EQ(TEST_VERSION, bootstate.slots[0].image_version);
    CHECK_EQ(TEST_LOAD, bootstate.slots[0].load_address);
    CHECK_EQ(TEST_SIZE, bootstate.slots[0].image_size);
    CHECK_EQ(-1, bootstate.selected);
    CHECK_EQ(0, bootstate.slots[1].valid);

    CHECK_EQ(HELIKON_ERR_BOOTSTATE,
             helikon_bootstate_parse(data, sizeof(data) - 1u, TEST_BOARD,
                                     &bootstate));
    return 0;
}

static int test_promote_trying(void)
{
    uint8_t data[HELIKON_BOOTSTATE_SIZE];
    make_bootstate(data, HELIKON_BOOTSTATE_TRYING);
    uint8_t hash[HELIKON_SHA256_SIZE];
    expected_hash(hash);

    uint32_t sequence = 0;
    int changed = -1;
    CHECK_EQ(HELIKON_OK,
             helikon_bootstate_promote(data, sizeof(data), TEST_BOARD,
                                       TEST_VERSION, TEST_LOAD, TEST_SIZE,
                                       hash, &sequence, &changed));
    CHECK_EQ(1, changed);
    CHECK_EQ(6, sequence);
    CHECK(slot_is_good(data, 0));

    struct helikon_bootstate bootstate;
    CHECK_EQ(HELIKON_OK,
             helikon_bootstate_parse(data, sizeof(data), TEST_BOARD,
                                     &bootstate));
    CHECK_EQ(0, bootstate.selected);
    CHECK_EQ(6, bootstate.slots[0].sequence);

    /* A second promotion is a no-op. */
    sequence = 0;
    changed = -1;
    CHECK_EQ(HELIKON_OK,
             helikon_bootstate_promote(data, sizeof(data), TEST_BOARD,
                                       TEST_VERSION, TEST_LOAD, TEST_SIZE,
                                       hash, &sequence, &changed));
    CHECK_EQ(0, changed);
    CHECK_EQ(6, sequence);
    return 0;
}

static int test_promote_two_slots(void)
{
    uint8_t data[HELIKON_BOOTSTATE_SIZE];
    memset(data, 0, sizeof(data));
    make_slot(data, 10u, TEST_BOARD, TEST_VERSION, TEST_LOAD, TEST_SIZE,
              0x10u, HELIKON_BOOTSTATE_GOOD);
    make_slot(data + HELIKON_BOOTSTATE_SLOT_SIZE, 11u, TEST_BOARD,
              TEST_VERSION, TEST_LOAD, TEST_SIZE, 0x40u,
              HELIKON_BOOTSTATE_TRYING);

    uint8_t hash[HELIKON_SHA256_SIZE];
    expected_hash(hash);
    uint32_t sequence = 0;
    int changed = -1;
    CHECK_EQ(HELIKON_OK,
             helikon_bootstate_promote(data, sizeof(data), TEST_BOARD,
                                       TEST_VERSION, TEST_LOAD, TEST_SIZE,
                                       hash, &sequence, &changed));
    CHECK_EQ(1, changed);
    CHECK_EQ(12, sequence);
    CHECK(slot_is_good(data, 1));

    struct helikon_bootstate bootstate;
    CHECK_EQ(HELIKON_OK,
             helikon_bootstate_parse(data, sizeof(data), TEST_BOARD,
                                     &bootstate));
    CHECK_EQ(1, bootstate.selected);
    CHECK_EQ(12, bootstate.slots[1].sequence);
    return 0;
}

static int test_mismatch_and_corruption(void)
{
    uint8_t data[HELIKON_BOOTSTATE_SIZE];
    make_bootstate(data, HELIKON_BOOTSTATE_TRYING);
    uint8_t hash[HELIKON_SHA256_SIZE];
    expected_hash(hash);
    hash[0] ^= 1u;

    uint32_t sequence = 0;
    int changed = 0;
    uint8_t before[HELIKON_BOOTSTATE_SIZE];
    memcpy(before, data, sizeof(before));
    CHECK_EQ(HELIKON_ERR_BOOTSTATE,
             helikon_bootstate_promote(data, sizeof(data), TEST_BOARD,
                                       TEST_VERSION, TEST_LOAD, TEST_SIZE,
                                       hash, &sequence, &changed));
    CHECK_EQ(0, changed);
    CHECK(memcmp(data, before, sizeof(data)) == 0);

    /* Corrupt checksum: the slot is ignored, so no match exists. */
    make_bootstate(data, HELIKON_BOOTSTATE_TRYING);
    data[64] ^= 1u;
    expected_hash(hash);
    CHECK_EQ(HELIKON_ERR_BOOTSTATE,
             helikon_bootstate_promote(data, sizeof(data), TEST_BOARD,
                                       TEST_VERSION, TEST_LOAD, TEST_SIZE,
                                       hash, &sequence, &changed));

    /* Wrong board is not a valid slot. */
    make_bootstate(data, HELIKON_BOOTSTATE_TRYING);
    put32(data + 12, 7u);
    helikon_sha256(data, 64u, data + 64u);
    CHECK_EQ(HELIKON_ERR_BOOTSTATE,
             helikon_bootstate_promote(data, sizeof(data), TEST_BOARD,
                                       TEST_VERSION, TEST_LOAD, TEST_SIZE,
                                       hash, &sequence, &changed));

    /* Reserved bytes must stay zero. */
    make_bootstate(data, HELIKON_BOOTSTATE_TRYING);
    data[100] = 1u;
    helikon_sha256(data, 64u, data + 64u);
    struct helikon_bootstate bootstate;
    CHECK_EQ(HELIKON_OK,
             helikon_bootstate_parse(data, sizeof(data), TEST_BOARD,
                                     &bootstate));
    CHECK_EQ(0, bootstate.slots[0].valid);
    return 0;
}

int main(void)
{
    test_sha256_vector();
    test_parse_valid();
    test_promote_trying();
    test_promote_two_slots();
    test_mismatch_and_corruption();
    return helikon_test_report("bootstate");
}
