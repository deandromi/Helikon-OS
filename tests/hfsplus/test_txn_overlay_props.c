/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Property test for the transaction overlay.
 *
 * The overlay is the reason two updaters of the volume header can live in one
 * transaction: the second one reads through the first one's staged bytes
 * instead of the committed ones, so it cannot silently drop the first one's
 * change. `test_txn.c` checks the cases someone thought of; this one models the
 * contract as a byte map and hammers it with random stages and random read
 * ranges:
 *
 *   - `hfs_txn_overlay` fills exactly the staged bytes that intersect the range
 *     and leaves the rest of the caller's buffer alone;
 *   - it reports 1 when anything intersected and 0 when nothing did, and a
 *     zero-length range touches nothing;
 *   - a later stage of the same bytes wins, whatever its size: growing a staged
 *     run, patching a hole inside one, or replacing it exactly all leave the
 *     newest bytes in place, and adjacent stages of one node merge into a single
 *     transaction entry instead of one entry per sector (issue #115);
 *   - slices at both ends are copied from the right offset of the run data,
 *     including a range that starts or ends in the middle of a run;
 *   - `hfs_txn_read` is the disk plus the same overlay, and the staging rules
 *     hold: `hfs_txn_stage` refuses a duplicate start sector with BUSY and a
 *     33rd entry with NO_SPACE, a merge that would not fit one entry is BUSY,
 *     and a bad size is ARGUMENT.
 *
 * The model is a byte map - `model_have` says which bytes are staged and
 * `model_bytes` what they hold - so it does not repeat the implementation's run
 * bookkeeping. The only structural claim it makes is that the number of
 * contiguous staged runs never exceeds the number of transaction entries, which
 * follows from "entries never overlap".
 *
 *   ./test_txn_overlay_props            # the committed sweep
 *   ./test_txn_overlay_props 7 20000    # another seed, more steps
 */
#include "hfs_txn.h"
#include "test_check.h"
#include "test_image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 512u
#define MAX_BLOCK 8192u
#define IMAGE TEST_IMAGE_SIZE
#define DEFAULT_STEPS 4000u

static struct hfs_txn tx;
static struct hfs_journal journal;
static struct hfs_volume volume;

/* The model: which bytes are staged, and what they hold. */
static uint8_t model_have[IMAGE];
static uint8_t model_bytes[IMAGE];
static size_t model_runs;

static uint8_t image_copy[IMAGE];
static uint8_t expected[IMAGE];
static uint8_t actual[IMAGE];

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

static void fill_random(uint8_t *data, uint32_t size)
{
    for (uint32_t i = 0u; i < size; ++i)
        data[i] = (uint8_t)next_random();
}

static void model_take(uint64_t offset, const uint8_t *data, size_t length)
{
    memcpy(model_bytes + offset, data, length);
    memset(model_have + offset, 1, length);
}

/* Number of contiguous staged runs, recomputed from the byte map. */
static size_t model_count_runs(void)
{
    size_t runs = 0u;
    int inside = 0;
    for (size_t i = 0u; i < IMAGE; ++i) {
        if (model_have[i] != 0u && !inside) {
            ++runs;
            inside = 1;
        } else if (model_have[i] == 0u) {
            inside = 0;
        }
    }
    return runs;
}

/*
 * The model's answer for `hfs_txn_overlay`: patch `out`, which the caller has
 * already filled, with the staged bytes that intersect the range. This mirrors
 * the contract, not the code - the overlay never reads the disk itself.
 */
static int model_patch(uint64_t offset, size_t length, uint8_t *out)
{
    if (length == 0u)
        return 0;
    int overlapped = 0;
    for (size_t i = 0u; i < length; ++i) {
        if (model_have[offset + i] != 0u) {
            out[i] = model_bytes[offset + i];
            overlapped = 1;
        }
    }
    return overlapped;
}

/* The model's answer for `hfs_txn_read`: the disk, then the same patch. */
static int model_read(uint64_t offset, size_t length, uint8_t *out)
{
    memcpy(out, image_copy + (size_t)offset, length);
    return model_patch(offset, length, out);
}

static int range_is_free(uint64_t offset, size_t length)
{
    for (size_t i = 0u; i < length; ++i) {
        if (model_have[offset + i] != 0u)
            return 0;
    }
    return 1;
}

/*
 * The union the transaction must produce for a stage of this range: the range
 * itself plus every staged run that overlaps it, closed transitively. A stage
 * never joins a run it only touches, so neighbours stay separate entries. When
 * the union would exceed one entry the stage must be refused; that is the one
 * part of the answer the test predicts from the transaction's own runs, because
 * a byte map cannot tell two abutting runs from one.
 */
static int union_fits(uint64_t offset, size_t size)
{
    uint64_t start = offset;
    uint64_t end = offset + size;
    int changed = 1;
    while (changed) {
        changed = 0;
        for (size_t i = 0; i < tx.count; ++i) {
            const uint64_t run_start = tx.blocks[i].sector * SECTOR;
            const uint64_t run_end = run_start + tx.blocks[i].size;
            if (run_end <= start || run_start >= end)
                continue;
            if (run_start < start) {
                start = run_start;
                changed = 1;
            }
            if (run_end > end) {
                end = run_end;
                changed = 1;
            }
        }
    }
    return end - start <= MAX_BLOCK;
}

static int range_is_staged(uint64_t offset, size_t length)
{
    for (size_t i = 0u; i < length; ++i) {
        if (model_have[offset + i] != 0u)
            return 1;
    }
    return 0;
}

/* Compares the byte map with the transaction's own entries. */
static int cross_check(void)
{
    uint8_t have[IMAGE];
    memset(have, 0, sizeof(have));
    for (size_t i = 0; i < tx.count; ++i) {
        const uint64_t start = tx.blocks[i].sector * SECTOR;
        for (uint32_t b = 0; b < tx.blocks[i].size; ++b) {
            if (have[start + b] != 0u) {
                fprintf(stderr, "  FAIL entries overlap at %llu\n",
                        (unsigned long long)(start + b));
                return 1;
            }
            have[start + b] = 1;
            if (model_have[start + b] == 0u ||
                model_bytes[start + b] != tx.blocks[i].data[b]) {
                fprintf(stderr,
                        "  FAIL model and transaction disagree at %llu "
                        "(model_have=%u)\n",
                        (unsigned long long)(start + b),
                        (unsigned)model_have[start + b]);
                return 1;
            }
        }
    }
    for (size_t i = 0; i < IMAGE; ++i) {
        if (model_have[i] != 0u && have[i] == 0u) {
            fprintf(stderr, "  FAIL model staged %zu, transaction did not\n", i);
            return 1;
        }
    }
    return 0;
}

static int stage_reports(void)
{
    /* Entries never overlap, so the staged bytes need at least this many
     * entries, and the array never holds more than the budget. */
    CHECK(tx.count <= HFS_TXN_MAX_BLOCKS);
    CHECK(model_runs <= tx.count);
    CHECK(cross_check() == 0);
    return 0;
}

/* Stages a fresh range in both the transaction and the model. */
static int stage_new(void)
{
    uint8_t data[MAX_BLOCK];
    uint32_t size = SECTOR * (1u + next_random() % (MAX_BLOCK / SECTOR));
    uint64_t offset = SECTOR * (uint64_t)(next_random() %
                       (IMAGE / SECTOR - MAX_BLOCK / SECTOR));
    /*
     * Free ground with a free sector either side: adjacent runs would look like
     * one run to the byte map while the transaction keeps them as separate
     * entries, and the union arithmetic below would stop matching.
     */
    if (!range_is_free(offset, size) ||
        (offset != 0u && model_have[offset - 1u] != 0u) ||
        model_have[offset + size] != 0u)
        return 0; /* overlapping ranges belong to the replace cases */

    fill_random(data, size);
    int rc = hfs_txn_stage(&tx, offset / SECTOR, data, size);
    if (tx.count >= HFS_TXN_MAX_BLOCKS) {
        CHECK(rc == HFS_ERR_NO_SPACE);
        return 0;
    }
    CHECK(rc == HFS_OK);
    model_take(offset, data, size);
    model_runs = model_count_runs();
    CHECK(stage_reports() == 0);
    return 0;
}

/*
 * Stages over ground that is already staged: the same range, a range inside
 * one, a range that grows one, and a range that joins two of them.
 */
static int replace_staged(void)
{
    if (model_runs == 0u)
        return 0;

    uint8_t data[MAX_BLOCK];
    const uint32_t pick = next_random() % 4u;
    uint64_t offset = 0u;
    uint32_t size = 0u;

    if (pick == 0u) {
        /* Exactly one staged sector. */
        for (size_t attempt = 0; attempt < 64u; ++attempt) {
            offset = SECTOR * (uint64_t)(next_random() % (IMAGE / SECTOR));
            if (model_have[offset] != 0u && model_have[offset + SECTOR - 1u] != 0u)
                break;
        }
        size = SECTOR;
    } else if (pick == 1u) {
        /* Grow a staged sector by a few more. */
        for (size_t attempt = 0; attempt < 64u; ++attempt) {
            offset = SECTOR * (uint64_t)(next_random() %
                                         (IMAGE / SECTOR - 8u));
            if (model_have[offset] != 0u)
                break;
        }
        size = SECTOR * (2u + next_random() % 7u);
    } else if (pick == 2u) {
        /* Patch a sub-range in the middle of whatever is staged. */
        offset = SECTOR * (uint64_t)(next_random() % (IMAGE / SECTOR - 8u));
        for (size_t attempt = 0; attempt < 64u; ++attempt) {
            if (model_have[offset] != 0u && model_have[offset + 2u * SECTOR] != 0u)
                break;
            offset = SECTOR * (uint64_t)(next_random() %
                                         (IMAGE / SECTOR - 8u));
        }
        offset += SECTOR;
        size = SECTOR;
    } else {
        /* Join a staged run with its neighbour. */
        for (size_t attempt = 0; attempt < 64u; ++attempt) {
            const uint32_t sector = next_random() % (IMAGE / SECTOR - 2u);
            if (model_have[sector * SECTOR] != 0u &&
                model_have[(sector + 1u) * SECTOR] == 0u) {
                offset = (uint64_t)sector * SECTOR;
                size = 2u * SECTOR;
                break;
            }
        }
        if (size == 0u)
            return 0;
    }

    if (offset + size > IMAGE)
        return 0;
    fill_random(data, size);
    /* Growing the run must stay within one entry, or the merge is refused. */
    const int fits = union_fits(offset, size);
    const int was_staged = range_is_staged(offset, size);
    int rc = hfs_txn_stage_replace(&tx, offset / SECTOR, data, size);
    if (!fits) {
        CHECK(rc == HFS_ERR_BUSY);
        return 0;
    }
    CHECK(rc == HFS_OK);
    (void)was_staged;
    model_take(offset, data, size);
    model_runs = model_count_runs();
    CHECK(stage_reports() == 0);
    return 0;
}

/* A merge that would spill past one entry is refused and changes nothing. */
static int oversized_merge_is_refused(void)
{
    uint8_t data[MAX_BLOCK + SECTOR];
    uint64_t offset = SECTOR * (uint64_t)(next_random() %
                       (IMAGE / SECTOR - MAX_BLOCK / SECTOR - 2u));
    /* Free ground plus a free sector either side, so the 8192-byte stage can
     * become one entry without merging. */
    if (!range_is_free(offset, MAX_BLOCK + 3u * SECTOR) ||
        (offset != 0u && model_have[offset - 1u] != 0u))
        return 0;

    fill_random(data, MAX_BLOCK);
    CHECK(hfs_txn_stage_replace(&tx, offset / SECTOR, data, MAX_BLOCK) ==
          HFS_OK);
    model_runs = model_count_runs();
    model_take(offset, data, MAX_BLOCK);

    const size_t before = tx.count;
    /* Overlapping the last sector makes the union 8192 + 512 bytes, which no
     * single entry may hold, and the refuse must leave the staged bytes alone. */
    const uint64_t last = offset + MAX_BLOCK - SECTOR;
    uint8_t twice[2u * SECTOR];
    fill_random(twice, sizeof(twice));
    CHECK(hfs_txn_stage_replace(&tx, last / SECTOR, twice, sizeof(twice)) ==
          HFS_ERR_BUSY);
    CHECK(tx.count == before);
    CHECK(model_bytes[last] == data[MAX_BLOCK - SECTOR]);
    model_runs = model_count_runs();
    CHECK(stage_reports() == 0);
    return 0;
}

/* Ranges chosen to land exactly on and around a staged run. */
static int boundary_ranges(void)
{
    /* Pick a staged byte, then read around it. */
    uint64_t anchor = IMAGE;
    for (size_t attempt = 0; attempt < 64u; ++attempt) {
        const uint64_t candidate = next_random() % IMAGE;
        if (model_have[candidate] != 0u) {
            anchor = candidate;
            break;
        }
    }
    if (anchor == IMAGE)
        return 0;

    static const struct {
        int64_t delta;
        size_t length;
    } cases[] = {
        { 0, SECTOR }, { -1, 2 }, { -(int64_t)SECTOR, SECTOR },
        { 1, SECTOR }, { -1, (size_t)SECTOR + 2u }, { 0, 0 },
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const int64_t base = (int64_t)anchor + cases[i].delta;
        if (base < 0 || (uint64_t)base + cases[i].length > IMAGE)
            continue;
        memset(actual, 0x5a, sizeof(actual));
        memset(expected, 0x5a, sizeof(expected));
        int rc = hfs_txn_overlay(&tx, (uint64_t)base, actual, cases[i].length);
        CHECK(rc >= 0);
        const int expected_overlap =
            model_patch((uint64_t)base, cases[i].length, expected);
        CHECK(rc == expected_overlap);
        CHECK(memcmp(actual, expected, cases[i].length) == 0);
    }
    return 0;
}

/* Argument rules, exercised against the live transaction. */
static int rules(void)
{
    uint8_t block[MAX_BLOCK];
    memset(block, 0, sizeof(block));

    CHECK(hfs_txn_stage(&tx, 0u, block, 0u) == HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_stage(&tx, 0u, block, 3u) == HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_stage(&tx, 0u, block, MAX_BLOCK + SECTOR) ==
          HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_stage(NULL, 0u, block, SECTOR) == HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_stage_replace(&tx, 0u, block, 0u) == HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_stage_replace(NULL, 0u, block, SECTOR) == HFS_ERR_ARGUMENT);
    CHECK(hfs_txn_overlay(&tx, 0u, block, 0u) == 0);
    CHECK(hfs_txn_overlay(&tx, 0u, NULL, 8u) == HFS_ERR_ARGUMENT);

    /* `hfs_txn_stage` appends; a duplicate start sector is its own error. */
    for (size_t i = 0u; i < tx.count; ++i) {
        CHECK(hfs_txn_stage(&tx, tx.blocks[i].sector, block, SECTOR) ==
              HFS_ERR_BUSY);
    }
    return 0;
}

static int overlay_random_range(void)
{
    size_t length = 1u + next_random() % 2048u;
    uint64_t offset = next_random() % (IMAGE - length);
    memset(actual, 0xa5, sizeof(actual));
    memset(expected, 0xa5, sizeof(expected));
    int rc = hfs_txn_overlay(&tx, offset, actual, length);
    CHECK(rc >= 0);
    const int expected_overlap = model_patch(offset, length, expected);
    CHECK(rc == expected_overlap);
    CHECK(memcmp(actual, expected, length) == 0);
    /* Bytes outside the range must be untouched by the overlay. */
    CHECK(actual[length] == 0xa5);
    return 0;
}

static int read_random_range(void)
{
    size_t length = 1u + next_random() % 2048u;
    uint64_t offset = next_random() % (IMAGE - length);
    int rc = hfs_txn_read(&tx, &volume, offset, actual, (uint32_t)length);
    CHECK(rc == HFS_OK);
    (void)model_read(offset, length, expected);
    CHECK(memcmp(actual, expected, length) == 0);
    return 0;
}

/*
 * Fills the transaction to its limit with separated sectors, checks the 33rd
 * stage is refused, and checks the overlay over the whole image.
 */
static int fill_to_capacity(void)
{
    uint8_t data[SECTOR];
    for (uint32_t i = 0u; i < HFS_TXN_MAX_BLOCKS; ++i) {
        const uint64_t offset = (uint64_t)i * 2u * SECTOR;
        fill_random(data, SECTOR);
        CHECK(hfs_txn_stage(&tx, offset / SECTOR, data, SECTOR) == HFS_OK);
        model_take(offset, data, SECTOR);
    }
    model_runs = model_count_runs();
    CHECK(tx.count == HFS_TXN_MAX_BLOCKS);
    CHECK(model_runs == HFS_TXN_MAX_BLOCKS);

    fill_random(data, SECTOR);
    CHECK(hfs_txn_stage(&tx, (uint64_t)HFS_TXN_MAX_BLOCKS * 2u, data,
                        SECTOR) == HFS_ERR_NO_SPACE);

    /* The whole image against the model, staged sectors included. */
    memset(actual, 0x11, sizeof(actual));
    memset(expected, 0x11, sizeof(expected));
    CHECK(hfs_txn_overlay(&tx, 0u, actual, IMAGE) == 1);
    CHECK(model_patch(0u, IMAGE, expected) == 1);
    CHECK(memcmp(actual, expected, IMAGE) == 0);
    return 0;
}

/* A fresh transaction and an empty model. */
static int restart(void)
{
    memset(image_copy, 0, sizeof(image_copy));
    test_image_clear();
    fill_random(test_image, IMAGE);
    memcpy(image_copy, test_image, IMAGE);
    memset(&volume, 0, sizeof(volume));
    volume.read = test_image_read;
    volume.length = IMAGE;
    memset(&journal, 0, sizeof(journal));
    journal.sector_size = SECTOR;
    CHECK(hfs_txn_begin(&tx, &journal) == HFS_OK);
    memset(model_have, 0, sizeof(model_have));
    memset(model_bytes, 0, sizeof(model_bytes));
    model_runs = 0u;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t steps = DEFAULT_STEPS;
    uint32_t seed = 0x54584f56u; /* "TXOV" */
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        steps = (uint32_t)strtoul(argv[2], NULL, 0);

    rng_state = seed;
    CHECK(restart() == 0);

    uint32_t overlays = 0u;
    size_t max_entries = 0u;
    for (uint32_t step = 0u; step < steps; ++step) {
        if (tx.count > max_entries)
            max_entries = tx.count;
        const uint32_t pick = next_random() % 100u;
        if (pick < 18u) {
            CHECK(stage_new() == 0);
        } else if (pick < 32u) {
            CHECK(replace_staged() == 0);
        } else if (pick < 38u) {
            CHECK(oversized_merge_is_refused() == 0);
        } else if (pick < 50u) {
            CHECK(boundary_ranges() == 0);
            ++overlays;
        } else if (pick < 58u) {
            CHECK(rules() == 0);
        } else if (pick < 82u) {
            CHECK(overlay_random_range() == 0);
            ++overlays;
        } else {
            CHECK(read_random_range() == 0);
            ++overlays;
        }

        /* Start over now and then, so the array is not full all the time. */
        if ((step % 97u) == 96u)
            CHECK(restart() == 0);
        if ((step % 50u) == 49u) {
            CHECK(restart() == 0);
            CHECK(fill_to_capacity() == 0);
            CHECK(restart() == 0);
        }
    }

    printf("  txn overlay properties: %u steps, %u overlay checks, at most %u "
           "entries\n",
           (unsigned)steps, (unsigned)overlays, (unsigned)max_entries);
    CHECK(overlays > steps / 2u);
    return 0;
}
