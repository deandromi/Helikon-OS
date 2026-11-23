/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AIFF / AIFF-C decoder (T-068, Tier 1 format).
 *
 * AIFF is the Apple container next to WAV, and the two are the same problem
 * seen from different ends: a chunk walk, one PCM parameter block, one sound
 * chunk. Everything here is big-endian, including the sample rate, which is a
 * 80-bit IEEE extended float rather than a byte count.
 *
 * Supported encodings, all decoded to interleaved stereo 16-bit PCM:
 *
 *   AIFF        signed 8, 16, 24 or 32-bit big-endian PCM
 *   AIFC 'NONE' the AIFF case, spelled out
 *   AIFC 'twos' 16/24/32-bit big-endian PCM
 *   AIFC 'sowt' the same, little-endian (the byte-swapped name is Apple's)
 *   AIFC 'in24', 'in32', '23ni' explicit 24/32-bit and little-endian 24
 *   AIFC 'fl32' 32-bit IEEE float
 *   AIFC 'raw ' unsigned 8-bit
 *
 * A compression type outside that list is `HELIKON_ERR_UNSUPPORTED` at probe,
 * which is a different answer from "not AIFF": the browser can then say the
 * file is AIFF in a codec Helikon does not decode, instead of hiding it.
 *
 * Why the sample count comes from COMM and not from the SSND size: a file may
 * carry a shorter sound chunk than it claims, and a decoder that trusts the
 * byte count would report a duration the file itself disagrees with. The count
 * is authoritative, the chunk is bounded by it, and a short read is the end of
 * the stream.
 *
 * 24-bit audio loses its low byte and 32-bit loses two: the output is 16-bit,
 * so the shift is truncation toward negative infinity, the same rule the FLAC
 * decoder uses. That is a documented loss, not a rounding decision made here.
 */
#ifndef HELIKON_CODEC_AIFF_CODEC_H
#define HELIKON_CODEC_AIFF_CODEC_H

#include "codec.h"

/* The probe sees the head; COMM and SSND may both fit in it. */
#define HELIKON_AIFF_CHUNK_LIMIT 64u

struct helikon_aiff_reader {
    int (*read)(void *context, uint64_t offset, void *buffer, size_t length);
    void *context;
};

enum helikon_aiff_encoding {
    HELIKON_AIFF_PCM_BE = 0,   /* AIFF, 'NONE', 'twos' */
    HELIKON_AIFF_PCM_LE,       /* 'sowt' */
    HELIKON_AIFF_FLOAT32_BE,   /* 'fl32', 'FL32' */
    HELIKON_AIFF_UNSIGNED8     /* 'raw ' */
};

struct helikon_aiff {
    uint32_t sample_rate;
    uint32_t frames;      /* numSampleFrames from COMM */
    uint64_t data_offset; /* absolute offset of the first sample byte */
    uint64_t data_bytes;  /* bytes available from that offset */
    uint16_t channels;
    uint16_t bits;
    uint8_t encoding;
};

extern const struct helikon_codec_ops helikon_aiff_codec_ops;

#endif
