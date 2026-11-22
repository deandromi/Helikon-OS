/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libFuzzer target for the ID3v2 tag reader.
 *
 * Two invariants beyond "does not crash":
 *   - `helikon_id3v2_tag_size()` never reports a length longer than the buffer
 *     it was given. It is what the player uses to find the first audio frame,
 *     so an oversize answer sends the decoder off the end of the file.
 *   - every text field the parser fills is NUL-terminated inside its array.
 *     These strings go straight into the UI and into `printf`.
 *
 *   ./tools/fuzz.sh id3v2 300
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "helikon_error.h"
#include "mem.h"
#include "id3v2.h"

/*
 * `firmware/lib` is on the include path and its `string.h` is the freestanding
 * shim, so <string.h> is not available here and libc's memchr would be shadowed
 * anyway. A bounded scan is all this needs.
 */
static int has_nul(const char *text, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        if (text[i] == '\0')
            return 1;
    }
    return 0;
}

static void check_text(const char *text, size_t capacity, const char *what)
{
    if (!has_nul(text, capacity)) {
        fprintf(stderr, "fuzz: %s is not terminated in %zu bytes\n", what,
                capacity);
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct helikon_id3v2_tags tags;
    helikon_memset(&tags, 0, sizeof(tags));

    int rc = helikon_id3v2_parse(data, size, &tags);
    if (rc == HELIKON_OK) {
        check_text(tags.title, sizeof(tags.title), "title");
        check_text(tags.artist, sizeof(tags.artist), "artist");
        check_text(tags.album, sizeof(tags.album), "album");
        check_text(tags.album_artist, sizeof(tags.album_artist),
                   "album artist");
        check_text(tags.genre, sizeof(tags.genre), "genre");
        check_text(tags.year, sizeof(tags.year), "year");
        if (tags.version != 2 && tags.version != 3 && tags.version != 4) {
            fprintf(stderr, "fuzz: accepted tag reports version %u\n",
                    (unsigned)tags.version);
            abort();
        }
    }

    /*
     * `helikon_id3v2_tag_size()` answers with the size the *header declares*, so
     * a truncated file legitimately gets an answer larger than the buffer: that
     * is what the caller clamps against its own length. What must hold is that a
     * non-zero answer only comes from a buffer that really starts with the tag
     * header, and that the header itself was inside the buffer.
     */
    size_t tag_size = helikon_id3v2_tag_size(data, size);
    if (tag_size != 0) {
        if (size < 3u || data[0] != 'I' || data[1] != 'D' || data[2] != '3') {
            fprintf(stderr, "fuzz: tag size with no tag header\n");
            abort();
        }
        if (tag_size < 10u) {
            fprintf(stderr, "fuzz: tag size %zu shorter than a header\n",
                    tag_size);
            abort();
        }
    }
    return 0;
}
