/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded read-ahead for a codec pull source (T-058).
 *
 * A decoder asks for small pieces at absolute offsets: a FLAC frame header, a
 * 4 KiB frame window, an MP3 reservoir tail. Handing each of those straight to
 * the VFS costs one seek and one read per call, and on the 6G that is a disk
 * access per decoder hiccup. This wrapper sits between the two: it keeps one
 * window of the file in memory, serves every request that falls inside it, and
 * refills with one large read when it does not.
 *
 * The media is immutable while a track is open, so a window never needs
 * invalidating — not even across a seek. A backward seek the player already has
 * in the window is served without touching storage.
 *
 * The wrapper is pure: it owns no storage, allocates nothing and knows nothing
 * about HFS+ or the audio HAL. The caller provides the buffer.
 */
#ifndef HELIKON_PLAYER_READ_AHEAD_H
#define HELIKON_PLAYER_READ_AHEAD_H

#include <stddef.h>
#include <stdint.h>

#include "codec.h"

/*
 * The window the player gives a storage-backed stream: 16 KiB is four 4 KiB
 * sectors, enough to cover a FLAC frame and an MP3 reservoir refill, and small
 * enough to stay out of the codec workspace budget.
 */
#define HELIKON_READ_AHEAD_BYTES (16u * 1024u)

/* Counters a test (and `state.md`) can read; no effect on behaviour. */
struct helikon_read_ahead_stats {
    uint32_t reads;      /* downstream reads issued */
    uint64_t bytes_read; /* bytes actually fetched from downstream */
    uint32_t hits;       /* requests fully served from the window */
    uint32_t partial;    /* requests served partly from the window, then refilled */
    uint32_t bypasses;   /* requests too large to amortise, read straight through */
    uint32_t refills;    /* window fills */
};

struct helikon_read_ahead {
    const struct helikon_codec_source *downstream;
    uint8_t *buffer;
    size_t capacity;
    uint64_t start;   /* stream offset of buffer[0] */
    size_t length;    /* valid bytes at buffer[0] */
    uint64_t end;     /* known end of stream; UINT64_MAX while unknown */
    struct helikon_read_ahead_stats stats;
};

/*
 * Wraps `downstream` and fills `source` so a decoder can pull through the
 * window. `buffer` must outlive the source and the open codec; `capacity` of
 * zero is allowed and turns the wrapper into a pass-through, which keeps a
 * caller that has no spare buffer working.
 *
 * `source->size` reports `downstream->size` unchanged, so a decoder sees the
 * same stream length either way. The same context rule applies: the wrapper
 * borrows `downstream`, so all three outlive the codec.
 */
int helikon_read_ahead_init(struct helikon_read_ahead *read_ahead,
                            uint8_t *buffer, size_t capacity,
                            const struct helikon_codec_source *downstream,
                            struct helikon_codec_source *source);

#endif
