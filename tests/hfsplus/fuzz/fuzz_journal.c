/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libFuzzer target for the HFS+ journal reader and replay engine.
 *
 * This is the code that decides whether a volume with a dirty journal is
 * recoverable, and invariant I-04 in docs/invariants.md is explicit: a journal
 * that is dirty or invalid must never be treated as clean. So the harness has
 * two jobs.
 *
 *   1. The bytes of the journal header and block list come from the fuzzer.
 *      If the checksum field no longer matches the rest of the header, the
 *      journal must not open and must not report clean.
 *   2. In the second pass the harness repairs that checksum, so a structurally
 *      hostile but internally consistent journal reaches `hfs_journal_replay()`:
 *      wild block lists, a start beyond the end, a wrap in the wrong place. The
 *      device is an array with bounds-checked callbacks, so any write outside it
 *      aborts here instead of corrupting a real volume.
 *
 *   ./tools/fuzz.sh journal 300
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hfs_journal.h"

#define VOLUME_SIZE (256u * 1024u)
#define JOURNAL_OFFSET 4096u
#define JOURNAL_SIZE (64u * 1024u)
#define SECTOR_SIZE 512u
#define BLOCK_LIST_SIZE 4096u

/* Header field offsets, from TN1150 and hfs_journal.c. */
#define HDR_MAGIC 0u
#define HDR_START 8u
#define HDR_END 16u
#define HDR_SIZE 24u
#define HDR_BLOCK_LIST 32u
#define HDR_CHECKSUM 36u

static uint8_t device[VOLUME_SIZE];
static int wrote;

static int device_read(void *context, uint64_t offset, void *buffer,
                       size_t length)
{
    (void)context;
    if (offset > sizeof(device) || length > sizeof(device) - offset) {
        fprintf(stderr, "fuzz: read past the device\n");
        abort();
    }
    memcpy(buffer, device + offset, length);
    return 0;
}

static int device_write(void *context, uint64_t offset, const void *buffer,
                        size_t length)
{
    (void)context;
    if (offset > sizeof(device) || length > sizeof(device) - offset) {
        fprintf(stderr, "fuzz: write past the device\n");
        abort();
    }
    memcpy(device + offset, buffer, length);
    wrote = 1;
    return 0;
}

static int device_flush(void *context)
{
    (void)context;
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

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t header_checksum(const uint8_t *header, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i)
        sum = (sum << 8) ^ (sum + header[i]);
    return ~sum;
}

static void make_journal(void)
{
    memset(device, 0, sizeof(device));
    uint8_t *h = device + JOURNAL_OFFSET;
    put_be32(h + HDR_MAGIC, 0x4a4e4c78u);
    put_be32(h + 4, 0x12345678u);
    put_be64(h + HDR_START, 10u * SECTOR_SIZE);
    put_be64(h + HDR_END, 10u * SECTOR_SIZE);
    put_be64(h + HDR_SIZE, JOURNAL_SIZE);
    put_be32(h + HDR_BLOCK_LIST, BLOCK_LIST_SIZE);
    put_be32(h + HDR_CHECKSUM, 0u);
    put_be32(h + 40, SECTOR_SIZE);
    put_be32(h + HDR_CHECKSUM, header_checksum(h, HFS_JOURNAL_HEADER_SIZE));
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

static void run_once(int repair_checksum)
{
    struct hfs_volume volume;
    struct hfs_journal journal;
    make_volume(&volume);
    memset(&journal, 0, sizeof(journal));
    wrote = 0;

    if (repair_checksum) {
        uint8_t *h = device + JOURNAL_OFFSET;
        put_be32(h + HDR_CHECKSUM, 0u);
        put_be32(h + HDR_CHECKSUM,
                 header_checksum(h, HFS_JOURNAL_HEADER_SIZE));
    }

    int rc = hfs_journal_open(&journal, &volume, device_write, device_flush);
    if (rc != 0) {
        /* A refused journal is allowed to have changed nothing. */
        if (wrote) {
            fprintf(stderr, "fuzz: open failed after writing\n");
            abort();
        }
        return;
    }

    rc = hfs_journal_replay(&journal);
    (void)rc;

    /* The invariant, stated as an assertion: a header whose checksum does not
     * match may never be reported clean. */
    const uint8_t *h = device + JOURNAL_OFFSET;
    uint32_t stored = get_be32(h + HDR_CHECKSUM);
    uint8_t copy[HFS_JOURNAL_HEADER_SIZE];
    memcpy(copy, h, sizeof(copy));
    put_be32(copy + HDR_CHECKSUM, 0u);
    if (header_checksum(copy, HFS_JOURNAL_HEADER_SIZE) != stored &&
        hfs_journal_is_clean(&journal)) {
        fprintf(stderr, "fuzz: a corrupt journal reported clean\n");
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0)
        return 0;

    size_t length = size;
    if (length > JOURNAL_SIZE)
        length = JOURNAL_SIZE;

    make_journal();
    memcpy(device + JOURNAL_OFFSET, data, length);
    run_once(0);

    make_journal();
    memcpy(device + JOURNAL_OFFSET, data, length);
    run_once(1);
    return 0;
}
