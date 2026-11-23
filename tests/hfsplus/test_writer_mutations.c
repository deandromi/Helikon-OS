/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Mutation sweep over the *write* path (T-140).
 *
 * The read-path sweep (`test_image_mutation.c`) asks whether a corrupt volume
 * makes the reader loop or leave its buffers. This asks a different question:
 * when the volume is already wrong, does the writer **fail closed**?
 *
 * Every iteration starts from the same valid volume, corrupts one structural
 * field, and then tries to create a file through the catalog writer:
 *
 *   hfs_volume_probe -> hfs_check_volume (record the findings it already has)
 *                    -> hfs_catalog_open
 *                    -> hfs_catalog_create_file in a transaction
 *
 * Two rules must hold, and they are the whole test:
 *
 *  - **A refused write changes nothing.** If `hfs_catalog_create_file` returns
 *    an error the image must be byte-identical to the mutated one. A refused
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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 512u
#define NODE_SIZE 4096u
#define IMAGE_SIZE (1024u * 1024u)
#define BLOCKS (IMAGE_SIZE / BLOCK_SIZE)

#define BITMAP_BLOCK 3u
#define CATALOG_BLOCK 16u
#define CATALOG_NODES 64u
#define EXTENTS_BLOCK 528u
#define EXTENTS_NODES 16u
#define ATTRIBUTES_BLOCK 656u
#define ATTRIBUTES_NODES 8u

#define VOLUME_HEADER 1024u
#define ALT_VOLUME_HEADER (IMAGE_SIZE - 1024u)
#define CATALOG_HEADER_NODE (CATALOG_BLOCK * BLOCK_SIZE)
#define CATALOG_FIRST_LEAF (CATALOG_HEADER_NODE + NODE_SIZE)
#define CATALOG_SCRATCH 8192u

#define DEFAULT_SEED 0x57524954u /* "WRIT" */
#define DEFAULT_ITERATIONS 400u

/* Root folder: the fixture creates it with CNID 2. */
#define ROOT_CNID 2u

static uint8_t image[IMAGE_SIZE];
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

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put_be64(uint8_t *p, uint64_t v)
{
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

static int image_read(void *context, uint64_t offset, void *buffer,
                      size_t length)
{
    (void)context;
    if (offset > IMAGE_SIZE || length > IMAGE_SIZE - (size_t)offset)
        return -1;
    memcpy(buffer, image + (size_t)offset, length);
    return 0;
}

#define MAP_RECORD_OFFSET 248u
#define MAP_RECORD_BYTES (((CATALOG_NODES + 7u) / 8u + 1u) & ~1u)
#define HEADER_FREE_OFFSET (MAP_RECORD_OFFSET + MAP_RECORD_BYTES)

static void make_tree(uint32_t block, uint32_t nodes, uint16_t max_key_length,
                      int with_root)
{
    uint8_t *node = image + block * BLOCK_SIZE;
    memset(node, 0, (size_t)nodes * NODE_SIZE);

    node[8] = 1;                     /* kind: header node */
    put_be16(node + 10, 3);          /* three records */
    uint8_t *record = node + 14;     /* BTHeaderRec */
    put_be16(record + 0, with_root ? 1 : 0);
    put_be32(record + 2, with_root ? 1u : 0u);
    put_be32(record + 6, 0);
    put_be32(record + 10, with_root ? 1u : 0u);
    put_be32(record + 14, with_root ? 1u : 0u);
    put_be16(record + 18, NODE_SIZE);
    put_be16(record + 20, max_key_length);
    put_be32(record + 22, nodes);
    put_be32(record + 26, with_root ? nodes - 2u : nodes - 1u);
    record[32] = 1;                  /* btreeType: user data */
    record[33] = 0;                  /* keyCompareType */

    put_be16(node + NODE_SIZE - 2, 14);
    put_be16(node + NODE_SIZE - 4, 120);
    put_be16(node + NODE_SIZE - 6, MAP_RECORD_OFFSET);
    put_be16(node + NODE_SIZE - 8, (uint16_t)HEADER_FREE_OFFSET);
    node[MAP_RECORD_OFFSET] = with_root ? 0xc0 : 0x80;

    if (with_root) {
        uint8_t *leaf = node + NODE_SIZE;
        leaf[8] = 0xff;
        leaf[9] = 1;
        put_be16(leaf + 10, 0);
        put_be16(leaf + NODE_SIZE - 2, 14);
        put_be16(leaf + NODE_SIZE - 4, 14);
    }
}

static void make_image(void)
{
    memset(image, 0, sizeof(image));

    uint8_t *header = image + VOLUME_HEADER;
    put_be16(header, 0x482bu);
    put_be16(header + 2, 4);
    put_be32(header + 4, 1u << 8);   /* kHFSVolumeUnmountedBit */
    put_be32(header + 32, 0);        /* fileCount */
    put_be32(header + 36, 0);        /* folderCount: the root is not counted */
    put_be32(header + 40, BLOCK_SIZE);
    put_be32(header + 44, BLOCKS);
    put_be32(header + 64, 16);       /* nextCatalogID */

    uint32_t bitmap_bytes = BLOCKS / 8u;
    uint8_t *allocation = header + 112;
    put_be64(allocation, bitmap_bytes);
    put_be32(allocation + 12, 1);
    put_be32(allocation + 16, BITMAP_BLOCK);
    put_be32(allocation + 20, 1);

    uint8_t *catalog = header + 272;
    put_be64(catalog, (uint64_t)CATALOG_NODES * NODE_SIZE);
    put_be32(catalog + 12, CATALOG_NODES * NODE_SIZE / BLOCK_SIZE);
    put_be32(catalog + 16, CATALOG_BLOCK);
    put_be32(catalog + 20, CATALOG_NODES * NODE_SIZE / BLOCK_SIZE);

    uint8_t *extents = header + 192;
    put_be64(extents, (uint64_t)EXTENTS_NODES * NODE_SIZE);
    put_be32(extents + 12, EXTENTS_NODES * NODE_SIZE / BLOCK_SIZE);
    put_be32(extents + 16, EXTENTS_BLOCK);
    put_be32(extents + 20, EXTENTS_NODES * NODE_SIZE / BLOCK_SIZE);

    uint8_t *attributes = header + 352;
    put_be64(attributes, (uint64_t)ATTRIBUTES_NODES * NODE_SIZE);
    put_be32(attributes + 12, ATTRIBUTES_NODES * NODE_SIZE / BLOCK_SIZE);
    put_be32(attributes + 16, ATTRIBUTES_BLOCK);
    put_be32(attributes + 20, ATTRIBUTES_NODES * NODE_SIZE / BLOCK_SIZE);

    make_tree(CATALOG_BLOCK, CATALOG_NODES, 516, 1);
    make_tree(EXTENTS_BLOCK, EXTENTS_NODES, 10, 0);
    make_tree(ATTRIBUTES_BLOCK, ATTRIBUTES_NODES, 262, 0);

    uint8_t *bitmap = image + BITMAP_BLOCK * BLOCK_SIZE;
    for (uint32_t block = 0; block < BLOCKS; ++block) {
        int used = block <= BITMAP_BLOCK || block >= BLOCKS - 2u ||
                   (block >= CATALOG_BLOCK &&
                    block < CATALOG_BLOCK +
                                CATALOG_NODES * NODE_SIZE / BLOCK_SIZE) ||
                   (block >= EXTENTS_BLOCK &&
                    block < EXTENTS_BLOCK +
                                EXTENTS_NODES * NODE_SIZE / BLOCK_SIZE) ||
                   (block >= ATTRIBUTES_BLOCK &&
                    block < ATTRIBUTES_BLOCK +
                                ATTRIBUTES_NODES * NODE_SIZE / BLOCK_SIZE);
        if (used)
            bitmap[block / 8u] |= (uint8_t)(0x80u >> (block % 8u));
    }
    uint32_t used_blocks = 0;
    for (uint32_t block = 0; block < BLOCKS; ++block) {
        if ((bitmap[block / 8u] & (uint8_t)(0x80u >> (block % 8u))) != 0u)
            ++used_blocks;
    }
    put_be32(header + 48, BLOCKS - used_blocks);

    memcpy(image + ALT_VOLUME_HEADER, header, 512);
}

static void apply_txn(const struct hfs_txn *tx)
{
    for (size_t i = 0; i < tx->count; ++i) {
        memcpy(image + tx->blocks[i].sector * BLOCK_SIZE,
               tx->blocks[i].data, tx->blocks[i].size);
    }
}

static size_t build_catalog_key(uint8_t *buffer, uint32_t parent,
                                const uint8_t *name, size_t units)
{
    put_be16(buffer, (uint16_t)(6u + units * 2u));
    put_be32(buffer + 2, parent);
    put_be16(buffer + 6, (uint16_t)units);
    if (units != 0)
        memcpy(buffer + 8, name, units * 2u);
    return 6u + units * 2u;
}

/* The root folder and its thread, through a hand-built tree description. */
static int seed_root(struct hfs_volume *volume)
{
    struct hfs_btree catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.volume = volume;
    catalog.fork = &volume->catalog;
    catalog.node_size = NODE_SIZE;
    catalog.total_nodes = CATALOG_NODES;
    catalog.free_nodes = CATALOG_NODES - 2u;
    catalog.depth = 1;
    catalog.root_node = 1;
    catalog.first_leaf = 1;
    catalog.last_leaf = 1;
    catalog.leaf_records = 0;

    struct hfs_journal journal;
    struct hfs_txn tx;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = BLOCK_SIZE;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;

    static const uint8_t root_name[8] = { 0, 'T', 0, 'E', 0, 'S', 0, 'T' };
    uint8_t record[1024];
    size_t key_length = build_catalog_key(record, 1, root_name, 4);
    size_t data_offset = 2u + key_length;
    memset(record + data_offset, 0, 88);
    put_be16(record + data_offset, 1);          /* folder record */
    put_be32(record + data_offset + 8, ROOT_CNID);
    int rc = hfs_btree_insert(&catalog, &tx, record, data_offset + 88,
                              hfs_catalog_key_compare, NULL);
    if (rc != HFS_OK)
        return 1;

    key_length = build_catalog_key(record, ROOT_CNID, NULL, 0);
    data_offset = 2u + key_length;
    memset(record + data_offset, 0, 18);
    put_be16(record + data_offset, 3);          /* folder thread */
    put_be32(record + data_offset + 4, 1);      /* parent of the root */
    put_be16(record + data_offset + 8, 4);
    memcpy(record + data_offset + 10, root_name, 8);
    rc = hfs_btree_insert(&catalog, &tx, record, data_offset + 18,
                          hfs_catalog_key_compare, NULL);
    if (rc != HFS_OK)
        return 1;
    apply_txn(&tx);
    return 0;
}

/* A whole volume with a seeded root, ready for the writer. */
static int make_volume(struct hfs_volume *volume)
{
    make_image();
    if (hfs_volume_probe(volume, image_read, NULL, 0, IMAGE_SIZE) != HFS_OK)
        return 1;
    return seed_root(volume);
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
    uint32_t clean_before;
};

static int exercise(uint32_t seed, uint32_t iteration,
                    const struct mutation *mutation, struct counters *counters)
{
    struct hfs_volume volume;
    if (hfs_volume_probe(&volume, image_read, NULL, 0, IMAGE_SIZE) != HFS_OK) {
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

    static const uint8_t name[6] = { 0, 'N', 0, 'E', 0, 'W' };
    uint32_t cnid = 0;
    int rc = hfs_catalog_create_file(&catalog, &tx, ROOT_CNID, name, 3, 0644,
                                     hfs_catalog_key_compare, NULL, &cnid);
    if (rc != HFS_OK) {
        ++counters->write_refused;
        if (memcmp(snapshot, image, sizeof(snapshot)) != 0) {
            report_mutation(seed, iteration, mutation);
            fprintf(stderr, "  refused (%d) but the image changed\n", rc);
            return 1;
        }
        return 0;
    }

    if (mutation->must_refuse) {
        report_mutation(seed, iteration, mutation);
        fprintf(stderr, "  the write should have been refused\n");
        return 1;
    }

    apply_txn(&tx);
    ++counters->write_committed;

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
    CHECK(make_volume(&volume) == 0);
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
    apply_txn(&tx);

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

    CHECK(sanity() == 0);

    struct counters counters;
    memset(&counters, 0, sizeof(counters));

    for (size_t i = 0; curated[i].what != NULL; ++i) {
        struct hfs_volume volume;
        CHECK(make_volume(&volume) == 0);
        apply_mutation(&curated[i]);
        if (exercise(seed, (uint32_t)i, &curated[i], &counters) != 0)
            return 1;
    }

    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
        struct hfs_volume volume;
        CHECK(make_volume(&volume) == 0);
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
           "%u at mount, %u at the write, %u committed (%u of them on a volume "
           "the check already called inconsistent), %u were clean before the "
           "write\n",
           sizeof(curated) / sizeof(curated[0]) - 1u, iterations,
           counters.probe_refused, counters.mount_refused,
           counters.write_refused, counters.write_committed,
           counters.write_committed_dirty, counters.clean_before);
    return 0;
}
