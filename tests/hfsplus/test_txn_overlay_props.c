/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Property test for the transaction overlay.
 *
 * The overlay is the reason two updaters of the volume header can live in one
 * transaction: the second one reads through the first one's staged bytes
 * instead of the committed ones, so it cannot silently drop the first one's
 * change. `test_txn.c` checks the cases someone thought of; this one models the
 * contract and hammers it with random stages and random read ranges:
 *
 *   - `hfs_txn_overlay` fills exactly the staged bytes that intersect the range
 *     and leaves the rest of the caller's buffer alone;
 *   - it reports 1 when anything intersected and 0 when nothing did, and a
 *     zero-length range touches nothing;
 *   - staged blocks are applied in the order they were staged, so a range that
 *     several of them cover ends up with the last one's bytes where they
 *     overlap (callers stage whole sectors, so an overlap means a caller bug,
 *     but the answer must still be the documented one);
 *   - slices at both ends are copied from the right offset of the block data,
 *     including a range that starts or ends in the middle of a block;
 *   - `hfs_txn_read` is the disk plus the same overlay, and `hfs_txn_stage`
 *     rules hold: a duplicate sector is BUSY, a replacement of a different size
 *     is BUSY, a 33rd block is NO_SPACE, and a bad size is ARGUMENT.
 *
 * The expected answer is computed by applying the model's staged list to a copy
 * of the image, which is a second, independent expression of the same contract
 * rather than a call back into the code under test.
 *
 *   ./test_txn_overlay_props            # the committed sweep
 *   ./test_txn_overlay_props 7 20000    # another seed, more steps
 */
#include "hfs_txn.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 512u
#define MAX_BLOCK 8192u
#define IMAGE TEST_IMAGE_SIZE
#define DEFAULT_STEPS 4000u

struct model_block {
    uint64_t sector;
    uint32_t size;
    uint8_t data[MAX_BLOCK];
};

static struct hfs_txn tx;
static struct hfs_journal journal;
static struct hfs_volume volume;

static struct model_block model[HFS_TXN_MAX_BLOCKS];
static size_t model_count;

static uint8_t image_copy[IMAGE];
static uint8_t expected[IMAGE];
static uint8_t actual[IMAGE];

static uint32_t rng_state;

static uint32_t next_random(void)
{
    uint32_t value = rng_state;
    if (value == 0u)
        value = 0x2545f491u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    rng_state = value;
    return value;
}

static void fill_random(uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0u; i < size; ++i)
        data[i] = (uint8_t)next_random();
}

/*
 * Applies the model's staged blocks to `out`, which the caller has already
 * filled with whatever the buffer held. This mirrors `hfs_txn_overlay`, which
 * patches a caller's buffer and never reads the disk itself.
 */
static int model_patch(uint64_t offset, size_t length, uint8_t *out)
{
    /* A zero-length range touches nothing, which the implementation returns
     * before it looks at the staged blocks at all. */
    if (length == 0u)
        return 0;
    int overlapped = 0;
    for (size_t i = 0u; i < model_count; ++i) {
        const uint64_t start = model[i].sector * SECTOR;
        const uint64_t end = start + model[i].size;
        if (end <= offset || start >= offset + length)
            continue;
        const uint64_t from = start > offset ? start : offset;
        const uint64_t to = end < offset + length ? end : offset + length;
        memcpy(out + (size_t)(from - offset),
               model[i].data + (size_t)(from - start), (size_t)(to - from));
        overlapped = 1;
    }
    return overlapped;
}

/* The model's answer for `hfs_txn_read`: the disk, then the same patch. */
static int model_read(uint64_t offset, size_t length, uint8_t *out)
{
    memcpy(out, image_copy + (size_t)offset, length);
    return model_patch(offset, length, out);
}

static int same_size(uint64_t sector, size_t *index)
{
    for (size_t i = 0u; i < model_count; ++i) {
        if (model[i].sector == sector) {
            *index = i;
            return 1;
        }
    }
    return 0;
}

/* Stages a fresh block in both the transaction and the model. */
static int stage_new(void)
{
    uint8_t data[MAX_BLOCK];
    uint32_t size = SECTOR * (1u + next_random() % (MAX_BLOCK / SECTOR));
    const uint64_t sector = next_random() % (IMAGE / SECTOR - MAX_BLOCK / SECTOR);
    size_t index = 0u;
    if (same_size(sector, &index))
        return 0; /* already staged: BUSY is exercised by its own step */

    fill_random(data, size);
    int rc = hfs_txn_stage(&tx, sector, data, size);
    if (model_count >= HFS_TXN_MAX_BLOCKS) {
        CHECK(rc == HFS_ERR_NO_SPACE);
        return 0;
    }
    CHECK(rc == HFS_OK);
    model[model_count].sector = sector;
    model[model_count].size = size;
    memcpy(model[model_count].data, data, size);
    ++model_count;
    return 0;
}

static int replace_existing(void)
{
    if (model_count == 0u)
        return 0;
    size_t index = next_random() % model_count;
    uint8_t data[MAX_BLOCK];
    const int same = (next_random() % 4u) != 0u;
    uint32_t size = model[index].size;
    if (!same) {
        /* A different size for the same sector must be refused. */
        size = size == MAX_BLOCK ? SECTOR : size + SECTOR;
    }
    fill_random(data, model[index].size);
    int rc = hfs_txn_stage_replace(&tx, model[index].sector, data, size);
    if (!same) {
        CHECK(rc == HFS_ERR_BUSY);
        return 0;
    }
    CHECK(rc == HFS_OK);
    memcpy(model[index].data, data, size);
    return 0;
}

static int overlay_random_range(void)
{
    size_t length = 1u + next_random() % 2048u;
    uint64_t offset = next_random() % (IMAGE - length);
    memset(actual, 0xa5, sizeof(actual));
    memset(expected, 0xa5, sizeof(expected));
    int rc = hfs_txn_overlay(&tx, offset, actual, length);
    CHECK(rc >= 0);
    const int expected_overlap = model_patch(offset, length, expected);
    CHECK(rc == expected_overlap);
    CHECK(memcmp(actual, expected, length) == 0);
    /* Bytes outside the range must be untouched by the overlay. */
    CHECK(actual[length] == 0xa5);
    return 0;
}

static int read_random_range(void)
{
    size_t length = 1u + next_random() % 2048u;
    uint64_t offset = next_random() % (IMAGE - length);
    int rc = hfs_txn_read(&tx, &volume, offset, actual, (uint32_t)length);
    CHECK(rc == HFS_OK);
    (void)model_read(offset, length, expected);
    CHECK(memcmp(actual, expected, length) == 0);
    return 0;
}

/* Ranges chosen to land exactly on and around a staged block. */
static int boundary_ranges(void)
{
    if (model_count == 0u)
        return 0;
    const struct model_block *block = &model[next_random() % model_count];
    const uint64_t start = block->sector * SECTOR;
    static const struct {
        int64_t delta;
        size_t length;
    } cases[] = {
        { 0, SECTOR }, { -1, 2 }, { -(int64_t)SECTOR, SECTOR },
        { 1, SECTOR },
        { -1, (size_t)SECTOR + 2u }, { 0, 0 },
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const int64_t base = (int64_t)start + cases[i].delta;
        if (base < 0 || (uint64_t)base + cases[i].length > IMAGE)
            continue;
        memset(actual, 0x5a, sizeof(actual));
        memset(expected, 0x5a, sizeof(expected));
        int rc = hfs_txn_overlay(&tx, (uint64_t)base, actual, cases[i].length);
        CHECK(rc >= 0);
        const int expected_overlap =
            model_patch((uint64_t)base, cases[i].length, expected);
        CHECK(rc == expected_overlap);
        CHECK(memcmp(actual, expected, cases[i].length) == 0);
    }
    return 0;
}

/* Argument and capacity rules, exercised against the live transaction. */
static int rules(void)
{
    uint8_t block[MAX_BLOCK];
    memset(block, 0, sizeof(block));

    CHECK(hfs_txn_stage(&tx, 0u, block, 0u) == HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_stage(&tx, 0u, block, 3u) == HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_stage(&tx, 0u, block, MAX_BLOCK + SECTOR) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_stage(NULL, 0u, block, SECTOR) == HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_overlay(&tx, 0u, block, 0u) == 0);
    CHECK(hfs_txn_overlay(&tx, 0u, NULL, 8u) == HFS_ERR_ARGUMENT);

    if (model_count != 0u) {
        const struct model_block *staged = &model[next_random() % model_count];
        CHECK(hfs_txn_stage(&tx, staged->sector, block, staged->size) ==
              HFS_ERR_BUSY);
    }
    return 0;
}

/*
 * Fills the transaction to its limit, checks the 33rd stage is refused, and
 * checks the overlay over the whole image with a full block set.
 */
static int fill_to_capacity(void)
{
    uint8_t data[SECTOR];
    for (uint32_t sector = 0u; sector < HFS_TXN_MAX_BLOCKS; ++sector) {
        fill_random(data, SECTOR);
        CHECK(hfs_txn_stage(&tx, sector, data, SECTOR) == HFS_OK);
        model[model_count].sector = sector;
        model[model_count].size = SECTOR;
        memcpy(model[model_count].data, data, SECTOR);
        ++model_count;
    }
    CHECK(model_count == HFS_TXN_MAX_BLOCKS);
    CHECK(tx.count == HFS_TXN_MAX_BLOCKS);

    fill_random(data, SECTOR);
    CHECK(hfs_txn_stage(&tx, HFS_TXN_MAX_BLOCKS, data, SECTOR) ==
          HFS_ERR_NO_SPACE);
    CHECK(hfs_txn_stage_replace(&tx, 0u, data, SECTOR * 2u) == HFS_ERR_BUSY);

    /* The whole image, staged end of the transaction included. */
    memset(actual, 0x11, sizeof(actual));
    memset(expected, 0x11, sizeof(expected));
    CHECK(hfs_txn_overlay(&tx, 0u, actual, IMAGE) == 1);
    CHECK(model_patch(0u, IMAGE, expected) == 1);
    CHECK(memcmp(actual, expected, IMAGE) == 0);
    return 0;
}

/* A fresh transaction whose blocks match the model, rebuilt from scratch. */
static int restart(void)
{
    memset(image_copy, 0, sizeof(image_copy));
    test_image_clear();
    fill_random(test_image, IMAGE);
    memcpy(image_copy, test_image, IMAGE);
    memset(&volume, 0, sizeof(volume));
    volume.read = test_image_read;
    volume.length = IMAGE;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = SECTOR;
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_OK);
    model_count = 0u;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t steps = DEFAULT_STEPS;
    uint32_t seed = 0x54584f56u; /* "TXOV" */
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        steps = (uint32_t)strtoul(argv[2], NULL, 0);

    rng_state = seed;
    CHECK(restart() == 0);

    uint32_t overlays = 0u;
    size_t max_staged = 0u;
    for (uint32_t step = 0u; step < steps; ++step) {
        if (model_count > max_staged)
            max_staged = model_count;
        const uint32_t pick = next_random() % 100u;
        if (pick < 18u) {
            CHECK(stage_new() == 0);
        } else if (pick < 30u) {
            CHECK(replace_existing() == 0);
        } else if (pick < 45u) {
            CHECK(boundary_ranges() == 0);
            ++overlays;
        } else if (pick < 55u) {
            CHECK(rules() == 0);
        } else if (pick < 80u) {
            CHECK(overlay_random_range() == 0);
            ++overlays;
        } else {
            CHECK(read_random_range() == 0);
            ++overlays;
        }

        /* Start over now and then, so the array is not full all the time. */
        if ((step % 97u) == 96u)
            CHECK(restart() == 0);
        if ((step % 50u) == 49u) {
            /* A fresh transaction, so the fill starts from an empty set. */
            CHECK(restart() == 0);
            CHECK(fill_to_capacity() == 0);
            CHECK(restart() == 0);
        }
    }

    printf("  txn overlay properties: %u steps, %u overlay checks, at most %u "
           "staged blocks\n",
           (unsigned)steps, (unsigned)overlays, (unsigned)max_staged);
    CHECK(overlays > steps / 2u);
    return 0;
}
