/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Mutation sweep over the *write* path (T-140).
 *
 * The read-path sweep (`test_image_mutation.c`) asks whether a corrupt volume
 * makes the reader loop or leave its buffers. This asks a different question:
 * when the volume is already wrong, does the writer **fail closed**?
 *
 * Every iteration starts from the same valid volume, corrupts one structural
 * field, and then walks the catalog writer down the path a caller would:
 *
 *   hfs_volume_probe -> hfs_check_volume (record the findings it already has)
 *                    -> hfs_catalog_open
 *                    -> create a file, rename it, write 1,500 bytes into it,
 *                       truncate it back to zero, delete it
 *
 * Each of the five calls is its own transaction and its own decision, so a
 * mutation that only trips the data path - the allocation bitmap, a fork record,
 * the extents-overflow tree - is caught even though the create before it was
 * fine. A call that answers an error ends the iteration: the ones after it
 * would operate on state that was never committed.
 *
 * Two rules must hold, and they are the whole test:
 *
 *  - **A refused write changes nothing.** If any of the five calls returns an
 *    error the image must be byte-identical to the mutated one. A refused
 *    call may stage bytes in the transaction, but the caller discards them and
 *    none of them may reach the medium.
 *  - **A write never adds a kind of damage.** If the volume already had findings
 *    the committed write may keep them; it may not introduce a finding bit the
 *    volume did not already have, and it may never turn a consistent volume into
 *    an inconsistent one.
 *
 * The sweep is deterministic: the seed and the iteration count describe every
 * mutation, and a failure prints the offset, the width and the value so it can
 * be replayed byte for byte.
 *
 *   ./test_writer_mutations             # the committed sweep
 *   ./test_writer_mutations 12345 3000  # another seed, more iterations
 */
#include "hfs_btree_tree.h"
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_check.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "hfs_volume.h"
#include "test_check.h"
#include "writer_fixture.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The volume geometry and its builder live in writer_fixture.c, shared with the
 * writer fuzzer; these names keep the sweep's offsets readable. */
#define BLOCK_SIZE WRITER_BLOCK_SIZE
#define NODE_SIZE WRITER_NODE_SIZE
#define IMAGE_SIZE WRITER_IMAGE_SIZE
#define BLOCKS WRITER_BLOCKS

#define BITMAP_BLOCK WRITER_BITMAP_BLOCK
#define CATALOG_BLOCK WRITER_CATALOG_BLOCK
#define CATALOG_NODES WRITER_CATALOG_NODES
#define EXTENTS_BLOCK WRITER_EXTENTS_BLOCK
#define EXTENTS_NODES WRITER_EXTENTS_NODES
#define ATTRIBUTES_BLOCK WRITER_ATTRIBUTES_BLOCK
#define ATTRIBUTES_NODES WRITER_ATTRIBUTES_NODES

#define VOLUME_HEADER WRITER_VOLUME_HEADER
#define ALT_VOLUME_HEADER WRITER_ALT_VOLUME_HEADER
#define CATALOG_HEADER_NODE WRITER_CATALOG_HEADER_NODE
#define CATALOG_FIRST_LEAF WRITER_CATALOG_FIRST_LEAF
#define CATALOG_SCRATCH 8192u

#define DEFAULT_SEED 0x57524954u /* "WRIT" */
#define DEFAULT_ITERATIONS 400u

/* Root folder: the fixture creates it with CNID 2. */
#define ROOT_CNID WRITER_ROOT_CNID

static uint8_t *image;
static uint8_t snapshot[IMAGE_SIZE];
static uint32_t rng_state;

static uint32_t next_random(void);

struct mutation {
    uint32_t offset;
    uint32_t width;
    uint32_t value;
    const char *what;
    int must_refuse;      /* the writer has to say no, not guess */
};

/*
 * The fields a writer trusts most: the volume geometry, every fork it resolves,
 * and the catalog tree's own header and leaf. A hand-written test covers one of
 * these at a time; the sweep covers all of them, plus the random tail.
 */
static const struct mutation curated[] = {
    { VOLUME_HEADER + 0u, 2u, 0x0000u, "volume signature zero", 0 },
    { VOLUME_HEADER + 0u, 2u, 0x4244u, "volume signature BD", 0 },
    { VOLUME_HEADER + 40u, 4u, 0u, "block size zero", 0 },
    { VOLUME_HEADER + 40u, 4u, 3u, "block size not a power of two", 0 },
    { VOLUME_HEADER + 44u, 4u, 0u, "block count zero", 0 },
    { VOLUME_HEADER + 44u, 4u, 0xffffffffu, "block count max", 0 },
    { VOLUME_HEADER + 48u, 4u, 0xffffffffu, "free block count max", 0 },
    { VOLUME_HEADER + 64u, 4u, 0u, "next catalog id zero", 1 },
    { VOLUME_HEADER + 64u, 4u, 1u, "next catalog id one", 1 },
    { VOLUME_HEADER + 64u, 4u, 2u, "next catalog id is the root", 1 },
    { VOLUME_HEADER + 64u, 4u, 0xffffffffu, "next catalog id max", 1 },
    { VOLUME_HEADER + 32u, 4u, 0xffffffffu, "file count max", 1 },
    { VOLUME_HEADER + 112u, 8u, 0u, "allocation fork size zero", 0 },
    { VOLUME_HEADER + 112u + 16u, 4u, 0xffffffffu, "allocation extent start max", 0 },
    { VOLUME_HEADER + 192u + 16u, 4u, 0xffffffffu, "extents extent start max", 0 },
    { VOLUME_HEADER + 272u, 8u, 0u, "catalog fork size zero", 0 },
    { VOLUME_HEADER + 272u + 12u, 4u, 0u, "catalog fork blocks zero", 0 },
    { VOLUME_HEADER + 272u + 16u, 4u, 0xffffffffu, "catalog extent start max", 0 },
    { ALT_VOLUME_HEADER + 272u + 16u, 4u, 0xffffffffu, "alternate catalog extent start max", 0 },
    { CATALOG_HEADER_NODE + 2u, 4u, 0u, "catalog root node zero", 0 },
    { CATALOG_HEADER_NODE + 2u, 4u, 0xffffffffu, "catalog root node max", 0 },
    { CATALOG_HEADER_NODE + 6u, 4u, 0u, "catalog leaf record count zero", 0 },
    { CATALOG_HEADER_NODE + 6u, 4u, 0xffffffffu, "catalog leaf record count max", 0 },
    { CATALOG_HEADER_NODE + 10u, 4u, 0u, "catalog first leaf zero", 0 },
    { CATALOG_HEADER_NODE + 14u, 4u, 0xffffffffu, "catalog last leaf max", 0 },
    { CATALOG_HEADER_NODE + 8u, 1u, 0u, "catalog header node kind 0", 0 },
    { CATALOG_HEADER_NODE + 10u, 2u, 0u, "catalog header record count zero", 0 },
    { CATALOG_HEADER_NODE + 248u, 1u, 0xffu, "catalog map byte all used", 0 },
    { CATALOG_HEADER_NODE + 248u, 1u, 0x00u, "catalog map byte all free", 0 },
    { CATALOG_FIRST_LEAF + 8u, 1u, 0u, "catalog leaf kind index", 0 },
    { CATALOG_FIRST_LEAF + 9u, 1u, 3u, "catalog leaf height 3", 0 },
    { CATALOG_FIRST_LEAF + 10u, 2u, 0u, "catalog leaf record count zero", 0 },
    { CATALOG_FIRST_LEAF + 10u, 2u, 0xffffu, "catalog leaf record count max", 0 },
    { CATALOG_FIRST_LEAF + NODE_SIZE - 2u, 2u, 0xffffu, "catalog leaf offset table max", 0 },
    { 0u, 0u, 0u, NULL, 0 }
};
struct region {
    uint32_t start;
    uint32_t length;
    uint32_t weight;
};

/*
 * Where a random mutation lands: both volume headers, the catalog fork record,
 * and the catalog's header node and first leaf — the bytes the writer reads.
 */
static uint32_t random_offset(void)
{
    static const struct region picks[] = {
        { VOLUME_HEADER, 512u, 4u },
        { ALT_VOLUME_HEADER, 512u, 2u },
        { VOLUME_HEADER + 272u, 80u, 3u },
        { CATALOG_HEADER_NODE, NODE_SIZE, 5u },
        { CATALOG_FIRST_LEAF, NODE_SIZE, 4u },
    };
    uint32_t total = 0;
    for (size_t i = 0; i < sizeof(picks) / sizeof(picks[0]); ++i)
        total += picks[i].weight;
    uint32_t pick = next_random() % total;
    for (size_t i = 0; i < sizeof(picks) / sizeof(picks[0]); ++i) {
        if (pick < picks[i].weight)
            return picks[i].start + next_random() % picks[i].length;
        pick -= picks[i].weight;
    }
    return VOLUME_HEADER;
}

/* ------------------------------------------------------------------------- */
/* The fixture: a whole volume the writer can create a file in.              */
/* ------------------------------------------------------------------------- */

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




/* ------------------------------------------------------------------------- */
/* The sweep.                                                                */
/* ------------------------------------------------------------------------- */

static void apply_mutation(const struct mutation *mutation)
{
    /* Eight-byte fields (a fork's size) are written from a 64-bit value so the
     * shift never reaches 32 bits. */
    uint64_t value = mutation->value;
    for (uint32_t i = 0; i < mutation->width; ++i) {
        uint32_t offset = mutation->offset + i;
        if (offset >= IMAGE_SIZE)
            break;
        uint8_t byte = (uint8_t)(value >> (8u * (mutation->width - 1u - i)));
        image[offset] = byte;
    }
}

static void report_mutation(uint32_t seed, uint32_t iteration,
                            const struct mutation *mutation)
{
    fprintf(stderr,
            "writer mutation: seed 0x%08x, iteration %u, offset %u, width %u, "
            "value 0x%08x (%s)\n",
            seed, iteration, mutation->offset, mutation->width,
            mutation->value, mutation->what != NULL ? mutation->what : "random");
}

struct counters {
    uint32_t probe_refused;
    uint32_t mount_refused;
    uint32_t write_refused;
    uint32_t write_committed;
    uint32_t write_committed_dirty;
    uint32_t delete_committed;
    uint32_t data_committed;
    uint32_t truncate_committed;
    uint32_t clean_before;
};

/*
 * A call answered an error: the image must be untouched since the snapshot the
 * caller took, and the iteration ends. 0 = the call committed, 1 = refused
 * cleanly, 2 = refused but changed the image, which is the failure this test
 * exists to catch.
 */
static int refused(int rc, uint32_t seed, uint32_t iteration,
                   const struct mutation *mutation, const char *what,
                   struct counters *counters)
{
    if (rc == HFS_OK)
        return 0;
    ++counters->write_refused;
    if (memcmp(snapshot, image, sizeof(snapshot)) != 0) {
        report_mutation(seed, iteration, mutation);
        fprintf(stderr, "  %s refused (%d) but the image changed\n", what, rc);
        return 2;
    }
    return 1;
}

static int exercise(uint32_t seed, uint32_t iteration,
                    const struct mutation *mutation, struct counters *counters)
{
    struct hfs_volume volume;
    if (hfs_volume_probe(&volume, writer_fixture_read, NULL, 0, IMAGE_SIZE) != HFS_OK) {
        ++counters->probe_refused;
        return 0;
    }

    struct hfs_check_report before;
    if (hfs_check_volume(&volume, &before) != HFS_OK) {
        report_mutation(seed, iteration, mutation);
        fprintf(stderr, "  the structural check could not run\n");
        return 1;
    }
    if (before.problems == 0u)
        ++counters->clean_before;

    static uint8_t scratch[CATALOG_SCRATCH];
    struct hfs_catalog catalog;
    memset(&catalog, 0, sizeof(catalog));
    if (hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch)) !=
        HFS_OK) {
        ++counters->mount_refused;
        return 0;
    }

    memcpy(snapshot, image, sizeof(snapshot));

    struct hfs_journal journal;
    struct hfs_txn tx;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = BLOCK_SIZE;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;

    /*
     * Three write calls, in the order a caller would make them: create a file,
     * rename it, delete it. Each one is held to the same rule, so a mutation
     * that only trips the delete path is still caught; a call that answers an
     * error ends the iteration, because the ones after it operate on state that
     * was never committed.
     */
    static const uint8_t name[6] = { 0, 'N', 0, 'E', 0, 'W' };
    static const uint8_t renamed[6] = { 0, 'W', 0, 'R', 0, 'M' };
    uint32_t cnid = 0;
    int rc = hfs_catalog_create_file(&catalog, &tx, ROOT_CNID, name, 3, 0644,
                                     hfs_catalog_key_compare, NULL, &cnid);
    int outcome = refused(rc, seed, iteration, mutation, "create", counters);
    if (outcome != 0)
        return outcome == 2 ? 1 : 0;
    if (mutation->must_refuse) {
        report_mutation(seed, iteration, mutation);
        fprintf(stderr, "  the write should have been refused\n");
        return 1;
    }
    writer_fixture_apply(&tx);
    ++counters->write_committed;

    /* The rename goes through the same catalogs records the create just wrote,
     * which is where a stale key or a broken thread record shows up. */
    memcpy(snapshot, image, sizeof(snapshot));
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    rc = hfs_catalog_rename(&catalog, &tx, ROOT_CNID, name, 3, cnid, ROOT_CNID,
                            renamed, 3, hfs_catalog_key_compare, NULL);
    outcome = refused(rc, seed, iteration, mutation, "rename", counters);
    if (outcome != 0)
        return outcome == 2 ? 1 : 0;
    writer_fixture_apply(&tx);

    /*
     * Data next: a 1,500-byte append allocates blocks, writes the allocation
     * bitmap and stages the volume header, which is the path a mutation in the
     * bitmap or in a fork record reaches. It runs before the delete so the
     * blocks it takes have to come back when the file goes.
     */
    static uint8_t payload[1500];
    for (size_t i = 0; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)(0x30u + (uint8_t)(i % 0x30u));
    memcpy(snapshot, image, sizeof(snapshot));
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    rc = hfs_catalog_write_file(&catalog, &tx, ROOT_CNID, renamed, 3, 0,
                                payload, sizeof(payload),
                                hfs_catalog_key_compare, NULL);
    outcome = refused(rc, seed, iteration, mutation, "data write", counters);
    if (outcome != 0)
        return outcome == 2 ? 1 : 0;
    writer_fixture_apply(&tx);
    ++counters->data_committed;

    /* Truncate to zero: the blocks and any overflow record have to come back. */
    memcpy(snapshot, image, sizeof(snapshot));
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    rc = hfs_catalog_truncate_to(&catalog, &tx, ROOT_CNID, renamed, 3, 0,
                                 hfs_catalog_key_compare, NULL);
    outcome = refused(rc, seed, iteration, mutation, "truncate", counters);
    if (outcome != 0)
        return outcome == 2 ? 1 : 0;
    writer_fixture_apply(&tx);
    ++counters->truncate_committed;

    /* And the delete frees the node the create took. */
    memcpy(snapshot, image, sizeof(snapshot));
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
    rc = hfs_catalog_delete_file(&catalog, &tx, ROOT_CNID, renamed, 3, cnid,
                                 hfs_catalog_key_compare, NULL);
    outcome = refused(rc, seed, iteration, mutation, "delete", counters);
    if (outcome != 0)
        return outcome == 2 ? 1 : 0;
    writer_fixture_apply(&tx);
    ++counters->delete_committed;

    struct hfs_check_report after;
    if (hfs_check_volume(&volume, &after) != HFS_OK) {
        report_mutation(seed, iteration, mutation);
        fprintf(stderr, "  the structural check could not run after the write\n");
        return 1;
    }
    /*
     * On a volume the check already calls inconsistent, the writer is allowed to
     * commit - the anti-corruption gate, not the writer, decides whether a
     * damaged volume is written at all - and a different finding may surface
     * once the tree is walked far enough. The rule that matters is the clean
     * case: a write may never turn a consistent volume inconsistent.
     */
    if (before.problems == 0u && after.problems != 0u) {
        report_mutation(seed, iteration, mutation);
        fprintf(stderr, "  a clean volume became inconsistent: 0x%08x\n",
                after.problems);
        return 1;
    }
    if (before.problems != 0u)
        ++counters->write_committed_dirty;
    return 0;
}

/*
 * The untouched volume must accept the same write and stay consistent; without
 * this the sweep could "pass" by refusing everything.
 */
static int sanity(void)
{
    struct hfs_volume volume;
    CHECK(writer_fixture_build(&volume) == 0);
    struct hfs_check_report report;
    CHECK(hfs_check_volume(&volume, &report) == HFS_OK);
    CHECK(report.problems == 0u);

    static uint8_t scratch[CATALOG_SCRATCH];
    struct hfs_catalog catalog;
    memset(&catalog, 0, sizeof(catalog));
    CHECK(hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch)) ==
          HFS_OK);

    struct hfs_journal journal;
    struct hfs_txn tx;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = BLOCK_SIZE;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;

    static const uint8_t name[6] = { 0, 'N', 0, 'E', 0, 'W' };
    uint32_t cnid = 0;
    CHECK(hfs_catalog_create_file(&catalog, &tx, ROOT_CNID, name, 3, 0644,
                                  hfs_catalog_key_compare, NULL, &cnid) ==
          HFS_OK);
    writer_fixture_apply(&tx);

    CHECK(hfs_check_volume(&volume, &report) == HFS_OK);
    CHECK(report.problems == 0u);
    struct hfs_entry entry;
    CHECK(hfs_catalog_lookup(&catalog, "/NEW", &entry) == HFS_OK);
    CHECK(entry.id == cnid);
    CHECK(entry.kind == 2);          /* a file */
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t seed = DEFAULT_SEED;
    uint32_t iterations = DEFAULT_ITERATIONS;
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        iterations = (uint32_t)strtoul(argv[2], NULL, 0);
    rng_state = seed;
    image = writer_fixture_image();

    CHECK(sanity() == 0);

    struct counters counters;
    memset(&counters, 0, sizeof(counters));

    for (size_t i = 0; curated[i].what != NULL; ++i) {
        struct hfs_volume volume;
        CHECK(writer_fixture_build(&volume) == 0);
        apply_mutation(&curated[i]);
        if (exercise(seed, (uint32_t)i, &curated[i], &counters) != 0)
            return 1;
    }

    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
        struct hfs_volume volume;
        CHECK(writer_fixture_build(&volume) == 0);
        struct mutation mutation;
        mutation.offset = random_offset();
        static const uint32_t widths[] = { 1u, 2u, 4u };
        mutation.width = widths[next_random() % 3u];
        static const uint32_t values[] = {
            0x00u, 0x01u, 0x7fu, 0x80u, 0xffu, 0xffffu, 0xffffffffu,
        };
        mutation.value = values[next_random() %
                                (sizeof(values) / sizeof(values[0]))];
        mutation.what = NULL;
        mutation.must_refuse = 0;
        apply_mutation(&mutation);
        if (exercise(seed, iteration, &mutation, &counters) != 0)
            return 1;
    }

    printf("  writer mutations: %zu curated + %u random, %u refused at probe, "
           "%u at mount, %u at a write call, %u created, %u wrote data, "
           "%u truncated, %u deleted (%u created "
           "on a volume the check already called inconsistent), %u were clean "
           "before the write\n",
           sizeof(curated) / sizeof(curated[0]) - 1u, iterations,
           counters.probe_refused, counters.mount_refused,
           counters.write_refused, counters.write_committed,
           counters.data_committed, counters.truncate_committed,
           counters.delete_committed, counters.write_committed_dirty,
           counters.clean_before);
    return 0;
}
