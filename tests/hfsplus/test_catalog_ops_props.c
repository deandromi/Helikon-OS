/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Model-based property test for catalog operations.
 *
 * `test_catalog_write.c` walks the writer one case at a time: a create, a
 * delete, a rename, a truncate, each with the failure that particular call can
 * produce. This one does not name a case. It runs a long random sequence of
 * creates, deletes, renames, writes and truncates against a model of the
 * directory tree, and after every operation it asks the image what it holds:
 *
 *  - the children of every folder, by name, ID and kind, against the model;
 *  - each folder's valence against the number of children the model says it
 *    has (a valence that drifts is a folder that later refuses to be deleted,
 *    or one that gets deleted with its children still inside);
 *  - each file's size and full contents, read back through the reader;
 *  - the volume header's file and folder counts;
 *  - `hfs_check_volume`, which independently re-walks the catalog and the
 *    extents tree, checks key order across leaf boundaries, the leaf chain,
 *    every fork against the allocation bitmap, and the free-block count.
 *
 * What that combination is for: one operation is easy to get right, and a
 * sequence is where the invariants break. A delete that forgets to update the
 * parent's valence passes its own test and fails here on the next delete. A
 * rename that leaves the old thread record behind passes the rename test (which
 * looks the new name up) and fails here when the folder is enumerated. A
 * truncate that keeps a block allocated passes here when the free count stops
 * matching the bitmap.
 *
 * The model is written from the documentation, not from the code: names are
 * unique per parent, a folder's valence is its child count, deleting a folder
 * that has children must be refused with HFS_ERR_NOT_EMPTY, moving a folder
 * into itself or its own subtree must be refused with HFS_ERR_ARGUMENT,
 * truncating past the end must be refused with HFS_ERR_ARGUMENT, and a refused
 * call must not change a single byte of the image.
 *
 * The fixture is a 1 MiB volume with 4096-byte B-tree nodes, which is the shape
 * a real volume has: several file records fit in one leaf, so inserts split
 * nodes and deletes merge them instead of every record owning a node of its own.
 * Three trees are present (catalog, extents overflow, attributes), the extents
 * tree starts empty, and the root folder is the only record in the catalog.
 *
 *   ./test_catalog_ops_props              # the committed sweep
 *   ./test_catalog_ops_props 7 2000       # another seed, more operations
 */
#include "hfs_btree_tree.h"
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_check.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 512u
#define NODE_SIZE 4096u
#define IMAGE_SIZE (1024u * 1024u)
#define BLOCKS (IMAGE_SIZE / BLOCK_SIZE)

#define HEADER_BLOCK 0u
#define BITMAP_BLOCK 3u
#define CATALOG_BLOCK 16u
#define EXTENTS_BLOCK 528u
#define ATTRIBUTES_BLOCK 656u
#define FIRST_DATA_BLOCK 720u

#define CATALOG_NODES 64u
#define EXTENTS_NODES 16u
#define ATTRIBUTES_NODES 8u

#define CATALOG_SCRATCH 8192u

#define DEFAULT_STEPS 600u
#define DEFAULT_SEED 0x43415447u /* "CATG" */

/* The root folder the fixture creates: key (1, "TEST"), ID 2. */
#define ROOT_CNID 2u

#define MAX_ITEMS 24u
#define MAX_NAME 8u
#define MAX_FILE_BYTES 4096u

enum item_kind {
    ITEM_FREE = 0,
    ITEM_FILE = 1,
    ITEM_FOLDER = 2
};

struct item {
    int kind;
    uint32_t cnid;
    uint32_t parent;
    char name[MAX_NAME + 1u];
    size_t size;
    uint8_t content[MAX_FILE_BYTES];
};

static struct item items[MAX_ITEMS];

static uint8_t image[IMAGE_SIZE];
static uint8_t snapshot[IMAGE_SIZE];

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

/* ------------------------------------------------------------------------- */
/* The fixture.                                                              */
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
/* Free space starts after the map record: one bit per node, rounded to even. */
#define MAP_RECORD_BYTES (((CATALOG_NODES + 7u) / 8u + 1u) & ~1u)
#define HEADER_FREE_OFFSET (MAP_RECORD_OFFSET + MAP_RECORD_BYTES)

/*
 * One empty B-tree: a header node with the header record, the user record and
 * the free-node map, and - for a tree that already has a root - an empty leaf.
 */
static void make_tree(uint32_t block, uint32_t nodes, uint16_t max_key_length,
                      int with_root, uint32_t *free_start)
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
    put_be16(node + NODE_SIZE - 8, (uint16_t)*free_start);
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

    /* Allocation file: BLOCKS bits. */
    uint32_t bitmap_bytes = BLOCKS / 8u;
    uint8_t *allocation = header + 112;
    put_be64(allocation, bitmap_bytes);
    put_be32(allocation + 12, 1);
    put_be32(allocation + 16, BITMAP_BLOCK);
    put_be32(allocation + 20, 1);

    /* Catalog, extents overflow and attributes trees. */
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

    uint32_t free_start = HEADER_FREE_OFFSET;
    make_tree(CATALOG_BLOCK, CATALOG_NODES, 516, 1, &free_start);
    free_start = HEADER_FREE_OFFSET;
    make_tree(EXTENTS_BLOCK, EXTENTS_NODES, 10, 0, &free_start);
    free_start = HEADER_FREE_OFFSET;
    make_tree(ATTRIBUTES_BLOCK, ATTRIBUTES_NODES, 262, 0, &free_start);

    /* Mark every metadata block used and let the bitmap speak for the rest. */
    uint8_t *bitmap = image + BITMAP_BLOCK * BLOCK_SIZE;
    for (uint32_t block = 0; block < BLOCKS; ++block) {
        int used = block <= BITMAP_BLOCK || block >= BLOCKS - 2u ||
                   (block >= CATALOG_BLOCK &&
                    block < CATALOG_BLOCK + CATALOG_NODES * NODE_SIZE / BLOCK_SIZE) ||
                   (block >= EXTENTS_BLOCK &&
                    block < EXTENTS_BLOCK + EXTENTS_NODES * NODE_SIZE / BLOCK_SIZE) ||
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

static size_t build_key(uint8_t *buffer, uint32_t parent, const uint8_t *name,
                        size_t units)
{
    put_be16(buffer, (uint16_t)(6u + units * 2u));
    put_be32(buffer + 2, parent);
    put_be16(buffer + 6, (uint16_t)units);
    if (units != 0)
        memcpy(buffer + 8, name, units * 2u);
    return 6u + units * 2u;
}

/* UTF-16BE for an ASCII name. */
static size_t name_units(const char *name, uint8_t *out)
{
    size_t units = strlen(name);
    for (size_t i = 0; i < units; ++i) {
        out[i * 2] = 0;
        out[i * 2 + 1] = (uint8_t)name[i];
    }
    return units;
}

static int seed_root(struct hfs_btree *btree, struct hfs_txn *tx)
{
    static const uint8_t root_name[8] = { 0, 'T', 0, 'E', 0, 'S', 0, 'T' };
    uint8_t record[1024];
    size_t key_length = build_key(record, 1, root_name, 4);
    size_t data_offset = 2u + key_length;
    memset(record + data_offset, 0, 88);
    put_be16(record + data_offset, 1);          /* folder record */
    put_be32(record + data_offset + 8, ROOT_CNID);
    put_be32(record + data_offset + 4, 0);      /* valence */
    int rc = hfs_btree_insert(btree, tx, record, data_offset + 88,
                              hfs_catalog_key_compare, NULL);
    if (rc != HFS_OK)
        return rc;

    key_length = build_key(record, ROOT_CNID, NULL, 0);
    data_offset = 2u + key_length;
    memset(record + data_offset, 0, 18);
    put_be16(record + data_offset, 3);          /* folder thread */
    put_be32(record + data_offset + 4, 1);      /* the root's parent is 1 */
    put_be16(record + data_offset + 8, 4);
    memcpy(record + data_offset + 10, root_name, 8);
    return hfs_btree_insert(btree, tx, record, data_offset + 18,
                            hfs_catalog_key_compare, NULL);
}

static void apply_txn(const struct hfs_txn *tx)
{
    for (size_t i = 0; i < tx->count; ++i) {
        memcpy(image + tx->blocks[i].sector * BLOCK_SIZE,
               tx->blocks[i].data, tx->blocks[i].size);
    }
    if (getenv("OPS_TRACE") != NULL) {
        const uint8_t *h = image + CATALOG_BLOCK * BLOCK_SIZE + 14;
        fprintf(stderr,
                "  after: depth=%u root=%u records=%u first=%u last=%u "
                "free=%u | nodes",
                (unsigned)((h[0] << 8) | h[1]),
                (unsigned)((h[2] << 24) | (h[3] << 16) | (h[4] << 8) | h[5]),
                (unsigned)((h + 6)[0] << 24 | (h + 6)[1] << 16 |
                           (h + 6)[2] << 8 | (h + 6)[3]),
                (unsigned)((h + 10)[0] << 24 | (h + 10)[1] << 16 |
                           (h + 10)[2] << 8 | (h + 10)[3]),
                (unsigned)((h + 14)[0] << 24 | (h + 14)[1] << 16 |
                           (h + 14)[2] << 8 | (h + 14)[3]),
                (unsigned)((h + 26)[0] << 24 | (h + 26)[1] << 16 |
                           (h + 26)[2] << 8 | (h + 26)[3]));
        for (uint32_t node = 1; node < 4; ++node) {
            const uint8_t *n = image + CATALOG_BLOCK * BLOCK_SIZE +
                               node * NODE_SIZE;
            fprintf(stderr, " %u:%u/%u", (unsigned)node, (unsigned)n[8],
                    (unsigned)((n[10] << 8) | n[11]));
        }
        fprintf(stderr, "\n");
    }
}

/* ------------------------------------------------------------------------- */
/* The environment.                                                          */
/* ------------------------------------------------------------------------- */

struct env {
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_journal journal;
    uint8_t scratch[CATALOG_SCRATCH];
};

static int mount_env(struct env *env)
{
    if (hfs_volume_probe(&env->volume, image_read, NULL, 0, IMAGE_SIZE) !=
        HFS_OK)
        return 1;
    memset(&env->catalog, 0, sizeof(env->catalog));
    if (hfs_catalog_open(&env->catalog, &env->volume, env->scratch,
                         sizeof(env->scratch)) != HFS_OK)
        return 1;
    return 0;
}

static void start_tx(struct env *env, struct hfs_txn *tx)
{
    memset(tx, 0, sizeof(*tx));
    tx->journal = &env->journal;
    tx->active = 1;
}

/* ------------------------------------------------------------------------- */
/* The model.                                                                */
/* ------------------------------------------------------------------------- */

static int model_find(uint32_t cnid)
{
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind != ITEM_FREE && items[i].cnid == cnid)
            return (int)i;
    }
    return -1;
}

static int model_find_name(uint32_t parent, const char *name)
{
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind != ITEM_FREE && items[i].parent == parent &&
            strcmp(items[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static uint32_t children_of(uint32_t parent)
{
    uint32_t count = 0;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind != ITEM_FREE && items[i].parent == parent)
            ++count;
    }
    return count;
}

static uint32_t model_files(void)
{
    uint32_t count = 0;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind == ITEM_FILE)
            ++count;
    }
    return count;
}

static uint32_t model_folders(void)
{
    /* The volume header does not count the root folder. */
    uint32_t count = 0;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind == ITEM_FOLDER)
            ++count;
    }
    return count;
}

static int model_descends(uint32_t cnid, uint32_t ancestor)
{
    for (int guard = 0; guard < 32; ++guard) {
        if (cnid == ancestor)
            return 1;
        if (cnid == ROOT_CNID)
            return 0;
        int index = model_find(cnid);
        if (index < 0)
            return 0;
        cnid = items[index].parent;
    }
    return 0;
}

static int model_slot(void)
{
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind == ITEM_FREE)
            return (int)i;
    }
    return -1;
}

/* Every live folder, the root included. */
static void pick_folder(uint32_t folders[], size_t *count)
{
    *count = 0;
    folders[(*count)++] = ROOT_CNID;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind == ITEM_FOLDER)
            folders[(*count)++] = items[i].cnid;
    }
}

static void snapshot_take(void)
{
    memcpy(snapshot, image, IMAGE_SIZE);
}

static int snapshot_same(void)
{
    return memcmp(snapshot, image, IMAGE_SIZE) == 0;
}

static const char name_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static int make_unique_name(uint32_t parent, char *out)
{
    for (int attempt = 0; attempt < 60; ++attempt) {
        size_t length = 1u + next_random() % MAX_NAME;
        for (size_t i = 0; i < length; ++i)
            out[i] = name_letters[next_random() % 36u];
        out[length] = '\0';
        if (model_find_name(parent, out) < 0)
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Reading the image back.                                                   */
/* ------------------------------------------------------------------------- */

static int read_folder_valence(struct env *env, uint32_t parent,
                               const char *name, uint32_t *valence)
{
    uint8_t utf16[2u * MAX_NAME];
    size_t units = name_units(name, utf16);
    uint8_t key[64];
    size_t key_length = build_key(key, parent, utf16, units);
    uint8_t record[1024];
    size_t record_length = 0;
    struct hfs_txn tx;
    start_tx(env, &tx);
    int rc = hfs_btree_find(&env->catalog.btree, &tx, key + 2, key_length,
                            hfs_catalog_key_compare, NULL, record,
                            sizeof(record), &record_length);
    if (rc != HFS_OK)
        return rc;
    size_t data_offset = 2u + key_length;
    if (record_length < data_offset + 10u)
        return HFS_ERR_FORMAT;
    *valence = get_be32(record + data_offset + 4);
    return HFS_OK;
}

/* Compare the children of one folder with the model. */
static int check_children(struct env *env, uint32_t parent)
{
    struct hfs_catalog_iterator iterator;
    CHECK(hfs_catalog_iter_init(&env->catalog, parent, &iterator) == HFS_OK);
    uint32_t seen = 0;
    for (;;) {
        struct hfs_entry entry;
        int rc = hfs_catalog_iter_next(&env->catalog, &iterator, &entry);
        CHECK(rc >= 0);
        if (rc == 0)
            break;
        ++seen;
        int index = model_find(entry.id);
        if (index < 0) {
            fprintf(stderr,
                    "  FAIL folder %u lists id %u (%s), which the model does "
                    "not have\n",
                    (unsigned)parent, (unsigned)entry.id, entry.name);
            return 1;
        }
        if (items[index].parent != parent ||
            strcmp(items[index].name, entry.name) != 0) {
            fprintf(stderr,
                    "  FAIL folder %u lists id %u as %s, model has it as %s "
                    "under %u\n",
                    (unsigned)parent, (unsigned)entry.id, entry.name,
                    items[index].name, (unsigned)items[index].parent);
            return 1;
        }
        if ((items[index].kind == ITEM_FOLDER ? 1 : 2) != entry.kind) {
            fprintf(stderr, "  FAIL id %u kind %u, model kind %d\n",
                    (unsigned)entry.id, (unsigned)entry.kind,
                    items[index].kind);
            return 1;
        }
        if (items[index].kind == ITEM_FILE &&
            entry.data.size != (uint64_t)items[index].size) {
            fprintf(stderr, "  FAIL id %u size %llu, model %zu\n",
                    (unsigned)entry.id, (unsigned long long)entry.data.size,
                    items[index].size);
            return 1;
        }
    }
    if (seen != children_of(parent)) {
        fprintf(stderr, "  FAIL folder %u lists %u children, model has %u\n",
                (unsigned)parent, (unsigned)seen,
                (unsigned)children_of(parent));
        return 1;
    }
    return 0;
}

/* Temporary debugging aid: print the reachable catalog nodes. */
static void debug_dump_tree(struct env *env)
{
    struct hfs_btree *btree = &env->catalog.btree;
    static uint8_t node_buf[NODE_SIZE];
    {
        const uint8_t *imgh = image + CATALOG_BLOCK * BLOCK_SIZE + 14;
        fprintf(stderr,
                "  dump: image says depth=%u root=%u last=%u, btree says "
                "depth=%u root=%u last=%u\n",
                (unsigned)((imgh[0] << 8) | imgh[1]),
                (unsigned)((imgh[2] << 24) | (imgh[3] << 16) |
                           (imgh[4] << 8) | imgh[5]),
                (unsigned)((imgh[14] << 24) | (imgh[15] << 16) |
                           (imgh[16] << 8) | imgh[17]),
                (unsigned)btree->depth, (unsigned)btree->root_node,
                (unsigned)btree->last_leaf);
    }
    fprintf(stderr, "  dump: depth=%u root=%u first=%u last=%u total=%u free=%u\n",
            (unsigned)btree->depth, (unsigned)btree->root_node,
            (unsigned)btree->first_leaf, (unsigned)btree->last_leaf,
            (unsigned)btree->total_nodes, (unsigned)btree->free_nodes);
    for (uint32_t node = 0; node < btree->total_nodes; ++node) {
        memcpy(node_buf, image + CATALOG_BLOCK * BLOCK_SIZE + node * NODE_SIZE,
               NODE_SIZE);
        uint16_t count = (uint16_t)((node_buf[10] << 8) | node_buf[11]);
        fprintf(stderr, "  dump: node %u kind=%u height=%u records=%u\n",
                (unsigned)node, (unsigned)node_buf[8], (unsigned)node_buf[9],
                (unsigned)count);
        if (node_buf[8] == 1)
            continue;
        for (uint16_t i = 0; i < count; ++i) {
            const uint8_t *record = NULL;
            size_t length = 0;
            if (hfs_btree_node_record(node_buf, NODE_SIZE, i, &record,
                                      &length) != HFS_OK)
                continue;
            uint16_t key_length = (uint16_t)((record[0] << 8) | record[1]);
            uint32_t parent = ((uint32_t)record[2] << 24) |
                              ((uint32_t)record[3] << 16) |
                              ((uint32_t)record[4] << 8) | record[5];
            uint16_t units = (uint16_t)((record[6] << 8) | record[7]);
            char name[64];
            size_t n = units < 20u ? units : 20u;
            for (size_t c = 0; c < n; ++c)
                name[c] = (char)record[8 + c * 2 + 1];
            name[n] = '\0';
            if (node_buf[8] == 0xff) {
                fprintf(stderr, "  dump:   [%u] key parent=%u name=%s\n",
                        (unsigned)i, (unsigned)parent, name);
            } else {
                uint32_t child = ((uint32_t)record[2 + key_length] << 24) |
                                 ((uint32_t)record[3 + key_length] << 16) |
                                 ((uint32_t)record[4 + key_length] << 8) |
                                 record[5 + key_length];
                fprintf(stderr,
                        "  dump:   [%u] key parent=%u name=%s -> child %u\n",
                        (unsigned)i, (unsigned)parent, name, (unsigned)child);
            }
        }
    }
}

static int check_contents(struct env *env, size_t index)
{
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup_ascii(&env->catalog, items[index].parent,
                                      items[index].name, &entry);
    if (rc != HFS_OK) {
        fprintf(stderr,
                "  note: lookup %s under %u (id %u, size %zu) rc=%d\n",
                items[index].name, (unsigned)items[index].parent,
                (unsigned)items[index].cnid, items[index].size, rc);
        if (getenv("OPS_TRACE") != NULL)
            debug_dump_tree(env);
        CHECK(rc == HFS_OK);
    }
    CHECK(entry.id == items[index].cnid);
    CHECK(entry.data.size == (uint64_t)items[index].size);
    if (items[index].size == 0u)
        return 0;
    static uint8_t readback[MAX_FILE_BYTES];
    size_t done = 0;
    rc = hfs_catalog_read_file(&env->catalog, &entry, 0, readback,
                               items[index].size, &done);
    CHECK(rc == HFS_OK);
    if (done != items[index].size) {
        fprintf(stderr, "  FAIL %s read back %zu of %zu bytes\n",
                items[index].name, done, items[index].size);
        return 1;
    }
    for (size_t i = 0; i < items[index].size; ++i) {
        if (readback[i] != items[index].content[i]) {
            fprintf(stderr, "  FAIL %s byte %zu is 0x%02x, model 0x%02x\n",
                    items[index].name, i, readback[i],
                    items[index].content[i]);
            return 1;
        }
    }
    return 0;
}

/*
 * The whole world: counts in the header, every folder's children and valence,
 * every file's contents, and the independent structural check.
 */
static int check_world(struct env *env)
{
    uint32_t header_files = get_be32(image + 1024 + 32);
    uint32_t header_folders = get_be32(image + 1024 + 36);
    if (header_files != model_files() || header_folders != model_folders()) {
        fprintf(stderr,
                "  FAIL header counts %u files / %u folders, model %u / %u\n",
                (unsigned)header_files, (unsigned)header_folders,
                (unsigned)model_files(), (unsigned)model_folders());
        return 1;
    }

    /* The structural check runs first: when it fails it names the invariant,
     * and its mask is more useful than whichever field check trips next. */
    struct hfs_check_report report;
    memset(&report, 0, sizeof(report));
    CHECK(hfs_check_volume(&env->volume, &report) == HFS_OK);
    if (report.problems != 0u) {
        fprintf(stderr,
                "  FAIL structural check: problems 0x%08x (files %u/%u, "
                "folders %u/%u, free %u/%u)\n",
                (unsigned)report.problems, (unsigned)report.catalog_files,
                (unsigned)report.header_files, (unsigned)report.catalog_folders,
                (unsigned)report.header_folders,
                (unsigned)report.free_blocks_counted,
                (unsigned)report.free_blocks_claimed);
        return 1;
    }

    CHECK(check_children(env, ROOT_CNID) == 0);
    uint32_t valence = 0;
    CHECK(read_folder_valence(env, 1, "TEST", &valence) == HFS_OK);
    if (valence != children_of(ROOT_CNID)) {
        fprintf(stderr, "  FAIL root valence %u, model says %u children\n",
                (unsigned)valence, (unsigned)children_of(ROOT_CNID));
        return 1;
    }
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind == ITEM_FOLDER) {
            CHECK(check_children(env, items[i].cnid) == 0);
            CHECK(read_folder_valence(env, items[i].parent, items[i].name,
                                      &valence) == HFS_OK);
            if (valence != children_of(items[i].cnid)) {
                fprintf(stderr, "  FAIL %s valence %u, model says %u children\n",
                        items[i].name, (unsigned)valence,
                        (unsigned)children_of(items[i].cnid));
                return 1;
            }
        } else if (items[i].kind == ITEM_FILE) {
            CHECK(check_contents(env, i) == 0);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Operations.                                                               */
/* ------------------------------------------------------------------------- */

struct counters {
    uint32_t creates;
    uint32_t deletes;
    uint32_t renames;
    uint32_t writes;
    uint32_t truncates;
    uint32_t refusals;
    uint32_t cycles;
    uint32_t not_empty;
};

static int do_create(struct env *env, int kind, struct counters *count)
{
    uint32_t folders[MAX_ITEMS + 1u];
    size_t folder_count = 0;
    pick_folder(folders, &folder_count);
    uint32_t parent = folders[next_random() % folder_count];

    char name[MAX_NAME + 1u];
    if (make_unique_name(parent, name) != 0)
        return 0;

    int slot = model_slot();
    if (slot < 0)
        return 0;

    uint8_t utf16[2u * MAX_NAME];
    size_t units = name_units(name, utf16);
    uint32_t next_cnid = get_be32(image + 1024 + 64);
    uint32_t cnid = 0;
    struct hfs_txn tx;
    start_tx(env, &tx);
    snapshot_take();
    int rc;
    if (kind == ITEM_FILE) {
        rc = hfs_catalog_create_file(&env->catalog, &tx, parent, utf16, units,
                                     0644, hfs_catalog_key_compare, NULL,
                                     &cnid);
    } else {
        rc = hfs_catalog_create_folder(&env->catalog, &tx, parent, utf16, units,
                                       0755, hfs_catalog_key_compare, NULL,
                                       &cnid);
    }

    if (rc == HFS_ERR_NO_SPACE) {
        /* Out of nodes or blocks: the model does not change, and neither may
         * the image. */
        CHECK(snapshot_same());
        ++count->refusals;
        return 0;
    }
    if (rc != HFS_OK) {
        fprintf(stderr, "  FAIL create kind=%d parent=%u rc=%d\n", kind,
                (unsigned)parent, rc);
        return 1;
    }
    if (cnid != next_cnid) {
        fprintf(stderr, "  FAIL create returned CNID %u, header said %u\n",
                (unsigned)cnid, (unsigned)next_cnid);
        return 1;
    }
    apply_txn(&tx);
    CHECK(get_be32(image + 1024 + 64) == next_cnid + 1u);
    CHECK(mount_env(env) == 0);

    memset(&items[slot], 0, sizeof(items[slot]));
    items[slot].kind = kind;
    items[slot].cnid = cnid;
    items[slot].parent = parent;
    memcpy(items[slot].name, name, strlen(name) + 1u);
    ++count->creates;
    return 0;
}

static int do_delete(struct env *env, struct counters *count)
{
    uint32_t live[MAX_ITEMS];
    size_t live_count = 0;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind != ITEM_FREE)
            live[live_count++] = (uint32_t)i;
    }
    if (live_count == 0u)
        return 0;
    struct item *item = &items[live[next_random() % live_count]];

    uint8_t utf16[2u * MAX_NAME];
    size_t units = name_units(item->name, utf16);
    struct hfs_txn tx;
    start_tx(env, &tx);
    snapshot_take();
    int rc;
    if (item->kind == ITEM_FILE) {
        rc = hfs_catalog_delete_file(&env->catalog, &tx, item->parent, utf16,
                                     units, item->cnid,
                                     hfs_catalog_key_compare, NULL);
        if (rc != HFS_OK) {
            fprintf(stderr, "  FAIL delete file %s rc=%d\n", item->name, rc);
            return 1;
        }
    } else {
        rc = hfs_catalog_delete_folder(&env->catalog, &tx, item->parent, utf16,
                                       units, item->cnid,
                                       hfs_catalog_key_compare, NULL);
        if (children_of(item->cnid) != 0u) {
            /* A folder with children must be refused, and refusing must not
             * have staged anything. */
            if (rc != HFS_ERR_NOT_EMPTY) {
                fprintf(stderr, "  FAIL delete non-empty folder %s rc=%d\n",
                        item->name, rc);
                return 1;
            }
            CHECK(tx.count == 0u);
            CHECK(snapshot_same());
            ++count->not_empty;
            return 0;
        }
        if (rc != HFS_OK) {
            fprintf(stderr, "  FAIL delete folder %s rc=%d\n", item->name, rc);
            return 1;
        }
    }
    apply_txn(&tx);
    CHECK(mount_env(env) == 0);
    item->kind = ITEM_FREE;
    ++count->deletes;
    return 0;
}

static int do_rename(struct env *env, struct counters *count)
{
    uint32_t live[MAX_ITEMS];
    size_t live_count = 0;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind != ITEM_FREE)
            live[live_count++] = (uint32_t)i;
    }
    if (live_count == 0u)
        return 0;
    struct item *item = &items[live[next_random() % live_count]];

    uint32_t folders[MAX_ITEMS + 1u];
    size_t folder_count = 0;
    pick_folder(folders, &folder_count);
    uint32_t new_parent = folders[next_random() % folder_count];

    char new_name[MAX_NAME + 1u];
    if (make_unique_name(new_parent, new_name) != 0)
        return 0;

    uint8_t old_utf16[2u * MAX_NAME];
    uint8_t new_utf16[2u * MAX_NAME];
    size_t old_units = name_units(item->name, old_utf16);
    size_t new_units = name_units(new_name, new_utf16);

    struct hfs_txn tx;
    start_tx(env, &tx);
    snapshot_take();
    int rc = hfs_catalog_rename(&env->catalog, &tx, item->parent, old_utf16,
                                old_units, item->cnid, new_parent, new_utf16,
                                new_units, hfs_catalog_key_compare, NULL);
    if (item->kind == ITEM_FOLDER && model_descends(new_parent, item->cnid)) {
        /* Moving a folder inside itself would detach the subtree from the
         * root; it must be refused before anything is staged. */
        if (rc != HFS_ERR_ARGUMENT) {
            fprintf(stderr, "  FAIL cycle rename %s rc=%d\n", item->name, rc);
            return 1;
        }
        CHECK(tx.count == 0u);
        CHECK(snapshot_same());
        ++count->cycles;
        return 0;
    }
    if (rc == HFS_ERR_NO_SPACE) {
        /* The old record is gone and the new one does not fit: a caller has to
         * retry after freeing space, and nothing may reach the image. */
        CHECK(snapshot_same());
        ++count->refusals;
        return 0;
    }
    if (rc != HFS_OK) {
        fprintf(stderr,
                "  FAIL rename %s (id %u, kind %d) from %u to %u as %s rc=%d\n",
                item->name, (unsigned)item->cnid, item->kind,
                (unsigned)item->parent, (unsigned)new_parent, new_name, rc);
        return 1;
    }
    apply_txn(&tx);
    CHECK(mount_env(env) == 0);
    item->parent = new_parent;
    memcpy(item->name, new_name, strlen(new_name) + 1u);
    ++count->renames;
    return 0;
}

static int do_write(struct env *env, struct counters *count)
{
    uint32_t files[MAX_ITEMS];
    size_t file_count = 0;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind == ITEM_FILE)
            files[file_count++] = (uint32_t)i;
    }
    if (file_count == 0u)
        return 0;
    struct item *item = &items[files[next_random() % file_count]];

    const uint32_t pick = next_random() % 100u;
    uint64_t offset;
    if (pick < 55u)
        offset = item->size;                     /* append */
    else if (pick < 90u)
        offset = item->size == 0u ? 0u : next_random() % item->size;
    else
        offset = item->size + 1u + next_random() % 64u; /* zero-filled gap */
    size_t length = 1u + next_random() % 256u;
    if (offset + length > MAX_FILE_BYTES)
        return 0;

    uint8_t data[256];
    const uint8_t pattern = (uint8_t)(0x40u + (next_random() % 0x40u));
    for (size_t i = 0; i < length; ++i)
        data[i] = (uint8_t)(pattern + (uint8_t)i);

    uint8_t utf16[2u * MAX_NAME];
    size_t units = name_units(item->name, utf16);
    struct hfs_txn tx;
    start_tx(env, &tx);
    snapshot_take();
    int rc = hfs_catalog_write_file(&env->catalog, &tx, item->parent, utf16,
                                    units, offset, data, length,
                                    hfs_catalog_key_compare, NULL);
    if (rc == HFS_ERR_NO_SPACE) {
        CHECK(snapshot_same());
        ++count->refusals;
        return 0;
    }
    if (rc != HFS_OK) {
        fprintf(stderr, "  FAIL write %s at %llu rc=%d\n", item->name,
                (unsigned long long)offset, rc);
        return 1;
    }
    apply_txn(&tx);
    CHECK(mount_env(env) == 0);

    if (offset > item->size)
        memset(item->content + item->size, 0, (size_t)offset - item->size);
    memcpy(item->content + offset, data, length);
    if (offset + length > item->size)
        item->size = (size_t)(offset + length);
    ++count->writes;
    return 0;
}

static int do_truncate(struct env *env, struct counters *count)
{
    uint32_t files[MAX_ITEMS];
    size_t file_count = 0;
    for (size_t i = 0; i < MAX_ITEMS; ++i) {
        if (items[i].kind == ITEM_FILE)
            files[file_count++] = (uint32_t)i;
    }
    if (file_count == 0u)
        return 0;
    struct item *item = &items[files[next_random() % file_count]];

    uint8_t utf16[2u * MAX_NAME];
    size_t units = name_units(item->name, utf16);
    struct hfs_txn tx;

    /* Growing through this call is a caller error: ARGUMENT, nothing staged. */
    if ((next_random() % 5u) == 0u) {
        start_tx(env, &tx);
        snapshot_take();
        int rc = hfs_catalog_truncate_to(&env->catalog, &tx, item->parent,
                                         utf16, units, item->size + 1u,
                                         hfs_catalog_key_compare, NULL);
        if (rc != HFS_ERR_ARGUMENT) {
            fprintf(stderr, "  FAIL grow-through-truncate %s rc=%d\n",
                    item->name, rc);
            return 1;
        }
        CHECK(snapshot_same());
        ++count->refusals;
        return 0;
    }

    size_t new_size = item->size == 0u ? 0u : next_random() % (item->size + 1u);
    start_tx(env, &tx);
    snapshot_take();
    int rc = hfs_catalog_truncate_to(&env->catalog, &tx, item->parent, utf16,
                                     units, new_size,
                                     hfs_catalog_key_compare, NULL);
    if (rc == HFS_ERR_NO_SPACE) {
        CHECK(snapshot_same());
        ++count->refusals;
        return 0;
    }
    if (rc != HFS_OK) {
        fprintf(stderr, "  FAIL truncate %s to %zu rc=%d\n", item->name,
                new_size, rc);
        return 1;
    }
    apply_txn(&tx);
    CHECK(mount_env(env) == 0);
    memset(item->content + new_size, 0, MAX_FILE_BYTES - new_size);
    item->size = new_size;
    ++count->truncates;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t steps = DEFAULT_STEPS;
    uint32_t seed = DEFAULT_SEED;
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        steps = (uint32_t)strtoul(argv[2], NULL, 0);

    make_image();
    memset(items, 0, sizeof(items));
    rng_state = seed;

    struct env env;
    memset(&env, 0, sizeof(env));
    env.journal.sector_size = BLOCK_SIZE;
    int probe = hfs_volume_probe(&env.volume, image_read, NULL, 0, IMAGE_SIZE);
    if (probe != HFS_OK) {
        fprintf(stderr, "  FAIL the fixture does not probe: %d (%s)\n", probe,
                hfs_strerror(probe));
        return 1;
    }

    struct hfs_btree btree;
    memset(&btree, 0, sizeof(btree));
    btree.volume = &env.volume;
    btree.fork = &env.volume.catalog;
    btree.node_size = NODE_SIZE;
    btree.total_nodes = CATALOG_NODES;
    btree.free_nodes = CATALOG_NODES - 2u;
    btree.depth = 1;
    btree.root_node = 1;
    btree.first_leaf = 1;
    btree.last_leaf = 1;
    static uint8_t seed_scratch[NODE_SIZE];
    btree.scratch = seed_scratch;
    btree.scratch_size = sizeof(seed_scratch);

    struct hfs_txn tx;
    memset(&tx, 0, sizeof(tx));
    tx.journal = &env.journal;
    tx.active = 1;
    if (seed_root(&btree, &tx) != HFS_OK) {
        fprintf(stderr, "  FAIL seeding the root record\n");
        return 1;
    }
    apply_txn(&tx);
    CHECK(mount_env(&env) == 0);
    CHECK(check_world(&env) == 0);

    struct counters count;
    memset(&count, 0, sizeof(count));

    const int trace_ops = getenv("OPS_TRACE") != NULL;
    for (uint32_t step = 0u; step < steps; ++step) {
        const uint32_t pick = next_random() % 100u;
        if (trace_ops)
            fprintf(stderr, "  op %u pick %u\n", (unsigned)step,
                    (unsigned)pick);
        if (pick < 26u) {
            CHECK(do_create(&env, ITEM_FILE, &count) == 0);
        } else if (pick < 40u) {
            CHECK(do_create(&env, ITEM_FOLDER, &count) == 0);
        } else if (pick < 60u) {
            CHECK(do_delete(&env, &count) == 0);
        } else if (pick < 78u) {
            CHECK(do_rename(&env, &count) == 0);
        } else if (pick < 93u) {
            CHECK(do_write(&env, &count) == 0);
        } else {
            CHECK(do_truncate(&env, &count) == 0);
        }
        if (check_world(&env) != 0) {
            fprintf(stderr, "  FAIL at step %u (seed 0x%08x)\n",
                    (unsigned)step, (unsigned)seed);
            return 1;
        }
    }

    printf("  catalog operations: %u steps, %u creates, %u deletes, "
           "%u renames, %u writes, %u truncates, %u refusals "
           "(%u cycle, %u not-empty)\n",
           (unsigned)steps, (unsigned)count.creates, (unsigned)count.deletes,
           (unsigned)count.renames, (unsigned)count.writes,
           (unsigned)count.truncates, (unsigned)count.refusals,
           (unsigned)count.cycles, (unsigned)count.not_empty);
    CHECK(count.creates > steps / 20u);
    CHECK(count.deletes > steps / 50u);
    return 0;
}
