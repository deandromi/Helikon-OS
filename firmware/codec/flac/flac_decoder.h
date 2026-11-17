/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Native FLAC subset decoder for the embedded proof path.
 *
 * Supports the native FLAC container, STREAMINFO, constant/verbatim/fixed/LPC
 * subframes with Rice residuals, independent/left-side/right-side/mid-side
 * stereo, 16/24-bit samples and 44.1/48 kHz. It decodes into fixed workspace
 * and emits interleaved stereo 16-bit PCM for the audio ring. Higher rates,
 * more than two channels and 24-bit output are outside this first tranche.
 */
#ifndef HELIKON_CODEC_FLAC_DECODER_H
#define HELIKON_CODEC_FLAC_DECODER_H

#include <stddef.h>
#include <stdint.h>

#include "codec.h"

#define HELIKON_FLAC_MAX_BLOCK 8192u
#define HELIKON_FLAC_MAX_CHANNELS 2u

struct helikon_flac_decoder {
    const uint8_t *data;
    size_t size;
    size_t first_frame;
    size_t next_frame;
    uint32_t sample_rate;
    uint32_t total_frames;
    uint32_t stream_frames;
    uint32_t min_block;
    uint32_t max_block;
    uint32_t frame_frames;
    uint32_t frame_pos;
    uint16_t channels;
    uint16_t bits;
    uint8_t have_frame;
    uint8_t error;
    int32_t decoded[HELIKON_FLAC_MAX_CHANNELS][HELIKON_FLAC_MAX_BLOCK];
};

int helikon_flac_probe(const uint8_t *data, size_t size,
                       struct helikon_codec_info *info);
int helikon_flac_open(struct helikon_flac_decoder *decoder,
                      const uint8_t *data, size_t size);
int helikon_flac_read(struct helikon_flac_decoder *decoder, int16_t *pcm,
                      size_t max_frames, size_t *frames);
int helikon_flac_seek(struct helikon_flac_decoder *decoder, uint32_t frame);
void helikon_flac_close(struct helikon_flac_decoder *decoder);

#endif
