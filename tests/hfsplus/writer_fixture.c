/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "writer_fixture.h"

#include "hfs_btree_tree.h"
#include "hfs_catalog_compare.h"

#include <string.h>

#define IMAGE_SIZE WRITER_IMAGE_SIZE
#define BLOCK_SIZE WRITER_BLOCK_SIZE
#define NODE_SIZE WRITER_NODE_SIZE
#define BLOCKS WRITER_BLOCKS
#define BITMAP_BLOCK WRITER_BITMAP_BLOCK
#define CATALOG_BLOCK WRITER_CATALOG_BLOCK
#define CATALOG_NODES WRITER_CATALOG_NODES
#define EXTENTS_BLOCK WRITER_EXTENTS_BLOCK
#define EXTENTS_NODES WRITER_EXTENTS_NODES
#define ATTRIBUTES_BLOCK WRITER_ATTRIBUTES_BLOCK
#define ATTRIBUTES_NODES WRITER_ATTRIBUTES_NODES
#define JOURNAL_BLOCK WRITER_JOURNAL_BLOCK
#define JOURNAL_BLOCKS WRITER_JOURNAL_BLOCKS
#define JOURNAL_INFO_BLOCK WRITER_JOURNAL_INFO_BLOCK
#define JOURNAL_SIZE WRITER_JOURNAL_SIZE
#define JOURNAL_HEADER_SIZE WRITER_JOURNAL_HEADER_SIZE
#define JOURNAL_BLOCK_LIST WRITER_JOURNAL_BLOCK_LIST
#define VOLUME_HEADER WRITER_VOLUME_HEADER
#define ALT_VOLUME_HEADER WRITER_ALT_VOLUME_HEADER
#define ROOT_CNID WRITER_ROOT_CNID

static uint8_t image[IMAGE_SIZE];

uint8_t *writer_fixture_image(void)
{
    return image;
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

int writer_fixture_read(void *context, uint64_t offset, void *buffer,
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


static void build_image(int journaled)
{
    memset(image, 0, IMAGE_SIZE);

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
                   (journaled != 0 &&
                    ((block >= JOURNAL_BLOCK &&
                      block < JOURNAL_BLOCK + JOURNAL_BLOCKS) ||
                     block == JOURNAL_INFO_BLOCK)) ||
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

/*
 * The journal, installed the way a formatted volume carries it: an info block
 * that says where the journal lives, a journal header at that offset, and a
 * clean start==end pair. The checksum is over the 44-byte header with its own
 * field zeroed, which is what both the probe and `hfs_journal_open` verify.
 */
static uint32_t journal_checksum(const uint8_t *data, size_t length)
{
    uint32_t sum = 0u;
    for (size_t index = 0u; index < length; ++index)
        sum = (sum << 8) ^ (sum + data[index]);
    return ~sum;
}

static void install_journal(void)
{
    uint8_t *header = image + VOLUME_HEADER;
    put_be32(header + 4, (1u << 8) | (1u << 13)); /* unmounted | journaled */
    put_be32(header + 12, JOURNAL_INFO_BLOCK);

    uint8_t *info = image + JOURNAL_INFO_BLOCK * BLOCK_SIZE;
    put_be32(info, 1u); /* kJIJournalInFSMask */
    put_be64(info + 36, (uint64_t)JOURNAL_BLOCK * BLOCK_SIZE);
    put_be64(info + 44, (uint64_t)JOURNAL_SIZE);

    uint8_t *journal = image + JOURNAL_BLOCK * BLOCK_SIZE;
    put_be32(journal, 0x4a4e4c78u);
    put_be32(journal + 4, 0x12345678u);
    put_be64(journal + 8, JOURNAL_HEADER_SIZE);
    put_be64(journal + 16, JOURNAL_HEADER_SIZE);
    put_be64(journal + 24, (uint64_t)JOURNAL_SIZE);
    put_be32(journal + 32, JOURNAL_BLOCK_LIST);
    put_be32(journal + 40, JOURNAL_HEADER_SIZE);
    put_be32(journal + 36, journal_checksum(journal, 44u));

    memcpy(image + ALT_VOLUME_HEADER, header, 512);
}

void writer_fixture_apply(const struct hfs_txn *tx)
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
    writer_fixture_apply(&tx);
    return 0;
}

/* A whole volume with a seeded root, ready for the writer. */
int writer_fixture_build(struct hfs_volume *volume)
{
    build_image(0);
    if (hfs_volume_probe(volume, writer_fixture_read, NULL, 0, IMAGE_SIZE) != HFS_OK)
        return 1;
    return seed_root(volume);
}

int writer_fixture_write(void *context, uint64_t offset, const void *buffer,
                         size_t length)
{
    (void)context;
    if (offset > IMAGE_SIZE || length > IMAGE_SIZE - (size_t)offset)
        return -1;
    memcpy(image + (size_t)offset, buffer, length);
    return 0;
}

int writer_fixture_flush(void *context)
{
    (void)context;
    return 0;
}

int writer_fixture_build_journaled(struct hfs_volume *volume)
{
    build_image(1);
    install_journal();
    if (hfs_volume_probe(volume, writer_fixture_read, NULL, 0, IMAGE_SIZE) !=
        HFS_OK)
        return 1;
    if (volume->journaled == 0u)
        return 1;
    if (seed_root(volume) != 0)
        return 1;
    return 0;
}
