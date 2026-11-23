/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Read-only structural check of an HFS+ volume. See `hfs_check.h` for what is
 * and is not covered.
 *
 * Ground rules, the same ones the writer obeys:
 *  - read-only: the volume's write callback is never used here, and this file
 *    includes no write path at all;
 *  - allocation-free: one stack scratch buffer, bounded, no malloc;
 *  - findings, not exceptions: a corrupt fork does not stop the check, it sets
 *    a bit and the next check runs. A report that says "four things are wrong"
 *    is more useful than the first error, and a scrub must not give up on a
 *    volume because one tree is broken.
 */
#include "hfs_check.h"

#include <stddef.h>
#include <string.h>

#include "hfs_btree.h"
#include "hfs_catalog_compare.h"
#include "hfs_extents.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static int power2(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static uint32_t inline_blocks(const struct hfs_fork *fork)
{
    uint32_t total = 0u;
    for (uint32_t index = 0u; index < HFS_EXTENT_COUNT; ++index) {
        uint32_t count = fork->extents[index].count;
        if (count > UINT32_MAX - total)
            return UINT32_MAX;
        total += count;
    }
    return total;
}

/*
 * One fork's geometry: every extent inside the volume, no more blocks claimed
 * than the extents can hold, and a byte size that fits in those blocks.
 */
static void check_fork(const struct hfs_volume *volume,
                       const struct hfs_fork *fork, uint32_t *problems)
{
    uint32_t total = 0u;
    for (uint32_t index = 0u; index < HFS_EXTENT_COUNT; ++index) {
        uint32_t start = fork->extents[index].start;
        uint32_t count = fork->extents[index].count;
        if (count == 0u)
            continue;
        if (start >= volume->blocks || count > volume->blocks - start) {
            *problems |= HFS_CHECK_EXTENT_OUTSIDE;
            continue;
        }
        if (count > UINT32_MAX - total) {
            *problems |= HFS_CHECK_FORK_BLOCK_OVERFLOW;
            return;
        }
        total += count;
    }

    if (total > fork->blocks) {
        *problems |= HFS_CHECK_FORK_BLOCK_OVERFLOW;
        return;
    }
    /*
     * The inline extents may cover less than `blocks` when the rest lives in the
     * extents overflow tree; they may never cover more, and the byte size may
     * never exceed what the blocks can hold.
     */
    if (fork->size > (uint64_t)fork->blocks * (uint64_t)volume->block_size)
        *problems |= HFS_CHECK_FORK_SIZE_MISMATCH;
}

static void check_header(struct hfs_volume *volume, struct hfs_check_report *report)
{
    report->block_size = volume->block_size;
    report->blocks = volume->blocks;
    report->free_blocks_claimed = volume->free_blocks;

    if (!power2(volume->block_size) || volume->block_size < 512u ||
        volume->block_size > 65536u)
        report->problems |= HFS_CHECK_BAD_BLOCK_SIZE;
    if (volume->blocks == 0u)
        report->problems |= HFS_CHECK_BAD_BLOCK_COUNT;
    if (volume->free_blocks > volume->blocks)
        report->problems |= HFS_CHECK_FREE_EXCEEDS_TOTAL;

    /*
     * The next catalog ID is a writer input, not a structural count: a volume
     * that says 0, 1 or 2 will hand a reserved CNID to the next created file, so
     * the volume is refused a clean report before that happens.
     */
    uint8_t header[512];
    if (volume->read != NULL && volume->length >= 1024u + sizeof(header) &&
        volume->read(volume->context, volume->base + 1024u, header,
                     sizeof(header)) == 0) {
        uint32_t next_id = ((uint32_t)header[64] << 24) |
                           ((uint32_t)header[65] << 16) |
                           ((uint32_t)header[66] << 8) | header[67];
        /* Below 3 is a reserved CNID; 0xffffffff is the one value a create
         * cannot follow, because the increment wraps to zero. */
        if (next_id < 3u || next_id == 0xffffffffu)
            report->problems |= HFS_CHECK_CATALOG_ID;
    }
}

/*
 * Read a fork through a resolved extent map: the inline extents plus whatever
 * the extents overflow tree supplied, already in logical order. A range no run
 * covers is a hole, and a hole in the allocation file is a finding, not a
 * reason to guess.
 */
static int read_map(struct hfs_volume *volume, const struct hfs_extent *map,
                    size_t count, uint64_t offset, void *buffer, size_t length)
{
    uint8_t *out = buffer;
    const uint64_t block_size = volume->block_size;
    size_t done = 0u;
    while (done < length) {
        uint64_t run_start = 0u;
        int found = 0;
        for (size_t index = 0u; index < count; ++index) {
            uint64_t run_bytes = (uint64_t)map[index].count * block_size;
            if (offset < run_start || offset >= run_start + run_bytes) {
                run_start += run_bytes;
                continue;
            }
            uint64_t within = offset - run_start;
            size_t chunk = length - done;
            if ((uint64_t)chunk > run_bytes - within)
                chunk = (size_t)(run_bytes - within);
            if ((uint64_t)chunk > block_size)
                chunk = (size_t)block_size;
            uint64_t physical = volume->base +
                                (uint64_t)map[index].start * block_size + within;
            if (volume->read == NULL ||
                volume->read(volume->context, physical, out + done, chunk) != 0)
                return HFS_ERR_IO;
            done += chunk;
            offset += chunk;
            found = 1;
            break;
        }
        if (found == 0)
            return HFS_ERR_UNSUPPORTED;
    }
    return HFS_OK;
}

/*
 * The allocation bitmap. Its fork may spill into the extents overflow tree, so
 * the map is resolved first: a large volume's bitmap is several megabytes in
 * dozens of runs, and skipping it would leave the free-block count unverified on
 * exactly the volumes where it matters most. The pass is I/O-bound by design and
 * belongs off the boot path.
 */
static void check_allocation(struct hfs_volume *volume,
                             struct hfs_check_report *report)
{
    const struct hfs_fork *fork = &volume->allocation;
    uint64_t needed = ((uint64_t)volume->blocks + 7u) / 8u;
    if (fork->size < needed) {
        report->problems |= HFS_CHECK_ALLOCATION_TOO_SMALL;
        return;
    }
    if (needed > HFS_CHECK_BITMAP_BYTES) {
        report->skipped |= HFS_CHECK_SKIPPED_BITMAP;
        return;
    }

    /* HFS+ gives the allocation file a fixed CNID of 6. */
    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_extents extents;
    struct hfs_extent map[HFS_OVERFLOW_EXTENT_MAX];
    size_t map_count = 0u;
    uint32_t inline_count = inline_blocks(fork);
    int have_map = 0;
    if (inline_count == fork->blocks) {
        for (size_t index = 0u; index < HFS_EXTENT_COUNT; ++index) {
            map[index] = fork->extents[index];
        }
        map_count = HFS_EXTENT_COUNT;
        have_map = 1;
    } else if (hfs_extents_open(&extents, volume, scratch, sizeof(scratch)) ==
                   HFS_OK &&
               hfs_extents_collect(&extents, 6u, inline_count, fork->blocks, map,
                                   HFS_OVERFLOW_EXTENT_MAX, &map_count) ==
                   HFS_OK) {
        have_map = 1;
    }
    if (have_map == 0) {
        report->skipped |= HFS_CHECK_SKIPPED_BITMAP;
        return;
    }

    uint8_t buffer[4096];
    uint32_t counted = 0u;
    uint64_t block = 0u;
    uint64_t byte_offset = 0u;
    while (byte_offset < needed) {
        uint32_t chunk = (uint32_t)(needed - byte_offset);
        if (chunk > sizeof(buffer))
            chunk = sizeof(buffer);
        if (read_map(volume, map, map_count, byte_offset, buffer, chunk) !=
            HFS_OK) {
            report->skipped |= HFS_CHECK_SKIPPED_BITMAP;
            return;
        }
        for (uint32_t index = 0u; index < chunk; ++index) {
            uint8_t byte = buffer[index];
            for (uint32_t bit = 0u; bit < 8u; ++bit) {
                /*
                 * The last byte of the allocation file is padded when the block
                 * count is not a multiple of eight. macOS writes those padding
                 * bits as zero on a freshly formatted volume, so they are not a
                 * finding: only the real blocks are counted and compared.
                 */
                if (block < volume->blocks &&
                    (byte & (uint8_t)(0x80u >> bit)) == 0u)
                    counted += 1u;
                block += 1u;
            }
        }
        byte_offset += chunk;
    }

    report->bitmap_counted = 1;
    report->free_blocks_counted = counted;
    if (counted != volume->free_blocks)
        report->problems |= HFS_CHECK_FREE_MISMATCH;
}

/*
 * The three metadata trees. `hfs_btree_open` already validates the header record
 * and its indices, so the check reuses it rather than duplicating the rules: a
 * tree the reader refuses is a tree the volume cannot trust.
 */
static uint32_t check_tree(struct hfs_volume *volume, const struct hfs_fork *fork,
                           uint16_t *depth, uint32_t *nodes)
{
    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_btree btree;
    int rc = hfs_btree_open(&btree, volume, fork, scratch, sizeof(scratch));
    if (rc != HFS_OK) {
        /* A tree that cannot be opened is a header finding; a tree whose size
         * does not match its node count is the fork's own finding. */
        return rc == HFS_ERR_UNSUPPORTED ? HFS_CHECK_BTREE_SIZE
                                         : HFS_CHECK_BTREE_HEADER;
    }
    *depth = btree.depth;
    *nodes = btree.total_nodes;
    if (btree.total_nodes == 0u)
        return HFS_CHECK_BTREE_HEADER;
    if ((uint64_t)btree.total_nodes * btree.node_size >
        (uint64_t)fork->blocks * volume->block_size)
        return HFS_CHECK_BTREE_SIZE;
    if (btree.empty == 0u &&
        (btree.root_node >= btree.total_nodes ||
         btree.first_leaf >= btree.total_nodes ||
         btree.last_leaf >= btree.total_nodes))
        return HFS_CHECK_BTREE_INDEX;
    return 0u;
}

/*
 * Node ownership: which nodes the tree can reach, and what its free-node map says
 * about them. A B-tree hands a node out from that map, so the two must agree
 * exactly. A node marked used that no path reaches is a leak - its block is gone
 * forever and `fsck_hfs` reports the free count as wrong - and a node marked free
 * that a path does reach will be handed out again over live records, which is
 * the second kind of damage.
 *
 * The reachable set is found from the root: an index node's records point at
 * their children, and with depth one the root is itself a leaf. The walk is
 * breadth-first with a bounded stack, and a tree too large for the marks is
 * skipped rather than guessed - the pass must stay bounded on a real volume.
 */
#define HFS_CHECK_NODE_BITS 16384u
#define HFS_CHECK_NODE_BYTES (HFS_CHECK_NODE_BITS / 8u)
#define HFS_CHECK_NODE_STACK 512u

static int node_marked(const uint8_t *map, uint32_t node)
{
    return (map[node / 8u] & (uint8_t)(0x80u >> (node % 8u))) != 0;
}

static void check_tree_nodes(struct hfs_volume *volume,
                             const struct hfs_fork *fork,
                             struct hfs_check_report *report)
{
    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_btree btree;
    if (hfs_btree_open(&btree, volume, fork, scratch, sizeof(scratch)) != HFS_OK)
        return; /* check_tree already reported the header */
    if (btree.total_nodes == 0u || btree.total_nodes > HFS_CHECK_NODE_BITS) {
        report->skipped |= HFS_CHECK_SKIPPED_NODES;
        return;
    }

    /*
     * Read the header node through the fork, not `hfs_btree_read_node`: that one
     * refuses an empty tree, and an empty tree still has a map and a count to
     * check.
     */
    static uint8_t header_node[HFS_MAX_NODE_SIZE];
    if (btree.node_size > sizeof(header_node) ||
        hfs_fork_read(volume, fork, 0u, header_node, btree.node_size) != HFS_OK) {
        report->problems |= HFS_CHECK_BTREE_HEADER;
        return;
    }
    const uint8_t *node = header_node;
    const uint8_t *map = NULL;
    size_t map_length = 0u;
    if (hfs_btree_node_record(node, btree.node_size, 2u, &map,
                              &map_length) != HFS_OK) {
        report->problems |= HFS_CHECK_BTREE_HEADER;
        return;
    }
    if (map_length == 0u || map_length > HFS_CHECK_NODE_BYTES) {
        /* The map record is part of the header: a tree without one cannot say
         * which nodes are free, so this pass cannot either. */
        report->skipped |= HFS_CHECK_SKIPPED_NODES;
        return;
    }

    /* The node buffer is reused by the walk, so keep the map. */
    static uint8_t map_copy[HFS_CHECK_NODE_BYTES];
    memset(map_copy, 0, sizeof(map_copy));
    memcpy(map_copy, map, map_length);

    static uint8_t visited[HFS_CHECK_NODE_BYTES];
    memset(visited, 0, sizeof(visited));
    visited[0] = 0x80u; /* the header node is always in use */

    static uint32_t stack[HFS_CHECK_NODE_STACK];
    size_t pending = 0u;
    if (btree.empty == 0u) {
        if (btree.root_node >= btree.total_nodes) {
            report->problems |= HFS_CHECK_BTREE_INDEX;
            return;
        }
        stack[pending++] = btree.root_node;
    }

    while (pending > 0u) {
        const uint32_t number = stack[--pending];
        if (number >= btree.total_nodes) {
            report->problems |= HFS_CHECK_BTREE_INDEX;
            continue;
        }
        if (node_marked(visited, number))
            continue; /* a cycle: check_index_tree names it */
        visited[number / 8u] |= (uint8_t)(0x80u >> (number % 8u));
        if (hfs_btree_read_node(&btree, number, &node) != HFS_OK) {
            report->problems |= HFS_CHECK_BTREE_CHAIN;
            continue;
        }
        if (node[8] != 0)
            continue; /* leaf, or a kind the header checks already report */
        const uint16_t count = be16(node + 10);
        for (uint16_t index = 0u; index < count; ++index) {
            const uint8_t *record = NULL;
            size_t length = 0u;
            if (hfs_btree_node_record(node, btree.node_size, index, &record,
                                      &length) != HFS_OK)
                continue;
            if (length < 2u)
                continue;
            const uint16_t key_length = be16(record);
            if ((size_t)key_length + 2u + 4u > length)
                continue;
            const uint32_t child =
                ((uint32_t)record[2u + key_length] << 24) |
                ((uint32_t)record[3u + key_length] << 16) |
                ((uint32_t)record[4u + key_length] << 8) |
                record[5u + key_length];
            if (pending == HFS_CHECK_NODE_STACK) {
                report->skipped |= HFS_CHECK_SKIPPED_NODES;
                return;
            }
            stack[pending++] = child;
        }
    }

    uint32_t free_claimed = 0u;
    for (uint32_t node_number = 0u; node_number < btree.total_nodes;
         ++node_number) {
        const int used = node_marked(map_copy, node_number);
        const int reachable = node_marked(visited, node_number);
        if (used == reachable)
            continue;
        report->problems |= HFS_CHECK_BTREE_NODES;
        break;
    }
    for (uint32_t node_number = 0u; node_number < btree.total_nodes;
         ++node_number) {
        if (!node_marked(map_copy, node_number))
            ++free_claimed;
    }
    if (free_claimed != btree.free_nodes)
        report->problems |= HFS_CHECK_BTREE_NODES;
}

/*
 * The longest key either metadata tree can hold: a catalog key is six bytes of
 * parent ID and name length plus at most 255 UTF-16 units. The previous key is
 * kept whole so ordering across a leaf boundary is checked the same way as
 * ordering inside one leaf, which is the only way a chain of leaves can be
 * trusted.
 */
#define CHECK_KEY_MAX 516u

/* The reader's own depth limit (`CHECK_MAX_DEPTH`); the walk stack matches it. */
#define CHECK_MAX_DEPTH 32u

typedef int (*check_compare_fn)(const uint8_t *a, size_t a_length,
                                const uint8_t *b, size_t b_length);

/*
 * One leaf-chain walk. `compare` receives a key without its two-byte length
 * field, exactly as the B-tree's own comparator sees it; `record`, when set,
 * gets every record for a tree-specific look at its body. The three bits are
 * the findings this walk can raise, so the caller keeps its own vocabulary.
 */
struct leaf_walk {
    check_compare_fn compare;
    void (*record)(const uint8_t *record, size_t length, uint16_t key_length,
                   struct hfs_check_report *report, void *context);
    uint32_t key_bit;
    uint32_t order_bit;
    uint32_t chain_bit;
    void *context;
};

/*
 * The leaf chain promises four things: it starts at the header's first leaf, it
 * ends at the header's last leaf, it holds the number of records the header
 * claims, and its keys strictly increase. `hfs_btree_leaf_next` already refuses
 * a broken previous pointer, a leaf outside the tree and a loop, so a walk that
 * fails is a chain finding and nothing after it can be ordered against what came
 * before.
 */
static void walk_leaves(struct hfs_btree *btree, const struct leaf_walk *walk,
                        struct hfs_check_report *report)
{
    uint8_t previous[CHECK_KEY_MAX];
    size_t previous_length = 0u;
    uint32_t counted = 0u;
    int have_previous = 0;

    if (btree->empty != 0u) {
        /* An empty tree has no leaves at all, so a record count would be a lie. */
        if (btree->leaf_records != 0u)
            report->problems |= walk->chain_bit;
        return;
    }

    struct hfs_btree_leaf_iterator iterator;
    hfs_btree_leaf_init(btree, &iterator);
    for (;;) {
        const uint8_t *record = NULL;
        size_t length = 0u;
        int rc = hfs_btree_leaf_next(btree, &iterator, &record, &length);
        if (rc < 0) {
            report->problems |= walk->chain_bit;
            return;
        }
        if (rc == 0)
            break;

        if (length < 2u) {
            report->problems |= walk->key_bit;
            continue;
        }
        uint16_t key_length = be16(record);
        if ((size_t)key_length + 2u > length ||
            (size_t)key_length > CHECK_KEY_MAX) {
            report->problems |= walk->key_bit;
            continue;
        }

        if (walk->compare != NULL) {
            /*
             * Strictly greater: a catalog or extents key is unique, so an equal
             * neighbour is a duplicate, and a duplicate is a key the tree's own
             * search can no longer find reliably.
             */
            if (have_previous != 0 &&
                walk->compare(previous, previous_length, record + 2,
                              key_length) >= 0)
                report->problems |= walk->order_bit;
            for (size_t index = 0u; index < (size_t)key_length; ++index)
                previous[index] = record[2u + index];
            previous_length = (size_t)key_length;
            have_previous = 1;
        }

        if (walk->record != NULL)
            walk->record(record, length, key_length, report, walk->context);
        counted += 1u;
    }

    if (iterator.previous != btree->last_leaf || counted != btree->leaf_records)
        report->problems |= walk->chain_bit;
}

/*
 * The order the writer inserts in, so the check and the writes cannot disagree.
 * Only HFS+ reaches here — the probe refuses HFSX, the one format whose catalog
 * compares binary instead — so FastUnicodeCompare is always the right rule.
 */
static int catalog_key_compare(const uint8_t *a, size_t a_length,
                               const uint8_t *b, size_t b_length)
{
    return hfs_catalog_key_compare(NULL, a, a_length, b, b_length);
}

/*
 * The thread cross-check needs lookups while the leaf walk is running, so it uses
 * a second B-tree handle over the same catalog. Two handles mean two scratch
 * buffers, which is what keeps the record pointer the walk holds valid while a
 * lookup reads nodes underneath it.
 */
/*
 * Three sizes the check needs and the B-tree layer keeps to itself:
 * `CHECK_CATALOG_KEY_FIXED` is the part of a catalog key that is always there
 * (parent ID and name length), `CHECK_RECORD_PREFIX` is the 2-byte length that
 * opens every B-tree record, and `CHECK_CHILD_SIZE` is an index record's child
 * pointer. `CHECK_MAX_DEPTH` only bounds the lookups against a corrupt tree.
 */
#define CHECK_CATALOG_KEY_FIXED 6u
#define CHECK_RECORD_PREFIX 2u
#define CHECK_CHILD_SIZE 4u
#define CHECK_MAX_DEPTH 32u

struct catalog_lookup {
    struct hfs_btree btree;
    int usable;
};

static void write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/*
 * An exact catalog key match, walked by hand because the check has no transaction
 * to read through: 1 found with `record` pointing into the lookup's scratch, 0 no
 * such key, -1 the tree could not be read or is not a tree. The walk is bounded by
 * the tree's own depth limit, so a cycle in a corrupt tree cannot spin here.
 */
static int catalog_find(struct catalog_lookup *lookup, const uint8_t *key,
                        size_t key_length, const uint8_t **record,
                        size_t *record_length)
{
    if (lookup == NULL || lookup->usable == 0 || lookup->btree.empty != 0u)
        return 0;
    struct hfs_btree *btree = &lookup->btree;

    uint32_t node_number = btree->root_node;
    for (unsigned step = 0u; step <= CHECK_MAX_DEPTH; ++step) {
        const uint8_t *node = NULL;
        if (hfs_btree_read_node(btree, node_number, &node) != HFS_OK)
            return -1;
        uint16_t count = be16(node + 10);
        if (count == 0u)
            return -1;

        if (node[8] == 0xffu) {
            for (uint16_t i = 0u; i < count; ++i) {
                const uint8_t *existing = NULL;
                size_t existing_length = 0u;
                if (hfs_btree_node_record(node, btree->node_size, i, &existing,
                                          &existing_length) != HFS_OK)
                    return -1;
                if (existing_length < 2u)
                    return -1;
                uint16_t stored = be16(existing);
                if ((size_t)stored + 2u > existing_length)
                    return -1;
                int cmp = catalog_key_compare(existing + 2, stored, key,
                                              key_length);
                if (cmp == 0) {
                    *record = existing;
                    *record_length = existing_length;
                    return 1;
                }
                if (cmp > 0)
                    return 0;
            }
            return 0;
        }

        if (node[8] != 0u)
            return -1;
        uint16_t child = 0u;
        for (uint16_t i = 0u; i < count; ++i) {
            const uint8_t *existing = NULL;
            size_t existing_length = 0u;
            if (hfs_btree_node_record(node, btree->node_size, i, &existing,
                                      &existing_length) != HFS_OK)
                return -1;
            if (existing_length < 2u)
                return -1;
            uint16_t stored = be16(existing);
            if ((size_t)stored + CHECK_RECORD_PREFIX + CHECK_CHILD_SIZE >
                existing_length)
                return -1;
            int cmp = catalog_key_compare(existing + 2, stored, key,
                                          key_length);
            if (cmp <= 0)
                child = i;
            else
                break;
        }
        const uint8_t *child_record = NULL;
        size_t child_length = 0u;
        if (hfs_btree_node_record(node, btree->node_size, child, &child_record,
                                  &child_length) != HFS_OK)
            return -1;
        uint16_t stored = be16(child_record);
        if ((size_t)stored + CHECK_RECORD_PREFIX + CHECK_CHILD_SIZE >
            child_length)
            return -1;
        node_number = be32(child_record + CHECK_RECORD_PREFIX + stored);
    }
    return -1;
}

/*
 * A file or folder record has to be threaded: the thread record keyed by its CNID
 * carries the parent and the name the item is filed under, and a search by inode
 * reads exactly that. So the thread has to exist, be the kind that pairs with the
 * record (folder thread for a folder, file thread for a file), and carry the
 * record's own parent and name byte for byte.
 */
static void check_object_thread(const uint8_t *record, uint16_t name_length,
                                uint32_t parent, uint32_t cnid, uint16_t kind,
                                struct catalog_lookup *lookup,
                                struct hfs_check_report *report)
{
    uint8_t key[CHECK_CATALOG_KEY_FIXED];
    write_be32(key, cnid);
    write_be16(key + 4, 0u);

    const uint8_t *thread = NULL;
    size_t thread_length = 0u;
    if (catalog_find(lookup, key, sizeof(key), &thread, &thread_length) != 1) {
        report->problems |= HFS_CHECK_CATALOG_THREAD;
        return;
    }
    uint16_t thread_key_length = be16(thread);
    size_t field_offset = (size_t)thread_key_length + 2u;
    if ((size_t)thread_key_length + 2u > thread_length ||
        field_offset + 10u > thread_length) {
        report->problems |= HFS_CHECK_CATALOG_THREAD;
        return;
    }
    const uint8_t *field = thread + field_offset;
    uint16_t type = be16(field);
    uint16_t units = be16(field + 8);
    size_t name_bytes = (size_t)name_length * 2u;
    if (type != (kind == 1u ? 3u : 4u) || be32(field + 4) != parent ||
        units != name_length ||
        (size_t)units * 2u + 10u > thread_length - field_offset ||
        memcmp(field + 10, record + 8, name_bytes) != 0)
        report->problems |= HFS_CHECK_CATALOG_THREAD;
}

/*
 * And the other direction: a thread record has to name something that is really
 * filed under the parent and name it carries, with the CNID the thread is keyed
 * by. A thread pointing at a name that does not exist is an inode lookup that
 * resolves to nothing; a thread pointing at a name that belongs to another item is
 * worse, because the lookup succeeds and returns the wrong file.
 */
static void check_thread_object(const uint8_t *record, uint16_t key_length,
                                const uint8_t *field, size_t body_length,
                                struct catalog_lookup *lookup,
                                struct hfs_check_report *report)
{
    if (key_length != CHECK_CATALOG_KEY_FIXED)
        return;   /* a thread key that names something is CATALOG_BODY's finding */
    uint16_t type = be16(field);
    uint32_t parent = be32(field + 4);
    uint16_t units = be16(field + 8);
    if ((size_t)units * 2u + 10u > body_length)
        return;   /* the body check already reported it */

    uint8_t key[6u + 255u * 2u];
    if ((size_t)key_length + 2u + (size_t)units * 2u > 2u + sizeof(key))
        return;
    write_be32(key, parent);
    write_be16(key + 4, units);
    memcpy(key + 6, field + 10, (size_t)units * 2u);
    size_t key_size = 6u + (size_t)units * 2u;

    const uint8_t *object = NULL;
    size_t object_length = 0u;
    if (catalog_find(lookup, key, key_size, &object, &object_length) != 1) {
        report->problems |= HFS_CHECK_CATALOG_THREAD;
        return;
    }
    uint16_t object_key_length = be16(object);
    size_t object_offset = (size_t)object_key_length + 2u;
    if (object_offset + 10u > object_length) {
        report->problems |= HFS_CHECK_CATALOG_THREAD;
        return;
    }
    const uint8_t *body = object + object_offset;
    uint16_t object_type = be16(body);
    uint32_t cnid = be32(body + 8);
    uint32_t thread_cnid = be32(record + 2);
    if (object_type != (type == 3u ? 1u : 2u) || cnid != thread_cnid)
        report->problems |= HFS_CHECK_CATALOG_THREAD;
}

/*
 * Catalog record bodies. The key names the parent and the name; the body must be
 * the structure its record type promises, because a reader that trusts a short
 * body reads whatever follows it in the node.
 *
 *  - a file record carries an HFSPlusCatalogFile (248 bytes) and a real file ID;
 *  - a folder record carries an HFSPlusCatalogFolder (88 bytes) and a real
 *    folder ID — 0 and 1 are reserved, the root's own ID is 2;
 *  - a thread record (types 3 and 4) has an empty key name and carries the
 *    parent and the name of the item it threads, so a thread key that names
 *    something is damage, and so is a thread that names nothing.
 *
 * The volume header's file and folder counts are tallied here too, and the root
 * folder — parent ID 1 — is excluded, exactly as the header excludes it.
 */
static void catalog_record(const uint8_t *record, size_t length,
                           uint16_t key_length,
                           struct hfs_check_report *report, void *context)
{
    struct catalog_lookup *lookup = context;
    if (key_length < 6u) {
        report->problems |= HFS_CHECK_CATALOG_RECORD;
        return;
    }
    uint16_t name_length = be16(record + 6);
    if ((uint32_t)name_length * 2u + 6u > key_length) {
        report->problems |= HFS_CHECK_CATALOG_RECORD;
        return;
    }
    size_t field_offset = (size_t)key_length + 2u;
    if (field_offset + 2u > length) {
        report->problems |= HFS_CHECK_CATALOG_RECORD;
        return;
    }
    const uint8_t *field = record + field_offset;
    /* Catalog records start with a 16-bit recordType, not a byte. */
    uint16_t type = be16(field);
    /* The body length includes the recordType, as the HFS+ structures do. */
    size_t body_length = length - field_offset;
    uint32_t parent = be32(record + 2);

    switch (type) {
    case 1u: /* folder */
        if (body_length < 88u || name_length == 0u) {
            report->problems |= HFS_CHECK_CATALOG_BODY;
            break;
        }
        if (be32(field + 8) < 2u)
            report->problems |= HFS_CHECK_CATALOG_BODY;
        else
            check_object_thread(record, name_length, parent, be32(field + 8),
                                1u, lookup, report);
        if (parent != 1u)
            report->catalog_folders += 1u;
        break;
    case 2u: /* file */
        if (body_length < 248u || name_length == 0u) {
            report->problems |= HFS_CHECK_CATALOG_BODY;
            break;
        }
        if (be32(field + 8) < 2u)
            report->problems |= HFS_CHECK_CATALOG_BODY;
        else
            check_object_thread(record, name_length, parent, be32(field + 8),
                                2u, lookup, report);
        report->catalog_files += 1u;
        break;
    case 3u: /* folder thread */
    case 4u: { /* file thread */
        if (name_length != 0u || body_length < 10u) {
            report->problems |= HFS_CHECK_CATALOG_BODY;
            break;
        }
        uint16_t thread_name = be16(field + 8);
        if (thread_name == 0u ||
            (uint32_t)thread_name * 2u + 10u > body_length)
            report->problems |= HFS_CHECK_CATALOG_BODY;
        else
            check_thread_object(record, key_length, field, body_length, lookup,
                                report);
        break;
    }
    default:
        report->problems |= HFS_CHECK_CATALOG_RECORD;
        break;
    }
}

/*
 * The catalog: walk every leaf record, check the key the record claims to have,
 * check the body that key promises, and count what is actually there. The volume
 * header keeps the file and folder counts, and fsck checks them the same way; a
 * count that drifts is the quietest kind of catalog damage.
 */
static void check_catalog(struct hfs_volume *volume,
                          struct hfs_check_report *report)
{
    uint8_t header[HFS_VOLUME_HEADER_SIZE];
    if (volume->read == NULL ||
        volume->read(volume->context,
                     volume->base + HFS_VOLUME_HEADER_OFFSET, header,
                     sizeof(header)) != 0) {
        report->problems |= HFS_CHECK_CATALOG_RECORD;
        return;
    }
    report->header_files = be32(header + 32);
    report->header_folders = be32(header + 36);

    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_btree btree;
    if (hfs_btree_open(&btree, volume, &volume->catalog, scratch,
                       sizeof(scratch)) != HFS_OK) {
        report->problems |= HFS_CHECK_BTREE_HEADER;
        return;
    }

    /*
     * A second handle over the same tree, for the thread cross-check: the walk's
     * record pointer lives in the first handle's scratch, so lookups need their
     * own.
     */
    static uint8_t lookup_scratch[HFS_MAX_NODE_SIZE + 1u];
    static struct catalog_lookup lookup;
    memset(&lookup, 0, sizeof(lookup));
    lookup.usable =
        hfs_btree_open(&lookup.btree, volume, &volume->catalog, lookup_scratch,
                       sizeof(lookup_scratch)) == HFS_OK ? 1 : 0;

    static const struct leaf_walk walk = {
        catalog_key_compare, catalog_record, HFS_CHECK_CATALOG_RECORD,
        HFS_CHECK_CATALOG_ORDER, HFS_CHECK_BTREE_CHAIN, &lookup
    };
    walk_leaves(&btree, &walk, report);

    if (report->catalog_files != report->header_files ||
        report->catalog_folders != report->header_folders)
        report->problems |= HFS_CHECK_CATALOG_COUNT;
}

/*
 * Extents keys order by fork type, then file ID, then start block, and every
 * one of those is big-endian, so comparing the raw key bytes is the same order
 * the tree itself uses. A record's extent list needs no separate check:
 * `check_allocation` already walks the map this tree supplies.
 */
static int extents_key_compare(const uint8_t *a, size_t a_length,
                               const uint8_t *b, size_t b_length)
{
    size_t common = a_length < b_length ? a_length : b_length;
    int result = common == 0u ? 0 : memcmp(a, b, common);
    if (result != 0)
        return result < 0 ? -1 : 1;
    if (a_length != b_length)
        return a_length < b_length ? -1 : 1;
    return 0;
}

static void check_extents_keys(struct hfs_volume *volume,
                               struct hfs_check_report *report)
{
    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_btree btree;
    if (hfs_btree_open(&btree, volume, &volume->extents, scratch,
                       sizeof(scratch)) != HFS_OK) {
        report->problems |= HFS_CHECK_BTREE_HEADER;
        return;
    }
    static const struct leaf_walk walk = {
        extents_key_compare, NULL, HFS_CHECK_EXTENTS_ORDER,
        HFS_CHECK_EXTENTS_ORDER, HFS_CHECK_BTREE_CHAIN, NULL
    };
    walk_leaves(&btree, &walk, report);
}

/*
 * Index nodes. Every record in an index node is a key and a child pointer, and a
 * search trusts three things about them: the child exists, it is the level below
 * (an index node until the last step, a leaf after it), and the key that points
 * at a subtree is not greater than the first key in that subtree. HFS+ lets an
 * ancestor key go stale after a delete — it may sit below the subtree's first
 * key — so the rule is "not greater", which is exactly what a search needs, and
 * a key that sorts above the subtree makes those records unreachable.
 *
 * The walk is iterative and bounded by the tree's depth; reaching more nodes than
 * the tree has is a cycle, and a cycle is reported as a chain finding.
 */
struct index_frame {
    uint32_t node;
    uint16_t height;
};

static void check_index_nodes(struct hfs_btree *btree, check_compare_fn compare,
                              struct hfs_check_report *report)
{
    if (btree->empty != 0u || btree->depth < 2u)
        return;

    struct index_frame frames[CHECK_MAX_DEPTH];
    uint8_t previous[CHECK_KEY_MAX];
    uint8_t key[CHECK_KEY_MAX];
    size_t previous_length = 0u;
    uint32_t visited = 0u;
    size_t depth = 0u;
    frames[depth].node = btree->root_node;
    frames[depth].height = btree->depth;
    depth += 1u;

    while (depth != 0u) {
        struct index_frame frame = frames[depth - 1u];
        depth -= 1u;
        if (++visited > btree->total_nodes) {
            report->problems |= HFS_CHECK_BTREE_CHAIN;
            return;
        }

        const uint8_t *node = NULL;
        if (hfs_btree_read_node(btree, frame.node, &node) != HFS_OK ||
            node[8] != 0u || node[9] != (uint8_t)frame.height ||
            be16(node + 10) == 0u) {
            report->problems |= HFS_CHECK_BTREE_INDEX;
            continue;
        }
        uint16_t count = be16(node + 10);

        int have_previous = 0;
        for (uint16_t index = 0u; index < count; ++index) {
            /*
             * The node is re-read for every record: the child read below lands in
             * the same scratch buffer, so a record pointer only lives until then.
             */
            if (hfs_btree_read_node(btree, frame.node, &node) != HFS_OK) {
                report->problems |= HFS_CHECK_BTREE_INDEX;
                break;
            }
            const uint8_t *record = NULL;
            size_t length = 0u;
            if (hfs_btree_node_record(node, btree->node_size, index, &record,
                                      &length) != HFS_OK) {
                report->problems |= HFS_CHECK_BTREE_INDEX;
                break;
            }
            uint16_t key_length = be16(record);
            if ((size_t)key_length + 6u > length ||
                (size_t)key_length > CHECK_KEY_MAX) {
                report->problems |= HFS_CHECK_BTREE_INDEX;
                break;
            }
            uint32_t child = be32(record + 2u + (size_t)key_length);
            for (uint16_t copy = 0u; copy < key_length; ++copy)
                key[copy] = record[2u + copy];

            if (have_previous != 0 &&
                compare(previous, previous_length, key, key_length) >= 0)
                report->problems |= HFS_CHECK_BTREE_INDEX;
            for (uint16_t copy = 0u; copy < key_length; ++copy)
                previous[copy] = key[copy];
            previous_length = (size_t)key_length;
            have_previous = 1;

            if (child >= btree->total_nodes) {
                report->problems |= HFS_CHECK_BTREE_INDEX;
                continue;
            }

            const uint8_t *child_node = NULL;
            if (hfs_btree_read_node(btree, child, &child_node) != HFS_OK) {
                report->problems |= HFS_CHECK_BTREE_INDEX;
                continue;
            }
            /* The last level of index nodes points at leaves; the rest at index. */
            uint8_t kind = frame.height == 2u ? 0xffu : 0x00u;
            if (child_node[8] != kind ||
                (uint16_t)child_node[9] + 1u != (uint16_t)frame.height ||
                be16(child_node + 10) == 0u) {
                report->problems |= HFS_CHECK_BTREE_INDEX;
                continue;
            }
            const uint8_t *first = NULL;
            size_t first_length = 0u;
            if (hfs_btree_node_record(child_node, btree->node_size, 0u, &first,
                                      &first_length) != HFS_OK ||
                first_length < 2u) {
                report->problems |= HFS_CHECK_BTREE_INDEX;
                continue;
            }
            uint16_t first_key_length = be16(first);
            if ((size_t)first_key_length + 2u > first_length) {
                report->problems |= HFS_CHECK_BTREE_INDEX;
                continue;
            }
            if (compare(key, key_length, first + 2u, first_key_length) > 0)
                report->problems |= HFS_CHECK_BTREE_INDEX;

            /* Leaves are already covered by the leaf-chain walk. */
            if (frame.height > 2u) {
                if (depth == CHECK_MAX_DEPTH) {
                    report->problems |= HFS_CHECK_BTREE_CHAIN;
                    return;
                }
                frames[depth].node = child;
                frames[depth].height = (uint16_t)(frame.height - 1u);
                depth += 1u;
            }
        }
    }
}

static void check_index_tree(struct hfs_volume *volume, const struct hfs_fork *fork,
                             check_compare_fn compare,
                             struct hfs_check_report *report)
{
    static uint8_t scratch[HFS_MAX_NODE_SIZE + 1u];
    struct hfs_btree btree;
    /* A tree that will not open is already a header finding. */
    if (hfs_btree_open(&btree, volume, fork, scratch, sizeof(scratch)) != HFS_OK)
        return;
    check_index_nodes(&btree, compare, report);
}

int hfs_check_volume(struct hfs_volume *volume, struct hfs_check_report *report)
{
    if (volume == NULL || report == NULL)
        return HFS_ERR_ARGUMENT;

    for (size_t index = 0u; index < sizeof(*report); ++index)
        ((uint8_t *)report)[index] = 0u;

    check_header(volume, report);
    if ((report->problems & HFS_CHECK_BAD_BLOCK_SIZE) != 0u ||
        (report->problems & HFS_CHECK_BAD_BLOCK_COUNT) != 0u) {
        /* Without a block size and a block count nothing else can be checked
         * honestly: every fork geometry below depends on both. */
        return HFS_OK;
    }

    check_fork(volume, &volume->allocation, &report->problems);
    check_fork(volume, &volume->extents, &report->problems);
    check_fork(volume, &volume->catalog, &report->problems);
    check_fork(volume, &volume->attributes_file, &report->problems);

    check_allocation(volume, report);

    report->problems |= check_tree(volume, &volume->catalog, &report->catalog_depth,
                                   &report->catalog_nodes);
    report->problems |= check_tree(volume, &volume->extents, &report->extents_depth,
                                   &report->extents_nodes);
    report->problems |= check_tree(volume, &volume->attributes_file,
                                   &report->attributes_depth,
                                   &report->attributes_nodes);

    if (report->catalog_nodes != 0u)
        check_tree_nodes(volume, &volume->catalog, report);
    if (report->extents_nodes != 0u)
        check_tree_nodes(volume, &volume->extents, report);
    if (report->attributes_nodes != 0u)
        check_tree_nodes(volume, &volume->attributes_file, report);

    if (report->extents_nodes != 0u) {
        check_extents_keys(volume, report);
        check_index_tree(volume, &volume->extents, extents_key_compare, report);
    }
    if (report->catalog_nodes != 0u) {
        check_catalog(volume, report);
        check_index_tree(volume, &volume->catalog, catalog_key_compare, report);
    }
    return HFS_OK;
}
