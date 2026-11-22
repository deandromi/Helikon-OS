/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "wav.h"

#include <stddef.h>
#include <stdint.h>

#include "codec.h"
#include "helikon_error.h"
#include "mem.h"

/*
 * One RIFF chunk walker over two backends: a resident buffer or a pull
 * source. Every read is absolute and bounded, so a streaming parse never
 * needs the payload it walks past, and a memory parse behaves exactly as the
 * previous pointer-walking version did.
 */
struct wav_reader {
    const uint8_t *data; /* non-NULL for the memory backend */
    size_t size;
    const struct helikon_codec_source *source;
};

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

static int reader_read_at(const struct wav_reader *reader, uint64_t offset,
                          void *buffer, size_t length, size_t *read)
{
    if (read == NULL)
        return HELIKON_ERR_ARGUMENT;
    *read = 0u;
    if (length == 0u)
        return HELIKON_OK;

    if (reader->source == NULL) {
        if (offset > reader->size)
            return HELIKON_ERR_FORMAT;
        size_t available = reader->size - (size_t)offset;
        size_t count = available < length ? available : length;
        helikon_memcpy(buffer, reader->data + (size_t)offset, count);
        *read = count;
        return HELIKON_OK;
    }

    if (reader->source->read_at == NULL)
        return HELIKON_ERR_ARGUMENT;
    size_t count = 0u;
    int rc = reader->source->read_at(reader->source->context, offset, buffer,
                                     length, &count);
    if (rc != HELIKON_OK)
        return rc;
    if (count > length)
        return HELIKON_ERR_FORMAT;
    *read = count;
    return HELIKON_OK;
}

static int wav_parse_reader(const struct wav_reader *reader, uint64_t total_size,
                            struct helikon_wav *wav)
{
    if (reader == NULL || wav == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (reader->source == NULL && reader->data == NULL)
        return HELIKON_ERR_ARGUMENT;

    uint8_t header[12];
    size_t got = 0u;
    int rc = reader_read_at(reader, 0u, header, sizeof(header), &got);
    if (rc != HELIKON_OK)
        return rc;
    if (got < sizeof(header))
        return HELIKON_ERR_UNSUPPORTED;
    if (!tag_is(header, "RIFF") || !tag_is(header + 8u, "WAVE"))
        return HELIKON_ERR_UNSUPPORTED;

    uint32_t riff_size = read_u32(header + 4u);
    if (riff_size < 4u)
        return HELIKON_ERR_FORMAT;
    uint64_t limit = (uint64_t)riff_size + 8u;
    if (total_size != 0u && total_size < limit)
        return HELIKON_ERR_FORMAT;

    int have_format = 0;
    uint16_t format = 0u;
    uint16_t channels = 0u;
    uint32_t sample_rate = 0u;
    uint16_t bits = 0u;
    int have_data = 0;
    uint64_t pcm_offset = 0u;
    uint64_t pcm_bytes = 0u;

    uint64_t offset = 12u;
    while (offset + 8u <= limit) {
        uint8_t chunk[8];
        size_t chunk_got = 0u;
        rc = reader_read_at(reader, offset, chunk, sizeof(chunk), &chunk_got);
        if (rc != HELIKON_OK)
            return rc;
        /* A chunk header the stream cannot supply ends the walk. */
        if (chunk_got < sizeof(chunk))
            break;

        uint32_t chunk_size = read_u32(chunk + 4u);
        uint64_t body = offset + 8u;
        if ((uint64_t)chunk_size > limit - body)
            return HELIKON_ERR_FORMAT;

        if (tag_is(chunk, "fmt ")) {
            if (chunk_size < 16u)
                return HELIKON_ERR_FORMAT;
            uint8_t fmt[16];
            size_t fmt_got = 0u;
            rc = reader_read_at(reader, body, fmt, sizeof(fmt), &fmt_got);
            if (rc != HELIKON_OK)
                return rc;
            if (fmt_got < sizeof(fmt))
                return HELIKON_ERR_FORMAT;
            format = read_u16(fmt);
            channels = read_u16(fmt + 2u);
            sample_rate = read_u32(fmt + 4u);
            bits = read_u16(fmt + 14u);
            have_format = 1;
        } else if (tag_is(chunk, "data")) {
            pcm_offset = body;
            pcm_bytes = chunk_size;
            have_data = 1;
        }

        if (have_format && have_data)
            break;

        uint64_t next = body + chunk_size + (uint64_t)(chunk_size & 1u);
        if (next <= offset)
            return HELIKON_ERR_FORMAT;
        offset = next;
    }

    if (!have_format || !have_data)
        return HELIKON_ERR_FORMAT;
    if (format != 1u || bits != 16u || (channels != 1u && channels != 2u))
        return HELIKON_ERR_UNSUPPORTED;
    if (sample_rate != 44100u && sample_rate != 48000u)
        return HELIKON_ERR_UNSUPPORTED;
    if (total_size != 0u && pcm_offset + pcm_bytes > total_size)
        return HELIKON_ERR_FORMAT;

    uint64_t frame_bytes = (uint64_t)channels * 2u;
    pcm_bytes -= pcm_bytes % frame_bytes;
    if (pcm_bytes == 0u)
        return HELIKON_ERR_FORMAT;

    uint64_t frames = pcm_bytes / frame_bytes;
    wav->pcm = NULL;
    wav->pcm_offset = pcm_offset;
    wav->bytes = (size_t)pcm_bytes;
    wav->sample_rate = sample_rate;
    wav->channels = channels;
    wav->bits = bits;
    wav->duration_ms = (uint32_t)((frames * 1000u) / sample_rate);
    return HELIKON_OK;
}

int helikon_wav_parse_prefix(const uint8_t *data, size_t size,
                             uint64_t total_size, struct helikon_wav *wav)
{
    if (data == NULL || wav == NULL)
        return HELIKON_ERR_ARGUMENT;

    struct wav_reader reader;
    reader.data = data;
    reader.size = size;
    reader.source = NULL;

    int rc = wav_parse_reader(&reader, total_size, wav);
    if (rc != HELIKON_OK)
        return rc;
    if (wav->pcm_offset > size)
        return HELIKON_ERR_FORMAT;
    wav->pcm = data + (size_t)wav->pcm_offset;
    return HELIKON_OK;
}

int helikon_wav_parse(const uint8_t *data, size_t size, struct helikon_wav *wav)
{
    return helikon_wav_parse_prefix(data, size, size, wav);
}

int helikon_wav_parse_source(const struct helikon_codec_source *source,
                             struct helikon_wav *wav)
{
    if (source == NULL || wav == NULL || source->read_at == NULL)
        return HELIKON_ERR_ARGUMENT;

    struct wav_reader reader;
    reader.data = NULL;
    reader.size = 0u;
    reader.source = source;

    return wav_parse_reader(&reader, source->size, wav);
}
