/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Playlist text import: M3U, M3U8 and PLS.
 *
 * A playlist file is a list of paths, so the only thing this module has to be
 * good at is text. It is a *streaming* parser: `next()` yields one entry at a
 * time from a buffer the caller already owns, because 512 entries times a path
 * is not something a 6G should hold in RAM at once. The caller resolves an entry
 * to a library id (or refuses it) and moves on.
 *
 * The rules, in one place, because every one of them is a bug otherwise:
 *
 *   - Both LF and CRLF end a line; a UTF-8 BOM at the start is skipped.
 *   - Leading and trailing spaces and tabs are trimmed. A path with a leading
 *     space is not a thing HFS+ users have; a trailing one is a common artifact
 *     of hand-edited files.
 *   - `#EXTINF:<seconds>,<title>` carries the title and duration for the next
 *     path. Every other `#` line is a comment and is ignored, including
 *     `#EXTM3U`.
 *   - `#EXTINF` durations may be fractional; they are truncated to seconds.
 *   - Paths are UTF-8, not percent-encoded (M3U8 in practice is not a URI
 *     list), and are resolved against the playlist's own folder. `.` components
 *     are dropped and `..` pops one; an entry that escapes the root is refused.
 *   - A path that is not valid UTF-8 or contains a control character is refused.
 *   - An entry longer than `HELIKON_PLAYLIST_PATH_MAX`, or a file with more than
 *     `HELIKON_PLAYLIST_MAX_ENTRIES` entries, is refused rather than truncated.
 *     Refusals are counted in `skipped` so the UI can say "3 entries skipped"
 *     instead of silently dropping them.
 *   - PLS is read by index: `File1=`, `Title1=`, `Length1=`, and so on, in
 *     numeric order rather than file order, because writers do not agree on
 *     whether Title comes before or after its File line. The index scan stops at
 *     `HELIKON_PLAYLIST_MAX_ENTRIES`: an entry numbered above the cap is ignored
 *     rather than walked to, because `File99999999=` is otherwise a very slow
 *     way to return nothing.
 *
 * Cuesheets are a different animal (tracks inside one file, with frame offsets)
 * and are not handled here yet.
 */
#ifndef HELIKON_PLAYER_PLAYLIST_H
#define HELIKON_PLAYER_PLAYLIST_H

#include <stddef.h>
#include <stdint.h>

/* A path HFS+ can hold is longer, but an entry this long is not a playlist. */
#define HELIKON_PLAYLIST_PATH_MAX 512u
#define HELIKON_PLAYLIST_TITLE_MAX 128u
#define HELIKON_PLAYLIST_BASE_MAX 256u
#define HELIKON_PLAYLIST_MAX_ENTRIES 512u

enum helikon_playlist_format {
    HELIKON_PLAYLIST_AUTO = 0,
    HELIKON_PLAYLIST_M3U = 1,
    HELIKON_PLAYLIST_PLS = 2
};

struct helikon_playlist_entry {
    char path[HELIKON_PLAYLIST_PATH_MAX];
    char title[HELIKON_PLAYLIST_TITLE_MAX];
    uint32_t duration_s;
    uint8_t has_title;
    uint8_t has_duration;
};

struct helikon_playlist_parser {
    const char *text;
    size_t length;
    size_t offset;             /* next unread byte, for M3U */
    char base[HELIKON_PLAYLIST_BASE_MAX];
    char pending_title[HELIKON_PLAYLIST_TITLE_MAX];
    uint32_t pending_duration;
    uint32_t pls_index;        /* next PLS index to look for */
    uint32_t pls_count;        /* highest FileN seen at open */
    uint32_t emitted;
    uint32_t skipped;
    uint8_t format;
    uint8_t pending_kind;      /* 0 none, 1 title, 2 duration, 3 both */
};

/*
 * Prepare a parser over `length` bytes of `text`. `base` is the playlist's own
 * folder as an absolute path ("/" or "/Music/Playlists"); a NULL or empty base
 * means the root. `format` may be AUTO to detect from the first line.
 *
 * Refuses a buffer that contains a NUL byte: a text playlist with one is corrupt,
 * and pretending the rest of the file does not exist would hide it.
 */
int helikon_playlist_open(struct helikon_playlist_parser *parser,
                          const char *text, size_t length, const char *base,
                          uint8_t format);

/*
 * Next entry: 1 for an entry, 0 at the end, or a negative HELIKON_ERR_*. Entries
 * that are refused are counted and skipped, so a malformed line in the middle of
 * a list does not lose the rest of it; a file that is not a playlist at all is
 * refused at `open`.
 */
int helikon_playlist_next(struct helikon_playlist_parser *parser,
                          struct helikon_playlist_entry *entry);

/*
 * Export: the same text, written back out.
 *
 * A sink, not a buffer, for the same reason the parser streams: a 512-entry
 * playlist is 512 paths and the firmware should not need all of them in RAM to
 * save one. The sink is whatever holds the bytes — a file through `vfs_rw`, a
 * pipe, a test array — and a short or failing write is a failing export, never a
 * silently truncated playlist.
 *
 * The rules, each one a decision:
 *
 *   - M3U is written with an `#EXTM3U` header and `#EXTINF:<seconds>,<title>`
 *     before each entry whose duration or title is known. A reader that ignores
 *     both still gets a usable path list, which is why they are per-entry rather
 *     than mandatory.
 *   - Paths are written **absolute**, exactly as the catalog names them. A
 *     relative form would be shorter and is what a hand-written playlist usually
 *     looks like, but it would also mean the same list means something different
 *     after the file is moved; the import side already resolves both forms, so
 *     absolute costs nothing and cannot be wrong.
 *   - PLS needs its entry count up front (`NumberOfEntries=` is the first record
 *     a reader trusts), so the caller passes it at open. `FileN=`, `TitleN=`,
 *     `LengthN=` and a closing `Version=2` are written; the import side reads PLS
 *     by numeric index so writer order does not matter.
 *   - A path must be absolute, control-character free and shorter than
 *     `HELIKON_PLAYLIST_PATH_MAX`; a title must be control-character free and
 *     shorter than `HELIKON_PLAYLIST_TITLE_MAX`. Anything else is ARGUMENT and
 *     counted in `refused`: a playlist is a text file, and one with a newline in
 *     the middle of a path is a corrupt file, not a clever one.
 *   - An entry that is refused leaves the sink as it was except for the entries
 *     already written. This is not atomic; the caller that wants atomicity writes
 *     a temporary file and renames it (the update client's pattern), because a
 *     playlist is not worth a transaction of its own.
 */
struct helikon_playlist_sink {
    int (*write)(void *context, const void *data, size_t length);
    void *context;
};

struct helikon_playlist_writer {
    const struct helikon_playlist_sink *sink;
    uint32_t written;
    uint32_t refused;
    uint8_t format;
    uint8_t opened;
};

/*
 * Prepare a writer. `format` is M3U or PLS (AUTO means M3U); `entries` is the
 * PLS `NumberOfEntries` and is ignored for M3U.
 */
int helikon_playlist_write_open(struct helikon_playlist_writer *writer,
                                const struct helikon_playlist_sink *sink,
                                uint8_t format, uint32_t entries);

/* Appends one entry. Returns HELIKON_OK, ARGUMENT (and counts it), or the
 * sink's error. Writing after close is ARGUMENT. */
int helikon_playlist_write_entry(struct helikon_playlist_writer *writer,
                                 const struct helikon_playlist_entry *entry);

/* Writes the closing record PLS wants; a no-op for M3U. */
int helikon_playlist_write_close(struct helikon_playlist_writer *writer);

#endif
