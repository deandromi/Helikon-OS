/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host tests for the library glue: the path index over HelikonDB and playlist
 * resolution into the play queue.
 *
 * The index exists so a playlist costs one pass over the catalog instead of one
 * catalog read per entry per track. These tests use the generated fixture, read
 * a few real track paths out of it and build the playlist text from those, so the
 * assertions do not depend on what the fixture happens to be called.
 */
#include "library.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helikon_error.h"
#include "hdb.h"
#include "mem.h"

static int failures;
static char paths[64][HELIKON_LIBRARY_PATH_MAX];

static int check(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        failures += 1;
        return 1;
    }
    return 0;
}

struct memory {
    const uint8_t *data;
    uint32_t size;
};

static int memory_read(void *context, uint32_t offset, void *buffer,
                       uint32_t length)
{
    struct memory *memory = context;
    if (offset > memory->size || length > memory->size - offset)
        return HELIKON_ERR_IO;
    helikon_memcpy(buffer, memory->data + offset, length);
    return HELIKON_OK;
}

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *data = malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(data, 1u, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (uint32_t)length;
    return data;
}

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

static int check_index_covers_every_track(const struct hdb *db,
                                          const struct helikon_library_index *index)
{
    if (check(index->complete == 1u, "the fixture fits the index"))
        return 1;
    if (check(index->indexed == db->track_count, "every track was indexed"))
        return 1;

    for (uint32_t track = 0u; track < db->track_count; ++track) {
        if (check(track_path(db, track, paths[0], sizeof(paths[0])) == HELIKON_OK,
                  "a track path reads"))
            return 1;
        uint32_t found = 0xffffffffu;
        if (check(helikon_library_index_find(index, paths[0], &found) ==
                      HELIKON_OK,
                  "every indexed path resolves"))
            return 1;
        if (check(found == track, "a path resolves to its own track"))
            return 1;
    }
    return 0;
}

static int check_misses(const struct hdb *db,
                        const struct helikon_library_index *index)
{
    uint32_t track = 0u;
    (void)db;
    if (check(helikon_library_index_find(index, "/Music/not-there.mp3",
                                         &track) == HELIKON_ERR_NOT_FOUND,
              "an absent path is not found"))
        return 1;
    if (check(helikon_library_index_find(index, "relative/path.mp3", &track) ==
                  HELIKON_ERR_NOT_FOUND,
              "a relative path is not looked up"))
        return 1;
    if (check(helikon_library_index_find(index, "", &track) ==
                  HELIKON_ERR_NOT_FOUND,
              "an empty path is not looked up"))
        return 1;
    if (check(helikon_library_index_find(index, NULL, &track) ==
                  HELIKON_ERR_ARGUMENT,
              "a NULL path is an argument error"))
        return 1;
    if (check(helikon_library_index_find(NULL, "/a", &track) ==
                  HELIKON_ERR_ARGUMENT,
              "a NULL index is an argument error"))
        return 1;
    return 0;
}

static int check_queue_from_playlist(const struct hdb *db,
                                     const struct helikon_library_index *index)
{
    /* Three real tracks, in an order that is not their catalog order, then one
     * path the library does not have and one entry the parser refuses. */
    static char text[4u * HELIKON_LIBRARY_PATH_MAX + 64u];
    uint32_t wants[3] = { 2u, 0u, 1u };
    if (db->track_count < 3u) {
        printf("SKIP the fixture has fewer than three tracks\n");
        return 0;
    }

    size_t used = 0u;
    for (size_t index_of = 0u; index_of < 3u; ++index_of) {
        uint32_t track = wants[index_of];
        int rc = track_path(db, track, paths[1], sizeof(paths[1]));
        if (check(rc == HELIKON_OK, "a playlist path reads"))
            return 1;
        int written = snprintf(text + used, sizeof(text) - used, "%s\n",
                               paths[1]);
        if (check(written > 0, "the playlist fixture fits"))
            return 1;
        used += (size_t)written;
    }
    used += (size_t)snprintf(text + used, sizeof(text) - used,
                             "/Music/not-there.mp3\n");
    used += (size_t)snprintf(text + used, sizeof(text) - used,
                             "../escape.mp3\n");

    struct helikon_queue queue;
    helikon_memset(&queue, 0, sizeof(queue));
    struct helikon_playlist_queue_result result;
    int rc = helikon_library_queue_from_playlist(index, &queue, text, used,
                                                 "/", &result);
    if (check(rc == HELIKON_OK, "the playlist resolves"))
        return 1;
    /* Four produced: the refused ../escape line never becomes an entry. */
    if (check(result.entries == 4u, "four entries were produced"))
        return 1;
    if (check(result.added == 3u, "three entries resolved"))
        return 1;
    if (check(result.missing == 1u, "the absent track is counted as missing"))
        return 1;
    if (check(result.refused == 1u, "the refused entry is counted separately"))
        return 1;

    uint32_t id = 0xffffffffu;
    if (check(helikon_queue_current(&queue, &id) == HELIKON_OK,
              "the queue has a current id"))
        return 1;
    if (check(id == wants[0], "the first playlist entry is first in the queue"))
        return 1;
    if (check(helikon_queue_next(&queue) == HELIKON_QUEUE_MOVED,
              "the queue advances"))
        return 1;
    if (check(helikon_queue_current(&queue, &id) == HELIKON_OK &&
                  id == wants[1],
              "the second playlist entry is second"))
        return 1;
    if (check(helikon_queue_next(&queue) == HELIKON_QUEUE_MOVED,
              "the queue advances again"))
        return 1;
    if (check(helikon_queue_current(&queue, &id) == HELIKON_OK &&
                  id == wants[2],
              "the third playlist entry is third"))
        return 1;
    if (check(helikon_queue_next(&queue) == HELIKON_QUEUE_END,
              "and the queue ends there"))
        return 1;
    return 0;
}

static int check_empty_and_broken(const struct helikon_library_index *index)
{
    struct helikon_queue queue;
    struct helikon_playlist_queue_result result;

    helikon_memset(&queue, 0, sizeof(queue));
    if (check(helikon_library_queue_from_playlist(index, &queue, "", 0u, "/",
                                                  &result) == HELIKON_OK,
              "an empty playlist resolves"))
        return 1;
    if (check(result.entries == 0u && result.added == 0u,
              "an empty playlist adds nothing"))
        return 1;
    uint32_t id = 0u;
    if (check(helikon_queue_current(&queue, &id) == HELIKON_ERR_NOT_FOUND,
              "the queue is empty afterwards"))
        return 1;

    /* A buffer with a NUL is refused by the parser, and that refusal is the
     * result rather than an empty queue: the caller has to know the file is
     * corrupt. */
    const char nul[] = { '/', 'a', '\0', '/', 'b' };
    if (check(helikon_library_queue_from_playlist(index, &queue, nul,
                                                  sizeof(nul), "/",
                                                  &result) ==
                  HELIKON_ERR_FORMAT,
              "a NUL in the playlist is reported, not swallowed"))
        return 1;

    if (check(helikon_library_queue_from_playlist(NULL, &queue, "", 0u, "/",
                                                  &result) ==
                  HELIKON_ERR_ARGUMENT,
              "a NULL index is refused"))
        return 1;
    return 0;
}

static int check_rebuild(const struct hdb *db,
                         struct helikon_library_index *index)
{
    /* Building twice has to give the same answers; a stale index that still
     * answers from the previous pass would be worse than none. */
    uint32_t before = index->used;
    if (check(helikon_library_index_build(index, db) == HELIKON_OK,
              "the index rebuilds"))
        return 1;
    if (check(index->used == before, "the rebuild indexes the same number"))
        return 1;
    uint32_t track = 0u;
    if (check(track_path(db, 0u, paths[0], sizeof(paths[0])) == HELIKON_OK,
              "a track path reads"))
        return 1;
    if (check(helikon_library_index_find(index, paths[0], &track) == HELIKON_OK &&
                  track == 0u,
              "the rebuilt index still resolves"))
        return 1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s FIXTURE.hdb\n", argv[0]);
        return 2;
    }
    uint32_t size = 0u;
    uint8_t *data = read_file(argv[1], &size);
    if (data == NULL) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    struct memory memory = { data, size };
    const struct hdb_io io = { .read = memory_read, .context = &memory,
                               .size = size };
    struct hdb db;
    int rc = hdb_open(&db, &io);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "hdb_open: error %d\n", rc);
        free(data);
        return 1;
    }

    static struct helikon_library_index index;
    if (check(helikon_library_index_build(&index, &db) == HELIKON_OK,
              "the index builds")) {
        free(data);
        return 1;
    }

    if (check_index_covers_every_track(&db, &index) || check_misses(&db, &index) ||
        check_queue_from_playlist(&db, &index) ||
        check_empty_and_broken(&index) || check_rebuild(&db, &index)) {
        free(data);
        return 1;
    }

    free(data);
    if (failures != 0) {
        printf("FAIL library index (%d checks)\n", failures);
        return 1;
    }
    printf("PASS library index (%u tracks, %u slots used)\n", db.track_count,
           index.used);
    return 0;
}
