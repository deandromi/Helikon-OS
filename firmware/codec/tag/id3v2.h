/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded ID3v2 tag reader (v2.2/v2.3/v2.4 text frames).
 *
 * This is metadata only: it parses the tag in place, reports the fields the
 * library needs and never touches playback state or storage. Strings are
 * converted to UTF-8 (Latin-1, UTF-16 with BOM/surrogates, UTF-8), truncated
 * at a fixed size rather than allocated, and every read is bounds-checked
 * against the tag. Artwork, ReplayGain and chapter frames come later (T-099).
 */
#ifndef HELIKON_CODEC_TAG_ID3V2_H
#define HELIKON_CODEC_TAG_ID3V2_H

#include <stddef.h>
#include <stdint.h>

#define HELIKON_ID3_TEXT_MAX 96u

struct helikon_id3v2_tags {
    char title[HELIKON_ID3_TEXT_MAX];
    char artist[HELIKON_ID3_TEXT_MAX];
    char album[HELIKON_ID3_TEXT_MAX];
    char album_artist[HELIKON_ID3_TEXT_MAX];
    char genre[HELIKON_ID3_TEXT_MAX];
    char year[8];
    uint16_t track;
    uint16_t disc;
    uint16_t version; /* 2, 3 or 4; 0 when no tag was parsed */
};

/*
 * Parses the tag at `data`. Returns HELIKON_OK with the fields that were
 * present (absent ones stay empty), HELIKON_ERR_NOT_FOUND when there is no
 * ID3v2 tag, HELIKON_ERR_UNSUPPORTED for a recognized tag this reader does
 * not decode (unsynchronization, compression, encryption), or
 * HELIKON_ERR_FORMAT for a tag whose frames do not fit their declared sizes.
 * A tag with a valid header but only unknown frames is HELIKON_OK.
 */
int helikon_id3v2_parse(const uint8_t *data, size_t size,
                        struct helikon_id3v2_tags *tags);

/* Total byte length of the ID3v2 tag at `data` (header + body + footer), or 0
 * when there is none. Used to find the first audio frame. */
size_t helikon_id3v2_tag_size(const uint8_t *data, size_t size);

#endif
