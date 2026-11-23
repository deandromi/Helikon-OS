/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The steal half of index-node merging (T-152): a one-child index node whose
 * sibling is too full to absorb.
 *
 * `settle_index_nodes` merges two index nodes when their records fit in one node.
 * When they do not fit it moves a single child across instead, and that branch is
 * never reached by `test_btree_model`: after a split a node is at most half full,
 * so one child plus a sibling always fits. The fallback exists for the shape where
 * a sibling *is* full, which is what this file builds by hand.
 *
 * The keys are 236 bytes so a 512-byte node holds two index records and no more:
 * an index record is 2 + 236 + 4 = 242 bytes plus its 2-byte offset entry, so
 * three of them (732 bytes) never fit in the 498 bytes a node has after its
 * descriptor, and two (488) do. That makes "full" a shape nine nodes can reach:
 *
 *   root    [k1 -> A] [k4 -> B]                 index, 2 records
 *   A       [k1 -> LA1] [k2 -> LA2]             index, 2 records
 *   B       [k4 -> LB1] [k5 -> LB2]             index, 2 records, full
 *   LA1     [k1]                                the leaf the delete empties
 *   LA2     [k2] [k3]
 *   LB1     [k4]
 *   LB2     [k5] [k6]
 *
 * Deleting `k1` empties LA1, so A loses its first child and is left with one.
 * `settle_index_nodes` pairs A with B and tries to merge: three records do not
 * fit, so B gives its first child (k4) to A instead. A ends with
 * `[k2 -> LA2] [k4 -> LB1]`, B with `[k5 -> LB2]`, the root's record for B carries
 * `k5`, and the emptied leaf is the only node that left the free-node map.
 *
 * What this asserts, in order: the delete stages a transaction and leaves the
 * medium byte-identical until it is applied; afterwards the six remaining keys are
 * all findable and `k1` is not; the leaf chain is 5 -> 6 -> 7 in ascending key
 * order; A holds the two records with B's first child appended, B holds one, and
 * the root's keys are k2 and k5; node 4 is back on the free-node map, erased, and
 * no other bit moved; the header counts 5 leaf records, 5 free nodes and still
 * three levels.
 */
#include "hfs_btree.h"
#include "hfs_btree_tree.h"
#include "hfs_btree_write.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "hfs_volume.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_SIZE 512u
#define NODE_SIZE 512u
#define IMAGE_SIZE (1024u * 1024u)
#define BLOCKS (IMAGE_SIZE / BLOCK_SIZE)

#define BITMAP_BLOCK 3u
#define CATALOG_BLOCK 16u
#define CATALOG_NODES 4u
#define TREE_BLOCK 32u
#define TREE_NODES 12u
#define ATTRIBUTES_BLOCK 48u
#define ATTRIBUTES_NODES 4u

/* Two records per node at every level: see the header comment. */
#define KEY_BYTES 236u
#define DATA_BYTES 4u
#define RECORD_BYTES (2u + KEY_BYTES + DATA_BYTES)

#define ROOT_NODE 1u
#define NODE_A 2u
#define NODE_B 3u
#define LEAF_LA1 4u
#define LEAF_LA2 5u
#define LEAF_LB1 6u
#define LEAF_LB2 7u

#define K1 1u
#define K2 2u
#define K3 3u
#define K4 4u
#define K5 5u
#define K6 6u

/* The tree starts with eight of its twelve nodes in use. */
#define TREE_FREE_AT_START 4u
#define TREE_FREE_AFTER 5u
#define LEAF_RECORDS_AT_START 6u
#define LEAF_RECORDS_AFTER 5u

static uint8_t image[IMAGE_SIZE];
static uint8_t snapshot[IMAGE_SIZE];
static struct hfs_volume volume;
static struct hfs_btree tree;

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

static uint16_t get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
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

/* ------------------------------------------------------------------------- */
/* The fixture.                                                              */
/* ------------------------------------------------------------------------- */

#define MAP_RECORD_OFFSET 248u
#define MAP_RECORD_BYTES 2u   /* twelve nodes: one bit each, rounded to even */
#define HEADER_FREE_OFFSET (MAP_RECORD_OFFSET + MAP_RECORD_BYTES)

/*
 * A B-tree header node: the header record, the user record and the free-node map.
 * `used_bits` says which of the tree's nodes are in use, as a 16-bit mask read the
 * way the map is: node 0 is the top bit.
 */
static void make_tree(uint32_t block, uint32_t nodes, uint16_t max_key_length,
                      uint32_t root, uint16_t depth, uint32_t leaf_records,
                      uint32_t first_leaf, uint32_t last_leaf,
                      uint32_t free_nodes, uint16_t used_bits)
{
    uint8_t *node = image + block * BLOCK_SIZE;
    memset(node, 0, (size_t)nodes * NODE_SIZE);

    node[8] = 1;                          /* kind: header node */
    put_be16(node + 10, 3);               /* header, user record, map */
    uint8_t *record = node + 14;          /* BTHeaderRec */
    put_be16(record + 0, depth);
    put_be32(record + 2, root);
    put_be32(record + 6, leaf_records);
    put_be32(record + 10, first_leaf);
    put_be32(record + 14, last_leaf);
    put_be16(record + 18, NODE_SIZE);
    put_be16(record + 20, max_key_length);
    put_be32(record + 22, nodes);
    put_be32(record + 26, free_nodes);
    record[32] = 1;                       /* btreeType: user data */
    record[33] = 0;                       /* keyCompareType */

    put_be16(node + NODE_SIZE - 2, 14);
    put_be16(node + NODE_SIZE - 4, 120);
    put_be16(node + NODE_SIZE - 6, MAP_RECORD_OFFSET);
    put_be16(node + NODE_SIZE - 8, HEADER_FREE_OFFSET);
    node[MAP_RECORD_OFFSET] = (uint8_t)(used_bits >> 8);
    node[MAP_RECORD_OFFSET + 1] = (uint8_t)used_bits;
}

static void make_image(void)
{
    memset(image, 0, sizeof(image));

    uint8_t *header = image + 1024;
    put_be16(header, 0x482bu);
    put_be16(header + 2, 4);
    put_be32(header + 4, 1u << 8);        /* kHFSVolumeUnmountedBit */
    put_be32(header + 32, 0);             /* fileCount */
    put_be32(header + 36, 0);             /* folderCount: the root is not counted */
    put_be32(header + 40, BLOCK_SIZE);
    put_be32(header + 44, BLOCKS);
    put_be32(header + 64, 16);            /* nextCatalogID */

    /* Allocation file: BLOCKS bits. */
    uint32_t bitmap_bytes = BLOCKS / 8u;
    uint8_t *allocation = header + 112;
    put_be64(allocation, bitmap_bytes);
    put_be32(allocation + 12, 1);
    put_be32(allocation + 16, BITMAP_BLOCK);
    put_be32(allocation + 20, 1);

    /* Three trees: the extents fork is the one this test drives. */
    uint8_t *catalog = header + 272;
    put_be64(catalog, (uint64_t)CATALOG_NODES * NODE_SIZE);
    put_be32(catalog + 12, CATALOG_NODES * NODE_SIZE / BLOCK_SIZE);
    put_be32(catalog + 16, CATALOG_BLOCK);
    put_be32(catalog + 20, CATALOG_NODES * NODE_SIZE / BLOCK_SIZE);

    uint8_t *extents = header + 192;
    put_be64(extents, (uint64_t)TREE_NODES * NODE_SIZE);
    put_be32(extents + 12, TREE_NODES * NODE_SIZE / BLOCK_SIZE);
    put_be32(extents + 16, TREE_BLOCK);
    put_be32(extents + 20, TREE_NODES * NODE_SIZE / BLOCK_SIZE);

    uint8_t *attributes = header + 352;
    put_be64(attributes, (uint64_t)ATTRIBUTES_NODES * NODE_SIZE);
    put_be32(attributes + 12, ATTRIBUTES_NODES * NODE_SIZE / BLOCK_SIZE);
    put_be32(attributes + 16, ATTRIBUTES_BLOCK);
    put_be32(attributes + 20, ATTRIBUTES_NODES * NODE_SIZE / BLOCK_SIZE);

    make_tree(CATALOG_BLOCK, CATALOG_NODES, 516, 0, 0, 0, 0, 0,
              CATALOG_NODES - 1u, 0x8000u);
    /* Header node plus root, two index nodes and four leaves: nodes 0..7. */
    make_tree(TREE_BLOCK, TREE_NODES, KEY_BYTES, ROOT_NODE, 3,
              LEAF_RECORDS_AT_START, LEAF_LA1, LEAF_LB2, TREE_FREE_AT_START,
              0xff00u);
    make_tree(ATTRIBUTES_BLOCK, ATTRIBUTES_NODES, 262, 0, 0, 0, 0, 0,
              ATTRIBUTES_NODES - 1u, 0x8000u);

    /* Mark every metadata block used and let the bitmap speak for the rest. */
    uint8_t *bitmap = image + BITMAP_BLOCK * BLOCK_SIZE;
    for (uint32_t block = 0; block < BLOCKS; ++block) {
        int used = block <= BITMAP_BLOCK || block >= BLOCKS - 2u ||
                   (block >= CATALOG_BLOCK &&
                    block < CATALOG_BLOCK +
                                CATALOG_NODES * NODE_SIZE / BLOCK_SIZE) ||
                   (block >= TREE_BLOCK &&
                    block < TREE_BLOCK + TREE_NODES * NODE_SIZE / BLOCK_SIZE) ||
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

/* A node with an empty offset table, the way the writer leaves a fresh node. */
static void node_init(uint32_t node_number, uint8_t kind, uint8_t height)
{
    uint8_t *node = image + TREE_BLOCK * BLOCK_SIZE + node_number * NODE_SIZE;
    memset(node, 0, NODE_SIZE);
    node[8] = kind;
    node[9] = height;
    put_be16(node + 10, 0);
    put_be16(node + NODE_SIZE - 2, 14);
    put_be16(node + NODE_SIZE - 4, 14);
}

static uint8_t *node_at(uint32_t node_number)
{
    return image + TREE_BLOCK * BLOCK_SIZE + node_number * NODE_SIZE;
}

static void node_append(uint32_t node_number, const uint8_t *record,
                        size_t length)
{
    uint8_t *node = node_at(node_number);
    int rc = hfs_btree_node_insert(node, NODE_SIZE, get_be16(node + 10), record,
                                   length);
    if (rc != HFS_OK)
        fprintf(stderr, "  fixture: node %u append failed with %d\n",
                (unsigned)node_number, rc);
}

/* A leaf record: the 236-byte key, then four bytes of payload. */
static size_t make_record(uint8_t *out, uint32_t key)
{
    put_be16(out, KEY_BYTES);
    memset(out + 2, 0, KEY_BYTES);
    put_be32(out + 2, key);
    for (size_t i = 0; i < DATA_BYTES; ++i)
        out[2 + KEY_BYTES + i] = (uint8_t)(key + i);
    return RECORD_BYTES;
}

/* An index record: the same key, then the child node number. */
static size_t make_index(uint8_t *out, uint32_t key, uint32_t child)
{
    put_be16(out, KEY_BYTES);
    memset(out + 2, 0, KEY_BYTES);
    put_be32(out + 2, key);
    put_be32(out + 2 + KEY_BYTES, child);
    return 2u + KEY_BYTES + 4u;
}

/*
 * The tree's key order: the 236 key bytes as they are, big endian, so the four
 * significant bytes come first and the padding does not decide anything.
 */
static int compare_keys(void *context, const uint8_t *a, size_t a_length,
                        const uint8_t *b, size_t b_length)
{
    (void)context;
    size_t length = a_length < b_length ? a_length : b_length;
    if (length > KEY_BYTES)
        length = KEY_BYTES;
    int cmp = memcmp(a, b, length);
    if (cmp != 0)
        return cmp < 0 ? -1 : 1;
    if (a_length == b_length)
        return 0;
    return a_length < b_length ? -1 : 1;
}

/* The nine-node shape from the header comment, built record by record. */
static void build_shape(void)
{
    uint8_t record[512];

    node_init(ROOT_NODE, 0, 3);
    node_append(ROOT_NODE, record, make_index(record, K1, NODE_A));
    node_append(ROOT_NODE, record, make_index(record, K4, NODE_B));

    node_init(NODE_A, 0, 2);
    node_append(NODE_A, record, make_index(record, K1, LEAF_LA1));
    node_append(NODE_A, record, make_index(record, K2, LEAF_LA2));

    node_init(NODE_B, 0, 2);
    node_append(NODE_B, record, make_index(record, K4, LEAF_LB1));
    node_append(NODE_B, record, make_index(record, K5, LEAF_LB2));

    node_init(LEAF_LA1, 0xff, 1);
    put_be32(node_at(LEAF_LA1), LEAF_LA2);          /* next */
    put_be32(node_at(LEAF_LA1) + 4, 0);             /* previous */
    node_append(LEAF_LA1, record, make_record(record, K1));

    node_init(LEAF_LA2, 0xff, 1);
    put_be32(node_at(LEAF_LA2), LEAF_LB1);
    put_be32(node_at(LEAF_LA2) + 4, LEAF_LA1);
    node_append(LEAF_LA2, record, make_record(record, K2));
    node_append(LEAF_LA2, record, make_record(record, K3));

    node_init(LEAF_LB1, 0xff, 1);
    put_be32(node_at(LEAF_LB1), LEAF_LB2);
    put_be32(node_at(LEAF_LB1) + 4, LEAF_LA2);
    node_append(LEAF_LB1, record, make_record(record, K4));

    node_init(LEAF_LB2, 0xff, 1);
    put_be32(node_at(LEAF_LB2), 0);                 /* last leaf */
    put_be32(node_at(LEAF_LB2) + 4, LEAF_LB1);
    node_append(LEAF_LB2, record, make_record(record, K5));
    node_append(LEAF_LB2, record, make_record(record, K6));
}

/* ------------------------------------------------------------------------- */
/* The test.                                                                 */
/* ------------------------------------------------------------------------- */

static void apply_txn(const struct hfs_txn *tx)
{
    for (size_t i = 0; i < tx->count; ++i) {
        memcpy(image + tx->blocks[i].sector * BLOCK_SIZE,
               tx->blocks[i].data, tx->blocks[i].size);
    }
}

static int open_tree(void)
{
    static uint8_t scratch[8192];
    memset(&tree, 0, sizeof(tree));
    return hfs_btree_open(&tree, &volume, &volume.extents, scratch,
                          sizeof(scratch));
}

/* One present key, or NOT_FOUND for a key that should be gone. */
static int check_key(uint32_t key, int expected)
{
    uint8_t query[KEY_BYTES];
    memset(query, 0, sizeof(query));
    put_be32(query, key);
    uint8_t record[512];
    size_t length = 0;
    struct hfs_journal journal;
    struct hfs_txn tx;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = BLOCK_SIZE;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;

    int rc = hfs_btree_find(&tree, &tx, query, KEY_BYTES, compare_keys, NULL,
                            record, sizeof(record), &length);
    if (expected) {
        if (rc != HFS_OK) {
            fprintf(stderr, "  key %u: find returned %d\n", (unsigned)key, rc);
            return 1;
        }
        if (length != RECORD_BYTES || get_be16(record) != KEY_BYTES ||
            get_be32(record + 2) != key) {
            fprintf(stderr, "  key %u: wrong record back (length %zu)\n",
                    (unsigned)key, length);
            return 1;
        }
        for (size_t i = 0; i < DATA_BYTES; ++i) {
            if (record[2 + KEY_BYTES + i] != (uint8_t)(key + i)) {
                fprintf(stderr, "  key %u: payload differs\n", (unsigned)key);
                return 1;
            }
        }
        return 0;
    }
    if (rc != HFS_ERR_NOT_FOUND) {
        fprintf(stderr, "  key %u: expected NOT_FOUND, got %d\n",
                (unsigned)key, rc);
        return 1;
    }
    return 0;
}

/* The leaf chain, in order, is the list of keys the model still holds. */
static int check_chain(const uint32_t *keys, size_t count)
{
    struct hfs_btree_leaf_iterator iterator;
    int rc = hfs_btree_leaf_init(&tree, &iterator);
    if (rc != HFS_OK) {
        fprintf(stderr, "  leaf init failed: %d\n", rc);
        return 1;
    }
    size_t seen = 0;
    for (;;) {
        const uint8_t *record = NULL;
        size_t length = 0;
        rc = hfs_btree_leaf_next(&tree, &iterator, &record, &length);
        if (rc < 0) {
            fprintf(stderr, "  leaf_next returned %d after %zu records\n", rc,
                    seen);
            return 1;
        }
        if (rc == 0)
            break;
        if (seen >= count) {
            fprintf(stderr, "  the chain holds more than %zu records\n", count);
            return 1;
        }
        if (length != RECORD_BYTES || get_be32(record + 2) != keys[seen]) {
            fprintf(stderr, "  chain record %zu is key %u, expected %u\n", seen,
                    (unsigned)get_be32(record + 2), (unsigned)keys[seen]);
            return 1;
        }
        ++seen;
    }
    if (seen != count) {
        fprintf(stderr, "  the chain holds %zu records, expected %zu\n", seen,
                count);
        return 1;
    }
    return 0;
}

/* One index node's records, as (key, child) pairs. */
static int check_index(uint32_t node_number, const uint32_t *keys,
                       const uint32_t *children, size_t count)
{
    uint8_t *node = node_at(node_number);
    if (node[8] != 0) {
        fprintf(stderr, "  node %u kind %u, expected an index node\n",
                (unsigned)node_number, (unsigned)node[8]);
        return 1;
    }
    uint16_t records = get_be16(node + 10);
    if (records != count) {
        fprintf(stderr, "  node %u holds %u records, expected %zu\n",
                (unsigned)node_number, (unsigned)records, count);
        return 1;
    }
    for (uint16_t i = 0; i < records; ++i) {
        const uint8_t *record = NULL;
        size_t length = 0;
        int rc = hfs_btree_node_record(node, NODE_SIZE, i, &record, &length);
        if (rc != HFS_OK || length != 2u + KEY_BYTES + 4u) {
            fprintf(stderr, "  node %u record %u: rc=%d length %zu\n",
                    (unsigned)node_number, (unsigned)i, rc, length);
            return 1;
        }
        uint32_t key = get_be32(record + 2);
        uint32_t child = get_be32(record + 2 + KEY_BYTES);
        if (key != keys[i] || child != children[i]) {
            fprintf(stderr,
                    "  node %u record %u is key %u -> %u, expected %u -> %u\n",
                    (unsigned)node_number, (unsigned)i, (unsigned)key,
                    (unsigned)child, (unsigned)keys[i], (unsigned)children[i]);
            return 1;
        }
    }
    return 0;
}

static int map_bit(uint32_t node_number)
{
    const uint8_t *header = image + TREE_BLOCK * BLOCK_SIZE;
    uint8_t byte = header[MAP_RECORD_OFFSET + node_number / 8u];
    return (byte & (uint8_t)(0x80u >> (node_number % 8u))) != 0u;
}

static int check_header(uint32_t free_nodes, uint32_t leaf_records,
                        uint32_t first_leaf_expected)
{
    uint8_t *node = node_at(0);
    const uint8_t *record = NULL;
    size_t length = 0;
    int rc = hfs_btree_node_record(node, NODE_SIZE, 0, &record, &length);
    if (rc != HFS_OK || length < 106u) {
        fprintf(stderr, "  header record: rc=%d length %zu\n", rc, length);
        return 1;
    }
    uint16_t depth = get_be16(record + 0);
    uint32_t root = get_be32(record + 2);
    uint32_t records = get_be32(record + 6);
    uint32_t first_leaf = get_be32(record + 10);
    uint32_t last_leaf = get_be32(record + 14);
    uint32_t free_count = get_be32(record + 26);
    if (depth != 3u || root != ROOT_NODE || records != leaf_records ||
        first_leaf != first_leaf_expected || last_leaf != LEAF_LB2 ||
        free_count != free_nodes) {
        fprintf(stderr,
                "  header: depth %u root %u records %u leaves %u..%u free %u\n",
                (unsigned)depth, (unsigned)root, (unsigned)records,
                (unsigned)first_leaf, (unsigned)last_leaf,
                (unsigned)free_count);
        return 1;
    }
    return 0;
}

int main(void)
{
    static const uint32_t present[] = { K2, K3, K4, K5, K6 };
    static const uint32_t root_keys[] = { K2, K5 };
    static const uint32_t root_children[] = { NODE_A, NODE_B };
    static const uint32_t a_keys[] = { K2, K4 };
    static const uint32_t a_children[] = { LEAF_LA2, LEAF_LB1 };
    static const uint32_t b_keys[] = { K5 };
    static const uint32_t b_children[] = { LEAF_LB2 };

    make_image();
    build_shape();

    CHECK(hfs_volume_probe(&volume, image_read, NULL, 0, IMAGE_SIZE) == HFS_OK);
    CHECK(open_tree() == HFS_OK);
    if (tree.depth != 3u || tree.root_node != ROOT_NODE ||
        tree.free_nodes != TREE_FREE_AT_START) {
        fprintf(stderr, "  the fixture does not open as built: depth %u root "
                        "%u free %u\n",
                (unsigned)tree.depth, (unsigned)tree.root_node,
                (unsigned)tree.free_nodes);
        return 1;
    }

    /* The shape holds what the header comment says before anything moves. */
    CHECK(check_header(TREE_FREE_AT_START, LEAF_RECORDS_AT_START,
                       LEAF_LA1) == 0);
    CHECK(check_index(ROOT_NODE, (const uint32_t[]){ K1, K4 },
                      (const uint32_t[]){ NODE_A, NODE_B }, 2) == 0);
    CHECK(check_index(NODE_A, (const uint32_t[]){ K1, K2 },
                      (const uint32_t[]){ LEAF_LA1, LEAF_LA2 }, 2) == 0);
    CHECK(check_index(NODE_B, (const uint32_t[]){ K4, K5 },
                      (const uint32_t[]){ LEAF_LB1, LEAF_LB2 }, 2) == 0);
    {
        static const uint32_t before[] = { K1, K2, K3, K4, K5, K6 };
        CHECK(check_chain(before, 6u) == 0);
        for (size_t i = 0; i < 6u; ++i)
            CHECK(check_key(before[i], 1) == 0);
    }

    /* Delete the only record of the leftmost leaf. */
    memcpy(snapshot, image, IMAGE_SIZE);
    struct hfs_journal journal;
    struct hfs_txn tx;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = BLOCK_SIZE;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &journal;
    tx.active = 1;

    uint8_t query[KEY_BYTES];
    memset(query, 0, sizeof(query));
    put_be32(query, K1);
    int rc = hfs_btree_delete(&tree, &tx, query, KEY_BYTES, compare_keys, NULL);
    if (rc != HFS_OK) {
        fprintf(stderr, "  delete returned %d\n", rc);
        return 1;
    }
    if (tx.count == 0u) {
        fprintf(stderr, "  the delete staged nothing\n");
        return 1;
    }
    if (memcmp(snapshot, image, IMAGE_SIZE) != 0) {
        fprintf(stderr, "  the delete wrote the medium before the commit\n");
        return 1;
    }

    apply_txn(&tx);
    CHECK(open_tree() == HFS_OK);

    /* Every remaining key is findable, k1 is gone, and the chain agrees. */
    for (size_t i = 0; i < 5u; ++i)
        CHECK(check_key(present[i], 1) == 0);
    CHECK(check_key(K1, 0) == 0);
    CHECK(check_chain(present, 5u) == 0);

    /*
     * The steal: A took B's first child, so it holds the two records a merge
     * would have produced, B is down to one, and the root's key for B moved to
     * B's new first key.
     */
    CHECK(check_index(NODE_A, a_keys, a_children, 2) == 0);
    CHECK(check_index(NODE_B, b_keys, b_children, 1) == 0);
    CHECK(check_index(ROOT_NODE, root_keys, root_children, 2) == 0);

    /* The emptied leaf is back on the map, erased, and nothing else moved. */
    if (map_bit(LEAF_LA1) != 0) {
        fprintf(stderr, "  node %u is still marked used\n", (unsigned)LEAF_LA1);
        return 1;
    }
    for (uint32_t node = 0; node < TREE_NODES; ++node) {
        if (node == LEAF_LA1)
            continue;
        int used = node <= LEAF_LB2;
        if (map_bit(node) != used) {
            fprintf(stderr, "  node %u map bit is %d, expected %d\n",
                    (unsigned)node, map_bit(node), used);
            return 1;
        }
    }
    {
        const uint8_t *erased = node_at(LEAF_LA1);
        for (size_t i = 0; i < NODE_SIZE; ++i) {
            if (erased[i] != 0u) {
                fprintf(stderr, "  freed node %u was not erased\n",
                        (unsigned)LEAF_LA1);
                return 1;
            }
        }
    }

    CHECK(check_header(TREE_FREE_AFTER, LEAF_RECORDS_AFTER, LEAF_LA2) == 0);
    printf("  index merge: a one-child node stole a child from a full sibling, "
           "depth %u, %u of %u nodes free\n",
           (unsigned)tree.depth, (unsigned)tree.free_nodes,
           (unsigned)tree.total_nodes);
    return 0;
}
