/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded Vorbis-comment reader for FLAC files.
 *
 * This is metadata only. It parses a Vorbis-comment block (FLAC metadata type
 * 4), either from a resident buffer or straight from a pull source while
 * walking the FLAC metadata chain, into the fixed fields the library needs.
 * Values are UTF-8 already; an invalid byte sequence drops the value rather
 * than storing half a character. Strings are truncated at a fixed size instead
 * of allocated, the first value of a repeated field wins, and every read is
 * bounds-checked against the block or the declared file size.
 *
 * It does not touch playback state and it does not write to storage.
 */
#ifndef HELIKON_CODEC_TAG_VORBIS_H
#define HELIKON_CODEC_TAG_VORBIS_H

#include <stddef.h>
#include <stdint.h>

#include "codec.h"

#define HELIKON_VORBIS_TEXT_MAX 96u

struct helikon_vorbis_tags {
    char title[HELIKON_VORBIS_TEXT_MAX];
    char artist[HELIKON_VORBIS_TEXT_MAX];
    char album[HELIKON_VORBIS_TEXT_MAX];
    char album_artist[HELIKON_VORBIS_TEXT_MAX];
    char genre[HELIKON_VORBIS_TEXT_MAX];
    char year[8]; /* first four digits of DATE, or empty */
    uint16_t track;
    uint16_t disc;
};

/*
 * Parses the body of a Vorbis-comment block (vendor length + vendor string +
 * comment count + length-prefixed comments). Every field is zeroed first, so a
 * block with no tags of interest leaves the struct empty.
 *
 * Returns HELIKON_OK when the block was well formed, HELIKON_ERR_FORMAT when a
 * length runs past the block or a name has no `=`, and
 * HELIKON_ERR_ARGUMENT for a null argument.
 */
int helikon_vorbis_comment_parse(const uint8_t *data, size_t size,
                                 struct helikon_vorbis_tags *tags);

/*
 * Same, for a FLAC file held in memory: checks `fLaC`, walks the metadata
 * blocks and parses the first VORBIS_COMMENT block.
 *
 * Returns HELIKON_OK when a comment block was parsed (fields may be empty),
 * HELIKON_ERR_NOT_FOUND when the file has none or the buffer is not FLAC.
 */
int helikon_flac_tags_buffer(const uint8_t *data, size_t size,
                             struct helikon_vorbis_tags *tags);

/*
 * Same, for a FLAC file behind a `read_at` source. The source must report its
 * size (the metadata chain has no other bound) and only the block header and at
 * most HELIKON_VORBIS_BLOCK_MAX bytes of the comment block are read, so a huge
 * padded tag cannot be pulled into RAM. A block larger than that cap is parsed
 * prefix-wise: complete comments are kept, a cut-off trailing comment is
 * dropped.
 *
 * Returns HELIKON_OK, HELIKON_ERR_NOT_FOUND, HELIKON_ERR_UNSUPPORTED for a
 * source without a size, or an error from the source.
 */
int helikon_flac_tags_source(const struct helikon_codec_source *source,
                             struct helikon_vorbis_tags *tags);

/* Bytes of a comment block this reader will pull from a source. */
#define HELIKON_VORBIS_BLOCK_MAX 4096u

#endif
