/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Property test for the allocation bitmap.
 *
 * `test_bitmap.c` walks the API case by case. This one does not name a case at
 * all: it allocates and frees random runs for thousands of steps and keeps a
 * model of what the bitmap should say, so the failures it catches are the ones
 * nobody writes a case for -
 *
 *  - a run handed out that overlaps one already allocated (a double allocation,
 *    the bug that later overwrites someone else's file);
 *  - a free of a block that is not allocated (which silently inflates the free
 *    count until the volume claims blocks that do not exist);
 *  - `free_blocks` drifting away from the bits;
 *  - a search that misses a run that does exist, or refuses one that does not;
 *  - the transaction overlay handing the same block out twice inside one
 *    commit window, which the committed bitmap alone cannot catch.
 *
 * Every window of staged allocations is applied to the image and the bitmap is
 * reopened from disk, so the assertions are made against the committed volume,
 * not against a cached field.
 *
 *   ./test_bitmap_props              # the committed sweep
 *   ./test_bitmap_props 7 20000      # another seed, more steps
 */
#include "hfs_bitmap.h"
#include "hfs_volume_write.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 512u
#define BLOCKS 128u
#define BITMAP_BLOCK 20u
/* Blocks 0..20 are metadata and the bitmap itself: never allocated by the test. */
#define FIRST_DATA_BLOCK 21u
/* A run for the search to work with, after the fixture's 16-byte bitmap. */
#define MAX_RUN 8u
#define DEFAULT_STEPS 4000u
/* Staged allocations per commit window: small enough that the transaction
 * overlay is exercised thousands of times. */
#define WINDOW 16u

static uint8_t model[BLOCKS];
static uint32_t model_free;
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

static void make_volume_image(uint32_t free_blocks)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4,
                           TEST_ATTR_UNMOUNTED, BLOCK_SIZE, BLOCKS,
                           free_blocks);

    uint8_t *fork = test_image + 1024 + 112;
    test_image_put_be64(fork, 16);
    test_image_put_be32(fork + 12, 1);
    test_image_put_be32(fork + 16, BITMAP_BLOCK);
    test_image_put_be32(fork + 20, 1);

    uint8_t *bitmap = test_image + BITMAP_BLOCK * BLOCK_SIZE;
    bitmap[0] = 0xff;
    bitmap[1] = 0xff;
    bitmap[2] = 0xf8; /* blocks 0..20 used */
}

static void make_txn(struct hfs_txn *tx, struct hfs_journal *journal)
{
    memset(journal, 0, sizeof(*journal));
    journal->sector_size = BLOCK_SIZE;
    memset(tx, 0, sizeof(*tx));
    tx->journal = journal;
    tx->active = 1;
}

static void apply_txn(const struct hfs_txn *tx)
{
    for (size_t i = 0; i < tx->count; ++i) {
        memcpy(test_image + tx->blocks[i].sector * BLOCK_SIZE,
               tx->blocks[i].data, tx->blocks[i].size);
    }
}

/*
 * Reopen the volume and its bitmap from the image and compare every block, plus
 * the free count, against the model. Returns 1 and names the first block that
 * disagrees.
 */
static int verify_committed(const char *where)
{
    struct hfs_volume volume;
    struct hfs_bitmap bitmap;
    if (hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE) !=
        HFS_OK) {
        fprintf(stderr, "  FAIL %s: the volume no longer probes\n", where);
        return 1;
    }
    if (hfs_bitmap_open(&bitmap, &volume) != HFS_OK) {
        fprintf(stderr, "  FAIL %s: the bitmap no longer opens\n", where);
        return 1;
    }
    if (bitmap.free_blocks != model_free) {
        fprintf(stderr,
                "  FAIL %s: free_blocks %u on disk, model says %u\n", where,
                (unsigned)bitmap.free_blocks, (unsigned)model_free);
        return 1;
    }
    if (volume.free_blocks != model_free) {
        fprintf(stderr,
                "  FAIL %s: volume header free_blocks %u, model says %u\n",
                where, (unsigned)volume.free_blocks, (unsigned)model_free);
        return 1;
    }
    for (uint32_t block = 0u; block < BLOCKS; ++block) {
        int used = 0;
        if (hfs_bitmap_block_used(&bitmap, block, &used) != HFS_OK) {
            fprintf(stderr, "  FAIL %s: block %u unreadable\n", where,
                    (unsigned)block);
            return 1;
        }
        if ((used != 0) != (model[block] != 0u)) {
            fprintf(stderr, "  FAIL %s: block %u is %s, model says %s\n", where,
                    (unsigned)block, used != 0 ? "used" : "free",
                    model[block] != 0u ? "used" : "free");
            return 1;
        }
    }
    return 0;
}

/* Longest run of free blocks, and the first free block of a run of `count`. */
static uint32_t free_run_at(uint32_t count, uint32_t *start)
{
    for (uint32_t block = FIRST_DATA_BLOCK; block + count <= BLOCKS; ++block) {
        uint32_t run = 0u;
        while (run < count && model[block + run] == 0u)
            ++run;
        if (run == count) {
            *start = block;
            return 1u;
        }
    }
    return 0u;
}

/* A run of `count` consecutive blocks the model says are used, or 0. */
static uint32_t used_run_at(uint32_t count, uint32_t *start)
{
    for (uint32_t block = FIRST_DATA_BLOCK; block + count <= BLOCKS; ++block) {
        uint32_t run = 0u;
        while (run < count && model[block + run] != 0u)
            ++run;
        if (run == count) {
            *start = block;
            return 1u;
        }
    }
    return 0u;
}

/* ---------- a volume whose bitmap spans two blocks ---------- */

/*
 * 5000 blocks of 512 bytes: one bitmap block describes 4096 blocks, so the
 * allocation fork needs two and a range can cross the seam. The read-only image
 * fixture is 64 KiB, too small for this, so the case builds its own 2.4 MiB
 * volume with its own read callback.
 */
#define BIG_BLOCKS 5000u
#define BIG_BITMAP_BLOCK 20u
/*
 * Blocks 0..20 are metadata. The file covers 4090..4097, so it fills the end of
 * the first bitmap block and spills into the second: a free of 4090..4099 is
 * accepted for the first map block and refused in the second, which is exactly
 * the partial-staging case.
 */
#define BIG_FILE_START 4090u
#define BIG_FILE_BLOCKS 8u

static uint8_t big_image[BIG_BLOCKS * BLOCK_SIZE];

static int big_read(void *context, uint64_t offset, void *buffer,
                    size_t length)
{
    (void)context;
    if (offset > sizeof(big_image) || length > sizeof(big_image) - offset)
        return 1;
    memcpy(buffer, big_image + (size_t)offset, length);
    return 0;
}

static void big_set_bit(uint32_t block, int value)
{
    const uint32_t bits_per_block = BLOCK_SIZE * 8u;
    const uint32_t within = block % bits_per_block;
    uint8_t *map = big_image +
                   (BIG_BITMAP_BLOCK + block / bits_per_block) * BLOCK_SIZE;
    const uint8_t mask = (uint8_t)(0x80u >> (within % 8u));
    if (value)
        map[within / 8u] |= mask;
    else
        map[within / 8u] &= (uint8_t)~mask;
}

static void make_big_image(void)
{
    memset(big_image, 0, sizeof(big_image));
    /* 625 bytes of bitmap for 5000 blocks, in two blocks. */
    test_image_make_header(big_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED,
                           BLOCK_SIZE, BIG_BLOCKS, BIG_BLOCKS - 30u);
    uint8_t *fork = big_image + 1024 + 112;
    test_image_put_be64(fork, 625);
    test_image_put_be32(fork + 12, 2);
    test_image_put_be32(fork + 16, BIG_BITMAP_BLOCK);
    test_image_put_be32(fork + 20, 2);

    for (uint32_t block = 0u; block <= BIG_BITMAP_BLOCK + 1u; ++block)
        big_set_bit(block, 1);
    for (uint32_t block = BIG_FILE_START;
         block < BIG_FILE_START + BIG_FILE_BLOCKS; ++block)
        big_set_bit(block, 1);
}

/*
 * A free that spans the bitmap seam must be refused with nothing staged: the
 * first block of the range is allocated and the second is not, and the caller's
 * transaction must come back exactly as it went in.
 */
static int test_partial_free_is_refused(void)
{
    make_big_image();
    struct hfs_volume volume;
    struct hfs_bitmap bitmap;
    struct hfs_journal journal;
    struct hfs_txn tx;

    CHECK(hfs_volume_probe(&volume, big_read, NULL, 0, sizeof(big_image)) ==
          HFS_OK);
    CHECK(hfs_bitmap_open(&bitmap, &volume) == HFS_OK);
    make_txn(&tx, &journal);

    /* 4090..4099: 4090..4097 are allocated, 4098 is not. */
    CHECK(hfs_bitmap_free(&bitmap, &tx, BIG_FILE_START, 10) ==
          HFS_ERR_FORMAT);
    if (tx.count != 0u) {
        fprintf(stderr,
                "  FAIL a refused free left %u block(s) staged in the "
                "transaction\n",
                (unsigned)tx.count);
        return 1;
    }
    CHECK(bitmap.free_blocks == BIG_BLOCKS - 30u);

    /* The same range without the free block is accepted and staged once. */
    CHECK(hfs_bitmap_free(&bitmap, &tx, BIG_FILE_START, BIG_FILE_BLOCKS) ==
          HFS_OK);
    /* Two bitmap blocks (the range crosses the seam) and two headers. */
    CHECK(tx.count == 4u);
    CHECK(bitmap.free_blocks == BIG_BLOCKS - 30u + BIG_FILE_BLOCKS);
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t steps = DEFAULT_STEPS;
    uint32_t seed = 0x424d4150u; /* "BMAP" */
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        steps = (uint32_t)strtoul(argv[2], NULL, 0);

    make_volume_image(BLOCKS - 21u);
    model_free = BLOCKS - FIRST_DATA_BLOCK;
    for (uint32_t block = 0u; block < BLOCKS; ++block)
        model[block] = block < FIRST_DATA_BLOCK ? 1u : 0u;

    struct hfs_volume volume;
    struct hfs_bitmap bitmap;
    struct hfs_journal journal;
    struct hfs_txn tx;
    CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0,
                           TEST_IMAGE_SIZE) == HFS_OK);
    CHECK(hfs_bitmap_open(&bitmap, &volume) == HFS_OK);
    make_txn(&tx, &journal);
    CHECK(verify_committed("fresh image") == 0);

    rng_state = seed;
    uint32_t allocated = 0u;
    uint32_t freed = 0u;
    uint32_t refusals = 0u;

    for (uint32_t step = 0u; step < steps; ++step) {
        const int do_alloc = (next_random() % 2u) == 0u || model_free == 0u;
        const uint32_t count = 1u + next_random() % MAX_RUN;

        if (do_alloc) {
            uint32_t hint = FIRST_DATA_BLOCK + next_random() % (BLOCKS - FIRST_DATA_BLOCK);
            uint32_t first = 0u;
            uint32_t start = 0u;
            const int room = free_run_at(count, &start) != 0u;
            int rc = hfs_bitmap_alloc(&bitmap, &tx, hint, count, &first);
            if (!room) {
                /* The model says no run of that length exists: the allocator
                 * must refuse, and must not have moved the free count. */
                CHECK(rc == HFS_ERR_NO_SPACE);
                ++refusals;
            } else {
                CHECK(rc == HFS_OK);
                CHECK(first >= FIRST_DATA_BLOCK);
                CHECK(first + count <= BLOCKS);
                for (uint32_t block = first; block < first + count; ++block) {
                    if (model[block] != 0u) {
                        fprintf(stderr,
                                "  FAIL double allocation at block %u "
                                "(step %u, run %u)\n",
                                (unsigned)block, (unsigned)step,
                                (unsigned)count);
                        return 1;
                    }
                    model[block] = 1u;
                }
                model_free -= count;
                allocated++;
            }
            CHECK(bitmap.free_blocks == model_free);
        } else {
            uint32_t start = 0u;
            if (used_run_at(count, &start) == 0u)
                continue;
            CHECK(hfs_bitmap_free(&bitmap, &tx, start, count) == HFS_OK);
            for (uint32_t block = start; block < start + count; ++block)
                model[block] = 0u;
            model_free += count;
            freed++;
            CHECK(bitmap.free_blocks == model_free);
        }

        /*
         * Freeing a block that is not allocated must be refused. Without this
         * the free count drifts up and the volume eventually hands out blocks
         * that hold data.
         */
        if ((step % 32u) == 0u) {
            uint32_t free_block = 0u;
            for (uint32_t block = FIRST_DATA_BLOCK; block < BLOCKS; ++block) {
                if (model[block] == 0u) {
                    free_block = block;
                    break;
                }
            }
            if (free_block != 0u) {
                int rc = hfs_bitmap_free(&bitmap, &tx, free_block, 1u);
                if (rc == HFS_OK) {
                    fprintf(stderr,
                            "  FAIL double free accepted at block %u "
                            "(step %u)\n",
                            (unsigned)free_block, (unsigned)step);
                    return 1;
                }
                CHECK(bitmap.free_blocks == model_free);
            }
        }

        /* Commit the window and let the image speak for itself. */
        if (((step + 1u) % WINDOW) == 0u || step + 1u == steps) {
            apply_txn(&tx);
            make_txn(&tx, &journal);
            CHECK(hfs_volume_probe(&volume, test_image_read, NULL, 0,
                                   TEST_IMAGE_SIZE) == HFS_OK);
            CHECK(hfs_bitmap_open(&bitmap, &volume) == HFS_OK);
            CHECK(bitmap.free_blocks == model_free);
            CHECK(verify_committed("committed window") == 0);
        }
    }

    CHECK(test_partial_free_is_refused() == 0);

    printf("  bitmap properties: %u steps, %u allocations, %u frees, "
           "%u refusals, %u free blocks\n",
           (unsigned)steps, (unsigned)allocated, (unsigned)freed,
           (unsigned)refusals, (unsigned)model_free);
    return 0;
}
