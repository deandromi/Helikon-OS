/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "library.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"

/* The parser stops at the playlist cap, so a queue at least that long cannot
 * drop an entry: one more place where two limits have to agree. */
_Static_assert(HELIKON_QUEUE_CAPACITY >= HELIKON_PLAYLIST_MAX_ENTRIES,
               "the queue must be able to hold a whole playlist");

/* FNV-1a over the path bytes: one multiply per byte, no table. */
static uint32_t hash_path(const char *path)
{
    uint32_t hash = 2166136261u;
    while (*path != '\0') {
        hash ^= (uint32_t)(unsigned char)*path++;
        hash *= 16777619u;
    }
    return hash;
}

static size_t text_length(const char *text, size_t limit)
{
    size_t length = 0u;
    while (length < limit && text[length] != '\0')
        ++length;
    return length;
}

static int same_path(const char *left, const char *right)
{
    for (;;) {
        if (*left != *right)
            return 0;
        if (*left == '\0')
            return 1;
        ++left;
        ++right;
    }
}

/* Read one track's path. Returns BUFFER when it does not fit. */
static int track_path(const struct hdb *db, uint32_t track, char *buffer,
                      size_t capacity)
{
    struct hdb_track record;
    int rc = hdb_track(db, track, &record);
    if (rc != HELIKON_OK)
        return rc;
    uint32_t length = 0u;
    return hdb_string(db, record.path_off, buffer, (uint32_t)capacity, &length);
}

/*
 * Insert one track. Probing stops at the first empty slot; the table is filled to
 * half its capacity at most, so a probe is short and a lookup that reaches an
 * empty slot can stop there.
 */
static void index_insert(struct helikon_library_index *index, uint32_t hash,
                         uint32_t track)
{
    uint32_t mask = HELIKON_LIBRARY_INDEX_CAPACITY - 1u;
    uint32_t slot = hash & mask;
    for (uint32_t probe = 0u; probe < HELIKON_LIBRARY_INDEX_CAPACITY; ++probe) {
        if (index->slots[slot] == 0u) {
            index->hashes[slot] = hash;
            index->slots[slot] = track + 1u;
            ++index->used;
            return;
        }
        slot = (slot + 1u) & mask;
    }
}

int helikon_library_index_build(struct helikon_library_index *index,
                                const struct hdb *db)
{
    if (index == NULL || db == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(index, 0, sizeof(*index));
    index->db = db;

    const uint32_t limit = HELIKON_LIBRARY_INDEX_CAPACITY / 2u;
    char path[HELIKON_LIBRARY_PATH_MAX];
    uint32_t track_count = db->track_count;
    for (uint32_t track = 0u; track < track_count; ++track) {
        int rc = track_path(db, track, path, sizeof(path));
        if (rc == HELIKON_ERR_BUFFER) {
            /* A path longer than the buffer is not indexed; the index stays
             * usable but says it is not complete. */
            index->complete = 0u;
            continue;
        }
        if (rc != HELIKON_OK)
            return rc;
        if (index->used >= limit) {
            index->complete = 0u;
            break;
        }
        index_insert(index, hash_path(path), track);
        ++index->indexed;
    }
    if (index->indexed == track_count)
        index->complete = 1u;
    return HELIKON_OK;
}

int helikon_library_index_find(const struct helikon_library_index *index,
                               const char *path, uint32_t *track)
{
    if (index == NULL || index->db == NULL || path == NULL || track == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (path[0] != '/' || text_length(path, HELIKON_LIBRARY_PATH_MAX) == 0u)
        return HELIKON_ERR_NOT_FOUND;

    const uint32_t mask = HELIKON_LIBRARY_INDEX_CAPACITY - 1u;
    uint32_t hash = hash_path(path);
    uint32_t slot = hash & mask;
    char stored[HELIKON_LIBRARY_PATH_MAX];
    for (uint32_t probe = 0u; probe < HELIKON_LIBRARY_INDEX_CAPACITY; ++probe) {
        if (index->slots[slot] == 0u)
            return HELIKON_ERR_NOT_FOUND;
        if (index->hashes[slot] == hash) {
            uint32_t candidate = index->slots[slot] - 1u;
            int rc = track_path(index->db, candidate, stored, sizeof(stored));
            if (rc == HELIKON_OK && same_path(stored, path)) {
                *track = candidate;
                return HELIKON_OK;
            }
            if (rc != HELIKON_OK && rc != HELIKON_ERR_BUFFER)
                return rc;
        }
        slot = (slot + 1u) & mask;
    }
    return HELIKON_ERR_NOT_FOUND;
}

int helikon_library_queue_from_playlist(
    const struct helikon_library_index *index, struct helikon_queue *queue,
    const char *text, size_t length, const char *base,
    struct helikon_playlist_queue_result *result)
{
    if (index == NULL || queue == NULL || result == NULL ||
        (text == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(result, 0, sizeof(*result));

    struct helikon_playlist_parser parser;
    int rc = helikon_playlist_open(&parser, text, length, base,
                                   HELIKON_PLAYLIST_AUTO);
    if (rc != HELIKON_OK)
        return rc;

    uint32_t ids[HELIKON_QUEUE_CAPACITY];
    struct helikon_playlist_entry entry;
    for (;;) {
        rc = helikon_playlist_next(&parser, &entry);
        if (rc < 0)
            return rc;
        if (rc == 0)
            break;
        ++result->entries;

        uint32_t track = 0u;
        int found = helikon_library_index_find(index, entry.path, &track);
        if (found == HELIKON_ERR_NOT_FOUND) {
            ++result->missing;
            continue;
        }
        if (found != HELIKON_OK)
            return found;
        ids[result->added] = track;
        ++result->added;
    }

    result->refused = parser.skipped;
    return helikon_queue_set(queue, ids, result->added);
}
