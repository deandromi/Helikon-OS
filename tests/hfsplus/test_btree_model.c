/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * B-tree-level model test (T-138): random inserts, deletes and in-place updates
 * against a real extents-overflow tree, compared after every operation with a
 * sorted model of the keys and their data.
 *
 * Why the extents tree and not the catalog: its records are a 10-byte key and 64
 * bytes of data with no per-type body to keep valid, so the test is about the
 * tree itself - splits, index growth, root changes, leaf merges, freed nodes -
 * and not about catalog record layout, which T-136 covers. The fixture is still a
 * complete volume with a bitmap, an alternate header and a seeded catalog, so
 * `hfs_check_volume` runs after every step: key order across leaf boundaries,
 * the leaf chain, every fork against the bitmap, the free-block count and node
 * ownership (T-137).
 *
 * What the model promises, and what a failure means:
 *  - `hfs_btree_find` returns every present key's record, byte for byte. A miss
 *    or a wrong body is a corrupt tree: a search path lost a record.
 *  - Walking the leaf chain visits exactly the present keys, strictly ascending.
 *    A missing, repeated or out-of-order record is a split or merge that dropped
 *    or duplicated one.
 *  - A delete that is refused - the first record of a multi-record non-root node
 *    is `HFS_ERR_UNSUPPORTED` by design, and `HFS_ERR_NO_SPACE` when the map has
 *    no free node left - must leave the image byte-identical. A refusal that
 *    changed something is a half-applied transaction.
 *  - Nothing ever answers `HFS_ERR_FORMAT`, and `hfs_check_volume` never finds
 *    anything: a tree that its own structural check rejects is corrupt whatever
 *    the search results say.
 *  - No index node holds fewer than two records. That is a shape rule, not a
 *    consistency rule - a one-child index node passes the structural check - and
 *    it is the one thing a delete used to leave behind: a level of the tree that
 *    holds one child, never freed and never merged, so a drained tree kept chains
 *    of them (T-150).
 *
 * After the random sweep an ordered pass runs, because random deletes rarely
 * empty a leaf in the order that forces the cascades. It drains the tree one
 * ascending key at a time - the first record of the leftmost leaf, so the parent
 * key is rewritten every step - and requires the canonical empty tree at the end:
 * depth 0, no root, no chain, every node but the header back on the map. Then it
 * fills from empty with ascending keys (rightmost splits, a root that grows out of
 * depth 0), requires depth 2 or more, drains again, reopens the tree from the
 * medium and inserts three keys through the reopened handle. The first version of
 * that pass found issue #125: the last delete of a deep tree answered
 * `HFS_ERR_UNSUPPORTED`, so a tree could not be emptied from the top.
 */
#include "hfs_btree.h"
#include "hfs_btree_tree.h"
#include "hfs_catalog_compare.h"
#include "hfs_check.h"
#include "hfs_volume.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 512u
#define NODE_SIZE 512u
#define IMAGE_SIZE (1024u * 1024u)
#define BLOCKS (IMAGE_SIZE / BLOCK_SIZE)

#define BITMAP_BLOCK 3u
#define CATALOG_BLOCK 16u
#define CATALOG_NODES 64u
#define EXTENTS_BLOCK 80u
#define EXTENTS_NODES 64u
#define ATTRIBUTES_BLOCK 144u
#define ATTRIBUTES_NODES 8u

#define EXTENTS_KEY 10u
#define EXTENTS_DATA 64u
/* [key length][key][data]; the key length counts the key, not itself. */
#define RECORD_BYTES (2u + EXTENTS_KEY + EXTENTS_DATA)

#define DEFAULT_STEPS 1800u
#define DEFAULT_SEED 0x42545245u /* "BTRE" */
#define MAX_ITEMS 1024u
#define MAX_FILE_ID 24u
#define MAX_START_BLOCK 256u

static uint8_t image[IMAGE_SIZE];
static uint8_t snapshot[IMAGE_SIZE];

static uint32_t rng_state;

static struct hfs_volume volume;
static struct hfs_btree extents;

/* The model: sorted keys (`id` = file << 32 | start) and their 64 data bytes. */
static uint64_t model_id[MAX_ITEMS];
static uint8_t model_data[MAX_ITEMS][EXTENTS_DATA];
static size_t model_count;

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
/* Fixture.                                                                  */
/* ------------------------------------------------------------------------- */

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

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
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

/* The map record of a header node starts here in every HFS+ node. */
#define MAP_RECORD_OFFSET 248u
#define MAP_RECORD_BYTES 8u          /* 64 nodes: one bit per node */
#define HEADER_FREE_OFFSET (MAP_RECORD_OFFSET + MAP_RECORD_BYTES)

/*
 * One B-tree: a header node with the header record, the user record and the
 * free-node map, and - for a tree that already has a root - an empty leaf.
 */
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
    put_be16(node + NODE_SIZE - 8, HEADER_FREE_OFFSET);
    node[MAP_RECORD_OFFSET] = with_root ? 0xc0 : 0x80;

    if (with_root) {
        uint8_t *leaf = node + NODE_SIZE;
        leaf[8] = 0xff;              /* kind: leaf */
        leaf[9] = 1;                 /* height */
        put_be16(leaf + 10, 0);
        put_be16(leaf + NODE_SIZE - 2, 14);
        put_be16(leaf + NODE_SIZE - 4, 14);
    }
}

static void make_image(void)
{
    memset(image, 0, sizeof(image));

    uint8_t *header = image + 1024;
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

    uint8_t *extents_fork = header + 192;
    put_be64(extents_fork, (uint64_t)EXTENTS_NODES * NODE_SIZE);
    put_be32(extents_fork + 12, EXTENTS_NODES * NODE_SIZE / BLOCK_SIZE);
    put_be32(extents_fork + 16, EXTENTS_BLOCK);
    put_be32(extents_fork + 20, EXTENTS_NODES * NODE_SIZE / BLOCK_SIZE);

    uint8_t *attributes = header + 352;
    put_be64(attributes, (uint64_t)ATTRIBUTES_NODES * NODE_SIZE);
    put_be32(attributes + 12, ATTRIBUTES_NODES * NODE_SIZE / BLOCK_SIZE);
    put_be32(attributes + 16, ATTRIBUTES_BLOCK);
    put_be32(attributes + 20, ATTRIBUTES_NODES * NODE_SIZE / BLOCK_SIZE);

    make_tree(CATALOG_BLOCK, CATALOG_NODES, 516, 1);
    make_tree(EXTENTS_BLOCK, EXTENTS_NODES, EXTENTS_KEY, 0);
    make_tree(ATTRIBUTES_BLOCK, ATTRIBUTES_NODES, 262, 0);

    /* Mark every metadata block used and let the bitmap speak for the rest. */
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

    /* The alternate volume header lives in the last 1024 bytes. */
    memcpy(image + IMAGE_SIZE - 1024, header, 512);
}

static void apply_txn(const struct hfs_txn *tx)
{
    for (size_t i = 0; i < tx->count; ++i) {
        memcpy(image + tx->blocks[i].sector * BLOCK_SIZE,
               tx->blocks[i].data, tx->blocks[i].size);
    }
}

/*
 * The catalog tree must hold the root folder and its thread, or the structural
 * check has a volume the format never produces. This is the same seed the
 * catalog model test uses.
 */
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

static int seed_root(struct hfs_btree *catalog, struct hfs_txn *tx)
{
    static const uint8_t root_name[8] = { 0, 'T', 0, 'E', 0, 'S', 0, 'T' };
    int rc;

    uint8_t record[1024];
    size_t key_length = build_catalog_key(record, 1, root_name, 4);
    size_t data_offset = 2u + key_length;
    memset(record + data_offset, 0, 88);
    put_be16(record + data_offset, 1);          /* folder record */
    put_be32(record + data_offset + 8, 2);      /* CNID 2 */
    rc = hfs_btree_insert(catalog, tx, record, data_offset + 88,
                          hfs_catalog_key_compare, NULL);
    if (rc != HFS_OK) {
        fprintf(stderr, "seed: root folder failed: %d\n", rc);
        CHECK(rc == HFS_OK);
        return 1;
    }

    key_length = build_catalog_key(record, 2, NULL, 0);
    data_offset = 2u + key_length;
    memset(record + data_offset, 0, 18);
    put_be16(record + data_offset, 3);          /* folder thread */
    put_be32(record + data_offset + 4, 1);      /* parent of the root */
    put_be16(record + data_offset + 8, 4);
    memcpy(record + data_offset + 10, root_name, 8);
    rc = hfs_btree_insert(catalog, tx, record, data_offset + 18,
                          hfs_catalog_key_compare, NULL);
    if (rc != HFS_OK) {
        fprintf(stderr, "seed: root thread failed: %d\n", rc);
        CHECK(rc == HFS_OK);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Records and the model.                                                    */
/* ------------------------------------------------------------------------- */

static uint64_t make_id(uint32_t file_id, uint32_t start_block)
{
    return ((uint64_t)file_id << 32) | start_block;
}

static uint32_t id_file(uint64_t id)
{
    return (uint32_t)(id >> 32);
}

static uint32_t id_start(uint64_t id)
{
    return (uint32_t)(id & 0xffffffffu);
}

/*
 * The extents key order: data-fork flag, pad, file ID, start block, all big
 * endian. It is the same order `hfs_extents_write.c` and `hfs_check.c` use.
 */
static int compare_extents(void *context, const uint8_t *a, size_t a_length,
                           const uint8_t *b, size_t b_length)
{
    (void)context;
    if (a_length < EXTENTS_KEY || b_length < EXTENTS_KEY)
        return a_length < b_length ? -1 : 1;
    if (a[0] != b[0])
        return a[0] < b[0] ? -1 : 1;
    uint32_t file_a = get_be32(a + 2);
    uint32_t file_b = get_be32(b + 2);
    if (file_a != file_b)
        return file_a < file_b ? -1 : 1;
    uint32_t start_a = get_be32(a + 6);
    uint32_t start_b = get_be32(b + 6);
    if (start_a != start_b)
        return start_a < start_b ? -1 : 1;
    return 0;
}

static void make_data(uint64_t id, uint32_t tag, uint8_t *data)
{
    put_be64(data, id);
    put_be64(data + 8, id ^ ((uint64_t)tag * 0x3f29a7b1u));
    for (size_t i = 16; i < EXTENTS_DATA; ++i)
        data[i] = (uint8_t)((id >> ((i % 8u) * 8u)) ^ (uint8_t)tag ^
                            (uint8_t)i);
}

static void make_record(uint64_t id, const uint8_t *data, uint8_t *record)
{
    put_be16(record, EXTENTS_KEY);
    record[2] = 0;                   /* data fork */
    record[3] = 0;                   /* pad */
    put_be32(record + 4, id_file(id));
    put_be32(record + 8, id_start(id));
    memcpy(record + 2u + EXTENTS_KEY, data, EXTENTS_DATA);
}

static size_t model_find(uint64_t id)
{
    for (size_t i = 0; i < model_count; ++i) {
        if (model_id[i] == id)
            return i;
    }
    return SIZE_MAX;
}

static void model_insert(uint64_t id, const uint8_t *data)
{
    size_t at = 0;
    while (at < model_count && model_id[at] < id)
        ++at;
    memmove(&model_id[at + 1], &model_id[at],
            (model_count - at) * sizeof(model_id[0]));
    memmove(model_data[at + 1], model_data[at],
            (model_count - at) * EXTENTS_DATA);
    model_id[at] = id;
    memcpy(model_data[at], data, EXTENTS_DATA);
    ++model_count;
}

static void model_remove(size_t at)
{
    memmove(&model_id[at], &model_id[at + 1],
            (model_count - at - 1) * sizeof(model_id[0]));
    memmove(model_data[at], model_data[at + 1],
            (model_count - at - 1) * EXTENTS_DATA);
    --model_count;
}

/* ------------------------------------------------------------------------- */
/* Verification.                                                             */
/* ------------------------------------------------------------------------- */

static struct hfs_txn read_tx;
static struct hfs_journal read_journal;

static void reset_read_tx(void)
{
    memset(&read_journal, 0, sizeof(read_journal));
    read_journal.sector_size = BLOCK_SIZE;
    memset(&read_tx, 0, sizeof(read_tx));
    read_tx.journal = &read_journal;
    read_tx.active = 1;
}

static void fail_report(const char *what, size_t step, const char *detail)
{
    fprintf(stderr, "btree model: %s at step %zu (%s), model %zu keys\n",
            what, step, detail, model_count);
}

/* Every present key's record, byte for byte, through the reader. */
static int verify_find(size_t step)
{
    reset_read_tx();
    for (size_t i = 0; i < model_count; ++i) {
        uint8_t key[EXTENTS_KEY];
        uint8_t expected[RECORD_BYTES];
        make_record(model_id[i], model_data[i], expected);

        memset(key, 0, sizeof(key));   /* data fork, no pad */
        put_be32(key + 2, id_file(model_id[i]));
        put_be32(key + 6, id_start(model_id[i]));

        uint8_t record[256];
        size_t length = 0;
        int rc = hfs_btree_find(&extents, &read_tx, key, EXTENTS_KEY,
                                compare_extents, NULL, record, sizeof(record),
                                &length);
        if (rc != HFS_OK) {
            fprintf(stderr, "  key %u/%u: find returned %d\n",
                    id_file(model_id[i]), id_start(model_id[i]), rc);
            fail_report("find failed", step, "present key not found");
            return 1;
        }
        if (length != RECORD_BYTES ||
            memcmp(record, expected, RECORD_BYTES) != 0) {
            fprintf(stderr, "  key %u/%u: record differs (length %zu)\n",
                    id_file(model_id[i]), id_start(model_id[i]), length);
            fail_report("wrong record", step, "find returned the wrong body");
            return 1;
        }
    }
    return 0;
}

/* The leaf chain is exactly the model, strictly ascending. */
static int verify_chain(size_t step)
{
    struct hfs_btree_leaf_iterator iterator;
    int rc = hfs_btree_leaf_init(&extents, &iterator);
    if (rc != HFS_OK) {
        fail_report("leaf init failed", step, "reader refused the tree");
        return 1;
    }

    size_t seen = 0;
    uint64_t previous = 0;
    int have_previous = 0;
    while (1) {
        const uint8_t *record = NULL;
        size_t length = 0;
        rc = hfs_btree_leaf_next(&extents, &iterator, &record, &length);
        if (rc < 0) {
            fprintf(stderr, "  leaf_next returned %d after %zu records\n", rc,
                    seen);
            fail_report("leaf walk failed", step, "the tree rejected its chain");
            return 1;
        }
        if (rc == 0)
            break;
        if (length != RECORD_BYTES) {
            fprintf(stderr, "  record %zu has length %zu\n", seen, length);
            fail_report("chain record size", step, "not an extents record");
            return 1;
        }
        uint64_t id = make_id(get_be32(record + 4), get_be32(record + 8));
        if (have_previous && id <= previous) {
            fprintf(stderr, "  record %zu key %u/%u after %u/%u\n", seen,
                    id_file(id), id_start(id), id_file(previous),
                    id_start(previous));
            fail_report("chain order", step, "keys are not strictly ascending");
            return 1;
        }
        if (seen >= model_count || model_id[seen] != id ||
            memcmp(record + 2u + EXTENTS_KEY, model_data[seen],
                   EXTENTS_DATA) != 0) {
            fprintf(stderr, "  record %zu key %u/%u\n", seen, id_file(id),
                    id_start(id));
            fail_report("chain contents", step, "chain and model disagree");
            return 1;
        }
        previous = id;
        have_previous = 1;
        ++seen;
    }

    if (seen != model_count) {
        fprintf(stderr, "  chain holds %zu records, model %zu\n", seen,
                model_count);
        fail_report("chain length", step, "records lost or duplicated");
        return 1;
    }
    return 0;
}

/* The volume's own structural check, which now includes node ownership. */
static int verify_volume(size_t step)
{
    struct hfs_check_report report;
    int rc = hfs_check_volume(&volume, &report);
    if (rc != HFS_OK) {
        fprintf(stderr, "  hfs_check_volume returned %d\n", rc);
        fail_report("structural check failed", step, "the check could not run");
        return 1;
    }
    if (report.problems != 0u) {
        fprintf(stderr, "  problems 0x%08x, skipped 0x%08x, nodes %u free %u\n",
                report.problems, report.skipped, report.extents_nodes,
                report.free_blocks_counted);
        fail_report("structural check", step, "the check found a defect");
        return 1;
    }
    if (report.skipped != 0u) {
        fprintf(stderr, "  skipped 0x%08x\n", report.skipped);
        fail_report("structural check skipped", step,
                   "the fixture should be small enough to check fully");
        return 1;
    }
    return 0;
}

/*
 * No index node holds fewer than two records.
 *
 * A one-child index node is not corrupt: every key below it is reachable, key
 * order holds, and `hfs_check_volume` passes. It is a level of the tree that buys
 * nothing - a whole node, and one more node read on every descent - and a drain
 * leaves a chain of them behind if nothing merges index nodes, so the nodes never
 * come back for reuse. The model asks for the tighter shape because that is the
 * shape a B-tree is supposed to have; a failure here means the tree is correct
 * but wasteful, not that a key moved.
 */
static int check_occupancy(uint32_t node, unsigned level, size_t step)
{
    if (level > 8u) {
        fprintf(stderr, "  node %u at level %u\n", (unsigned)node, level);
        fail_report("index descent", step,
                    "deeper than any tree this test builds");
        return 1;
    }
    const uint8_t *data = NULL;
    int rc = hfs_btree_read_node(&extents, node, &data);
    if (rc != HFS_OK) {
        fprintf(stderr, "  read node %u: rc=%d\n", (unsigned)node, rc);
        fail_report("index read failed", step, "the tree rejected a node");
        return 1;
    }
    /* One buffer per handle: a copy is what survives the descent into a child. */
    uint8_t copy[NODE_SIZE];
    memcpy(copy, data, NODE_SIZE);

    if (copy[8] == 0xff)
        return 0;
    if (copy[8] != 0) {
        fprintf(stderr, "  node %u kind %u at level %u\n", (unsigned)node,
                (unsigned)copy[8], level);
        fail_report("node kind", step, "neither an index node nor a leaf");
        return 1;
    }

    uint16_t count = (uint16_t)((copy[10] << 8) | copy[11]);
    if (count < 2) {
        fprintf(stderr, "  index node %u at level %u holds %u record(s)\n",
                (unsigned)node, level, (unsigned)count);
        fail_report("index occupancy", step,
                    "an index node does not hold at least two children");
        return 1;
    }

    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *record = NULL;
        size_t length = 0;
        rc = hfs_btree_node_record(copy, NODE_SIZE, i, &record, &length);
        if (rc != HFS_OK || length < 6u) {
            fprintf(stderr, "  node %u record %u: rc=%d length %zu\n",
                    (unsigned)node, (unsigned)i, rc, length);
            fail_report("index record", step, "an index record is malformed");
            return 1;
        }
        uint16_t key_length = (uint16_t)((record[0] << 8) | record[1]);
        if ((size_t)key_length + 6u > length) {
            fprintf(stderr, "  node %u record %u: key %u, length %zu\n",
                    (unsigned)node, (unsigned)i, (unsigned)key_length, length);
            fail_report("index record", step, "an index record is truncated");
            return 1;
        }
        uint32_t child = ((uint32_t)record[2u + key_length] << 24) |
                         ((uint32_t)record[3u + key_length] << 16) |
                         ((uint32_t)record[4u + key_length] << 8) |
                         record[5u + key_length];
        if (check_occupancy(child, level + 1u, step) != 0)
            return 1;
    }
    return 0;
}

static int verify_occupancy(size_t step)
{
    /* A tree with no root has no index node to look at. */
    if (extents.depth == 0u || extents.root_node == 0u)
        return 0;
    return check_occupancy(extents.root_node, 0u, step);
}

static int verify_all(size_t step)
{
    if (verify_find(step) != 0)
        return 1;
    if (verify_chain(step) != 0)
        return 1;
    if (verify_occupancy(step) != 0)
        return 1;
    return verify_volume(step);
}

/* ------------------------------------------------------------------------- */
/* Operations.                                                               */
/* ------------------------------------------------------------------------- */

static struct hfs_txn tx;
static struct hfs_journal journal;
static int last_refused;

static void reset_tx(void)
{
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = BLOCK_SIZE;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;
}

static void snapshot_image(void)
{
    memcpy(snapshot, image, sizeof(image));
}

static int image_unchanged(const char *what, size_t step)
{
    if (memcmp(snapshot, image, sizeof(image)) == 0)
        return 0;
    fail_report(what, step, "a refused call changed the image");
    return 1;
}

/*
 * One insert of an explicit key: 0 committed, 1 refused cleanly, -1 a defect.
 * The two orders that matter are split out from the policy of which key to pick
 * so the random loop and the ordered cascades share one implementation.
 */
static int insert_one(uint64_t id, uint32_t tag, size_t step)
{
    uint8_t data[EXTENTS_DATA];
    make_data(id, tag, data);
    uint8_t record[RECORD_BYTES];
    make_record(id, data, record);
    snapshot_image();
    reset_tx();
    int rc = hfs_btree_insert(&extents, &tx, record, sizeof(record),
                              compare_extents, NULL);
    if (rc == HFS_OK) {
        apply_txn(&tx);
        model_insert(id, data);
        return 0;
    }
    if (rc == HFS_ERR_NO_SPACE) {
        /* The tree is full: its map cannot widen and its fork cannot take
         * another extent. A legitimate refusal, and it must be a clean one. */
        return image_unchanged("refused insert", step) == 0 ? 1 : -1;
    }
    fprintf(stderr, "  insert %u/%u returned %d\n", id_file(id), id_start(id),
            rc);
    fail_report("insert failed", step, "unexpected error");
    return -1;
}

static int do_insert(size_t step)
{
    /* The model holds the working set; a sweep long enough to fill it is
     * dominated by deletes anyway, so skip rather than grow the array. */
    if (model_count >= MAX_ITEMS)
        return 0;
    uint32_t file_id = 1u + next_random() % MAX_FILE_ID;
    uint32_t start_block = next_random() % MAX_START_BLOCK;
    uint64_t id = make_id(file_id, start_block);
    if (model_find(id) != SIZE_MAX)
        return 0;
    int rc = insert_one(id, (uint32_t)step, step);
    if (rc < 0)
        return 1;
    if (rc == 1)
        last_refused = 1;
    return 0;
}

/* One delete of a model index: the same three answers as insert_one. */
static int delete_one_at(size_t at, size_t step)
{
    uint64_t id = model_id[at];
    uint8_t key[EXTENTS_KEY];
    memset(key, 0, sizeof(key));
    put_be32(key + 2, id_file(id));
    put_be32(key + 6, id_start(id));

    snapshot_image();
    reset_tx();
    int rc = hfs_btree_delete(&extents, &tx, key, EXTENTS_KEY, compare_extents,
                              NULL);
    if (rc == HFS_OK) {
        apply_txn(&tx);
        model_remove(at);
        return 0;
    }
    if (rc == HFS_ERR_UNSUPPORTED) {
        /* A collapse this writer does not implement. It has to be clean. */
        return image_unchanged("refused delete", step) == 0 ? 1 : -1;
    }
    if (rc == HFS_ERR_NOT_FOUND) {
        fprintf(stderr, "  delete %u/%u: present key answered NOT_FOUND\n",
                id_file(id), id_start(id));
        fail_report("delete lost a key", step, "NOT_FOUND for a present key");
        return -1;
    }
    fprintf(stderr, "  delete %u/%u returned %d\n", id_file(id), id_start(id),
            rc);
    fail_report("delete failed", step, "unexpected error");
    return -1;
}

static int do_delete(size_t step)
{
    if (model_count == 0)
        return 0;
    size_t at = next_random() % model_count;
    int rc = delete_one_at(at, step);
    if (rc < 0)
        return 1;
    if (rc == 1)
        last_refused = 1;
    return 0;
}

static int do_update(size_t step)
{
    if (model_count == 0)
        return 0;
    size_t at = next_random() % model_count;
    uint64_t id = model_id[at];

    uint8_t data[EXTENTS_DATA];
    make_data(id, (uint32_t)step ^ 0x9e3779b9u, data);
    uint8_t record[RECORD_BYTES];
    make_record(id, data, record);
    snapshot_image();
    reset_tx();
    int rc = hfs_btree_update(&extents, &tx, record, sizeof(record),
                              compare_extents, NULL);
    if (rc == HFS_OK) {
        apply_txn(&tx);
        memcpy(model_data[at], data, EXTENTS_DATA);
        return 0;
    }
    fprintf(stderr, "  update %u/%u returned %d\n", id_file(id), id_start(id),
            rc);
    fail_report("update failed", step, "unexpected error");
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Ordered cascades.                                                         */
/* ------------------------------------------------------------------------- */

#define CASCADE_KEYS 120u
#define CASCADE_SPARE 3u

/* Keys the random phase never picks: file IDs from 1000, starts from 4096. */
static uint64_t cascade_id(uint32_t index)
{
    return make_id(1000u + index / 128u, 4096u + index % 128u);
}

/* The canonical empty tree: no root, no chain, every node back on the map. */
static int verify_empty(size_t step, const char *what)
{
    if (model_count != 0u) {
        fprintf(stderr, "  %s: model still holds %zu keys\n", what,
                model_count);
        fail_report("drain", step, "the model did not empty");
        return 1;
    }
    if (!extents.empty || extents.depth != 0u || extents.root_node != 0u ||
        extents.leaf_records != 0u || extents.first_leaf != 0u ||
        extents.last_leaf != 0u) {
        fprintf(stderr, "  %s: depth %u root %u records %u chain %u..%u\n",
                what, extents.depth, extents.root_node, extents.leaf_records,
                extents.first_leaf, extents.last_leaf);
        fail_report("empty tree", step,
                   "an emptied tree is not in the canonical empty state");
        return 1;
    }
    /*
     * Every node back on the map except the header node itself, which is always
     * allocated: a freshly formatted tree reports totalNodes - 1 free.
     */
    if (extents.free_nodes + 1u != extents.total_nodes) {
        fprintf(stderr, "  %s: %u of %u nodes free\n", what,
                extents.free_nodes, extents.total_nodes);
        fail_report("empty tree", step,
                   "the drain did not return every node to the free map");
        return 1;
    }
    return verify_volume(step);
}

/*
 * Empty the tree in ascending key order: every step deletes the model's first
 * key, which is the first record of the leftmost leaf. That is the order that
 * rewrites a parent key from the new first record and then merges the leaf to
 * the left, and the last steps have to collapse the root down to a leaf and
 * finally to the empty tree. Returns 0 when it drained, 1 on a refused delete,
 * -1 on a defect.
 */
static int drain_ascending(unsigned *refusals, size_t *step, size_t base)
{
    while (model_count > 0u) {
        uint64_t id = model_id[0];
        size_t at_step = base + (*step)++;
        int rc = delete_one_at(0, at_step);
        if (rc < 0)
            return -1;
        if (rc == 1) {
            ++*refusals;
            fprintf(stderr, "  cascade: deleting key %u/%u was refused with "
                            "%zu keys left, depth %u\n",
                    id_file(id), id_start(id), model_count, extents.depth);
            return 1;
        }
        if ((*step % 16u) == 0u && verify_all(at_step) != 0)
            return -1;
    }
    return 0;
}

/*
 * The whole ordered pass: drain what the random phase built, check the canonical
 * empty tree - in memory and again from the medium - then fill and drain the
 * tree a second time in ascending order, and leave three keys behind so the
 * final state is a tree that works after having been emptied.
 */
static int run_cascades(unsigned *refusals, size_t *steps)
{
    size_t base = 1000000u;   /* far from the random phase's step numbers */

    int rc = drain_ascending(refusals, steps, base);
    if (rc != 0)
        return 1;
    if (verify_empty(base + *steps, "first drain") != 0)
        return 1;

    for (uint32_t i = 0; i < CASCADE_KEYS; ++i) {
        size_t at_step = base + (*steps)++;
        int irc = insert_one(cascade_id(i), 0xc0de0000u ^ i, at_step);
        if (irc < 0)
            return 1;
        if (irc == 1) {
            ++*refusals;
            fprintf(stderr, "  cascade: insert %u was refused after %u keys\n",
                    i, i);
            return 1;
        }
        if ((*steps % 16u) == 0u && verify_all(at_step) != 0)
            return 1;
    }
    if (extents.depth < 2u) {
        fprintf(stderr, "  cascade: %u ascending keys left depth %u\n",
                CASCADE_KEYS, extents.depth);
        fail_report("cascade depth", base + *steps,
                   "the ascending fill did not grow a multi-level tree");
        return 1;
    }
    if (verify_all(base + *steps) != 0)
        return 1;

    rc = drain_ascending(refusals, steps, base);
    if (rc != 0)
        return 1;
    if (verify_empty(base + *steps, "second drain") != 0)
        return 1;

    /*
     * The medium has to say the same thing the struct does: reopen the tree from
     * the image, then insert through the reopened handle.
     */
    memset(&extents, 0, sizeof(extents));
    static uint8_t scratch[32768];
    if (hfs_btree_open(&extents, &volume, &volume.extents, scratch,
                       sizeof(scratch)) != HFS_OK) {
        fail_report("reopen", base + *steps,
                   "the tree on the medium would not open after the drain");
        return 1;
    }
    if (verify_empty(base + *steps, "reopened") != 0)
        return 1;

    for (uint32_t i = 0; i < CASCADE_SPARE; ++i) {
        size_t at_step = base + (*steps)++;
        int irc = insert_one(cascade_id(CASCADE_KEYS + i), i, at_step);
        if (irc != 0) {
            fail_report("reuse", at_step,
                       "an emptied tree refused a fresh insert");
            return 1;
        }
    }
    if (verify_all(base + *steps) != 0)
        return 1;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t seed = DEFAULT_SEED;
    uint32_t steps = DEFAULT_STEPS;
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        steps = (uint32_t)strtoul(argv[2], NULL, 0);
    rng_state = seed;

    make_image();
    CHECK(hfs_volume_probe(&volume, image_read, NULL, 0, IMAGE_SIZE) == HFS_OK);

    /*
     * Seed the root folder through a hand-built tree description: the fixture's
     * catalog claims depth 1 with no records yet, which is the state the writer
     * leaves behind a fresh format and not a tree `hfs_btree_open` accepts.
     */
    struct hfs_btree catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.volume = &volume;
    catalog.fork = &volume.catalog;
    catalog.node_size = NODE_SIZE;
    catalog.total_nodes = CATALOG_NODES;
    catalog.free_nodes = CATALOG_NODES - 2u;
    catalog.depth = 1;
    catalog.root_node = 1;
    catalog.first_leaf = 1;
    catalog.last_leaf = 1;
    catalog.leaf_records = 0;

    reset_tx();
    CHECK(seed_root(&catalog, &tx) == 0);
    apply_txn(&tx);

    static uint8_t scratch[32768];
    CHECK(hfs_btree_open(&extents, &volume, &volume.extents, scratch,
                         sizeof(scratch)) == HFS_OK);

    if (verify_all(0) != 0)
        return 1;

    unsigned inserts = 0, deletes = 0, updates = 0, refusals = 0;
    for (size_t step = 1; step <= steps; ++step) {
        uint32_t pick = next_random() % 10u;
        int rc;
        last_refused = 0;
        if (pick < 5u || model_count == 0) {
            rc = do_insert(step);
            ++inserts;
        } else if (pick < 8u) {
            rc = do_delete(step);
            ++deletes;
        } else {
            rc = do_update(step);
            ++updates;
        }
        if (rc != 0) {
            fprintf(stderr, "seed 0x%08x, step %zu\n", seed, step);
            return 1;
        }
        if (last_refused)
            ++refusals;
        if (verify_all(step) != 0) {
            fprintf(stderr, "seed 0x%08x, step %zu\n", seed, step);
            return 1;
        }
    }

    printf("  btree model: %u steps, %u inserts, %u deletes, %u updates, "
           "%u refusals, %zu keys, depth %u, %u of %u nodes free\n",
           steps, inserts, deletes, updates, refusals, model_count,
           extents.depth, extents.free_nodes, extents.total_nodes);

    unsigned cascade_refusals = 0;
    size_t cascade_steps = 0;
    if (run_cascades(&cascade_refusals, &cascade_steps) != 0) {
        fprintf(stderr, "seed 0x%08x, cascades after step %u\n", seed, steps);
        return 1;
    }
    printf("  btree model: %zu cascade steps, %u refusals, depth %u, %u of %u "
           "nodes free, %zu keys\n", cascade_steps, cascade_refusals,
           extents.depth, extents.free_nodes, extents.total_nodes, model_count);
    return 0;
}
