/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal PCM WAV reader for the first playback path.
 *
 * This is a container parser only. It accepts uncompressed 16-bit PCM at
 * 44.1 or 48 kHz, mono or stereo, and returns a bounded pointer into the
 * caller's file image. Compressed formats belong to the codec framework.
 */
#ifndef HELIKON_PLAYER_WAV_H
#define HELIKON_PLAYER_WAV_H

#include <stddef.h>
#include <stdint.h>

struct helikon_wav {
    const uint8_t *pcm;
    size_t bytes;
    uint32_t sample_rate;
    uint32_t duration_ms;
    uint16_t channels;
    uint16_t bits;
};

int helikon_wav_parse(const uint8_t *data, size_t size, struct helikon_wav *wav);

#endif
