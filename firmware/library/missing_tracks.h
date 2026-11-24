/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * "3 of 12 tracks", and which three are missing -- without guessing.
 *
 * The temptation with a missing-track list is to count the files in a folder
 * and subtract. That is wrong in the one case the feature exists for: a library
 * whose tags disagree with its files. This module therefore works only from
 * numbers the tags actually state, and it refuses to make a claim it cannot
 * back:
 *
 *   - no stated total (`album->track_count == 0`) -> no claim, no missing list;
 *   - a multi-disc album -> no claim, because the album's total counts discs
 *     and the per-disc total is not in the database, so "9 of 12" would be a
 *     number nobody wrote down;
 *   - a total above `HELIKON_MISSING_MAX` -> no claim, because the list would
 *     be longer than the report can hold and a half list is a lie.
 *
 * When a total is stated and usable, the numbers that appear on the album's
 * tracks are matched **by number, not by title**, and a track with no number
 * (`track_no == 0`) is counted separately: it can never fill a gap, because
 * nothing says which gap it belongs in. A number above the stated total is
 * counted as numbered and ignored as a slot -- it is evidence the tags are
 * inconsistent, not evidence the album has thirteen tracks.
 *
 * The suite is a pure function over an array of numbers so it can be tested
 * without a catalog, a disk or a screen. Reading the numbers out of an HDB is a
 * loop in the caller.
 */
#ifndef HELIKON_LIBRARY_MISSING_TRACKS_H
#define HELIKON_LIBRARY_MISSING_TRACKS_H

#include <stddef.h>
#include <stdint.h>

/* The longest album a report will make a claim about. */
#define HELIKON_MISSING_MAX 128u
/* Long enough for the album line, the count line and a full list of numbers. */
#define HELIKON_MISSING_TEXT_MAX 512u

/* Why a report makes no claim. `KNOWN` is the only one that does. */
#define HELIKON_MISSING_KNOWN 0u
#define HELIKON_MISSING_NO_TOTAL 1u
#define HELIKON_MISSING_MULTI_DISC 2u
#define HELIKON_MISSING_TOTAL_TOO_LARGE 3u

struct helikon_missing_report {
    uint32_t stated_total; /* the album's own track count, 0 when unstated */
    uint32_t numbered;     /* tracks that state a number at all */
    uint32_t unnumbered;   /* tracks with track_no 0: never fill a gap */
    uint32_t missing_count;
    uint16_t missing[HELIKON_MISSING_MAX]; /* ascending, 1..stated_total */
    uint8_t known;  /* 1 only when the three refusals above do not apply */
    uint8_t reason; /* HELIKON_MISSING_* */
};

/*
 * Builds the report from the numbers the album's tracks state.
 *
 * `numbers` may be NULL when `count` is 0. A report built with `known == 0`
 * still carries `numbered` and `unnumbered`, because "this album's tags say
 * nothing about order" is itself worth showing.
 */
void helikon_missing_report(const uint16_t *numbers, size_t count,
                            uint32_t stated_total, uint32_t disc_count,
                            struct helikon_missing_report *report);

/*
 * Renders the report as plain text, `HELIKON_MISSING_TEXT_MAX` at most,
 * always NUL-terminated when `capacity > 0`. Returns the length written,
 * excluding the NUL. Two shapes:
 *
 *   Album: Kind of Blue
 *   Tracks: 9 of 12 stated
 *   Missing: 10, 11, 12
 *
 *   Album: Untagged
 *   Tracks: 5 present, total not stated
 *
 * A name longer than the line allows is truncated with `...` rather than
 * silently splitting the line.
 */
size_t helikon_missing_text(const struct helikon_missing_report *report,
                            const char *album_name, char *buffer,
                            size_t capacity);

#endif
