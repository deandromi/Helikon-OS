/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HelikonDB (HDB) read-only music catalog.
 *
 * A single immutable file built by the companion (or by the reference tool)
 * and read lazily by the firmware. The reader owns no buffers and performs no
 * allocation: every call reads one fixed record, string or artwork blob
 * through the caller's I/O function. All offsets and lengths are checked
 * against the header before use.
 *
 * Format v1 is documented in docs/helikondb.md. The companion must not write
 * iTunesDB or any Apple database; HDB is the only catalog.
 */
#ifndef HELIKON_MUSICDB_HDB_H
#define HELIKON_MUSICDB_HDB_H

#include <stdint.h>

#define HDB_HEADER_SIZE 128u
#define HDB_VERSION 1u
#define HDB_NO_ID 0xFFFFFFFFu

#define HDB_TRACK_SIZE 88u
#define HDB_ALBUM_SIZE 48u
#define HDB_ARTIST_SIZE 48u
#define HDB_PLAYLIST_SIZE 24u
#define HDB_INDEX_SIZE 16u
#define HDB_ART_SIZE 32u
#define HDB_ART_FORMAT_RGB565 1u

enum hdb_index_type {
    HDB_INDEX_TITLE = 1,
    HDB_INDEX_RECENT = 2,
    HDB_INDEX_GENRE = 3,
    HDB_INDEX_ALBUM_TITLE = 4
};

struct hdb_io {
    /* Read exactly `length` bytes at `offset`; return 0 on success. */
    int (*read)(void *context, uint32_t offset, void *buffer, uint32_t length);
    void *context;
};

struct hdb {
    struct hdb_io io;
    uint32_t file_size;
    uint32_t flags;
    uint32_t generation;
    uint32_t track_count;
    uint32_t album_count;
    uint32_t artist_count;
    uint32_t playlist_count;
    uint32_t track_offset;
    uint32_t album_offset;
    uint32_t artist_offset;
    uint32_t playlist_offset;
    uint32_t playlist_entry_offset;
    uint32_t string_offset;
    uint32_t string_bytes;
    uint32_t index_offset;
    uint32_t index_count;
    uint32_t art_offset;
    uint32_t art_count;
    uint32_t art_blob_offset;
    uint32_t art_blob_bytes;
    uint32_t source_digest;
    uint32_t created;
    uint32_t stored_crc;
};

struct hdb_track {
    uint64_t uid;
    uint32_t path_off;
    uint32_t title_off;
    uint32_t artist_id;
    uint32_t album_id;
    uint32_t genre_off;
    uint32_t composer_off;
    uint32_t sort_title_off;
    uint32_t sort_artist_off;
    uint32_t sort_album_off;
    uint32_t duration_ms;
    uint32_t size_bytes;
    uint32_t bitrate;
    uint32_t sample_rate;
    uint32_t added_time;
    uint32_t artwork_id;
    uint32_t filetype;
    uint16_t track_no;
    uint16_t disc_no;
    uint16_t year;
    uint16_t rating;
    uint8_t flags;
};

struct hdb_album {
    uint32_t title_off;
    uint32_t artist_id;
    uint32_t sort_off;
    uint32_t genre_off;
    uint32_t artwork_id;
    uint32_t year;
    uint32_t track_count;
    uint32_t first_track;
    uint32_t disc_count;
    uint32_t added_time;
    uint16_t rating;
    uint16_t flags;
};

struct hdb_artist {
    uint32_t name_off;
    uint32_t sort_off;
    uint32_t bio_off;
    uint32_t image_id;
    uint32_t album_count;
    uint32_t first_album;
    uint32_t track_count;
    uint32_t added_time;
    uint16_t rating;
    uint16_t flags;
};

struct hdb_playlist {
    uint32_t name_off;
    uint32_t sort_off;
    uint32_t entry_offset;
    uint32_t entry_count;
    uint16_t flags;
};

struct hdb_art {
    uint32_t kind;
    uint32_t owner;
    uint16_t width;
    uint16_t height;
    uint16_t format;
    uint32_t data_off;
    uint32_t data_len;
    uint32_t crc32;
};

int hdb_open(struct hdb *db, const struct hdb_io *io);
int hdb_track(const struct hdb *db, uint32_t index, struct hdb_track *track);
int hdb_album(const struct hdb *db, uint32_t index, struct hdb_album *album);
int hdb_artist(const struct hdb *db, uint32_t index, struct hdb_artist *artist);
int hdb_playlist(const struct hdb *db, uint32_t index,
                 struct hdb_playlist *playlist);
int hdb_playlist_entry(const struct hdb *db, uint32_t playlist,
                       uint32_t entry, uint32_t *track_index);

/*
 * Read a UTF-8 string. Returns HELIKON_ERR_BUFFER (and the required size in
 * *length) when the caller's buffer, including the NUL, is too small.
 */
int hdb_string(const struct hdb *db, uint32_t offset, char *buffer,
               uint32_t capacity, uint32_t *length);

int hdb_index(const struct hdb *db, uint32_t type, uint32_t *count,
              uint32_t *offset);
int hdb_index_get(const struct hdb *db, uint32_t type, uint32_t index,
                  uint32_t *value);
int hdb_art(const struct hdb *db, uint32_t index, struct hdb_art *art);
int hdb_art_read(const struct hdb *db, uint32_t index, void *buffer,
                 uint32_t capacity, uint32_t *length);

/* Optional full-file CRC check; reads every byte in small chunks. */
int hdb_verify_crc(const struct hdb *db);

#endif
