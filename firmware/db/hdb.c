/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hdb.h"

#include <stddef.h>

#include "crc32.h"
#include "error.h"
#include "mem.h"

#define HDB_MAGIC 0x42444C48u /* "HLDB" little-endian */

static uint16_t read16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read64(const uint8_t *bytes)
{
    return (uint64_t)read32(bytes) | ((uint64_t)read32(bytes + 4) << 32);
}

static int read_range(const struct hdb *db, uint32_t offset, void *buffer,
                      uint32_t length)
{
    if (db == NULL || db->io.read == NULL || buffer == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (offset > db->file_size || length > db->file_size - offset)
        return HELIKON_ERR_FORMAT;
    return db->io.read(db->io.context, offset, buffer, length);
}

static int table_fits(uint32_t offset, uint32_t count, uint32_t record_size,
                      uint32_t file_size)
{
    return (uint64_t)offset + (uint64_t)count * record_size <= file_size;
}

int hdb_open(struct hdb *db, const struct hdb_io *io)
{
    if (db == NULL || io == NULL || io->read == NULL)
        return HELIKON_ERR_ARGUMENT;

    uint8_t header[HDB_HEADER_SIZE];
    int rc = io->read(io->context, 0u, header, sizeof(header));
    if (rc != HELIKON_OK)
        return rc;

    if (read32(header) != HDB_MAGIC)
        return HELIKON_ERR_FORMAT;
    if (read16(header + 4) != HDB_VERSION ||
        read16(header + 6) != HDB_HEADER_SIZE)
        return HELIKON_ERR_FORMAT;

    db->io = *io;
    db->flags = read32(header + 8);
    db->generation = read32(header + 12);
    db->track_count = read32(header + 16);
    db->album_count = read32(header + 20);
    db->artist_count = read32(header + 24);
    db->playlist_count = read32(header + 28);
    db->track_offset = read32(header + 32);
    db->album_offset = read32(header + 36);
    db->artist_offset = read32(header + 40);
    db->playlist_offset = read32(header + 44);
    db->playlist_entry_offset = read32(header + 48);
    db->string_offset = read32(header + 52);
    db->string_bytes = read32(header + 56);
    db->index_offset = read32(header + 60);
    db->index_count = read32(header + 64);
    db->art_offset = read32(header + 68);
    db->art_count = read32(header + 72);
    db->art_blob_offset = read32(header + 76);
    db->art_blob_bytes = read32(header + 80);
    db->source_digest = read32(header + 84);
    db->created = read32(header + 88);
    db->file_size = read32(header + 92);
    db->stored_crc = read32(header + 96);

    if (db->file_size < HDB_HEADER_SIZE)
        return HELIKON_ERR_FORMAT;
    if (!table_fits(db->track_offset, db->track_count, HDB_TRACK_SIZE,
                    db->file_size) ||
        !table_fits(db->album_offset, db->album_count, HDB_ALBUM_SIZE,
                    db->file_size) ||
        !table_fits(db->artist_offset, db->artist_count, HDB_ARTIST_SIZE,
                    db->file_size) ||
        !table_fits(db->playlist_offset, db->playlist_count,
                    HDB_PLAYLIST_SIZE, db->file_size) ||
        !table_fits(db->string_offset, db->string_bytes, 1u, db->file_size) ||
        !table_fits(db->index_offset, db->index_count, HDB_INDEX_SIZE,
                    db->file_size) ||
        !table_fits(db->art_offset, db->art_count, HDB_ART_SIZE,
                    db->file_size) ||
        !table_fits(db->art_blob_offset, db->art_blob_bytes, 1u,
                    db->file_size) ||
        db->playlist_entry_offset > db->file_size ||
        db->index_count > 64u)
        return HELIKON_ERR_FORMAT;
    return HELIKON_OK;
}

int hdb_track(const struct hdb *db, uint32_t index, struct hdb_track *track)
{
    if (db == NULL || track == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (index >= db->track_count)
        return HELIKON_ERR_NOT_FOUND;

    uint8_t raw[HDB_TRACK_SIZE];
    int rc = read_range(db, db->track_offset + index * HDB_TRACK_SIZE, raw,
                        sizeof(raw));
    if (rc != HELIKON_OK)
        return rc;

    track->uid = read64(raw);
    track->path_off = read32(raw + 8);
    track->title_off = read32(raw + 12);
    track->artist_id = read32(raw + 16);
    track->album_id = read32(raw + 20);
    track->genre_off = read32(raw + 24);
    track->composer_off = read32(raw + 28);
    track->sort_title_off = read32(raw + 32);
    track->sort_artist_off = read32(raw + 36);
    track->sort_album_off = read32(raw + 40);
    track->duration_ms = read32(raw + 44);
    track->size_bytes = read32(raw + 48);
    track->bitrate = read32(raw + 52);
    track->sample_rate = read32(raw + 56);
    track->added_time = read32(raw + 60);
    track->artwork_id = read32(raw + 64);
    track->filetype = read32(raw + 68);
    track->track_no = read16(raw + 72);
    track->disc_no = read16(raw + 74);
    track->year = read16(raw + 76);
    track->rating = read16(raw + 78);
    track->flags = raw[80];
    return HELIKON_OK;
}

int hdb_album(const struct hdb *db, uint32_t index, struct hdb_album *album)
{
    if (db == NULL || album == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (index >= db->album_count)
        return HELIKON_ERR_NOT_FOUND;

    uint8_t raw[HDB_ALBUM_SIZE];
    int rc = read_range(db, db->album_offset + index * HDB_ALBUM_SIZE, raw,
                        sizeof(raw));
    if (rc != HELIKON_OK)
        return rc;

    album->title_off = read32(raw);
    album->artist_id = read32(raw + 4);
    album->sort_off = read32(raw + 8);
    album->genre_off = read32(raw + 12);
    album->artwork_id = read32(raw + 16);
    album->year = read32(raw + 20);
    album->track_count = read32(raw + 24);
    album->first_track = read32(raw + 28);
    album->disc_count = read32(raw + 32);
    album->added_time = read32(raw + 36);
    album->rating = read16(raw + 40);
    album->flags = read16(raw + 42);
    return HELIKON_OK;
}

int hdb_artist(const struct hdb *db, uint32_t index, struct hdb_artist *artist)
{
    if (db == NULL || artist == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (index >= db->artist_count)
        return HELIKON_ERR_NOT_FOUND;

    uint8_t raw[HDB_ARTIST_SIZE];
    int rc = read_range(db, db->artist_offset + index * HDB_ARTIST_SIZE, raw,
                        sizeof(raw));
    if (rc != HELIKON_OK)
        return rc;

    artist->name_off = read32(raw);
    artist->sort_off = read32(raw + 4);
    artist->bio_off = read32(raw + 8);
    artist->image_id = read32(raw + 12);
    artist->album_count = read32(raw + 16);
    artist->first_album = read32(raw + 20);
    artist->track_count = read32(raw + 24);
    artist->added_time = read32(raw + 28);
    artist->rating = read16(raw + 32);
    artist->flags = read16(raw + 34);
    return HELIKON_OK;
}

int hdb_playlist(const struct hdb *db, uint32_t index,
                 struct hdb_playlist *playlist)
{
    if (db == NULL || playlist == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (index >= db->playlist_count)
        return HELIKON_ERR_NOT_FOUND;

    uint8_t raw[HDB_PLAYLIST_SIZE];
    int rc = read_range(db, db->playlist_offset + index * HDB_PLAYLIST_SIZE,
                        raw, sizeof(raw));
    if (rc != HELIKON_OK)
        return rc;

    playlist->name_off = read32(raw);
    playlist->sort_off = read32(raw + 4);
    playlist->entry_offset = read32(raw + 8);
    playlist->entry_count = read32(raw + 12);
    playlist->flags = read16(raw + 16);
    if (!table_fits(playlist->entry_offset, playlist->entry_count, 4u,
                    db->file_size))
        return HELIKON_ERR_FORMAT;
    return HELIKON_OK;
}

int hdb_playlist_entry(const struct hdb *db, uint32_t playlist,
                       uint32_t entry, uint32_t *track_index)
{
    if (db == NULL || track_index == NULL)
        return HELIKON_ERR_ARGUMENT;
    struct hdb_playlist record;
    int rc = hdb_playlist(db, playlist, &record);
    if (rc != HELIKON_OK)
        return rc;
    if (entry >= record.entry_count)
        return HELIKON_ERR_NOT_FOUND;
    uint8_t raw[4];
    rc = read_range(db, record.entry_offset + entry * 4u, raw, sizeof(raw));
    if (rc != HELIKON_OK)
        return rc;
    *track_index = read32(raw);
    return HELIKON_OK;
}

int hdb_string(const struct hdb *db, uint32_t offset, char *buffer,
               uint32_t capacity, uint32_t *length)
{
    if (db == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (offset == HDB_NO_ID || offset == 0u) {
        if (length != NULL)
            *length = 0u;
        if (buffer == NULL || capacity == 0u)
            return HELIKON_ERR_BUFFER;
        buffer[0] = '\0';
        return HELIKON_OK;
    }
    if (offset < db->string_offset ||
        offset >= db->string_offset + db->string_bytes)
        return HELIKON_ERR_FORMAT;

    uint8_t raw[2];
    int rc = read_range(db, offset, raw, sizeof(raw));
    if (rc != HELIKON_OK)
        return rc;
    uint32_t string_length = read16(raw);
    if (offset + 2u + string_length >
        db->string_offset + db->string_bytes)
        return HELIKON_ERR_FORMAT;
    if (length != NULL)
        *length = string_length;
    if (buffer == NULL || capacity < string_length + 1u)
        return HELIKON_ERR_BUFFER;
    rc = read_range(db, offset + 2u, buffer, string_length);
    if (rc != HELIKON_OK)
        return rc;
    buffer[string_length] = '\0';
    return HELIKON_OK;
}

int hdb_index(const struct hdb *db, uint32_t type, uint32_t *count,
              uint32_t *offset)
{
    if (db == NULL || count == NULL || offset == NULL)
        return HELIKON_ERR_ARGUMENT;
    for (uint32_t index = 0; index < db->index_count; ++index) {
        uint8_t raw[HDB_INDEX_SIZE];
        int rc = read_range(db, db->index_offset + index * HDB_INDEX_SIZE,
                            raw, sizeof(raw));
        if (rc != HELIKON_OK)
            return rc;
        if (read32(raw) != type)
            continue;
        uint32_t entries = read32(raw + 4);
        uint32_t entries_offset = read32(raw + 8);
        if (!table_fits(entries_offset, entries, 4u, db->file_size))
            return HELIKON_ERR_FORMAT;
        *count = entries;
        *offset = entries_offset;
        return HELIKON_OK;
    }
    return HELIKON_ERR_NOT_FOUND;
}

int hdb_index_get(const struct hdb *db, uint32_t type, uint32_t index,
                  uint32_t *value)
{
    if (db == NULL || value == NULL)
        return HELIKON_ERR_ARGUMENT;
    uint32_t count = 0;
    uint32_t offset = 0;
    int rc = hdb_index(db, type, &count, &offset);
    if (rc != HELIKON_OK)
        return rc;
    if (index >= count)
        return HELIKON_ERR_NOT_FOUND;
    uint8_t raw[4];
    rc = read_range(db, offset + index * 4u, raw, sizeof(raw));
    if (rc != HELIKON_OK)
        return rc;
    *value = read32(raw);
    return HELIKON_OK;
}

int hdb_art(const struct hdb *db, uint32_t index, struct hdb_art *art)
{
    if (db == NULL || art == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (index >= db->art_count)
        return HELIKON_ERR_NOT_FOUND;
    uint8_t raw[HDB_ART_SIZE];
    int rc = read_range(db, db->art_offset + index * HDB_ART_SIZE, raw,
                        sizeof(raw));
    if (rc != HELIKON_OK)
        return rc;
    art->kind = read32(raw);
    art->owner = read32(raw + 4);
    art->width = read16(raw + 8);
    art->height = read16(raw + 10);
    art->format = read16(raw + 12);
    art->data_off = read32(raw + 16);
    art->data_len = read32(raw + 20);
    art->crc32 = read32(raw + 24);
    if (art->format != HDB_ART_FORMAT_RGB565 ||
        !table_fits(art->data_off, art->data_len, 1u, db->file_size))
        return HELIKON_ERR_FORMAT;
    return HELIKON_OK;
}

int hdb_art_read(const struct hdb *db, uint32_t index, void *buffer,
                 uint32_t capacity, uint32_t *length)
{
    struct hdb_art art;
    int rc = hdb_art(db, index, &art);
    if (rc != HELIKON_OK)
        return rc;
    if (length != NULL)
        *length = art.data_len;
    if (buffer == NULL || capacity < art.data_len)
        return HELIKON_ERR_BUFFER;
    return read_range(db, art.data_off, buffer, art.data_len);
}

int hdb_verify_crc(const struct hdb *db)
{
    if (db == NULL || db->io.read == NULL ||
        db->file_size < HDB_HEADER_SIZE)
        return HELIKON_ERR_ARGUMENT;

    uint8_t buffer[256];
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t offset = 0; offset < db->file_size;) {
        uint32_t length = db->file_size - offset;
        if (length > sizeof(buffer))
            length = sizeof(buffer);
        int rc = db->io.read(db->io.context, offset, buffer, length);
        if (rc != HELIKON_OK)
            return rc;
        /* The stored CRC field is excluded from the calculation. */
        for (uint32_t i = 0; i < length; ++i) {
            uint32_t absolute = offset + i;
            if (absolute >= 96u && absolute < 100u)
                buffer[i] = 0u;
        }
        crc = helikon_crc32_update(crc, buffer, length);
        offset += length;
    }
    return (~crc) == db->stored_crc ? HELIKON_OK : HELIKON_ERR_FORMAT;
}
