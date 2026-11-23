/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Resume bookmark text (T-093). See `bookmark.h` for the format and the rules.
 *
 * Nothing here allocates, and nothing here trusts the file: a bookmark is a
 * claim about where a track was when the power went, and resuming at a wrong
 * second is worse than not resuming at all.
 */
#include "bookmark.h"

#include "format.h"
#include "helikon_error.h"
#include "mem.h"

#define BOOKMARK_MAGIC "helikon-bookmark 1"
#define BOOKMARK_KEY_PATH "path"
#define BOOKMARK_KEY_POSITION "position_ms"
#define BOOKMARK_KEY_DURATION "duration_ms"
#define BOOKMARK_KEY_TRACK "track_id"
#define BOOKMARK_KEY_SAVED "saved_at"

/* Which optional keys a record carried, so a duplicate can be refused. */
#define SEEN_PATH 0x01u
#define SEEN_POSITION 0x02u
#define SEEN_DURATION 0x04u
#define SEEN_TRACK 0x08u
#define SEEN_SAVED 0x10u

static int path_ok(const char *path)
{
    if (path == NULL || path[0] != '/')
        return 0;
    for (size_t index = 0u; index < HELIKON_BOOKMARK_PATH_MAX; ++index) {
        unsigned char byte = (unsigned char)path[index];
        if (byte == 0u)
            return index > 1u; /* a root-only path names no track */
        if (byte < 0x20u || byte == 0x7fu)
            return 0;
    }
    /* No terminator inside the limit: longer than any track path we accept. */
    return 0;
}

static int key_is(const char *key, size_t key_length, const char *name)
{
    size_t index = 0u;
    for (; index < key_length; ++index) {
        if (name[index] == '\0' || name[index] != key[index])
            return 0;
    }
    return name[index] == '\0';
}

/* One decimal `uint32_t`, no sign, no whitespace, no overflow. */
static int parse_u32(const char *text, size_t length, uint32_t *value)
{
    if (length == 0u)
        return HELIKON_ERR_FORMAT;
    uint32_t result = 0u;
    for (size_t index = 0u; index < length; ++index) {
        unsigned char byte = (unsigned char)text[index];
        if (byte < (unsigned char)'0' || byte > (unsigned char)'9')
            return HELIKON_ERR_FORMAT;
        uint32_t digit = (uint32_t)(byte - (unsigned char)'0');
        if (result > (0xffffffffu - digit) / 10u)
            return HELIKON_ERR_FORMAT;
        result = result * 10u + digit;
    }
    *value = result;
    return HELIKON_OK;
}

size_t helikon_bookmark_write(const struct helikon_bookmark *bookmark, char *out,
                              size_t size)
{
    if (bookmark == NULL || out == NULL || size == 0u)
        return 0u;
    out[0] = '\0';
    if (!path_ok(bookmark->path))
        return 0u;

    size_t path_length = 0u;
    while (bookmark->path[path_length] != '\0')
        path_length += 1u;

    /*
     * Every number gets its worst case, so the size check does not need the
     * digits twice and a short buffer can never be filled halfway.
     */
    const size_t number_max = 10u;
    size_t needed = (sizeof(BOOKMARK_MAGIC) - 1u) + 1u;
    needed += (sizeof(BOOKMARK_KEY_PATH) - 1u) + 1u + path_length + 1u;
    needed += (sizeof(BOOKMARK_KEY_POSITION) - 1u) + 1u + number_max + 1u;
    needed += (sizeof(BOOKMARK_KEY_DURATION) - 1u) + 1u + number_max + 1u;
    needed += (sizeof(BOOKMARK_KEY_TRACK) - 1u) + 1u + number_max + 1u;
    needed += (sizeof(BOOKMARK_KEY_SAVED) - 1u) + 1u + number_max + 1u;
    if (needed > HELIKON_BOOKMARK_TEXT_MAX || size < needed + 1u)
        return 0u;

    size_t at = 0u;
    for (size_t index = 0u; index < sizeof(BOOKMARK_MAGIC) - 1u; ++index)
        out[at++] = BOOKMARK_MAGIC[index];
    out[at++] = '\n';

    for (size_t index = 0u; index < sizeof(BOOKMARK_KEY_PATH) - 1u; ++index)
        out[at++] = BOOKMARK_KEY_PATH[index];
    out[at++] = '=';
    for (size_t index = 0u; index < path_length; ++index)
        out[at++] = bookmark->path[index];
    out[at++] = '\n';

    struct {
        const char *key;
        size_t key_length;
        uint32_t value;
    } fields[4] = {
        { BOOKMARK_KEY_POSITION, sizeof(BOOKMARK_KEY_POSITION) - 1u,
          bookmark->position_ms },
        { BOOKMARK_KEY_DURATION, sizeof(BOOKMARK_KEY_DURATION) - 1u,
          bookmark->duration_ms },
        { BOOKMARK_KEY_TRACK, sizeof(BOOKMARK_KEY_TRACK) - 1u,
          bookmark->track_id },
        { BOOKMARK_KEY_SAVED, sizeof(BOOKMARK_KEY_SAVED) - 1u,
          bookmark->saved_at },
    };
    for (size_t field = 0u; field < 4u; ++field) {
        for (size_t index = 0u; index < fields[field].key_length; ++index)
            out[at++] = fields[field].key[index];
        out[at++] = '=';
        size_t digits = helikon_format_u32(out + at, size - at,
                                           fields[field].value);
        if (digits == 0u) {
            out[0] = '\0';
            return 0u;
        }
        at += digits;
        out[at++] = '\n';
    }
    out[at] = '\0';
    return at;
}

int helikon_bookmark_parse(struct helikon_bookmark *bookmark, const char *text,
                           size_t length)
{
    if (bookmark == NULL)
        return HELIKON_ERR_ARGUMENT;
    helikon_memset(bookmark, 0, sizeof(*bookmark));
    if (text == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (length == 0u || length > HELIKON_BOOKMARK_TEXT_MAX)
        return HELIKON_ERR_FORMAT;
    /* We wrote this file; a NUL, a CR or a missing final LF means we did not. */
    for (size_t index = 0u; index < length; ++index) {
        if (text[index] == '\0' || text[index] == '\r')
            goto refuse;
    }
    if (text[length - 1u] != '\n')
        goto refuse;

    unsigned int seen = 0u;
    size_t offset = 0u;
    unsigned int line = 0u;
    while (offset < length) {
        size_t end = offset;
        while (text[end] != '\n')
            end += 1u;
        size_t line_length = end - offset;
        if (line_length == 0u)
            goto refuse;
        if (line == 0u) {
            if (line_length != sizeof(BOOKMARK_MAGIC) - 1u)
                goto refuse;
            for (size_t index = 0u; index < line_length; ++index) {
                if (text[offset + index] != BOOKMARK_MAGIC[index])
                    goto refuse;
            }
        } else {
            size_t split = offset;
            while (split < end && text[split] != '=')
                split += 1u;
            if (split == end)
                goto refuse;
            size_t key_length = split - offset;
            const char *key = text + offset;
            const char *value = text + split + 1u;
            size_t value_length = end - split - 1u;
            if (key_length == 0u || value_length == 0u)
                goto refuse;

            if (key_is(key, key_length, BOOKMARK_KEY_PATH)) {
                if ((seen & SEEN_PATH) != 0u ||
                    value_length >= HELIKON_BOOKMARK_PATH_MAX)
                    goto refuse;
                for (size_t index = 0u; index < value_length; ++index)
                    bookmark->path[index] = value[index];
                bookmark->path[value_length] = '\0';
                if (!path_ok(bookmark->path))
                    goto refuse;
                seen |= SEEN_PATH;
            } else {
                uint32_t number = 0u;
                if (parse_u32(value, value_length, &number) != HELIKON_OK)
                    goto refuse;
                unsigned int bit = 0u;
                uint32_t *slot = NULL;
                if (key_is(key, key_length, BOOKMARK_KEY_POSITION)) {
                    bit = SEEN_POSITION;
                    slot = &bookmark->position_ms;
                } else if (key_is(key, key_length, BOOKMARK_KEY_DURATION)) {
                    bit = SEEN_DURATION;
                    slot = &bookmark->duration_ms;
                } else if (key_is(key, key_length, BOOKMARK_KEY_TRACK)) {
                    bit = SEEN_TRACK;
                    slot = &bookmark->track_id;
                } else if (key_is(key, key_length, BOOKMARK_KEY_SAVED)) {
                    bit = SEEN_SAVED;
                    slot = &bookmark->saved_at;
                } else {
                    goto refuse; /* a key we never write */
                }
                if ((seen & bit) != 0u)
                    goto refuse;
                *slot = number;
                seen |= bit;
            }
        }
        line += 1u;
        offset = end + 1u;
    }
    if (line < 2u || (seen & (SEEN_PATH | SEEN_POSITION)) !=
                         (SEEN_PATH | SEEN_POSITION))
        goto refuse;
    return HELIKON_OK;

refuse:
    helikon_memset(bookmark, 0, sizeof(*bookmark));
    return HELIKON_ERR_FORMAT;
}
