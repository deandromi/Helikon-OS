/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "playlist.h"

#include <stddef.h>
#include <stdint.h>

#include "format.h"
#include "helikon_error.h"
#include "mem.h"

#define LINE_MAX (HELIKON_PLAYLIST_PATH_MAX + HELIKON_PLAYLIST_TITLE_MAX + 64u)

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

static size_t text_length(const char *text, size_t limit)
{
    size_t length = 0u;
    while (length < limit && text[length] != '\0')
        ++length;
    return length;
}

/* Case-insensitive comparison of `length` bytes; both sides must be ASCII. */
static int ascii_iequal_n(const char *a, const char *b, size_t length)
{
    for (size_t index = 0u; index < length; ++index) {
        char left = a[index];
        char right = b[index];
        if (left >= 'A' && left <= 'Z')
            left = (char)(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z')
            right = (char)(right - 'A' + 'a');
        if (left != right)
            return 0;
    }
    return 1;
}

/*
 * Length of a UTF-8 sequence starting at `lead`, or 0 for a byte that cannot
 * start one. The checks are the ones a name has to pass to be storable: no
 * overlong form, no surrogate half, nothing past U+10FFFF and no control
 * character (a tab or a newline inside a path is a symptom, not a name).
 */
static size_t utf8_sequence_length(const uint8_t *text, size_t available)
{
    uint8_t lead = text[0];
    size_t extra;
    uint32_t code;
    if (lead < 0x80u) {
        if (lead < 0x20u || lead == 0x7fu)
            return 0u;
        return 1u;
    } else if ((lead & 0xe0u) == 0xc0u) {
        code = lead & 0x1fu;
        extra = 1u;
    } else if ((lead & 0xf0u) == 0xe0u) {
        code = lead & 0x0fu;
        extra = 2u;
    } else if ((lead & 0xf8u) == 0xf0u) {
        code = lead & 0x07u;
        extra = 3u;
    } else {
        return 0u;
    }
    if (extra >= available)
        return 0u;
    for (size_t index = 1u; index <= extra; ++index) {
        uint8_t next = text[index];
        if ((next & 0xc0u) != 0x80u)
            return 0u;
        code = (code << 6) | (uint32_t)(next & 0x3fu);
    }
    if ((extra == 1u && code < 0x80u) || (extra == 2u && code < 0x800u) ||
        (extra == 3u && code < 0x10000u))
        return 0u;
    if (code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu))
        return 0u;
    if (code < 0x20u || code == 0x7fu)
        return 0u;
    return extra + 1u;
}

static int valid_utf8(const char *text, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)text;
    size_t index = 0u;
    while (index < length) {
        size_t step = utf8_sequence_length(bytes + index, length - index);
        if (step == 0u)
            return 0;
        index += step;
    }
    return 1;
}

/*
 * Resolve one playlist path against the playlist's folder.
 *
 * Absolute entries start from the root; everything else starts where the
 * playlist is. `.` is dropped, `..` pops a component and is refused at the
 * root, and empty components (a doubled slash) are separators, not names.
 */
static int resolve_path(const char *base, const char *entry, size_t entry_length,
                        char *out, size_t out_size)
{
    size_t used = 0u;
    size_t index = 0u;

    if (entry_length == 0u)
        return HELIKON_ERR_ARGUMENT;
    if (!valid_utf8(entry, entry_length))
        return HELIKON_ERR_NAME_INVALID;

    if (entry[0] == '/') {
        out[used++] = '/';
    } else {
        size_t base_length = text_length(base, HELIKON_PLAYLIST_BASE_MAX);
        if (base_length == 0u || base[0] != '/')
            return HELIKON_ERR_ARGUMENT;
        if (base_length >= out_size - 1u)
            return HELIKON_ERR_BUFFER;
        helikon_memcpy(out, base, base_length);
        used = base_length;
    }

    while (index < entry_length) {
        while (index < entry_length && entry[index] == '/')
            ++index;
        size_t start = index;
        while (index < entry_length && entry[index] != '/')
            ++index;
        size_t component = index - start;
        if (component == 0u)
            continue;
        if (component == 1u && entry[start] == '.')
            continue;
        if (component == 2u && entry[start] == '.' && entry[start + 1u] == '.') {
            if (used <= 1u)
                return HELIKON_ERR_NAME_INVALID; /* escapes the root */
            while (used > 1u && out[used - 1u] != '/')
                --used;
            if (used > 1u)
                --used;
            continue;
        }
        if (used + 1u + component + 1u > out_size)
            return HELIKON_ERR_BUFFER;
        if (used == 0u || out[used - 1u] != '/')
            out[used++] = '/';
        helikon_memcpy(out + used, entry + start, component);
        used += component;
    }

    if (used == 0u)
        return HELIKON_ERR_NAME_INVALID;
    out[used] = '\0';
    return HELIKON_OK;
}

/* --------------------------------------------------------------- PLS keys */

struct pls_key {
    size_t value_offset;
    size_t value_length;
    int present;
};

/*
 * Walk the buffer for `key` + a decimal number + '='. Case-insensitive on the
 * key, exact on the number, and the value is the trimmed rest of that line. The
 * first occurrence wins, which is what INI readers do with a duplicated key.
 *
 * `want_number` of UINT32_MAX means "any number", and `number` is then filled in
 * when a key is found; that is how the pre-scan finds the highest index.
 */
static int pls_find(const struct helikon_playlist_parser *parser, const char *key,
                    uint32_t want_number, struct pls_key *result,
                    uint32_t *number)
{
    result->present = 0;
    result->value_offset = 0u;
    result->value_length = 0u;

    size_t key_length = text_length(key, 16u);
    size_t offset = 0u;
    while (offset < parser->length) {
        size_t start = offset;
        while (offset < parser->length && parser->text[offset] != '\n')
            ++offset;
        size_t end = offset;
        if (end > start && parser->text[end - 1u] == '\r')
            --end;
        ++offset; /* past the newline */

        if (end - start < key_length + 2u)
            continue;
        if (!ascii_iequal_n(parser->text + start, key, key_length))
            continue;
        char digit = parser->text[start + key_length];
        if (digit < '0' || digit > '9')
            continue;

        size_t cursor = start + key_length;
        uint32_t seen = 0u;
        int overflow = 0;
        while (cursor < end && parser->text[cursor] >= '0' &&
               parser->text[cursor] <= '9') {
            if (seen > 429496729u / 10u)
                overflow = 1;
            else
                seen = seen * 10u + (uint32_t)(parser->text[cursor] - '0');
            ++cursor;
        }
        if (cursor >= end || parser->text[cursor] != '=')
            continue;
        if (overflow)
            continue;
        if (want_number != UINT32_MAX && seen != want_number)
            continue;

        size_t value_start = cursor + 1u;
        size_t value_end = end;
        while (value_start < value_end && is_space(parser->text[value_start]))
            ++value_start;
        while (value_end > value_start && is_space(parser->text[value_end - 1u]))
            --value_end;
        result->present = 1;
        result->value_offset = value_start;
        result->value_length = value_end - value_start;
        if (number != NULL)
            *number = seen;
        return HELIKON_OK;
    }
    return HELIKON_OK;
}

/* Highest `FileN=` index in the buffer, found in one pass. */
static uint32_t pls_highest_index(const struct helikon_playlist_parser *parser)
{
    uint32_t highest = 0u;
    size_t offset = 0u;
    while (offset < parser->length) {
        size_t start = offset;
        while (offset < parser->length && parser->text[offset] != '\n')
            ++offset;
        size_t end = offset;
        if (end > start && parser->text[end - 1u] == '\r')
            --end;
        ++offset;
        if (end - start < 7u || !ascii_iequal_n(parser->text + start, "File", 4u))
            continue;
        size_t cursor = start + 4u;
        uint32_t seen = 0u;
        int digits = 0;
        int overflow = 0;
        while (cursor < end && parser->text[cursor] >= '0' &&
               parser->text[cursor] <= '9') {
            if (seen > 429496729u / 10u)
                overflow = 1;
            else
                seen = seen * 10u + (uint32_t)(parser->text[cursor] - '0');
            ++cursor;
            ++digits;
        }
        if (digits == 0 || overflow || cursor >= end ||
            parser->text[cursor] != '=')
            continue;
        if (seen > highest)
            highest = seen;
    }
    return highest;
}

/* ---------------------------------------------------------------- parsing */

static uint32_t parse_seconds(const char *text, size_t length, int *ok)
{
    size_t index = 0u;
    uint32_t value = 0u;
    int digits = 0;
    while (index < length && text[index] >= '0' && text[index] <= '9') {
        if (value > 429496729u / 10u) {
            *ok = 0;
            return 0u;
        }
        value = value * 10u + (uint32_t)(text[index] - '0');
        ++index;
        ++digits;
    }
    if (digits == 0) {
        *ok = 0;
        return 0u;
    }
    /* A fractional part is allowed and truncated: 215.42 s is 215 s. */
    if (index < length && text[index] == '.') {
        ++index;
        int fraction = 0;
        while (index < length && text[index] >= '0' && text[index] <= '9') {
            ++index;
            ++fraction;
        }
        if (fraction == 0) {
            *ok = 0;
            return 0u;
        }
    }
    while (index < length && is_space(text[index]))
        ++index;
    if (index != length) {
        *ok = 0;
        return 0u;
    }
    *ok = 1;
    return value;
}

static void clear_pending(struct helikon_playlist_parser *parser)
{
    parser->pending_title[0] = '\0';
    parser->pending_duration = 0u;
    parser->pending_kind = 0u;
}

/* `#EXTINF:<seconds>,<title>`: the only directive that carries anything. */
static void parse_extinf(struct helikon_playlist_parser *parser,
                         const char *line, size_t length)
{
    clear_pending(parser);
    if (length < 8u || line[7] != ':')
        return;
    size_t comma = 8u;
    while (comma < length && line[comma] != ',')
        ++comma;
    if (comma == length)
        return;

    int ok = 0;
    uint32_t seconds = parse_seconds(line + 8u, comma - 8u, &ok);
    if (ok) {
        parser->pending_duration = seconds;
        parser->pending_kind |= 2u;
    }

    size_t title_start = comma + 1u;
    size_t title_end = length;
    while (title_start < title_end && is_space(line[title_start]))
        ++title_start;
    while (title_end > title_start && is_space(line[title_end - 1u]))
        --title_end;
    size_t title_length = title_end - title_start;
    if (title_length == 0u || title_length >= sizeof(parser->pending_title))
        return;
    if (!valid_utf8(line + title_start, title_length))
        return;
    helikon_memcpy(parser->pending_title, line + title_start, title_length);
    parser->pending_title[title_length] = '\0';
    parser->pending_kind |= 1u;
}

/*
 * Resolve one path and fill the entry. Returns 0 when the entry was produced and
 * 1 when it was refused (counted in `skipped`), because one bad line does not
 * invalidate the rest of a playlist.
 */
static int emit_entry(struct helikon_playlist_parser *parser, const char *path,
                      size_t path_length, struct helikon_playlist_entry *entry)
{
    char resolved[HELIKON_PLAYLIST_PATH_MAX];
    int rc = resolve_path(parser->base, path, path_length, resolved,
                          sizeof(resolved));
    if (rc != HELIKON_OK) {
        ++parser->skipped;
        return 1;
    }

    helikon_memset(entry, 0, sizeof(*entry));
    size_t length = text_length(resolved, sizeof(resolved));
    helikon_memcpy(entry->path, resolved, length + 1u);
    if ((parser->pending_kind & 1u) != 0u) {
        size_t title_length =
            text_length(parser->pending_title, sizeof(parser->pending_title));
        helikon_memcpy(entry->title, parser->pending_title, title_length + 1u);
        entry->has_title = 1u;
    }
    if ((parser->pending_kind & 2u) != 0u) {
        entry->duration_s = parser->pending_duration;
        entry->has_duration = 1u;
    }
    clear_pending(parser);
    ++parser->emitted;
    return 0;
}

static int next_m3u(struct helikon_playlist_parser *parser,
                    struct helikon_playlist_entry *entry)
{
    while (parser->offset < parser->length) {
        size_t start = parser->offset;
        size_t end = start;
        while (end < parser->length && parser->text[end] != '\n')
            ++end;
        parser->offset = end < parser->length ? end + 1u : end;
        if (end > start && parser->text[end - 1u] == '\r')
            --end;

        size_t first = start;
        size_t last = end;
        while (first < last && is_space(parser->text[first]))
            ++first;
        while (last > first && is_space(parser->text[last - 1u]))
            --last;
        size_t length = last - first;
        if (length == 0u)
            continue;

        const char *line = parser->text + first;
        if (line[0] == '#') {
            if (length >= 7u && ascii_iequal_n(line, "#EXTINF", 7u))
                parse_extinf(parser, line, length);
            continue;
        }
        if (length >= LINE_MAX) {
            ++parser->skipped;
            clear_pending(parser);
            continue;
        }
        if (emit_entry(parser, line, length, entry) == 0)
            return 1;
    }
    return 0;
}

static int next_pls(struct helikon_playlist_parser *parser,
                    struct helikon_playlist_entry *entry)
{
    while (parser->pls_index <= parser->pls_count) {
        uint32_t number = parser->pls_index++;
        struct pls_key file;
        int rc = pls_find(parser, "File", number, &file, NULL);
        if (rc != HELIKON_OK)
            return rc;
        if (!file.present)
            continue;

        clear_pending(parser);
        struct pls_key title;
        rc = pls_find(parser, "Title", number, &title, NULL);
        if (rc != HELIKON_OK)
            return rc;
        if (title.present && title.value_length > 0u &&
            title.value_length < sizeof(parser->pending_title) &&
            valid_utf8(parser->text + title.value_offset, title.value_length)) {
            helikon_memcpy(parser->pending_title,
                           parser->text + title.value_offset,
                           title.value_length);
            parser->pending_title[title.value_length] = '\0';
            parser->pending_kind |= 1u;
        }

        struct pls_key length;
        rc = pls_find(parser, "Length", number, &length, NULL);
        if (rc != HELIKON_OK)
            return rc;
        if (length.present) {
            int ok = 0;
            uint32_t seconds = parse_seconds(parser->text + length.value_offset,
                                             length.value_length, &ok);
            if (ok) {
                parser->pending_duration = seconds;
                parser->pending_kind |= 2u;
            }
        }

        if (emit_entry(parser, parser->text + file.value_offset,
                       file.value_length, entry) == 0)
            return 1;
    }
    return 0;
}

int helikon_playlist_open(struct helikon_playlist_parser *parser,
                          const char *text, size_t length, const char *base,
                          uint8_t format)
{
    if (parser == NULL || (text == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    if (format != HELIKON_PLAYLIST_AUTO && format != HELIKON_PLAYLIST_M3U &&
        format != HELIKON_PLAYLIST_PLS)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(parser, 0, sizeof(*parser));
    parser->text = text;
    parser->length = length;

    const char *resolved_base = (base == NULL || base[0] == '\0') ? "/" : base;
    size_t base_length = text_length(resolved_base, HELIKON_PLAYLIST_BASE_MAX);
    if (base_length >= sizeof(parser->base))
        return HELIKON_ERR_BUFFER;
    helikon_memcpy(parser->base, resolved_base, base_length + 1u);
    if (parser->base[0] != '/')
        return HELIKON_ERR_ARGUMENT;

    for (size_t index = 0u; index < length; ++index) {
        if (text[index] == '\0')
            return HELIKON_ERR_FORMAT;
    }

    size_t start = 0u;
    if (length >= 3u && (uint8_t)text[0] == 0xefu && (uint8_t)text[1] == 0xbbu &&
        (uint8_t)text[2] == 0xbfu)
        start = 3u;
    parser->offset = start;

    if (format == HELIKON_PLAYLIST_AUTO) {
        size_t cursor = start;
        while (cursor < length && is_space(text[cursor]))
            ++cursor;
        format = (cursor + 10u <= length &&
                  ascii_iequal_n(text + cursor, "[playlist]", 10u))
                     ? HELIKON_PLAYLIST_PLS
                     : HELIKON_PLAYLIST_M3U;
    }
    parser->format = format;

    if (format == HELIKON_PLAYLIST_PLS) {
        /*
         * The scan index is bounded by the entry cap, not by the highest number
         * in the file: `File00000087161458=` is a valid line and iterating to it
         * one index at a time is 87 million scans for an answer that could only
         * ever be one of 512 entries. Indices above the cap are ignored, the
         * same as entries beyond it.
         */
        uint32_t highest = pls_highest_index(parser);
        if (highest > HELIKON_PLAYLIST_MAX_ENTRIES)
            highest = HELIKON_PLAYLIST_MAX_ENTRIES;
        parser->pls_count = highest;
        parser->pls_index = 1u;
    }
    return HELIKON_OK;
}

int helikon_playlist_next(struct helikon_playlist_parser *parser,
                          struct helikon_playlist_entry *entry)
{
    if (parser == NULL || entry == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (parser->emitted >= HELIKON_PLAYLIST_MAX_ENTRIES)
        return 0;
    if (parser->format == HELIKON_PLAYLIST_PLS)
        return next_pls(parser, entry);
    return next_m3u(parser, entry);
}

/* ------------------------------------------------------------------------- */
/* Export                                                                    */
/* ------------------------------------------------------------------------- */

/* Everything the writer needs to compose one line without allocating. */
#define WRITE_LINE_MAX (HELIKON_PLAYLIST_PATH_MAX + HELIKON_PLAYLIST_TITLE_MAX + 64u)

static int sink_write(struct helikon_playlist_writer *writer, const char *data,
                      size_t length)
{
    if (length == 0u)
        return HELIKON_OK;
    if (writer->sink->write == NULL)
        return HELIKON_ERR_ARGUMENT;
    return writer->sink->write(writer->sink->context, data, length);
}

static int sink_text(struct helikon_playlist_writer *writer, const char *text)
{
    return sink_write(writer, text, text_length(text, WRITE_LINE_MAX));
}

/* A path or title is refused, not repaired, when it cannot be written as text. */
static int text_is_writable(const char *text, size_t limit, int absolute)
{
    if (text == NULL)
        return 0;
    size_t length = text_length(text, limit);
    if (length == 0u || length >= limit)
        return 0;
    if (absolute != 0 && text[0] != '/')
        return 0;
    for (size_t index = 0u; index < length; ++index) {
        unsigned char byte = (unsigned char)text[index];
        /* No CR, LF, tab, NUL or any other control byte: each one ends or
         * mangles a line in every reader there is. */
        if (byte < 0x20u || byte == 0x7fu)
            return 0;
    }
    return 1;
}

int helikon_playlist_write_open(struct helikon_playlist_writer *writer,
                                const struct helikon_playlist_sink *sink,
                                uint8_t format, uint32_t entries)
{
    if (writer == NULL || sink == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (format == HELIKON_PLAYLIST_AUTO)
        format = HELIKON_PLAYLIST_M3U;
    if (format != HELIKON_PLAYLIST_M3U && format != HELIKON_PLAYLIST_PLS)
        return HELIKON_ERR_ARGUMENT;
    if (format == HELIKON_PLAYLIST_PLS &&
        entries > HELIKON_PLAYLIST_MAX_ENTRIES)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(writer, 0, sizeof(*writer));
    writer->sink = sink;
    writer->format = format;
    writer->opened = 1u;

    if (format == HELIKON_PLAYLIST_M3U)
        return sink_text(writer, "#EXTM3U\n");

    char line[WRITE_LINE_MAX];
    size_t used = 0u;
    static const char header[] = "[playlist]\nNumberOfEntries=";
    helikon_memcpy(line, header, sizeof(header) - 1u);
    used = sizeof(header) - 1u;
    used += helikon_format_u32(line + used, sizeof(line) - used, entries);
    line[used++] = '\n';
    return sink_write(writer, line, used);
}

static int write_m3u_entry(struct helikon_playlist_writer *writer,
                           const struct helikon_playlist_entry *entry)
{
    int rc = HELIKON_OK;
    if (entry->duration_s != 0u || entry->has_duration != 0u ||
        entry->has_title != 0u) {
        char line[WRITE_LINE_MAX];
        size_t used = 0u;
        static const char prefix[] = "#EXTINF:";
        helikon_memcpy(line, prefix, sizeof(prefix) - 1u);
        used = sizeof(prefix) - 1u;
        used += helikon_format_u32(line + used, sizeof(line) - used,
                                   entry->duration_s);
        line[used++] = ',';
        if (entry->has_title != 0u) {
            size_t length = text_length(entry->title, sizeof(entry->title));
            if (length + used + 2u > sizeof(line))
                return HELIKON_ERR_ARGUMENT;
            helikon_memcpy(line + used, entry->title, length);
            used += length;
        }
        line[used++] = '\n';
        rc = sink_write(writer, line, used);
        if (rc != HELIKON_OK)
            return rc;
    }
    rc = sink_write(writer, entry->path,
                    text_length(entry->path, sizeof(entry->path)));
    if (rc != HELIKON_OK)
        return rc;
    return sink_text(writer, "\n");
}

static int write_pls_entry(struct helikon_playlist_writer *writer,
                           const struct helikon_playlist_entry *entry)
{
    char line[WRITE_LINE_MAX];
    size_t used = 0u;
    uint32_t index = writer->written + 1u;

    static const char key[] = "File";
    helikon_memcpy(line, key, sizeof(key) - 1u);
    used = sizeof(key) - 1u;
    used += helikon_format_u32(line + used, sizeof(line) - used, index);
    line[used++] = '=';

    size_t length = text_length(entry->path, sizeof(entry->path));
    if (length + used + 2u > sizeof(line))
        return HELIKON_ERR_ARGUMENT;
    helikon_memcpy(line + used, entry->path, length);
    used += length;
    line[used++] = '\n';
    int rc = sink_write(writer, line, used);
    if (rc != HELIKON_OK)
        return rc;

    if (entry->has_title != 0u) {
        static const char title[] = "Title";
        helikon_memcpy(line, title, sizeof(title) - 1u);
        used = sizeof(title) - 1u;
        used += helikon_format_u32(line + used, sizeof(line) - used, index);
        line[used++] = '=';
        length = text_length(entry->title, sizeof(entry->title));
        if (length + used + 2u > sizeof(line))
            return HELIKON_ERR_ARGUMENT;
        helikon_memcpy(line + used, entry->title, length);
        used += length;
        line[used++] = '\n';
        rc = sink_write(writer, line, used);
        if (rc != HELIKON_OK)
            return rc;
    }

    if (entry->has_duration != 0u || entry->duration_s != 0u) {
        static const char length_key[] = "Length";
        helikon_memcpy(line, length_key, sizeof(length_key) - 1u);
        used = sizeof(length_key) - 1u;
        used += helikon_format_u32(line + used, sizeof(line) - used, index);
        line[used++] = '=';
        used += helikon_format_u32(line + used, sizeof(line) - used,
                                   entry->duration_s);
        line[used++] = '\n';
        rc = sink_write(writer, line, used);
        if (rc != HELIKON_OK)
            return rc;
    }
    return HELIKON_OK;
}

int helikon_playlist_write_entry(struct helikon_playlist_writer *writer,
                                 const struct helikon_playlist_entry *entry)
{
    if (writer == NULL || entry == NULL || writer->sink == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (writer->opened == 0u)
        return HELIKON_ERR_ARGUMENT;
    if (writer->written >= HELIKON_PLAYLIST_MAX_ENTRIES) {
        ++writer->refused;
        return HELIKON_ERR_ARGUMENT;
    }
    if (!text_is_writable(entry->path, sizeof(entry->path), 1) ||
        (entry->has_title != 0u &&
         !text_is_writable(entry->title, sizeof(entry->title), 0))) {
        ++writer->refused;
        return HELIKON_ERR_ARGUMENT;
    }

    int rc = writer->format == HELIKON_PLAYLIST_PLS
                 ? write_pls_entry(writer, entry)
                 : write_m3u_entry(writer, entry);
    if (rc != HELIKON_OK)
        return rc;
    ++writer->written;
    return HELIKON_OK;
}

int helikon_playlist_write_close(struct helikon_playlist_writer *writer)
{
    if (writer == NULL || writer->sink == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (writer->opened == 0u)
        return HELIKON_ERR_ARGUMENT;
    writer->opened = 0u;
    if (writer->format == HELIKON_PLAYLIST_PLS)
        return sink_text(writer, "Version=2\n");
    return HELIKON_OK;
}
