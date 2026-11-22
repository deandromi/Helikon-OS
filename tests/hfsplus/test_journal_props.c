/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Crash-atomicity property test for the journal.
 *
 * `test_journal.c` checks one transaction at a time. This one asks the question
 * that matters at a power cut: **for every write boundary in the commit path,
 * is the volume either exactly as it was or exactly as it should be, never in
 * between?** The expected "should be" is built from the transaction's own
 * payload, not from a reference run of the code under test, so a bug that
 * misapplies a block everywhere cannot hide by being reproduced everywhere.
 *
 * The device keeps a write budget. A budget of `i` means the first `i` writes
 * reach the medium and every later one is silently lost, which is what a power
 * cut looks like to a caller: the device accepted the buffer and then stopped
 * existing. For each transaction the test:
 *
 *   1. records the image before the transaction (targets all zero),
 *   2. runs append + replay with no budget and checks the payload actually
 *      landed, which also yields the number of writes the whole path performs,
 *   3. for every budget from 0 to that number, resets the image, runs append +
 *      replay into the budget, then remounts: reopens the journal from the
 *      device and replays whatever it says is committed,
 *   4. requires that, after the remount, every target sector holds either its
 *      whole payload or none of it, that no two targets disagree about which,
 *      that no byte outside the journal and the targets moved, and that the
 *      journal ends up clean.
 *
 * The journal's circular buffer is deliberately not compared: an uncommitted
 * transaction legitimately leaves its block list and payload there, and nothing
 * reads them while start == end.
 *
 * What this proves: the commit is ordered after its payload on the medium, and
 * a replay is idempotent, so a mount after a cut converges on the committed
 * state. What it cannot prove: that the device honours the barrier. The budget
 * model drops whole writes and never reorders them, so a missing `flush()`
 * between payload and commit is invisible here — that is the hardware's job and
 * the device matrix's.
 *
 *   ./test_journal_props            # the committed sweep
 *   ./test_journal_props 7 40       # another seed, more transactions
 */
#include "hfs_journal.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOLUME_SIZE (256u * 1024u)
#define JOURNAL_OFFSET 4096u
#define JOURNAL_SIZE (64u * 1024u)
#define SECTOR 512u
#define BLOCK_LIST 4096u
#define TARGET_FIRST 200u
#define TARGET_LAST 400u
#define MAX_ENTRIES 4u
#define DEFAULT_TRIALS 24u

static uint8_t device[VOLUME_SIZE];
static uint8_t before_image[VOLUME_SIZE];
static const uint8_t zeros[SECTOR];

/* Writes with a 1-based index above the budget are lost, as at a power cut. */
static uint32_t write_budget;
static uint32_t write_count;
static uint32_t flush_count;

static uint32_t rng_state;
static uint32_t boundaries_checked;

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

static int device_read(void *context, uint64_t offset, void *buffer,
                       size_t length)
{
    (void)context;
    if (offset > sizeof(device) || length > sizeof(device) - offset)
        return -1;
    memcpy(buffer, device + offset, length);
    return 0;
}

static int device_write(void *context, uint64_t offset, const void *buffer,
                        size_t length)
{
    (void)context;
    if (offset > sizeof(device) || length > sizeof(device) - offset)
        return -1;
    ++write_count;
    if (write_count > write_budget)
        return 0; /* accepted, then lost with the power */
    memcpy(device + offset, buffer, length);
    return 0;
}

static int device_flush(void *context)
{
    (void)context;
    ++flush_count;
    return 0;
}

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void put_be64(uint8_t *p, uint64_t value)
{
    put_be32(p, (uint32_t)(value >> 32));
    put_be32(p + 4, (uint32_t)value);
}

static uint32_t journal_checksum(const uint8_t *p, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i)
        sum = (sum << 8) ^ (sum + p[i]);
    return ~sum;
}

/* The header stores byte offsets inside the journal, not sectors. */
static void make_journal(uint64_t start, uint64_t end)
{
    memset(device, 0, sizeof(device));
    write_count = 0;
    flush_count = 0;
    uint8_t *h = device + JOURNAL_OFFSET;
    put_be32(h, 0x4a4e4c78u);
    put_be32(h + 4, 0x12345678u);
    put_be64(h + 8, start);
    put_be64(h + 16, end);
    put_be64(h + 24, JOURNAL_SIZE);
    put_be32(h + 32, BLOCK_LIST);
    put_be32(h + 36, 0);
    put_be32(h + 40, SECTOR);
    put_be32(h + 36, journal_checksum(h, 44));
}

static void make_volume(struct hfs_volume *volume)
{
    memset(volume, 0, sizeof(*volume));
    volume->read = device_read;
    volume->length = sizeof(device);
    volume->journal_offset = JOURNAL_OFFSET;
    volume->journal_size = JOURNAL_SIZE;
    volume->journaled = 1;
}

static int mount_journal(struct hfs_journal *journal)
{
    struct hfs_volume volume;
    make_volume(&volume);
    return hfs_journal_open(journal, &volume, device_write, device_flush);
}

/* One transaction: up to MAX_ENTRIES sectors, each a random 512-byte payload. */
struct trial {
    struct hfs_journal_entry entries[MAX_ENTRIES];
    uint8_t data[MAX_ENTRIES][SECTOR];
    uint32_t count;
};

static void build_trial(struct trial *trial)
{
    trial->count = 1u + next_random() % MAX_ENTRIES;
    for (uint32_t i = 0u; i < trial->count; ++i) {
        uint32_t sector = TARGET_FIRST +
                          next_random() % (TARGET_LAST - TARGET_FIRST);
        /* Distinct targets keep "applied" and "untouched" unambiguous. */
        while (i != 0u && sector == trial->entries[i - 1u].sector)
            sector = TARGET_FIRST + (sector - TARGET_FIRST + 1u) %
                                        (TARGET_LAST - TARGET_FIRST);
        trial->entries[i].sector = sector;
        trial->entries[i].size = SECTOR;
        trial->entries[i].data = trial->data[i];
        for (uint32_t b = 0u; b < SECTOR; ++b) {
            /* Never all zero, so an unwritten sector cannot match a payload. */
            trial->data[i][b] = (uint8_t)(1u + next_random() % 255u);
        }
    }
}

/*
 * Runs the transaction into the current device with the given budget, then
 * remounts and replays whatever the journal on the medium says is committed.
 * Counts every write the whole path performed.
 */
static int run_with_budget(const struct trial *trial, uint32_t budget,
                           uint32_t *performed)
{
    uint32_t total = 0u;
    write_budget = budget;
    write_count = 0;
    struct hfs_journal journal;
    int rc = mount_journal(&journal);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_journal_append(&journal, trial->entries, trial->count);
    if (rc == HFS_OK)
        rc = hfs_journal_replay(&journal);
    if (rc != HFS_OK && rc != HFS_ERR_FORMAT && rc != HFS_ERR_IO)
        return rc;
    total += write_count;

    /* The next mount sees only what reached the medium, so it gets no budget. */
    write_budget = UINT32_MAX;
    write_count = 0;
    struct hfs_journal remount;
    rc = mount_journal(&remount);
    if (rc != HFS_OK)
        return rc;
    rc = hfs_journal_replay(&remount);
    if (rc != HFS_OK && rc != HFS_ERR_FORMAT && rc != HFS_ERR_IO)
        return rc;
    total += write_count;
    if (performed != NULL)
        *performed = total;
    return 0;
}

/* 1 when the sector holds the payload, 0 when it is untouched, -1 on a mix. */
static int target_state(const struct trial *trial, uint32_t index)
{
    const uint8_t *sector = device + trial->entries[index].sector * SECTOR;
    if (memcmp(sector, trial->data[index], SECTOR) == 0)
        return 1;
    if (memcmp(sector, zeros, SECTOR) == 0)
        return 0;
    return -1;
}

/* Every byte outside the journal and the target sectors must still be zero. */
static int outside_is_untouched(const struct trial *trial)
{
    for (size_t offset = 0u; offset < sizeof(device); ++offset) {
        if (offset >= JOURNAL_OFFSET && offset < JOURNAL_OFFSET + JOURNAL_SIZE)
            continue;
        int in_target = 0;
        for (uint32_t i = 0u; i < trial->count; ++i) {
            const size_t start = (size_t)trial->entries[i].sector * SECTOR;
            if (offset >= start && offset < start + SECTOR) {
                in_target = 1;
                break;
            }
        }
        if (in_target)
            continue;
        if (device[offset] != before_image[offset])
            return 0;
    }
    return 1;
}

static int trial_is_atomic(const struct trial *trial, uint32_t index)
{
    /* Before: a clean journal, no target touched. */
    make_journal(SECTOR, SECTOR);
    memcpy(before_image, device, sizeof(device));

    /*
     * Reference: run it once with no budget and check the payload landed. The
     * expected "committed" state is the payload itself, so this run is only a
     * sanity check and the source of the write count.
     */
    uint32_t total_writes = 0u;
    CHECK(run_with_budget(trial, UINT32_MAX, &total_writes) == 0);
    CHECK(total_writes >= trial->count + 2u);
    for (uint32_t i = 0u; i < trial->count; ++i)
        CHECK(target_state(trial, i) == 1);

    /* Every write boundary, including before the first and after the last. */
    for (uint32_t budget = 0u; budget <= total_writes + 1u; ++budget) {
        ++boundaries_checked;
        make_journal(SECTOR, SECTOR);
        if (run_with_budget(trial, budget, NULL) != 0) {
            fprintf(stderr,
                    "  FAIL transaction %u: remount failed at write %u\n",
                    (unsigned)index, (unsigned)budget);
            return 1;
        }

        int applied = 0;
        int untouched = 0;
        for (uint32_t i = 0u; i < trial->count; ++i) {
            const int state = target_state(trial, i);
            if (state < 0) {
                fprintf(stderr,
                        "  FAIL transaction %u: target %u holds neither its "
                        "payload nor its old bytes at write %u\n",
                        (unsigned)index, (unsigned)i, (unsigned)budget);
                return 1;
            }
            applied += state;
            untouched += state == 0 ? 1 : 0;
        }
        if (applied != 0 && untouched != 0) {
            fprintf(stderr,
                    "  FAIL transaction %u: %d of %u targets applied at write "
                    "%u, the rest not\n",
                    (unsigned)index, applied, (unsigned)trial->count,
                    (unsigned)budget);
            return 1;
        }
        if (!outside_is_untouched(trial)) {
            fprintf(stderr,
                    "  FAIL transaction %u: a byte outside the journal and the "
                    "targets changed at write %u\n",
                    (unsigned)index, (unsigned)budget);
            return 1;
        }

        /* After a remount and a replay the journal must be clean again. */
        struct hfs_journal journal;
        if (mount_journal(&journal) != HFS_OK ||
            !hfs_journal_is_clean(&journal)) {
            fprintf(stderr,
                    "  FAIL transaction %u: the journal is not clean after the "
                    "remount at write %u\n",
                    (unsigned)index, (unsigned)budget);
            return 1;
        }
    }
    return 0;
}

/* A third of the trials start with the journal near its end, so replay wraps. */
static int trial_wraps(const struct trial *trial, uint32_t index)
{
    const uint64_t start = JOURNAL_SIZE - 2u * BLOCK_LIST;
    for (uint32_t budget = 0u; budget <= 16u; ++budget) {
        make_journal(start, start);
        if (run_with_budget(trial, budget, NULL) != 0) {
            fprintf(stderr, "  FAIL wrap trial %u: remount failed\n",
                    (unsigned)index);
            return 1;
        }
        int applied = 0;
        for (uint32_t i = 0u; i < trial->count; ++i) {
            const int state = target_state(trial, i);
            if (state < 0)
                return 1;
            applied += state;
        }
        if (applied != 0 && applied != (int)trial->count) {
            fprintf(stderr, "  FAIL wrap trial %u: partial application\n",
                    (unsigned)index);
            return 1;
        }
        /* Whatever happened, a replay of the resting journal is a fixed point. */
        struct hfs_journal journal;
        CHECK(mount_journal(&journal) == HFS_OK);
        memcpy(before_image, device, sizeof(device));
        CHECK(hfs_journal_replay(&journal) == HFS_OK);
        CHECK(memcmp(device, before_image, sizeof(device)) == 0);
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t trials = DEFAULT_TRIALS;
    uint32_t seed = 0x4a524e4cu; /* "JRNL" */
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        trials = (uint32_t)strtoul(argv[2], NULL, 0);

    rng_state = seed;
    for (uint32_t index = 0u; index < trials; ++index) {
        struct trial trial;
        build_trial(&trial);
        CHECK(trial_is_atomic(&trial, index) == 0);
        if ((index % 3u) == 2u)
            CHECK(trial_wraps(&trial, index) == 0);
    }

    printf("  journal properties: %u transactions, %u write boundaries, "
           "%u wrap trials\n",
           (unsigned)trials, (unsigned)boundaries_checked,
           (unsigned)((trials + 2u) / 3u));
    CHECK(boundaries_checked > trials * 8u);
    return 0;
}
