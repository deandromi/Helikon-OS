/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Library glue: a path index over HelikonDB, and playlist resolution into the
 * play queue.
 *
 * An HDB track record carries a string offset for its path, and nothing else
 * points at a track by name. Resolving a playlist therefore means finding the
 * track whose path equals an entry, and doing that per entry against every track
 * would read the catalog once per pair — 512 entries times a few thousand tracks
 * is a number of HFS+ reads that would be visible on the device. So the index in
 * this file is built once per mount: one pass over the tracks, one hash each,
 * and a lookup is a probe plus one string comparison to confirm.
 *
 * Everything is fixed-size and allocation-free, like the queue it fills. The
 * index covers `HELIKON_LIBRARY_INDEX_CAPACITY` tracks at a load factor of one
 * half, which is every library the 6G can hold in a 30 GB volume's worth of
 * music; `complete` says whether every track was indexed, so a caller can refuse
 * to answer rather than answer "not found" for a track that was never indexed.
 *
 * Comparison is a byte comparison, because that is what the catalog holds. HFS+
 * is case-insensitive, so a playlist written on a laptop may spell a path with
 * different case than the file has; that entry is reported as missing rather
 * than guessed at. Case folding here would need the HFS+ table, which is the
 * filesystem's business, not the library's.
 */
#ifndef HELIKON_LIBRARY_LIBRARY_H
#define HELIKON_LIBRARY_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

#include "hdb.h"
#include "playlist.h"
#include "queue.h"

#define HELIKON_LIBRARY_INDEX_CAPACITY 1024u
#define HELIKON_LIBRARY_PATH_MAX 512u

struct helikon_library_index {
    const struct hdb *db;
    uint32_t hashes[HELIKON_LIBRARY_INDEX_CAPACITY];
    uint32_t slots[HELIKON_LIBRARY_INDEX_CAPACITY]; /* track index + 1, 0 empty */
    uint32_t used;
    uint32_t indexed;
    uint8_t complete;
};

struct helikon_playlist_queue_result {
    uint32_t entries; /* entries the playlist parser produced */
    uint32_t added;   /* resolved and placed in the queue */
    uint32_t missing; /* well-formed entries with no track in the library */
    uint32_t refused; /* entries the parser refused (counted in the parser) */
};

/*
 * Walk the catalog once and index every track path. Returns HELIKON_OK even when
 * the library is larger than the index: `complete` then says so and lookups may
 * miss. A read error from the catalog is returned and leaves the index empty.
 * Calling it twice is fine; the second call starts over.
 */
int helikon_library_index_build(struct helikon_library_index *index,
                                const struct hdb *db);

/*
 * Track index for `path`, or HELIKON_ERR_NOT_FOUND. The path is compared byte for
 * byte; an index whose `complete` is 0 cannot answer for a track it never
 * indexed, so the caller should not treat NOT_FOUND as proof the file is absent.
 */
int helikon_library_index_find(const struct helikon_library_index *index,
                               const char *path, uint32_t *track);

/*
 * Parse a playlist buffer and fill `queue` with the track indices of the entries
 * the library knows, in playlist order. The queue is replaced, not appended to.
 *
 * The queue stores HDB track indices, so a caller that wants the file or the tag
 * asks the catalog with that index. Entries the parser refused and entries the
 * library does not have are counted separately, because they are different
 * questions: a malformed line is a broken file, an unresolved path is a stale
 * playlist or a library that changed since.
 */
int helikon_library_queue_from_playlist(
    const struct helikon_library_index *index, struct helikon_queue *queue,
    const char *text, size_t length, const char *base,
    struct helikon_playlist_queue_result *result);

#endif
