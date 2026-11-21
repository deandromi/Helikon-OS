/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "id3v2.h"

#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include "mem.h"

/* ID3v2.3/2.4 frame header: 4-byte id, 4-byte size, 2 bytes of flags. */
#define FRAME_HEADER 10u
/* ID3v2.2 frame header: 3-byte id, 3-byte size. */
#define FRAME_HEADER_V22 6u

static uint32_t be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

/* A sync-safe size stores 7 bits per byte. */
static uint32_t syncsafe32(const uint8_t *data)
{
    return ((uint32_t)(data[0] & 0x7fu) << 21) |
           ((uint32_t)(data[1] & 0x7fu) << 14) |
           ((uint32_t)(data[2] & 0x7fu) << 7) | (uint32_t)(data[3] & 0x7fu);
}

static int tag_is(const uint8_t *data, const char *tag, size_t length)
{
    for (size_t index = 0u; index < length; ++index) {
        if (data[index] != (uint8_t)tag[index])
            return 0;
    }
    return 1;
}

/* UTF-8 emission with a hard stop; a truncated sequence is dropped whole. */
struct utf8_out {
    char *buffer;
    size_t capacity;
    size_t used;
};

static void put_byte(struct utf8_out *out, uint8_t value)
{
    if (out->used + 1u < out->capacity) {
        out->buffer[out->used] = (char)value;
        out->used += 1u;
        out->buffer[out->used] = '\0';
    }
}

static uint32_t put_codepoint(struct utf8_out *out, uint32_t code)
{
    if (code < 0x80u) {
        put_byte(out, (uint8_t)code);
        return 1u;
    }
    if (code < 0x800u) {
        put_byte(out, (uint8_t)(0xc0u | (code >> 6)));
        put_byte(out, (uint8_t)(0x80u | (code & 0x3fu)));
        return 2u;
    }
    if (code < 0x10000u) {
        put_byte(out, (uint8_t)(0xe0u | (code >> 12)));
        put_byte(out, (uint8_t)(0x80u | ((code >> 6) & 0x3fu)));
        put_byte(out, (uint8_t)(0x80u | (code & 0x3fu)));
        return 3u;
    }
    if (code <= 0x10ffffu) {
        put_byte(out, (uint8_t)(0xf0u | (code >> 18)));
        put_byte(out, (uint8_t)(0x80u | ((code >> 12) & 0x3fu)));
        put_byte(out, (uint8_t)(0x80u | ((code >> 6) & 0x3fu)));
        put_byte(out, (uint8_t)(0x80u | (code & 0x3fu)));
        return 4u;
    }
    return 0u;
}

static void clear_text(char *text, size_t capacity)
{
    if (capacity != 0u)
        text[0] = '\0';
}

/* Copies a Latin-1 string as UTF-8, stopping at a terminator. */
static void decode_latin1(const uint8_t *data, size_t length, char *out,
                          size_t capacity)
{
    struct utf8_out sink = { out, capacity, 0u };
    clear_text(out, capacity);
    for (size_t index = 0u; index < length; ++index) {
        if (data[index] == 0u)
            break;
        put_codepoint(&sink, data[index]);
    }
}

static uint16_t read_u16(const uint8_t *data, int big_endian)
{
    return big_endian ? (uint16_t)(((uint16_t)data[0] << 8) | data[1])
                      : (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/* UTF-16 with an optional BOM; a missing BOM defaults to big-endian. */
static void decode_utf16(const uint8_t *data, size_t length, char *out,
                         size_t capacity)
{
    struct utf8_out sink = { out, capacity, 0u };
    clear_text(out, capacity);

    int big_endian = 1;
    size_t index = 0u;
    if (length >= 2u && data[0] == 0xffu && data[1] == 0xfeu) {
        big_endian = 0;
        index = 2u;
    } else if (length >= 2u && data[0] == 0xfeu && data[1] == 0xffu) {
        index = 2u;
    }

    while (index + 1u < length) {
        uint32_t code = read_u16(data + index, big_endian);
        index += 2u;
        if (code == 0u)
            break;
        if (code >= 0xd800u && code <= 0xdbffu && index + 1u < length) {
            const uint32_t low = read_u16(data + index, big_endian);
            if (low >= 0xdc00u && low <= 0xdfffu) {
                index += 2u;
                code = 0x10000u + ((code - 0xd800u) << 10) + (low - 0xdc00u);
            }
        }
        if (code >= 0xd800u && code <= 0xdfffu)
            continue; /* unpaired surrogate: skip rather than emit garbage */
        (void)put_codepoint(&sink, code);
    }
}

/* UTF-8 passthrough: validated enough to stop at NUL and drop bad bytes. */
static void decode_utf8(const uint8_t *data, size_t length, char *out,
                        size_t capacity)
{
    struct utf8_out sink = { out, capacity, 0u };
    clear_text(out, capacity);
    for (size_t index = 0u; index < length;) {
        const uint8_t lead = data[index];
        if (lead == 0u)
            break;
        size_t extra;
        if (lead < 0x80u)
            extra = 0u;
        else if ((lead & 0xe0u) == 0xc0u)
            extra = 1u;
        else if ((lead & 0xf0u) == 0xe0u)
            extra = 2u;
        else if ((lead & 0xf8u) == 0xf0u)
            extra = 3u;
        else {
            index += 1u;
            continue;
        }
        if (extra != 0u && index + extra >= length)
            break; /* truncated multi-byte sequence */
        put_byte(&sink, lead);
        for (size_t tail = 1u; tail <= extra; ++tail) {
            const uint8_t next = data[index + tail];
            if ((next & 0xc0u) != 0x80u)
                break;
            put_byte(&sink, next);
        }
        index += extra + 1u;
    }
}

/* Decodes a text frame body (encoding byte first) into `out`. */
static void decode_text(const uint8_t *data, size_t length, char *out,
                        size_t capacity)
{
    clear_text(out, capacity);
    if (length == 0u)
        return;
    const uint8_t encoding = data[0];
    data += 1;
    length -= 1u;
    switch (encoding) {
    case 0u:
        decode_latin1(data, length, out, capacity);
        break;
    case 1u:
        decode_utf16(data, length, out, capacity);
        break;
    case 2u:
        decode_utf16(data, length, out, capacity);
        break;
    case 3u:
        decode_utf8(data, length, out, capacity);
        break;
    default:
        break; /* unknown encoding: leave the field empty */
    }
}

/* "3/12" -> 3; "0" and garbage -> 0. */
static uint16_t parse_index(const uint8_t *data, size_t length)
{
    char text[16];
    decode_latin1(data, length, text, sizeof(text));
    uint32_t value = 0u;
    size_t index = 0u;
    while (text[index] == ' ')
        ++index;
    int digits = 0;
    for (; text[index] >= '0' && text[index] <= '9' && digits < 6; ++index) {
        value = value * 10u + (uint32_t)(text[index] - '0');
        ++digits;
    }
    if (digits == 0 || value > 0xffffu)
        return 0u;
    return (uint16_t)value;
}

static void parse_frame(const uint8_t *id, const uint8_t *body, size_t length,
                        struct helikon_id3v2_tags *tags)
{
    if (tag_is(id, "TIT2", 4u) || tag_is(id, "TT2", 3u)) {
        decode_text(body, length, tags->title, sizeof(tags->title));
    } else if (tag_is(id, "TPE1", 4u) || tag_is(id, "TP1", 3u)) {
        decode_text(body, length, tags->artist, sizeof(tags->artist));
    } else if (tag_is(id, "TALB", 4u) || tag_is(id, "TAL", 3u)) {
        decode_text(body, length, tags->album, sizeof(tags->album));
    } else if (tag_is(id, "TPE2", 4u) || tag_is(id, "TP2", 3u)) {
        decode_text(body, length, tags->album_artist,
                    sizeof(tags->album_artist));
    } else if (tag_is(id, "TCON", 4u) || tag_is(id, "TCO", 3u)) {
        decode_text(body, length, tags->genre, sizeof(tags->genre));
    } else if (tag_is(id, "TYER", 4u) || tag_is(id, "TDRC", 4u) ||
               tag_is(id, "TYE", 3u)) {
        decode_text(body, length, tags->year, sizeof(tags->year));
    } else if (tag_is(id, "TRCK", 4u) || tag_is(id, "TRK", 3u)) {
        if (length > 1u)
            tags->track = parse_index(body + 1u, length - 1u);
    } else if (tag_is(id, "TPOS", 4u) || tag_is(id, "TPA", 3u)) {
        if (length > 1u)
            tags->disc = parse_index(body + 1u, length - 1u);
    }
}

size_t helikon_id3v2_tag_size(const uint8_t *data, size_t size)
{
    if (data == NULL || size < 10u)
        return 0u;
    if (!tag_is(data, "ID3", 3u) || data[3] < 2u || data[3] > 4u)
        return 0u;
    if (data[6] >= 0x80u || data[7] >= 0x80u || data[8] >= 0x80u ||
        data[9] >= 0x80u)
        return 0u;
    size_t total = 10u + (size_t)syncsafe32(data + 6);
    if ((data[5] & 0x10u) != 0u)
        total += 10u; /* footer */
    return total;
}

int helikon_id3v2_parse(const uint8_t *data, size_t size,
                        struct helikon_id3v2_tags *tags)
{
    if (data == NULL || tags == NULL)
        return HELIKON_ERR_ARGUMENT;
    helikon_memset(tags, 0, sizeof(*tags));

    const size_t total = helikon_id3v2_tag_size(data, size);
    if (total == 0u)
        return HELIKON_ERR_NOT_FOUND;
    if (total > size)
        return HELIKON_ERR_FORMAT;

    const uint8_t version = data[3];
    const uint8_t flags = data[5];
    /* Unsynchronization, compression and encryption are frame-level rewrites
     * this reader does not decode; say so instead of returning half a tag. */
    if ((flags & 0x80u) != 0u || (flags & 0x40u) != 0u)
        return HELIKON_ERR_UNSUPPORTED;

    tags->version = version;
    size_t offset = 10u;
    size_t end = total;
    if (version == 4u && (flags & 0x10u) != 0u)
        end -= 10u; /* footer is not part of the frame area */

    if ((flags & 0x20u) != 0u) {
        /* Extended header: size excludes its own 4-byte length in v2.3 and
         * includes it in v2.4. */
        if (offset + 4u > end)
            return HELIKON_ERR_FORMAT;
        const uint32_t ext = version == 4u ? syncsafe32(data + offset)
                                           : be32(data + offset) + 4u;
        if (ext > end - offset)
            return HELIKON_ERR_FORMAT;
        offset += ext;
    }

    const size_t header = version == 2u ? FRAME_HEADER_V22 : FRAME_HEADER;
    while (offset + header <= end) {
        const uint8_t *frame = data + offset;
        if (frame[0] == 0u)
            break; /* padding */
        size_t length;
        size_t body_offset;
        if (version == 2u) {
            length = ((size_t)frame[3] << 16) | ((size_t)frame[4] << 8) |
                     frame[5];
            body_offset = offset + FRAME_HEADER_V22;
        } else if (version == 4u) {
            length = syncsafe32(frame + 4);
            body_offset = offset + FRAME_HEADER;
        } else {
            length = be32(frame + 4);
            body_offset = offset + FRAME_HEADER;
        }
        if (length == 0u || body_offset > end || length > end - body_offset)
            return HELIKON_ERR_FORMAT;

        /* v2.3/v2.4 frame flags: compressed, encrypted or grouped frames are
         * skipped rather than misread as text. */
        if (version == 3u && (frame[9] & 0xe0u) != 0u)
            return HELIKON_ERR_UNSUPPORTED; /* compression/encryption/grouping */
        if (version == 4u && (frame[9] & 0x4cu) != 0u)
            return HELIKON_ERR_UNSUPPORTED; /* grouping/compression/encryption */

        parse_frame(frame, data + body_offset, length, tags);
        offset = body_offset + length;
    }
    return HELIKON_OK;
}
