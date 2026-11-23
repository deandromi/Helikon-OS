/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Resume bookmarks (T-093).
 *
 * A player that cannot remember where it was makes every power-off a decision
 * to start over. This module is the record: one track path, one position, and
 * enough context to show it on a Resume screen. It is pure text in and out, so
 * the file is readable with `cat`, diffable, and testable without a filesystem;
 * `vfs_bookmark.c` is the only code that knows where the bytes live.
 *
 * The format is deliberately strict, because a bookmark written by a buggy
 * build must be refused rather than resume at a wrong second:
 *
 *     helikon-bookmark 1
 *     path=/Music/Artist/Album/01 Track.flac
 *     position_ms=12345
 *     duration_ms=234567
 *     track_id=42
 *     saved_at=1795483200
 *
 * Rules, all tested:
 *   - The first line is the magic plus the version, exactly, and every line
 *     ends with a single LF. A CR, a NUL or a missing final newline is a
 *     refusal: this file is written by us, so a byte we did not write means
 *     something else owns it now.
 *   - `path` and `position_ms` are required; `duration_ms`, `track_id` and
 *     `saved_at` are optional and default to 0, which means "unknown".
 *   - A key may appear once. An unknown key, a duplicate key, an empty value, a
 *     number that overflows `uint32_t`, or a path that is not absolute or holds
 *     a control byte is `HELIKON_ERR_FORMAT`.
 *   - A path longer than `HELIKON_BOOKMARK_PATH_MAX - 1` bytes is refused, not
 *     truncated.
 */
#ifndef HELIKON_PLAYER_BOOKMARK_H
#define HELIKON_PLAYER_BOOKMARK_H

#include <stddef.h>
#include <stdint.h>

/* The same ceiling a playlist entry uses; HFS+ can hold longer, a track is not. */
#define HELIKON_BOOKMARK_PATH_MAX 512u

/* Worst case: the header, every key, a maximum path and five 10-digit numbers. */
#define HELIKON_BOOKMARK_TEXT_MAX 1024u

struct helikon_bookmark {
    char path[HELIKON_BOOKMARK_PATH_MAX];
    uint32_t position_ms;
    uint32_t duration_ms; /* 0: unknown */
    uint32_t track_id;    /* 0: unknown */
    uint32_t saved_at;    /* seconds since the epoch; 0: unknown */
};

/*
 * Serialises `bookmark` into `out`, always NUL-terminating when `size` is
 * nonzero, and returns the number of bytes written (excluding the terminator).
 * Returns 0 -- never a partial record -- for a bookmark this module would not
 * accept back: a relative or empty path, a control byte, a path at or past the
 * limit, or a buffer too small for the whole record.
 */
size_t helikon_bookmark_write(const struct helikon_bookmark *bookmark, char *out,
                              size_t size);

/*
 * Parses `length` bytes of `text` into `bookmark`. On any refusal the struct is
 * zeroed, so a caller never resumes from half a record.
 */
int helikon_bookmark_parse(struct helikon_bookmark *bookmark, const char *text,
                           size_t length);

#endif
