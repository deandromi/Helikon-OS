/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libFuzzer target for the catalog writer.
 *
 * `test_writer_mutations.c` corrupts one field at a time from a curated list; this
 * harness lets the fuzzer choose the fields, the widths and the values, and it
 * carries its own oracle instead of only looking for crashes:
 *
 *   a refused call must leave the image byte-identical, and a call that commits
 *   must not turn a volume the structural check called clean into an
 *   inconsistent one.
 *
 * Those are the two rules the sweep holds the writer to, and a fuzz input that
 * breaks either of them aborts with a message, so libFuzzer saves the input as an
 * artifact and it becomes a corpus file.
 *
 * The base image is the shared writer fixture: a valid 1 MiB volume with the root
 * folder seeded through the real writer. Without a valid base a mutation is not
 * interesting - the probe refuses it before the writer runs - so the harness
 * spends its mutations on the bytes a writer actually reads.
 *
 * Input layout (a cursor, every field optional once the bytes run out):
 *
 *   [1]  operation: 0 create, 1 create folder, 2 rename, 3 write data,
 *       4 truncate to zero, 5 delete, 6 structural check only, 7 open only
 *   [1]  how many mutations follow (0..7)
 *   [6]  per mutation: offset (2 bytes, big-endian, modulo the image),
 *        width selector (1, 2, 4 or 8) and value (1 byte, repeated)
 *   [n]  what is left is payload: the data length and the bytes to write
 *
 *   ./tools/fuzz.sh writer 300
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_check.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "hfs_volume.h"

#include "writer_fixture.h"

#define IMAGE_SIZE WRITER_IMAGE_SIZE
#define BLOCK_SIZE WRITER_BLOCK_SIZE
#define CATALOG_SCRATCH 8192u
#define MAX_PAYLOAD 1500u
#define MAX_MUTATIONS 7u

static uint8_t snapshot[IMAGE_SIZE];

struct cursor {
    const unsigned char *data;
    size_t size;
    size_t at;
};

static unsigned next_byte(struct cursor *cursor)
{
    if (cursor->at >= cursor->size)
        return 0u;
    return cursor->data[cursor->at++];
}

static uint32_t next_u32(struct cursor *cursor)
{
    uint32_t value = 0u;
    for (int i = 0; i < 4; ++i)
        value = (value << 8) | next_byte(cursor);
    return value;
}

static void fail(const char *what, const char *detail)
{
    fprintf(stderr, "writer fuzz: %s (%s)\n", what, detail);
    abort();
}

static void apply_mutations(struct cursor *cursor, uint8_t *image,
                            unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        uint32_t offset = next_u32(cursor) % IMAGE_SIZE;
        uint32_t width = 1u << (next_byte(cursor) & 3u);
        uint8_t value = (uint8_t)next_byte(cursor);
        for (uint32_t byte = 0; byte < width; ++byte) {
            if (offset + byte < IMAGE_SIZE)
                image[offset + byte] = value;
        }
    }
}

static void start_tx(struct hfs_journal *journal, struct hfs_txn *tx)
{
    memset(journal, 0, sizeof(*journal));
    journal->sector_size = BLOCK_SIZE;
    memset(tx, 0, sizeof(*tx));
    tx->journal = journal;
    tx->active = 1;
}

/*
 * Both rules in one place: a refusal must not have touched anything, and a
 * commit must not have made a clean volume inconsistent.
 */
static void hold_to_the_rules(int rc, int had_problems, uint32_t before,
                              uint32_t after, const char *what)
{
    if (rc != HFS_OK) {
        if (memcmp(snapshot, writer_fixture_image(), IMAGE_SIZE) != 0)
            fail(what, "refused but the image changed");
        return;
    }
    if (had_problems == 0 && after != 0u)
        fail(what, "a clean volume became inconsistent");
    if (had_problems != 0 && after == 0u)
        return;   /* a commit may repair what a mutation broke */
    (void)before;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    struct cursor cursor = { data, size, 0u };
    unsigned operation = next_byte(&cursor) & 7u;
    unsigned mutations = next_byte(&cursor) % (MAX_MUTATIONS + 1u);

    struct hfs_volume volume;
    if (writer_fixture_build(&volume) != 0)
        fail("fixture", "the base image does not build");

    uint8_t *image = writer_fixture_image();
    apply_mutations(&cursor, image, mutations);

    if (hfs_volume_probe(&volume, writer_fixture_read, NULL, 0,
                         IMAGE_SIZE) != HFS_OK)
        return 0;

    uint32_t before = 0u;
    if (operation != 7u) {
        struct hfs_check_report report;
        if (hfs_check_volume(&volume, &report) != HFS_OK)
            return 0;
        before = report.problems;
    }

    static uint8_t scratch[CATALOG_SCRATCH];
    struct hfs_catalog catalog;
    memset(&catalog, 0, sizeof(catalog));
    if (hfs_catalog_open(&catalog, &volume, scratch, sizeof(scratch)) != HFS_OK)
        return 0;

    memcpy(snapshot, image, IMAGE_SIZE);

    struct hfs_journal journal;
    struct hfs_txn tx;
    start_tx(&journal, &tx);

    static uint8_t name[4] = { 0, 'f', 0, 'z' };
    static uint8_t renamed[4] = { 0, 'r', 0, 'z' };
    uint32_t cnid = 16u;
    int rc = HFS_OK;
    const char *what = "open";

    switch (operation) {
    case 0u:
        what = "create";
        rc = hfs_catalog_create_file(&catalog, &tx, WRITER_ROOT_CNID, name, 2u,
                                     0644, hfs_catalog_key_compare, NULL, &cnid);
        break;
    case 1u:
        what = "create folder";
        rc = hfs_catalog_create_folder(&catalog, &tx, WRITER_ROOT_CNID, name,
                                       2u, 0755, hfs_catalog_key_compare, NULL,
                                       &cnid);
        break;
    case 2u:
        what = "rename";
        rc = hfs_catalog_rename(&catalog, &tx, WRITER_ROOT_CNID, name, 2u,
                                cnid, WRITER_ROOT_CNID, renamed, 2u,
                                hfs_catalog_key_compare, NULL);
        break;
    case 3u:
    case 4u: {
        size_t length = cursor.size - cursor.at;
        if (length > MAX_PAYLOAD)
            length = MAX_PAYLOAD;
        static uint8_t payload[MAX_PAYLOAD];
        memcpy(payload, cursor.data + cursor.at, length);
        if (operation == 3u) {
            what = "write data";
            rc = hfs_catalog_write_file(&catalog, &tx, WRITER_ROOT_CNID, name,
                                        2u, 0u, payload, length,
                                        hfs_catalog_key_compare, NULL);
        } else {
            (void)payload;
            what = "truncate";
            rc = hfs_catalog_truncate_to(&catalog, &tx, WRITER_ROOT_CNID, name,
                                         2u, 0u, hfs_catalog_key_compare, NULL);
        }
        break;
    }
    case 5u:
        what = "delete";
        rc = hfs_catalog_delete_file(&catalog, &tx, WRITER_ROOT_CNID, name, 2u,
                                     cnid, hfs_catalog_key_compare, NULL);
        break;
    default:
        return 0;   /* create/check only: nothing to hold to the rules */
    }

    uint32_t after = before;
    if (rc == HFS_OK) {
        writer_fixture_apply(&tx);
        struct hfs_check_report report;
        if (hfs_check_volume(&volume, &report) == HFS_OK)
            after = report.problems;
    }
    hold_to_the_rules(rc, before != 0u, before, after, what);
    return 0;
}
