/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libFuzzer target for the HelikonDB reader.
 *
 * The database is a file the firmware trusts more than most: it is written once
 * on the host and then read on every screen. The invariant here is that the
 * reader only hands back strings and records inside the file it was given —
 * `hdb_open` may reject, and every accessor may fail, but nothing may read or
 * write past the buffer.
 *
 * The file is served through a bounds-checked `hdb_io`, so a bug that asks for
 * bytes past the end aborts in the reader rather than silently succeeding.
 *
 *   ./tools/fuzz.sh hdb 300
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hdb.h"
#include "helikon_error.h"
#include "mem.h"

struct blob {
    const uint8_t *data;
    size_t size;
};

static int blob_read(void *context, uint32_t offset, void *buffer,
                     uint32_t length)
{
    const struct blob *blob = context;
    /* A short database is an ordinary input, not a bug: `hdb_open` reads the
     * header before it knows the file size, and a real io layer answers -1. What
     * the harness checks afterwards is that no offset the reader *accepts* lands
     * outside the file. */
    if (offset > blob->size || length > blob->size - offset)
        return HELIKON_ERR_IO;
    helikon_memcpy(buffer, blob->data + offset, length);
    return HELIKON_OK;
}

/* firmware/lib's freestanding string.h shadows libc's on this include path. */
static void check_string(const char *text, size_t capacity)
{
    size_t i = 0;
    while (i < capacity && text[i] != '\0')
        ++i;
    if (i == capacity) {
        fprintf(stderr, "fuzz: string is not terminated in %zu bytes\n",
                capacity);
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct blob blob = { data, size };
    const struct hdb_io io = { .read = blob_read, .context = &blob };
    struct hdb db;
    helikon_memset(&db, 0, sizeof(db));

    if (hdb_open(&db, &io) != HELIKON_OK)
        return 0;

    char text[512];
    for (uint32_t i = 0; i < db.track_count && i < 64u; ++i) {
        struct hdb_track track;
        helikon_memset(&track, 0, sizeof(track));
        if (hdb_track(&db, i, &track) != HELIKON_OK)
            continue;
        uint32_t length = 0;
        helikon_memset(text, 0, sizeof(text));
        if (hdb_string(&db, track.title_off, text, sizeof(text), &length) ==
            HELIKON_OK) {
            check_string(text, sizeof(text));
            if (length >= sizeof(text)) {
                fprintf(stderr, "fuzz: string length %u past a %zu buffer\n",
                        length, sizeof(text));
                abort();
            }
        }
    }

    for (uint32_t i = 0; i < db.album_count && i < 64u; ++i) {
        struct hdb_album album;
        helikon_memset(&album, 0, sizeof(album));
        (void)hdb_album(&db, i, &album);
    }
    for (uint32_t i = 0; i < db.artist_count && i < 64u; ++i) {
        struct hdb_artist artist;
        helikon_memset(&artist, 0, sizeof(artist));
        (void)hdb_artist(&db, i, &artist);
    }
    for (uint32_t i = 0; i < db.playlist_count && i < 64u; ++i) {
        struct hdb_playlist playlist;
        helikon_memset(&playlist, 0, sizeof(playlist));
        if (hdb_playlist(&db, i, &playlist) != HELIKON_OK)
            continue;
        for (uint32_t entry = 0; entry < playlist.entry_count && entry < 64u;
             ++entry) {
            uint32_t track = 0;
            (void)hdb_playlist_entry(&db, i, entry, &track);
        }
    }
    (void)hdb_verify_crc(&db);

    /* Every offset the file reports must land inside the file. */
    const uint32_t offsets[] = { db.track_offset, db.album_offset,
                                 db.artist_offset, db.playlist_offset,
                                 db.playlist_entry_offset, db.string_offset };
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        if (offsets[i] > size) {
            fprintf(stderr, "fuzz: offset %u past a %zu byte file\n",
                    offsets[i], size);
            abort();
        }
    }
    return 0;
}
