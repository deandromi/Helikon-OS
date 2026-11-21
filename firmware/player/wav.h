/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal PCM WAV reader for the first playback path.
 *
 * This is a container parser only. It accepts uncompressed 16-bit PCM at
 * 44.1 or 48 kHz, mono or stereo. Two entry points share one chunk walker:
 * `helikon_wav_parse` parses a buffer the caller already holds, and
 * `helikon_wav_parse_source` walks a pull source so the file never has to be
 * resident. Compressed formats belong to the codec framework.
 */
#ifndef HELIKON_PLAYER_WAV_H
#define HELIKON_PLAYER_WAV_H

#include <stddef.h>
#include <stdint.h>

struct helikon_codec_source;

struct helikon_wav {
    /* Valid only for a memory parse; a source parse leaves this NULL. */
    const uint8_t *pcm;
    /* Absolute stream offset of the PCM payload. */
    uint64_t pcm_offset;
    size_t bytes;
    uint32_t sample_rate;
    uint32_t duration_ms;
    uint16_t channels;
    uint16_t bits;
};

int helikon_wav_parse(const uint8_t *data, size_t size, struct helikon_wav *wav);

/*
 * Parses a stream head that may be shorter than the stream: `total_size` is
 * the true length (0 when unknown) and bounds the RIFF walk. Returns
 * HELIKON_ERR_FORMAT when a chunk header runs past `total_size`, or when the
 * head ends before `fmt ` and `data` are both seen and the head is the whole
 * stream.
 */
int helikon_wav_parse_prefix(const uint8_t *data, size_t size,
                             uint64_t total_size, struct helikon_wav *wav);

/*
 * Parses through a pull source. Requires a readable stream of at least the
 * header; `source->size` may be 0 (unknown), in which case the RIFF size
 * field bounds the walk.
 */
int helikon_wav_parse_source(const struct helikon_codec_source *source,
                             struct helikon_wav *wav);

#endif
