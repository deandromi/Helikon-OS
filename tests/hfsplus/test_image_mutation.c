/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Mutation sweep over the read path.
 *
 * The other HFS+ tests each put *one* known thing wrong and prove the reader
 * names it. This one does the opposite: it takes a volume that is entirely
 * valid, corrupts a handful of bytes at random, and then drives the whole read
 * stack over the result -
 *
 *   hfs_volume_probe  ->  hfs_check_volume
 *                     ->  hfs_catalog_open
 *                     ->  hfs_catalog_lookup / iter / read_file
 *                     ->  hfs_extents_read (through the overflow fork)
 *
 * Nothing is asserted about the *answer*: a corrupted volume is allowed to
 * report any error it likes, or to answer nonsense. What must hold is that it
 * terminates, stays inside its buffers and does not trip the address or
 * undefined-behaviour sanitizer. Those are exactly the failures a hand-written
 * test never finds, because nobody thinks to corrupt the byte that matters.
 *
 * The sweep is deterministic: one seed, one sequence of mutations, so a finding
 * reproduces byte for byte. A failure prints the seed, the iteration and every
 * byte that was changed, which is everything needed to replay it.
 *
 *   ./test_image_mutation            # the committed sweep
 *   ./test_image_mutation 12345 5000 # another seed, more iterations
 */
#include "hfs_catalog.h"
#include "hfs_check.h"
#include "hfs_extents.h"
#include "hfs_volume.h"
#include "test_check.h"
#include "test_fs_image.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 512u
#define DEFAULT_ITERATIONS 1500u
#define MAX_MUTATIONS 8u
/*
 * A read path that returns more entries than this from a four-node catalog is
 * not answering, it is looping. 200000 is far past any legitimate walk of this
 * fixture and far below a wall-clock timeout.
 */
#define MAX_STEPS 200000u
#define READ_BUFFER 8192u

struct mutation {
    uint32_t offset;
    uint32_t width;
    uint32_t value;
};

/* Values a corrupt field tends to hold: the extremes of every width. */
static const uint32_t extreme_values[] = {
    0x00u, 0x01u, 0x7fu, 0x80u, 0xffu,
    0x0000u, 0x0001u, 0x7fffu, 0x8000u, 0xfffeu, 0xffffu,
    0x0000ffffu, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu,
};

static uint8_t pristine[TEST_IMAGE_SIZE];
static uint8_t scratch[BLOCK_SIZE];
static uint8_t buffer[READ_BUFFER];

static struct mutation mutations[MAX_MUTATIONS];
static uint32_t mutation_count;

static uint32_t rng_state;

/* xorshift32: the same generator the queue uses, so the sequence is portable. */
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

static void report_failure(const char *what, uint32_t seed, uint32_t iteration)
{
    fprintf(stderr, "  FAIL %s at iteration %u (seed %u)\n", what, iteration,
            seed);
    for (uint32_t i = 0; i < mutation_count; ++i)
        fprintf(stderr, "    %u byte(s) at %u := 0x%0*x\n",
                mutations[i].width, mutations[i].offset,
                (int)(mutations[i].width * 2u), mutations[i].value);
    fprintf(stderr, "  replay: ./test_image_mutation %u %u\n", seed,
            iteration + 1u);
}

/* Read one file at offsets that cross the inline/overflow boundary. */
static void read_entry(struct hfs_catalog *catalog,
                       const struct hfs_entry *entry)
{
    static const uint64_t offsets[] = { 0u, 1u, 511u, 512u, 513u, 1023u };
    static const size_t lengths[] = { 1u, 4u, 512u, 1024u, READ_BUFFER };

    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        for (size_t j = 0; j < sizeof(lengths) / sizeof(lengths[0]); ++j) {
            size_t done = 0;
            (void)hfs_catalog_read_file(catalog, entry, offsets[i], buffer,
                                        lengths[j], &done);
        }
    }
}

static const uint32_t widths[] = { 1u, 2u, 4u };

/*
 * Where the fixture keeps its structure. A byte of file content is far less
 * interesting than a byte of a B-tree offset table, so the regions that hold
 * metadata are hit most of the time.
 */
static uint32_t pick_offset(void)
{
    static const struct {
        uint32_t start;
        uint32_t length;
        uint32_t weight;
    } regions[] = {
        { 1024u, 512u, 30u },                    /* volume header           */
        { 8u * BLOCK_SIZE, 4u * BLOCK_SIZE, 25u },   /* catalog B-tree      */
        { 24u * BLOCK_SIZE, 2u * BLOCK_SIZE, 15u },  /* extents B-tree      */
        { 1024u, 40u * BLOCK_SIZE, 20u },        /* metadata, any block     */
        { 0u, TEST_IMAGE_SIZE, 10u },            /* anywhere                */
    };
    uint32_t total = 0;
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i)
        total += regions[i].weight;
    uint32_t pick = next_random() % total;
    for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i) {
        if (pick < regions[i].weight)
            return regions[i].start + next_random() % regions[i].length;
        pick -= regions[i].weight;
    }
    return 0u;
}

/*
 * Drive every reader over the current image. Returns 0 when the pass stayed
 * inside its buffers, 1 when a walk refused to terminate.
 */
static int exercise(uint32_t seed, uint32_t iteration, int *deep)
{
    struct hfs_volume volume;
    if (hfs_volume_probe(&volume, test_image_read, NULL, 0, TEST_IMAGE_SIZE) !=
        HFS_OK)
        return 0;
    ++deep[0];

    struct hfs_check_report report;
    (void)hfs_check_volume(&volume, &report);

    struct hfs_catalog catalog;
    if (hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch)) != HFS_OK)
        return 0;
    ++deep[1];

    static const char *const paths[] = {
        "/.firmware", "/.firmware/current.img", "/current.img", "/missing",
        "/.firmware/../../.firmware",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        struct hfs_entry entry;
        if (hfs_catalog_lookup(&catalog, paths[i], &entry) == HFS_OK) {
            ++deep[2];
            if (entry.kind == 2)
                read_entry(&catalog, &entry);
        }
    }

    /* The root directory: 2 is the first user folder id in HFS+. */
    struct hfs_catalog_iterator iterator;
    if (hfs_catalog_iter_init(&catalog, 2u, &iterator) != HFS_OK)
        return 0;

    struct hfs_entry entry;
    uint32_t steps = 0;
    int rc;
    while ((rc = hfs_catalog_iter_next(&catalog, &iterator, &entry)) > 0) {
        if (++steps > MAX_STEPS) {
            report_failure("catalog walk did not terminate", seed, iteration);
            return 1;
        }
        ++deep[3];
        if (entry.kind == 2)
            read_entry(&catalog, &entry);
    }

    /*
     * The extents-overflow tree directly, because a mutated catalog can hide a
     * file whose fork lives entirely in the overflow tree.
     */
    struct hfs_extents extents;
    if (hfs_extents_open(&extents, &volume, scratch, sizeof(scratch)) == HFS_OK) {
        struct hfs_extent overflow[HFS_OVERFLOW_EXTENT_MAX];
        size_t count = 0;
        (void)hfs_extents_collect(&extents, 4u, 1u, 2u, overflow,
                                  HFS_OVERFLOW_EXTENT_MAX, &count);
        ++deep[4];
    }

    return 0;
}

int main(int argc, char **argv)
{
    uint32_t seed = 0x48464d31u; /* "HFM1" */
    uint32_t iterations = DEFAULT_ITERATIONS;
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        iterations = (uint32_t)strtoul(argv[2], NULL, 0);

    struct test_fs_image fixture = test_fs_image_make_overflow();
    (void)fixture;
    memcpy(pristine, test_image, sizeof(pristine));

    int deep[5] = { 0, 0, 0, 0, 0 };
    rng_state = seed;

    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
        mutation_count = 1u + next_random() % MAX_MUTATIONS;
        for (uint32_t i = 0; i < mutation_count; ++i) {
            struct mutation *m = &mutations[i];
            m->width = widths[next_random() % 3u];
            m->offset = pick_offset() & ~(m->width - 1u);
            if (m->offset > TEST_IMAGE_SIZE - m->width)
                m->offset = TEST_IMAGE_SIZE - m->width;
            if ((next_random() % 2u) == 0u)
                m->value = extreme_values[next_random() %
                                          (sizeof(extreme_values) /
                                           sizeof(extreme_values[0]))];
            else
                m->value = next_random();
            for (uint32_t b = 0; b < m->width; ++b)
                test_image[m->offset + b] =
                    (uint8_t)(m->value >> (8u * (m->width - 1u - b)));
        }

        if (exercise(seed, iteration, deep) != 0) {
            memset(test_image, 0, sizeof(test_image));
            return 1;
        }
        memcpy(test_image, pristine, sizeof(test_image));
    }

    /*
     * A sweep that never gets past the probe proves nothing. These floors are
     * deliberately loose: they fail only if the fixture or the mutation budget
     * drifts so far that the deep readers stop being reached.
     */
    CHECK(deep[0] > (int)(iterations / 8u));
    CHECK(deep[1] > (int)(iterations / 16u));
    CHECK(deep[2] > 0);
    CHECK(deep[3] > 0);
    CHECK(deep[4] > 0);

    printf("  image mutation: %u iterations, %d probed, %d opened, "
           "%d looked up, %d entries, %d overflow trees\n",
           iterations, deep[0], deep[1], deep[2], deep[3], deep[4]);
    return 0;
}
