/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "vorbis.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"

static uint32_t le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint32_t be24(const uint8_t *data)
{
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) |
           (uint32_t)data[2];
}

static int same_name(const uint8_t *name, size_t length, const char *literal)
{
    size_t index = 0u;
    for (; index < length; ++index) {
        char wanted = literal[index];
        if (wanted == '\0')
            return 0;
        char actual = (char)name[index];
        if (actual >= 'a' && actual <= 'z')
            actual = (char)(actual - 'a' + 'A');
        if (actual != wanted)
            return 0;
    }
    return literal[index] == '\0';
}

/*
 * Copies the valid UTF-8 prefix of `length` bytes into `dest`, which holds
 * `capacity` bytes including the terminator. A truncated sequence at the end is
 * dropped; an invalid byte anywhere drops the whole value so the caller never
 * shows half a character.
 */
static int copy_utf8(char *dest, size_t capacity, const uint8_t *src,
                     size_t length)
{
    size_t used = 0u;
    size_t index = 0u;
    while (index < length) {
        uint8_t byte = src[index];
        size_t need;
        if (byte < 0x80u)
            need = 1u;
        else if ((byte & 0xe0u) == 0xc0u)
            need = 2u;
        else if ((byte & 0xf0u) == 0xe0u)
            need = 3u;
        else if ((byte & 0xf8u) == 0xf0u)
            need = 4u;
        else
            return HELIKON_ERR_FORMAT;
        if (index + need > length)
            break;
        if (used + need + 1u > capacity)
            break;
        for (size_t extra = 1u; extra < need; ++extra) {
            if ((src[index + extra] & 0xc0u) != 0x80u)
                return HELIKON_ERR_FORMAT;
        }
        for (size_t extra = 0u; extra < need; ++extra)
            dest[used + extra] = (char)src[index + extra];
        used += need;
        index += need;
    }
    dest[used] = '\0';
    return HELIKON_OK;
}

/* Leading digits of a TRACKNUMBER/DISCNUMBER value ("3" or "3/12"). */
static uint16_t number_prefix(const uint8_t *src, size_t length)
{
    uint32_t value = 0u;
    size_t digits = 0u;
    for (size_t index = 0u; index < length; ++index) {
        if (src[index] < '0' || src[index] > '9')
            break;
        value = value * 10u + (uint32_t)(src[index] - '0');
        ++digits;
        if (value > 0xffffu)
            return 0u;
    }
    return digits == 0u ? 0u : (uint16_t)value;
}

/* First four digits of a DATE ("2021-11-17" -> "2021"). */
static void year_from(const uint8_t *src, size_t length, char year[8])
{
    size_t digits = 0u;
    for (size_t index = 0u; index < length && digits < 4u; ++index) {
        if (src[index] < '0' || src[index] > '9')
            break;
        year[digits++] = (char)src[index];
    }
    year[digits] = '\0';
}

static void field_set(char *dest, size_t capacity, const uint8_t *value,
                      size_t length)
{
    if (dest[0] != '\0')
        return;
    if (copy_utf8(dest, capacity, value, length) != HELIKON_OK)
        dest[0] = '\0';
}

static int parse_comment(const uint8_t *entry, size_t length,
                         struct helikon_vorbis_tags *tags)
{
    size_t split = 0u;
    while (split < length && entry[split] != '=')
        ++split;
    if (split == length)
        return HELIKON_ERR_FORMAT;
    const uint8_t *value = entry + split + 1u;
    size_t value_length = length - split - 1u;

    if (same_name(entry, split, "TITLE"))
        field_set(tags->title, sizeof(tags->title), value, value_length);
    else if (same_name(entry, split, "ARTIST"))
        field_set(tags->artist, sizeof(tags->artist), value, value_length);
    else if (same_name(entry, split, "ALBUM"))
        field_set(tags->album, sizeof(tags->album), value, value_length);
    else if (same_name(entry, split, "ALBUMARTIST"))
        field_set(tags->album_artist, sizeof(tags->album_artist), value,
                  value_length);
    else if (same_name(entry, split, "GENRE"))
        field_set(tags->genre, sizeof(tags->genre), value, value_length);
    else if (same_name(entry, split, "DATE") ||
             same_name(entry, split, "YEAR")) {
        if (tags->year[0] == '\0')
            year_from(value, value_length, tags->year);
    } else if (same_name(entry, split, "TRACKNUMBER")) {
        if (tags->track == 0u)
            tags->track = number_prefix(value, value_length);
    } else if (same_name(entry, split, "DISCNUMBER")) {
        if (tags->disc == 0u)
            tags->disc = number_prefix(value, value_length);
    }
    return HELIKON_OK;
}

/*
 * `allow_truncated` lets a prefix of a block parse successfully: complete
 * comments are kept and a cut-off last comment ends the walk.
 */
static int vorbis_parse(const uint8_t *data, size_t size,
                        struct helikon_vorbis_tags *tags, int allow_truncated)
{
    if (data == NULL || tags == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(tags, 0, sizeof(*tags));
    if (size < 8u)
        return HELIKON_ERR_FORMAT;

    uint32_t vendor = le32(data);
    if (vendor > size - 4u)
        return HELIKON_ERR_FORMAT;
    size_t offset = 4u + vendor;
    if (size - offset < 4u)
        return HELIKON_ERR_FORMAT;

    uint32_t count = le32(data + offset);
    offset += 4u;
    for (uint32_t index = 0u; index < count; ++index) {
        if (size - offset < 4u) {
            if (allow_truncated)
                break;
            return HELIKON_ERR_FORMAT;
        }
        uint32_t length = le32(data + offset);
        offset += 4u;
        if ((size_t)length > size - offset) {
            if (allow_truncated)
                break;
            return HELIKON_ERR_FORMAT;
        }
        int rc = parse_comment(data + offset, length, tags);
        if (rc != HELIKON_OK)
            return rc;
        offset += length;
    }
    return HELIKON_OK;
}

int helikon_vorbis_comment_parse(const uint8_t *data, size_t size,
                                 struct helikon_vorbis_tags *tags)
{
    return vorbis_parse(data, size, tags, 0);
}

int helikon_flac_tags_buffer(const uint8_t *data, size_t size,
                             struct helikon_vorbis_tags *tags)
{
    if (data == NULL || tags == NULL)
        return HELIKON_ERR_ARGUMENT;
    helikon_memset(tags, 0, sizeof(*tags));

    if (size < 8u || data[0] != 'f' || data[1] != 'L' || data[2] != 'a' ||
        data[3] != 'C')
        return HELIKON_ERR_NOT_FOUND;

    size_t offset = 4u;
    for (;;) {
        if (size - offset < 4u)
            return HELIKON_ERR_FORMAT;
        uint8_t header = data[offset];
        int last = (header & 0x80u) != 0u;
        uint8_t type = (uint8_t)(header & 0x7fu);
        uint32_t length = be24(data + offset + 1u);
        offset += 4u;
        if ((size_t)length > size - offset)
            return HELIKON_ERR_FORMAT;
        if (type == 4u)
            return vorbis_parse(data + offset, length, tags, 0);
        offset += length;
        if (last)
            return HELIKON_ERR_NOT_FOUND;
    }
}

/*
 * The block buffer is static because the firmware stack budget for a codec is
 * 2 KiB; the reader is called from the single-threaded UI/library path.
 */
static uint8_t flac_block[HELIKON_VORBIS_BLOCK_MAX];

static int read_bytes(const struct helikon_codec_source *source, uint64_t offset,
                      uint8_t *buffer, size_t length)
{
    size_t done = 0u;
    while (done < length) {
        size_t got = 0u;
        int rc = source->read_at(source->context, offset + done,
                                 buffer + done, length - done, &got);
        if (rc != HELIKON_OK)
            return rc;
        if (got == 0u)
            return HELIKON_ERR_FORMAT;
        done += got;
    }
    return HELIKON_OK;
}

int helikon_flac_tags_source(const struct helikon_codec_source *source,
                             struct helikon_vorbis_tags *tags)
{
    if (source == NULL || source->read_at == NULL || tags == NULL)
        return HELIKON_ERR_ARGUMENT;
    helikon_memset(tags, 0, sizeof(*tags));

    if (source->size < 8u)
        return source->size == 0u ? HELIKON_ERR_UNSUPPORTED
                                  : HELIKON_ERR_NOT_FOUND;

    uint8_t magic[4];
    int rc = read_bytes(source, 0u, magic, sizeof(magic));
    if (rc != HELIKON_OK)
        return rc;
    if (magic[0] != 'f' || magic[1] != 'L' || magic[2] != 'a' ||
        magic[3] != 'C')
        return HELIKON_ERR_NOT_FOUND;

    uint64_t offset = 4u;
    for (;;) {
        if (source->size - offset < 4u)
            return HELIKON_ERR_FORMAT;
        uint8_t header[4];
        rc = read_bytes(source, offset, header, sizeof(header));
        if (rc != HELIKON_OK)
            return rc;
        int last = (header[0] & 0x80u) != 0u;
        uint8_t type = (uint8_t)(header[0] & 0x7fu);
        uint64_t length = be24(header + 1u);
        offset += 4u;
        if (length > source->size - offset)
            return HELIKON_ERR_FORMAT;
        if (type == 4u) {
            size_t wanted = length > HELIKON_VORBIS_BLOCK_MAX
                                ? HELIKON_VORBIS_BLOCK_MAX
                                : (size_t)length;
            rc = read_bytes(source, offset, flac_block, wanted);
            if (rc != HELIKON_OK)
                return rc;
            return vorbis_parse(flac_block, wanted, tags,
                                length > (uint64_t)wanted);
        }
        offset += length;
        if (last)
            return HELIKON_ERR_NOT_FOUND;
    }
}
