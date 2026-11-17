/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host test for the HelikonDB reader. Also opens a real library DB when a
 * second path is passed, so the same test covers the 100-song corpus. */
#include <stdint.h>
#include <stdio.h>

#include "crc32.h"
#include "error.h"
#include "hdb.h"
#include "string.h"

static int failures;

static void check(int condition, const char *message);

struct mem_io {
    const uint8_t *data;
    uint32_t size;
};

static int mem_read(void *context, uint32_t offset, void *buffer,
                    uint32_t length)
{
    struct mem_io *memory = context;
    if (offset > memory->size || length > memory->size - offset)
        return HELIKON_ERR_IO;
    helikon_memcpy(buffer, memory->data + offset, length);
    return HELIKON_OK;
}

static void check_bad_inputs(void)
{
    uint8_t empty[128] = { 0 };
    struct mem_io memory = { empty, sizeof(empty) };
    const struct hdb_io io = { .read = mem_read, .context = &memory };
    struct hdb db;
    check(hdb_open(&db, &io) == HELIKON_ERR_FORMAT, "bad magic rejected");

    empty[0] = 'H';
    empty[1] = 'L';
    empty[2] = 'D';
    empty[3] = 'B';
    check(hdb_open(&db, &io) == HELIKON_ERR_FORMAT, "bad version rejected");

    empty[4] = 1;
    empty[6] = 128;
    empty[92] = 64; /* file size smaller than the header */
    check(hdb_open(&db, &io) == HELIKON_ERR_FORMAT, "short file rejected");
}

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static int file_read(void *context, uint32_t offset, void *buffer,
                     uint32_t length)
{
    FILE *file = context;
    if (fseek(file, (long)offset, SEEK_SET) != 0)
        return HELIKON_ERR_IO;
    if (fread(buffer, 1u, (size_t)length, file) != (size_t)length)
        return HELIKON_ERR_IO;
    return HELIKON_OK;
}

static void expect_string(const struct hdb *db, uint32_t offset,
                          const char *expected, const char *message)
{
    char buffer[256];
    uint32_t length = 0;
    int rc = hdb_string(db, offset, buffer, sizeof(buffer), &length);
    if (rc != HELIKON_OK || helikon_strcmp(buffer, expected) != 0) {
        fprintf(stderr, "FAIL: %s (rc %d, got '%s')\n", message, rc,
                rc == HELIKON_OK ? buffer : "?");
        ++failures;
    }
}

static void check_fixture(const struct hdb *db)
{
    check(db->track_count == 6u, "fixture track count");
    check(db->album_count == 3u, "fixture album count");
    check(db->artist_count == 3u, "fixture artist count");
    check(db->playlist_count == 1u, "fixture playlist count");
    check(db->art_count == 6u, "fixture artwork count");

    struct hdb_artist artist;
    check(hdb_artist(db, 0u, &artist) == HELIKON_OK, "artist 0 reads");
    expect_string(db, artist.name_off, "The Ember Set", "artist 0 name");
    expect_string(db, artist.bio_off, "A test artist.", "artist 0 bio");
    check(artist.album_count == 1u, "artist 0 album count");
    check(artist.first_album == 0u, "artist 0 first album");
    check(artist.track_count == 2u, "artist 0 track count");
    check(artist.image_id == 3u, "artist 0 image id");

    struct hdb_album album;
    check(hdb_album(db, 0u, &album) == HELIKON_OK, "album 0 reads");
    expect_string(db, album.title_off, "Night Harbour", "album 0 title");
    check(album.artist_id == 0u, "album 0 artist");
    check(album.track_count == 2u, "album 0 track count");
    check(album.first_track == 0u, "album 0 first track");
    check(album.year == 2004u, "album 0 year");
    check(album.rating == 80u, "album 0 rating");
    check(album.artwork_id == 0u, "album 0 artwork");

    struct hdb_track track;
    check(hdb_track(db, 0u, &track) == HELIKON_OK, "track 0 reads");
    expect_string(db, track.title_off, "First Light", "track 0 title");
    expect_string(db, track.path_off, "/Music/The Ember Set/Night Harbour/01 First Light.m4a",
                  "track 0 path");
    check(track.artist_id == 0u, "track 0 artist");
    check(track.album_id == 0u, "track 0 album");
    check(track.track_no == 1u, "track 0 number");
    check(track.disc_no == 1u, "track 0 disc");
    check(track.year == 2004u, "track 0 year");
    check(track.rating == 90u, "track 0 rating");
    check(track.duration_ms == 210000u, "track 0 duration");
    check(track.artwork_id == 0u, "track 0 artwork");
    check(track.filetype == 2u, "track 0 filetype");
    check(track.uid != 0u, "track 0 uid");

    struct hdb_playlist playlist;
    check(hdb_playlist(db, 0u, &playlist) == HELIKON_OK, "playlist 0 reads");
    expect_string(db, playlist.name_off, "Favourites", "playlist 0 name");
    check(playlist.entry_count == 3u, "playlist 0 count");
    uint32_t entry = 0;
    check(hdb_playlist_entry(db, 0u, 0u, &entry) == HELIKON_OK && entry == 0u,
          "playlist entry 0");
    check(hdb_playlist_entry(db, 0u, 1u, &entry) == HELIKON_OK && entry == 2u,
          "playlist entry 1");
    check(hdb_playlist_entry(db, 0u, 2u, &entry) == HELIKON_OK && entry == 5u,
          "playlist entry 2");

    uint32_t count = 0;
    uint32_t offset = 0;
    check(hdb_index(db, HDB_INDEX_TITLE, &count, &offset) == HELIKON_OK,
          "title index present");
    check(count == 6u, "title index count");
    uint32_t value = 0;
    check(hdb_index_get(db, HDB_INDEX_TITLE, 0u, &value) == HELIKON_OK &&
              value == 4u,
          "title index first (Ember)");
    check(hdb_index_get(db, HDB_INDEX_TITLE, 5u, &value) == HELIKON_OK &&
              value == 5u,
          "title index last (Tide)");
    check(hdb_index(db, HDB_INDEX_ALBUM_TITLE, &count, &offset) == HELIKON_OK &&
              count == 3u,
          "album title index");
    check(hdb_index_get(db, HDB_INDEX_ALBUM_TITLE, 0u, &value) == HELIKON_OK &&
              value == 2u,
          "album title first (Mixtape)");
    check(hdb_index_get(db, HDB_INDEX_TITLE, 6u, &value) ==
              HELIKON_ERR_NOT_FOUND,
          "title index out of range");

    struct hdb_art art;
    check(hdb_art(db, 0u, &art) == HELIKON_OK, "art 0 reads");
    check(art.kind == 1u && art.owner == 0u, "art 0 owner");
    check(art.width == 96u && art.height == 96u, "art 0 dimensions");
    check(art.data_len == 96u * 96u * 2u, "art 0 length");
    static uint8_t blob[96u * 96u * 2u];
    uint32_t blob_length = 0;
    check(hdb_art_read(db, 0u, blob, sizeof(blob), &blob_length) == HELIKON_OK,
          "art 0 reads");
    check(blob_length == art.data_len, "art 0 copied length");
    check(helikon_crc32(blob, blob_length) == art.crc32, "art 0 crc");

    char small[4];
    uint32_t needed = 0;
    check(hdb_string(db, artist.name_off, small, sizeof(small), &needed) ==
              HELIKON_ERR_BUFFER,
          "short string buffer reports BUFFER");
    check(needed == helikon_strlen("The Ember Set", 64u),
          "short string reports length");

    check(hdb_verify_crc(db) == HELIKON_OK, "fixture CRC verifies");
}

static void check_real(const struct hdb *db)
{
    printf("real library: %u tracks, %u albums, %u artists, %u playlists, "
           "%u artwork records, %u bytes\n",
           db->track_count, db->album_count, db->artist_count,
           db->playlist_count, db->art_count, db->file_size);
    check(db->track_count > 0u, "real library has tracks");
    check(hdb_verify_crc(db) == HELIKON_OK, "real library CRC verifies");
    struct hdb_track track;
    check(hdb_track(db, 0u, &track) == HELIKON_OK, "real track 0 reads");
    char title[256];
    char path[512];
    uint32_t length = 0;
    check(hdb_string(db, track.title_off, title, sizeof(title), &length) ==
              HELIKON_OK,
          "real track 0 title reads");
    check(hdb_string(db, track.path_off, path, sizeof(path), &length) ==
              HELIKON_OK,
          "real track 0 path reads");
    printf("real track 0: %s | %s\n", title, path);
    struct hdb_album album;
    check(hdb_album(db, 0u, &album) == HELIKON_OK, "real album 0 reads");
    struct hdb_artist artist;
    check(hdb_artist(db, 0u, &artist) == HELIKON_OK, "real artist 0 reads");
    uint32_t count = 0;
    uint32_t offset = 0;
    check(hdb_index(db, HDB_INDEX_TITLE, &count, &offset) == HELIKON_OK,
          "real title index present");
    check(count == db->track_count, "real title index count");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s fixture.hdb [real.hdb]\n", argv[0]);
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const struct hdb_io io = { .read = file_read, .context = file };
    struct hdb db;
    int rc = hdb_open(&db, &io);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "fixture open failed: %d\n", rc);
        fclose(file);
        return 1;
    }
    check_fixture(&db);
    fclose(file);
    check_bad_inputs();

    if (argc > 2) {
        FILE *real = fopen(argv[2], "rb");
        if (real == NULL) {
            fprintf(stderr, "cannot open %s\n", argv[2]);
            return 2;
        }
        const struct hdb_io real_io = { .read = file_read, .context = real };
        struct hdb real_db;
        rc = hdb_open(&real_db, &real_io);
        if (rc != HELIKON_OK) {
            fprintf(stderr, "real open failed: %d\n", rc);
            fclose(real);
            return 1;
        }
        check_real(&real_db);
        fclose(real);
    }

    if (failures != 0) {
        fprintf(stderr, "%d HDB checks failed\n", failures);
        return 1;
    }
    printf("PASS hdb\n");
    return 0;
}
