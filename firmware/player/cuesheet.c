/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "cuesheet.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"

/* No `FILE` has been read yet, or the one that was read could not be used. */
#define NO_FILE 0xFFFFu
/* No track is current: an `INDEX` here belongs to a `TRACK` that was refused. */
#define NO_TRACK 0xFFFFu
/* The pregap arrives as `INDEX 00`, and a track without one starts at `INDEX 01`. */
#define NO_PREGAP 0xFFFFFFFFu

struct parse_state {
    struct helikon_cuesheet *cue;
    uint16_t current;     /* index into `cue->tracks`, or NO_TRACK */
    uint16_t last_number; /* last accepted TRACK number, 0 before the first */
    uint16_t last_file;   /* index into `cue->files`, or NO_FILE */
    uint8_t start_seen[HELIKON_CUESHEET_MAX_TRACKS];
};

static int is_blank(char c)
{
    return c == ' ' || c == '\t';
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

static size_t word_length(const char *word)
{
    size_t length = 0u;
    while (word[length] != '\0')
        ++length;
    return length;
}

static int key_is(const char *key, size_t key_length, const char *word)
{
    size_t expected = word_length(word);
    return key_length == expected && ascii_iequal_n(key, word, key_length);
}

/* Digits only, no sign, no overflow: a track number is not a calculation. */
static int parse_uint(const char *begin, const char *end, uint32_t *out)
{
    if (begin == end)
        return 0;
    uint32_t value = 0u;
    for (const char *digits = begin; digits < end; ++digits) {
        char digit = *digits;
        if (digit < '0' || digit > '9')
            return 0;
        uint32_t step = (uint32_t)(digit - '0');
        if (value > (0xFFFFFFFFu - step) / 10u)
            return 0;
        value = value * 10u + step;
    }
    *out = value;
    return 1;
}

/*
 * `mm:ss:ff` to frames. Every field is required: a cue time with two fields is a
 * different format, and guessing which one was meant is how a track starts in
 * the wrong place. Seconds stop at 59 and frames at 74 because a value past them
 * cannot be a time at all.
 */
static int parse_time(const char *begin, const char *end, uint32_t *frames)
{
    uint32_t values[3] = {0u, 0u, 0u};
    size_t part = 0u;
    const char *field = begin;

    for (const char *scan = begin; scan <= end; ++scan) {
        if (scan != end && *scan != ':')
            continue;
        if (part >= 3u)
            return 0;
        if (!parse_uint(field, scan, &values[part]))
            return 0;
        ++part;
        field = scan + 1;
    }
    if (part != 3u)
        return 0;
    if (values[0] > 9999u)
        return 0;
    if (values[1] >= 60u || values[2] >= HELIKON_CUESHEET_FRAMES_PER_SECOND)
        return 0;
    *frames = (values[0] * 60u + values[1]) * HELIKON_CUESHEET_FRAMES_PER_SECOND +
              values[2];
    return 1;
}

/* The next blank-separated token at or after `begin`, bounded by `end`. */
static void next_token(const char **begin, const char *end, const char **token,
                       const char **token_end)
{
    const char *scan = *begin;
    while (scan < end && is_blank(*scan))
        ++scan;
    *token = scan;
    while (scan < end && !is_blank(*scan))
        ++scan;
    *token_end = scan;
    *begin = scan;
}

/*
 * A value is either quoted or the rest of the line. A quoted value ends at the
 * last quote on the line, not the first: a path may legitimately contain an
 * escaped quote, and the closing one is the last.
 */
static void value_span(const char *begin, const char *end, const char **value,
                       const char **value_end)
{
    const char *left = begin;
    const char *right = end;
    while (left < right && is_blank(*left))
        ++left;
    while (right > left && is_blank(right[-1]))
        --right;
    if (left < right && *left == '"') {
        const char *close = NULL;
        for (const char *scan = left + 1; scan < right; ++scan) {
            if (*scan == '"')
                close = scan;
        }
        if (close != NULL) {
            *value = left + 1;
            *value_end = close;
            return;
        }
    }
    *value = left;
    *value_end = right;
}

static int statement_file(struct parse_state *state, const char *begin,
                          const char *end)
{
    struct helikon_cuesheet *cue = state->cue;
    const char *path = NULL;
    const char *path_end = NULL;
    const char *scan = begin;

    /* `FILE "name" WAVE`: the name is quoted or a single token, then a type. */
    if (scan < end && *scan == '"') {
        value_span(scan, end, &path, &path_end);
    } else {
        next_token(&scan, end, &path, &path_end);
    }
    if (path == NULL || path_end <= path)
        return 0;
    if ((size_t)(path_end - path) > HELIKON_CUESHEET_PATH_MAX)
        return 0;
    if (cue->file_count >= HELIKON_CUESHEET_MAX_FILES)
        return 0;

    cue->files[cue->file_count].path = path;
    cue->files[cue->file_count].path_length = (uint16_t)(path_end - path);
    state->last_file = cue->file_count;
    ++cue->file_count;
    return 1;
}

static int statement_track(struct parse_state *state, const char *begin,
                           const char *end)
{
    struct helikon_cuesheet *cue = state->cue;
    const char *token = NULL;
    const char *token_end = NULL;
    const char *scan = begin;
    uint32_t number = 0u;

    next_token(&scan, end, &token, &token_end);
    if (!parse_uint(token, token_end, &number))
        return 0;
    if (number == 0u || number > 99u)
        return 0;
    if (state->last_number != 0u && number <= state->last_number)
        return 0;
    if (state->last_file == NO_FILE)
        return 0;
    if (cue->track_count >= HELIKON_CUESHEET_MAX_TRACKS)
        return 0;

    struct helikon_cuesheet_track *track = &cue->tracks[cue->track_count];
    helikon_memset(track, 0, sizeof(*track));
    track->number = (uint16_t)number;
    track->file_index = state->last_file;
    track->pregap_frame = NO_PREGAP;
    state->current = cue->track_count;
    state->last_number = (uint16_t)number;
    ++cue->track_count;
    return 1;
}

static int statement_index(struct parse_state *state, const char *begin,
                           const char *end)
{
    struct helikon_cuesheet *cue = state->cue;
    const char *token = NULL;
    const char *token_end = NULL;
    const char *scan = begin;
    uint32_t number = 0u;
    uint32_t frames = 0u;

    /*
     * An INDEX with no current track belongs to a TRACK that was already
     * refused and counted; counting its INDEX lines too would report one broken
     * statement as three. A stray INDEX at the top of a sheet is ignored for the
     * same reason, and a sheet with no track at all is FORMAT regardless.
     */
    if (state->current == NO_TRACK)
        return 1;
    next_token(&scan, end, &token, &token_end);
    if (!parse_uint(token, token_end, &number))
        return 0;
    while (scan < end && is_blank(*scan))
        ++scan;
    const char *time_end = end;
    while (time_end > scan && is_blank(time_end[-1]))
        --time_end;
    if (!parse_time(scan, time_end, &frames))
        return 0;

    struct helikon_cuesheet_track *track = &cue->tracks[state->current];
    if (number == 0u) {
        track->pregap_frame = frames;
    } else if (number == 1u) {
        track->start_frame = frames;
        state->start_seen[state->current] = 1u;
    }
    /* INDEX 02 and up are sub-indexes: they do not move a track's start. */
    return 1;
}

static void statement_text(struct parse_state *state, int performer,
                           const char *begin, const char *end)
{
    struct helikon_cuesheet *cue = state->cue;
    const char *value = NULL;
    const char *value_end = NULL;

    value_span(begin, end, &value, &value_end);
    if (value == NULL || value_end <= value)
        return;
    if ((size_t)(value_end - value) > HELIKON_CUESHEET_FIELD_MAX)
        return;

    const char **text;
    uint16_t *text_length;
    if (state->current != NO_TRACK) {
        struct helikon_cuesheet_track *track = &cue->tracks[state->current];
        text = performer != 0 ? &track->performer : &track->title;
        text_length = performer != 0 ? &track->performer_length
                                     : &track->title_length;
    } else if (cue->track_count == 0u) {
        /* Before the first TRACK, TITLE and PERFORMER describe the album. */
        text = performer != 0 ? &cue->performer : &cue->title;
        text_length = performer != 0 ? &cue->performer_length
                                     : &cue->title_length;
    } else {
        /* After a refused TRACK: the line belongs to no track and to no album. */
        return;
    }
    *text = value;
    *text_length = (uint16_t)(value_end - value);
}

/* One logical line; returns 0 when it was refused. */
static int statement(struct parse_state *state, const char *line,
                     const char *end)
{
    const char *scan = line;
    const char *key = NULL;
    const char *key_end = NULL;
    const char *value = NULL;
    const char *value_end = NULL;

    next_token(&scan, end, &key, &key_end);
    if (key == key_end)
        return 1; /* a blank line is not a mistake */
    value = scan;
    while (value < end && is_blank(*value))
        ++value;
    value_end = end;

    size_t key_length = (size_t)(key_end - key);
    if (key_is(key, key_length, "FILE")) {
        return statement_file(state, value, value_end);
    }
    if (key_is(key, key_length, "TRACK")) {
        if (!statement_track(state, value, value_end)) {
            /* Its INDEX lines must not attach to the previous track. */
            state->current = NO_TRACK;
            return 0;
        }
        return 1;
    }
    if (key_is(key, key_length, "INDEX")) {
        return statement_index(state, value, value_end);
    }
    if (key_is(key, key_length, "TITLE")) {
        statement_text(state, 0, value, value_end);
        return 1;
    }
    if (key_is(key, key_length, "PERFORMER")) {
        statement_text(state, 1, value, value_end);
        return 1;
    }
    /* CATALOG, ISRC, FLAGS, REM, CDTEXTFILE and the rest: valid cue, no start. */
    return 1;
}

/*
 * Drops the tracks that cannot be played and puts the rest in start order, which
 * is the order the sheet put them in already — a track whose start goes
 * backwards is the one that gets refused, not the album that gets reordered.
 */
static void finish(struct parse_state *state)
{
    struct helikon_cuesheet *cue = state->cue;
    uint16_t kept = 0u;
    uint32_t previous_start = 0u;

    for (uint16_t index = 0u; index < cue->track_count; ++index) {
        struct helikon_cuesheet_track track = cue->tracks[index];
        if (state->start_seen[index] == 0u) {
            ++cue->refused;
            continue;
        }
        if (kept != 0u && track.start_frame < previous_start) {
            ++cue->refused;
            continue;
        }
        if (track.pregap_frame == NO_PREGAP) {
            track.pregap_frame = track.start_frame;
        } else if (track.pregap_frame > track.start_frame) {
            /* A pregap after its own start is not a pregap. */
            ++cue->refused;
            track.pregap_frame = track.start_frame;
        }
        cue->tracks[kept] = track;
        previous_start = track.start_frame;
        ++kept;
    }
    cue->track_count = kept;
    cue->complete = (uint8_t)(cue->refused == 0u);
}

int helikon_cuesheet_parse(struct helikon_cuesheet *cue, const char *text,
                           size_t length)
{
    if (cue == NULL || text == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(cue, 0, sizeof(*cue));
    if (length == 0u)
        return HELIKON_ERR_FORMAT;

    /*
     * A NUL inside the buffer means the caller's length and the file disagree.
     * Parsing up to it would silently play a truncated album.
     */
    for (size_t index = 0u; index < length; ++index) {
        if (text[index] == '\0')
            return HELIKON_ERR_FORMAT;
    }

    struct parse_state state;
    helikon_memset(&state, 0, sizeof(state));
    state.cue = cue;
    state.current = NO_TRACK;
    state.last_file = NO_FILE;

    const char *cursor = text;
    const char *limit = text + length;
    /* A UTF-8 BOM is an encoding marker, not a key. */
    if (length >= 3u && (uint8_t)cursor[0] == 0xEFu &&
        (uint8_t)cursor[1] == 0xBBu && (uint8_t)cursor[2] == 0xBFu)
        cursor += 3;

    while (cursor < limit) {
        const char *line_end = cursor;
        while (line_end < limit && *line_end != '\n')
            ++line_end;
        const char *end = line_end;
        if (end > cursor && end[-1] == '\r')
            --end;
        if ((size_t)(end - cursor) > HELIKON_CUESHEET_LINE_MAX) {
            ++cue->refused;
        } else if (!statement(&state, cursor, end)) {
            ++cue->refused;
        }
        cursor = (line_end < limit) ? line_end + 1 : limit;
    }

    finish(&state);
    if (cue->track_count == 0u)
        return HELIKON_ERR_FORMAT;
    return HELIKON_OK;
}

uint32_t helikon_cuesheet_frame_to_ms(uint32_t frame)
{
    uint32_t seconds = frame / HELIKON_CUESHEET_FRAMES_PER_SECOND;
    uint32_t rest = frame % HELIKON_CUESHEET_FRAMES_PER_SECOND;
    return seconds * 1000u + rest * 1000u / HELIKON_CUESHEET_FRAMES_PER_SECOND;
}

int helikon_cuesheet_track_by_number(const struct helikon_cuesheet *cue,
                                     uint16_t number, uint16_t *index)
{
    if (cue == NULL || index == NULL)
        return HELIKON_ERR_ARGUMENT;
    for (uint16_t at = 0u; at < cue->track_count; ++at) {
        if (cue->tracks[at].number == number) {
            *index = at;
            return HELIKON_OK;
        }
    }
    return HELIKON_ERR_NOT_FOUND;
}
