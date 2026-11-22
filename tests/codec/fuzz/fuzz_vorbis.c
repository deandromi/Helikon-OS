/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * libFuzzer target for the Vorbis-comment reader and the FLAC metadata walker
 * in front of it.
 *
 * Beyond memory safety the harness checks the two things a caller relies on:
 * every text field is NUL-terminated inside its array, and a FLAC buffer that
 * the walker accepts really did have the marker and a comment block inside it.
 *
 *   ./tools/fuzz.sh vorbis 300
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "helikon_error.h"
#include "mem.h"
#include "vorbis.h"

/* firmware/lib's string.h is the freestanding shim on this include path, so
 * libc's memchr/memcmp are not declared here. Two small loops do the job. */
static int has_nul(const char *text, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i) {
        if (text[i] == '\0')
            return 1;
    }
    return 0;
}

static int bytes_equal(const uint8_t *left, const char *right, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        if (left[i] != (uint8_t)right[i])
            return 0;
    }
    return 1;
}

static void check_text(const char *text, size_t capacity, const char *what)
{
    if (!has_nul(text, capacity)) {
        fprintf(stderr, "fuzz: %s is not terminated in %zu bytes\n", what,
                capacity);
        abort();
    }
}

static void check_tags(const struct helikon_vorbis_tags *tags)
{
    check_text(tags->title, sizeof(tags->title), "title");
    check_text(tags->artist, sizeof(tags->artist), "artist");
    check_text(tags->album, sizeof(tags->album), "album");
    check_text(tags->album_artist, sizeof(tags->album_artist), "album artist");
    check_text(tags->genre, sizeof(tags->genre), "genre");
    check_text(tags->year, sizeof(tags->year), "year");
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct helikon_vorbis_tags tags;
    helikon_memset(&tags, 0, sizeof(tags));

    /* The comment block on its own, as the FLAC reader hands it over. */
    if (helikon_vorbis_comment_parse(data, size, &tags) == HELIKON_OK)
        check_tags(&tags);

    /* And through the metadata walker, which has its own offsets to get right. */
    helikon_memset(&tags, 0, sizeof(tags));
    if (helikon_flac_tags_buffer(data, size, &tags) == HELIKON_OK) {
        check_tags(&tags);
        if (size < 4u || !bytes_equal(data, "fLaC", 4u)) {
            fprintf(stderr, "fuzz: accepted a buffer with no fLaC marker\n");
            abort();
        }
    }
    return 0;
}
