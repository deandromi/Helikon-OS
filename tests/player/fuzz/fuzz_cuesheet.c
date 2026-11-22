/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libFuzzer target for the cue sheet reader.
 *
 * A cue sheet is a text file from a download or a hand edit, and every number in
 * it is a seek position: a frame offset past the end of the audio is not a crash,
 * it is a track that plays silence, which is worse. The invariants, beyond "does
 * not crash":
 *
 *   - a successful parse has at least one track and at most
 *     `HELIKON_CUESHEET_MAX_TRACKS`;
 *   - every track belongs to a file that exists (`file_index < file_count`);
 *   - starts never go backwards and a pregap never follows its own start, which
 *     is the order the player walks;
 *   - `complete` is exactly `refused == 0`;
 *   - every returned string points inside the input buffer and inside its own
 *     length limit — a pointer into a bounds-checked slice is the whole reason
 *     this parser does not copy;
 *   - a refused parse reports no tracks, so a caller cannot play half a sheet.
 *
 *   ./tools/fuzz.sh cuesheet 300
 *   ./tools/fuzz.sh --replay cuesheet
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cuesheet.h"
#include "helikon_error.h"

static void fail(const char *what)
{
    fprintf(stderr, "fuzz: %s\n", what);
    abort();
}

/* A string that was returned as a pointer: inside the buffer, inside its cap. */
static void check_text(const char *text, const char *begin, size_t length,
                       uint16_t reported, uint16_t limit, const char *what)
{
    (void)text;
    if (reported == 0u) {
        if (begin != NULL)
            fail(what);
        return;
    }
    if (begin == NULL)
        fail(what);
    if (begin < text || begin + reported > text + length)
        fail(what);
    if (reported > limit)
        fail(what);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* The parser takes `char *`, and fuzz data is not a C string. */
    if (size > 64u * 1024u)
        return 0;

    struct helikon_cuesheet cue;
    int rc = helikon_cuesheet_parse(&cue, (const char *)data, size);
    if (rc == HELIKON_ERR_ARGUMENT)
        fail("a non-null buffer was reported as an argument error");
    if (rc != HELIKON_OK && rc != HELIKON_ERR_FORMAT)
        fail("parse returned something other than OK or FORMAT");

    if (rc == HELIKON_ERR_FORMAT) {
        if (cue.track_count != 0u)
            fail("a refused sheet still reports tracks");
        return 0;
    }

    if (cue.track_count == 0u || cue.track_count > HELIKON_CUESHEET_MAX_TRACKS)
        fail("a successful parse has an impossible track count");
    if (cue.file_count == 0u || cue.file_count > HELIKON_CUESHEET_MAX_FILES)
        fail("a successful parse has an impossible file count");
    if (cue.complete != (uint8_t)(cue.refused == 0u))
        fail("complete and refused disagree");

    for (uint16_t index = 0u; index < cue.file_count; ++index) {
        check_text((const char *)data, cue.files[index].path, size,
                   cue.files[index].path_length, HELIKON_CUESHEET_PATH_MAX,
                   "a file path is outside the buffer");
    }

    uint32_t previous_start = 0u;
    for (uint16_t index = 0u; index < cue.track_count; ++index) {
        const struct helikon_cuesheet_track *track = &cue.tracks[index];
        if (track->number == 0u || track->number > 99u)
            fail("a track number outside the disc");
        if (track->file_index >= cue.file_count)
            fail("a track belongs to a file that does not exist");
        if (index != 0u && track->start_frame < previous_start)
            fail("track starts go backwards");
        if (track->pregap_frame > track->start_frame)
            fail("a pregap after its own start");
        previous_start = track->start_frame;
        check_text((const char *)data, track->title, size, track->title_length,
                   HELIKON_CUESHEET_FIELD_MAX,
                   "a title is outside the buffer");
        check_text((const char *)data, track->performer, size,
                   track->performer_length, HELIKON_CUESHEET_FIELD_MAX,
                   "a performer is outside the buffer");
    }
    return 0;
}
