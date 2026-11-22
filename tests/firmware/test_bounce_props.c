/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Property test for the byte-range layer over a sector device.
 *
 * `helikon_block_read_bytes`/`write_bytes` hide the sector alignment the ATA
 * path needs. Everything they do is offsets and lengths, which is exactly the
 * kind of code where a hand-written case picks the same three offsets and misses
 * the fourth. This walks a deterministic pseudo-random sequence of ranges over a
 * model disk instead, and after every operation asks the model whether the disk
 * still matches and whether anything outside the requested range moved.
 *
 * Invariants, for every range:
 *   - a read returns the model's bytes for that range, or a documented error;
 *   - a write either changes exactly the requested bytes or returns an error
 *     without touching the disk at all;
 *   - a range past the end is refused, never clamped;
 *   - an unaligned range with no scratch buffer is refused, not silently done;
 *   - a read-only device refuses writes and reads still work.
 *
 * The sequence comes from one LCG, so a failure prints the iteration that reruns
 * it. Nothing here runs on a device.
 */
#include <stdint.h>

#include "block.h"
#include "bounce.h"
#include "helikon_error.h"
#include "mem.h"
#include "test_check.h"

#define SECTORS 64u
#define SECTOR_SIZE 512u
#define DISK_BYTES (SECTORS * SECTOR_SIZE)
#define ITERATIONS 4000u

struct model_disk {
    uint8_t data[DISK_BYTES];
    unsigned long reads;
    unsigned long writes;
};

static struct model_disk disk;

static int disk_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct model_disk *model = context;
    if (lba > SECTORS || count > SECTORS - lba)
        return HELIKON_ERR_IO;
    model->reads++;
    helikon_memcpy(buffer, model->data + (size_t)lba * SECTOR_SIZE,
                   (size_t)count * SECTOR_SIZE);
    return HELIKON_OK;
}

static int disk_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct model_disk *model = context;
    if (lba > SECTORS || count > SECTORS - lba)
        return HELIKON_ERR_IO;
    model->writes++;
    helikon_memcpy(model->data + (size_t)lba * SECTOR_SIZE, buffer,
                   (size_t)count * SECTOR_SIZE);
    return HELIKON_OK;
}

static int disk_flush(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    return HELIKON_OK;
}

static int disk_power(void *context, int on, uint32_t timeout_ms)
{
    (void)context;
    (void)on;
    (void)timeout_ms;
    return HELIKON_OK;
}

static const struct helikon_block_ops ops = {
    .read = disk_read,
    .write = disk_write,
    .flush = disk_flush,
    .power = disk_power
};

static uint32_t next_random(uint32_t *state)
{
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

/* Fill the model with a pattern derived from the byte offset, so a read that
 * lands one byte off is visible rather than plausible. */
static void fill_model(void)
{
    for (size_t i = 0; i < DISK_BYTES; ++i)
        disk.data[i] = (uint8_t)((i * 7u) ^ (i >> 8));
}

int main(void)
{
    struct helikon_block_device device;
    struct helikon_block_info info = {
        .sector_size = SECTOR_SIZE,
        .sector_count = SECTORS,
        .flags = 0u
    };
    fill_model();
    CHECK(helikon_block_init(&device, &ops, &disk, &info) == HELIKON_OK);

    uint8_t scratch[SECTOR_SIZE];
    uint8_t buffer[3u * SECTOR_SIZE];
    uint8_t expected[3u * SECTOR_SIZE];
    /* The whole disk, because the point of the check is that a write touches
     * nothing outside its own range and the range can be anywhere. */
    static uint8_t prior[DISK_BYTES];
    uint32_t state = 0x1234abcdu;
    unsigned int refused = 0;
    unsigned int refused_scratch = 0;
    unsigned int written = 0;

    for (unsigned int iteration = 0; iteration < ITERATIONS; ++iteration) {
        /* Bias toward the edges: the first and last sectors, zero lengths, and
         * lengths that cross a sector boundary in either direction. */
        uint32_t offset_draw = next_random(&state);
        uint64_t offset = offset_draw % (DISK_BYTES + 4u);
        if ((iteration % 7u) == 0u)
            offset = (offset_draw % 2u) ? 0u : DISK_BYTES - 1u;
        if ((iteration % 11u) == 0u)
            offset = DISK_BYTES + (offset_draw % 4u);

        size_t length = (next_random(&state) % 1537u);
        if ((iteration % 5u) == 0u)
            length = 1u;
        if ((iteration % 13u) == 0u)
            length = SECTOR_SIZE * 3u;

        int in_range = offset <= DISK_BYTES &&
                       length <= DISK_BYTES - offset;

        /* A read of a valid range must return exactly the model's bytes. */
        helikon_memset(buffer, 0xa5, sizeof(buffer));
        int rc = helikon_block_read_bytes(&device, offset, buffer, length,
                                          scratch, sizeof(scratch), 100u);
        if (length == 0u) {
            CHECK(rc == HELIKON_OK);
            continue;
        }
        if (!in_range) {
            CHECK(rc == HELIKON_ERR_ARGUMENT);
            ++refused;
            continue;
        }
        CHECK(rc == HELIKON_OK);
        helikon_memcpy(expected, disk.data + (size_t)offset, length);
        CHECK(helikon_memcmp(buffer, expected, length) == 0);

        /* A read with no scratch for an unaligned range is refused. */
        if ((offset % SECTOR_SIZE) != 0u || (length % SECTOR_SIZE) != 0u) {
            int narrow = helikon_block_read_bytes(&device, offset, buffer,
                                                  length, NULL, 0u, 100u);
            CHECK(narrow == HELIKON_ERR_BUFFER);
            ++refused_scratch;
        }

        /* A write must change exactly the requested bytes. */
        for (size_t i = 0; i < sizeof(buffer); ++i)
            buffer[i] = (uint8_t)(next_random(&state) & 0xffu);
        helikon_memcpy(prior, disk.data, DISK_BYTES);
        rc = helikon_block_write_bytes(&device, offset, buffer, length, scratch,
                                       sizeof(scratch), 100u);
        if (!in_range) {
            CHECK(rc == HELIKON_ERR_ARGUMENT);
            CHECK(helikon_memcmp(prior, disk.data, DISK_BYTES) == 0);
            continue;
        }
        CHECK(rc == HELIKON_OK);
        ++written;
        CHECK(helikon_memcmp(disk.data + (size_t)offset, buffer, length) == 0);
        /* Nothing outside the range moved. */
        if (offset > 0u)
            CHECK(helikon_memcmp(prior, disk.data, (size_t)offset) == 0);
        if (offset + length < DISK_BYTES)
            CHECK(helikon_memcmp(prior + offset + length,
                                 disk.data + offset + length,
                                 (size_t)(DISK_BYTES - offset - length)) == 0);
    }

    /* A read-only device refuses the write and still serves reads. */
    device.info.flags = HELIKON_BLOCK_FLAG_READ_ONLY;
    helikon_memcpy(expected, disk.data + 64u, 32u);
    CHECK(helikon_block_write_bytes(&device, 64u, buffer, 32u, scratch,
                                    sizeof(scratch), 100u) ==
          HELIKON_ERR_READ_ONLY);
    CHECK(helikon_block_read_bytes(&device, 64u, buffer, 32u, scratch,
                                   sizeof(scratch), 100u) == HELIKON_OK);
    CHECK(helikon_memcmp(buffer, expected, 32u) == 0);

    CHECK(refused != 0u);
    CHECK(refused_scratch != 0u);
    CHECK(written != 0u);
    printf("%u iterations, %u refused ranges, %u refused without scratch, "
           "%u writes\n", ITERATIONS, refused, refused_scratch, written);
    return helikon_test_report("bounce props");
}
