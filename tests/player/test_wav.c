/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "wav.h"

#include <stdio.h>
#include <string.h>

#include "error.h"

static void put16(uint8_t *data, uint32_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value & 0xffu);
    data[offset + 1u] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *data, uint32_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value & 0xffu);
    data[offset + 1u] = (uint8_t)((value >> 8) & 0xffu);
    data[offset + 2u] = (uint8_t)((value >> 16) & 0xffu);
    data[offset + 3u] = (uint8_t)(value >> 24);
}

static size_t make_wav(uint8_t *data, uint16_t channels, uint32_t rate,
                       uint16_t bits, size_t frames)
{
    size_t data_bytes = frames * channels * (bits / 8u);
    size_t riff_size = 36u + data_bytes;
    memcpy(data, "RIFF", 4u);
    put32(data, 4u, (uint32_t)riff_size);
    memcpy(data + 8u, "WAVE", 4u);
    memcpy(data + 12u, "fmt ", 4u);
    put32(data, 16u, 16u);
    put16(data, 20u, 1u);
    put16(data, 22u, channels);
    put32(data, 24u, rate);
    put32(data, 28u, rate * channels * (bits / 8u));
    put16(data, 32u, (uint16_t)(channels * (bits / 8u)));
    put16(data, 34u, bits);
    memcpy(data + 36u, "data", 4u);
    put32(data, 40u, (uint32_t)data_bytes);
    for (size_t index = 0u; index < frames; ++index) {
        for (uint16_t channel = 0u; channel < channels; ++channel) {
            size_t offset = 44u + (index * channels + channel) * 2u;
            put16(data, (uint32_t)offset, (uint16_t)(index + channel));
        }
    }
    return 44u + data_bytes;
}

int main(void)
{
    uint8_t data[32768];
    struct helikon_wav wav;

    size_t size = make_wav(data, 2u, 44100u, 16u, 16u);
    if (helikon_wav_parse(data, size, &wav) != HELIKON_OK) {
        fprintf(stderr, "valid stereo WAV rejected\n");
        return 1;
    }
    if (wav.channels != 2u || wav.sample_rate != 44100u ||
        wav.bytes != 64u || wav.duration_ms != 0u) {
        fprintf(stderr, "stereo WAV fields incorrect\n");
        return 1;
    }
    if (((const uint16_t *)wav.pcm)[0] != 0u ||
        ((const uint16_t *)wav.pcm)[1] != 1u) {
        fprintf(stderr, "stereo WAV PCM data incorrect\n");
        return 1;
    }

    size = make_wav(data, 2u, 48000u, 16u, 4800u);
    if (helikon_wav_parse(data, size, &wav) != HELIKON_OK ||
        wav.duration_ms != 100u) {
        fprintf(stderr, "48 kHz duration incorrect\n");
        return 1;
    }

    size = make_wav(data, 1u, 44100u, 16u, 8u);
    if (helikon_wav_parse(data, size, &wav) != HELIKON_OK ||
        wav.channels != 1u || wav.bytes != 16u) {
        fprintf(stderr, "mono WAV rejected\n");
        return 1;
    }

    size = make_wav(data, 2u, 44100u, 8u, 8u);
    if (helikon_wav_parse(data, size, &wav) != HELIKON_ERR_UNSUPPORTED) {
        fprintf(stderr, "8-bit WAV accepted\n");
        return 1;
    }

    size = make_wav(data, 2u, 44100u, 16u, 16u);
    data[0] = 'X';
    if (helikon_wav_parse(data, size, &wav) != HELIKON_ERR_UNSUPPORTED) {
        fprintf(stderr, "non-RIFF data accepted\n");
        return 1;
    }

    size = make_wav(data, 2u, 44100u, 16u, 16u);
    if (helikon_wav_parse(data, size - 8u, &wav) != HELIKON_ERR_FORMAT) {
        fprintf(stderr, "truncated WAV accepted\n");
        return 1;
    }

    printf("PASS wav\n");
    return 0;
}
