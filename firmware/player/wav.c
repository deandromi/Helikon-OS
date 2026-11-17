/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "wav.h"

#include <stddef.h>

#include "error.h"

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int tag_is(const uint8_t *data, const char *tag)
{
    return data[0] == (uint8_t)tag[0] && data[1] == (uint8_t)tag[1] &&
           data[2] == (uint8_t)tag[2] && data[3] == (uint8_t)tag[3];
}

int helikon_wav_parse(const uint8_t *data, size_t size, struct helikon_wav *wav)
{
    if (data == NULL || wav == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (size < 12u || !tag_is(data, "RIFF") || !tag_is(data + 8u, "WAVE"))
        return HELIKON_ERR_UNSUPPORTED;

    uint32_t riff_size = read_u32(data + 4u);
    if (riff_size < 4u || (size_t)riff_size + 8u > size)
        return HELIKON_ERR_FORMAT;

    int have_format = 0;
    uint16_t format = 0u;
    uint16_t channels = 0u;
    uint32_t sample_rate = 0u;
    uint16_t bits = 0u;
    const uint8_t *pcm = NULL;
    size_t pcm_bytes = 0u;

    size_t offset = 12u;
    while (offset + 8u <= (size_t)riff_size + 8u) {
        const uint8_t *chunk = data + offset;
        uint32_t chunk_size = read_u32(chunk + 4u);
        size_t body = offset + 8u;
        if ((size_t)chunk_size > size - body)
            return HELIKON_ERR_FORMAT;

        if (tag_is(chunk, "fmt ") && chunk_size >= 16u) {
            format = read_u16(data + body);
            channels = read_u16(data + body + 2u);
            sample_rate = read_u32(data + body + 4u);
            bits = read_u16(data + body + 14u);
            have_format = 1;
        } else if (tag_is(chunk, "data")) {
            pcm = data + body;
            pcm_bytes = chunk_size;
        }

        size_t next = body + chunk_size;
        if ((chunk_size & 1u) != 0u)
            ++next;
        if (next <= offset)
            return HELIKON_ERR_FORMAT;
        offset = next;
    }

    if (!have_format || pcm == NULL)
        return HELIKON_ERR_FORMAT;
    if (format != 1u || bits != 16u || (channels != 1u && channels != 2u))
        return HELIKON_ERR_UNSUPPORTED;
    if (sample_rate != 44100u && sample_rate != 48000u)
        return HELIKON_ERR_UNSUPPORTED;

    size_t frame_bytes = (size_t)channels * 2u;
    pcm_bytes -= pcm_bytes % frame_bytes;
    if (pcm_bytes == 0u)
        return HELIKON_ERR_FORMAT;

    uint32_t frames = (uint32_t)(pcm_bytes / frame_bytes);
    wav->pcm = pcm;
    wav->bytes = pcm_bytes;
    wav->sample_rate = sample_rate;
    wav->channels = channels;
    wav->bits = bits;
    wav->duration_ms = (uint32_t)(((uint64_t)frames * 1000u) / sample_rate);
    return HELIKON_OK;
}
