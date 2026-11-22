/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cue sheet reader (T-093, the album half).
 *
 * A cue sheet is the reason a single FLAC file can hold twelve tracks: it names
 * the image and gives every track's start as `mm:ss:ff` in CD frames (75 to the
 * second, the number that comes from the Red Book, not from a hard drive). The
 * player can seek, so a cue sheet plus one image is a playable album — no
 * splitting, no second copy of the audio, no database table.
 *
 * This parser is bounded and allocation-free like the playlist importer, and it
 * makes the same bargain: a statement it cannot trust is *counted* and skipped,
 * not fatal, so one hand-edited line does not lose the other eleven tracks. It
 * refuses, rather than guesses:
 *
 *   - a `TRACK` number outside 1..99, or one that does not increase;
 *   - a `TRACK` before any `FILE` (its offsets would be relative to nothing);
 *   - an `mm:ss:ff` with seconds over 59 or frames over 74 — a time that cannot
 *     exist is not a rounding problem. An `INDEX` under a `TRACK` that was
 *     itself refused is ignored rather than counted again: one broken statement
 *     is one refusal, not three;
 *   - more than `HELIKON_CUESHEET_MAX_TRACKS` tracks or
 *     `HELIKON_CUESHEET_MAX_FILES` files;
 *   - a track that reaches the end of the sheet with no `INDEX 01`: its start is
 *     unknown, and playing it from frame zero would replay the album.
 *
 * Keys are matched case-insensitively because cue sheets are written by a dozen
 * different programs and most of them disagree about capitalisation. Unknown keys
 * (`CATALOG`, `ISRC`, `FLAGS`, `REM`, `CDTEXTFILE`, …) are ignored: they are
 * valid cue, and none of them changes where a track starts.
 *
 * The strings this returns point into the caller's `text` buffer — nothing is
 * copied, because a caller that has the file in memory already has the strings.
 * Two consequences, both deliberate: the buffer has to outlive the cuesheet, and
 * a `\"` inside a quoted value stays escaped, because unescaping would need a
 * copy per field that the 6G cannot spare. Quote marks inside cue titles are
 * rare enough that showing them verbatim is the honest trade.
 *
 * Only statements that matter to playback are interpreted: `FILE`, `TRACK`,
 * `INDEX`, `TITLE` and `PERFORMER`. `INDEX 00` is the pregap and `INDEX 01` the
 * start; `INDEX 02` and up are sub-indexes with no meaning for a player that
 * seeks to a track.
 */
#ifndef HELIKON_PLAYER_CUESHEET_H
#define HELIKON_PLAYER_CUESHEET_H

#include <stddef.h>
#include <stdint.h>

/* A CD holds 99 tracks and a cue sheet numbers a whole disc. */
#define HELIKON_CUESHEET_MAX_TRACKS 99u
/* A cue sheet for a single image names one file; a hand-built set names a few. */
#define HELIKON_CUESHEET_MAX_FILES 16u
/* Longer than any path this project stores, and long enough for a quoted one. */
#define HELIKON_CUESHEET_PATH_MAX 512u
#define HELIKON_CUESHEET_FIELD_MAX 512u
/* Frames per second in the cue time format, from the CD, not from the disk. */
#define HELIKON_CUESHEET_FRAMES_PER_SECOND 75u
/* A line longer than this is not a statement anyone wrote on purpose. */
#define HELIKON_CUESHEET_LINE_MAX 1024u

struct helikon_cuesheet_track {
    uint16_t number;      /* the `TRACK` number, 1..99 */
    uint16_t file_index;  /* which `FILE` this track belongs to */
    uint32_t start_frame; /* `INDEX 01`, in 75ths of a second into that file */
    uint32_t pregap_frame; /* `INDEX 00`, or `start_frame` when there is none */
    const char *title;     /* NULL when the track has none */
    uint16_t title_length;
    const char *performer;
    uint16_t performer_length;
};

struct helikon_cuesheet_file {
    const char *path; /* as written, not resolved: the caller knows its root */
    uint16_t path_length;
};

struct helikon_cuesheet {
    struct helikon_cuesheet_file files[HELIKON_CUESHEET_MAX_FILES];
    struct helikon_cuesheet_track tracks[HELIKON_CUESHEET_MAX_TRACKS];
    uint16_t file_count;
    uint16_t track_count;
    /* Statements and tracks the parser refused; `complete` is `refused == 0`. */
    uint16_t refused;
    uint8_t complete;
    /* Album-level TITLE and PERFORMER, before the first TRACK. */
    const char *title;
    uint16_t title_length;
    const char *performer;
    uint16_t performer_length;
};

/*
 * Parses `length` bytes at `text`. Returns HELIKON_OK when at least one track
 * survived, even if some statements were refused; HELIKON_ERR_FORMAT when the
 * buffer holds a NUL byte or no track at all survived; HELIKON_ERR_ARGUMENT for
 * a null pointer. The struct is fully initialised on every return path, so a
 * caller can print `refused` after a refusal.
 */
int helikon_cuesheet_parse(struct helikon_cuesheet *cue, const char *text,
                           size_t length);

/* 75 frames to the second, so a frame is 13.33 ms and the rounding is down. */
uint32_t helikon_cuesheet_frame_to_ms(uint32_t frame);

/*
 * Array position of the track numbered `number`, or HELIKON_ERR_NOT_FOUND. Array
 * order is ascending start order, which is what a player walks.
 */
int helikon_cuesheet_track_by_number(const struct helikon_cuesheet *cue,
                                     uint16_t number, uint16_t *index);

#endif
