/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_JOURNAL_H
#define HELIKON_HFS_JOURNAL_H

#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Apple HFS+ journal access. The on-disk format is the one described by
 * TN1150: a journal info block, a one-sector journal header, and a circular
 * buffer of block-list headers and block contents.
 *
 * The caller supplies a write and flush callback over the same block device as
 * the read callback in `volume`. A read-only mount passes NULL write/flush and
 * can still open/validate the journal.
 */
typedef int (*hfs_write_fn)(void *context, uint64_t offset,
                            const void *buffer, size_t length);
typedef int (*hfs_flush_fn)(void *context);

#define HFS_JOURNAL_HEADER_SIZE 44u
#define HFS_JOURNAL_MAX_SECTOR 4096u
/*
 * macOS commonly uses 4096 or 8192; 16384 covers the observed range without
 * putting an unbounded buffer on the stack. Larger lists return FORMAT until a
 * scratch-buffer design exists.
 */
#define HFS_JOURNAL_MAX_BLOCK_LIST 16384u

struct hfs_journal_entry {
    uint64_t sector; /* target sector within the volume */
    const void *data;
    uint32_t size;   /* bytes, multiple of the sector size */
};

struct hfs_journal {
    hfs_read_fn read;
    hfs_write_fn write;
    hfs_flush_fn flush;
    void *context;

    uint64_t volume_base;
    uint64_t volume_length;
    uint64_t journal_base;   /* absolute byte offset of the journal header */
    uint64_t journal_offset; /* relative to volume_base */
    uint64_t journal_size;
    uint32_t sector_size;    /* jhdr_size */
    uint32_t block_list_size;/* blhdr_size */
    uint64_t start;
    uint64_t end;
    int little_endian;
    int dirty;
};

/* Open and validate the journal header. Does not replay. */
int hfs_journal_open(struct hfs_journal *journal,
                     const struct hfs_volume *volume,
                     hfs_write_fn write, hfs_flush_fn flush);

/* Replay all committed transactions and mark the journal clean. */
int hfs_journal_replay(struct hfs_journal *journal);

/*
 * Append one transaction and commit it by advancing the journal header's end.
 * The journal must be clean. The caller must call hfs_journal_replay() to apply
 * the blocks and advance start/clean the journal.
 */
int hfs_journal_append(struct hfs_journal *journal,
                       const struct hfs_journal_entry *entries, size_t count);

int hfs_journal_is_clean(const struct hfs_journal *journal);

#endif
