/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libFuzzer target for the playlist importer.
 *
 * A playlist is a text file from somewhere else, and every entry it produces is
 * a path the player will hand to the VFS. The invariants, beyond "does not
 * crash":
 *
 *   - an entry's path is always absolute, NUL-terminated inside its array, and
 *     free of control characters — a relative or unterminated path would be
 *     resolved against whatever the caller's working directory means this week;
 *   - `open` refuses a buffer with a NUL in it outright, because pretending the
 *     rest of the file does not exist hides a corrupt file;
 *   - the entry count never exceeds `HELIKON_PLAYLIST_MAX_ENTRIES`, so a huge
 *     file cannot walk a caller past its own capacity;
 *   - `next` never starts failing after `open` succeeded: one bad line is
 *     skipped and counted, not fatal.
 *
 *   ./tools/fuzz.sh playlist 300
 *   ./tools/fuzz.sh --replay playlist
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "helikon_error.h"
#include "playlist.h"

static void fail(const char *what)
{
    fprintf(stderr, "fuzz: %s\n", what);
    abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct helikon_playlist_parser parser;
    int rc = helikon_playlist_open(&parser, (const char *)data, size,
                                   "/Music/Lists", HELIKON_PLAYLIST_AUTO);
    if (rc == HELIKON_ERR_FORMAT)
        return 0; /* a NUL in the buffer: the documented refusal */
    if (rc != HELIKON_OK)
        fail("open failed with something other than FORMAT");

    struct helikon_playlist_entry entry;
    uint32_t produced = 0u;
    for (;;) {
        rc = helikon_playlist_next(&parser, &entry);
        if (rc == 0)
            break;
        if (rc < 0)
            fail("next failed after open succeeded");
        ++produced;
        if (produced > HELIKON_PLAYLIST_MAX_ENTRIES)
            fail("more entries than the documented cap");

        size_t length = 0u;
        while (length < sizeof(entry.path) && entry.path[length] != '\0')
            ++length;
        if (length == 0u)
            fail("an entry with an empty path");
        if (length >= sizeof(entry.path))
            fail("an unterminated path");
        if (entry.path[0] != '/')
            fail("a path that is not absolute");
        for (size_t index = 0u; index < length; ++index) {
            if ((unsigned char)entry.path[index] < 0x20u)
                fail("a control character in a path");
        }
    }

    if (produced != parser.emitted)
        fail("the entry count disagrees with the parser's own counter");
    return 0;
}
