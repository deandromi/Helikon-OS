/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AIFF / AIFF-C decoder. See aiff_codec.h for the supported encodings and for
 * why the frame count comes from COMM rather than from the sound chunk.
 */
#include "aiff_codec.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"

/* 256 frames of two channels at four bytes is the decode batch ceiling. */
#define AIFF_BATCH_FRAMES 256u
#define AIFF_BATCH_BYTES (AIFF_BATCH_FRAMES * 2u * 4u)

struct aiff_state {
    struct helikon_aiff aiff;
    uint32_t frame;
};

struct aiff_memory {
    const uint8_t *data;
    size_t size;
};

static uint16_t aiff_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t aiff_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static int aiff_id_is(const uint8_t *data, const char id[4])
{
    return data[0] == (uint8_t)id[0] && data[1] == (uint8_t)id[1] &&
           data[2] == (uint8_t)id[2] && data[3] == (uint8_t)id[3];
}

static int aiff_memory_read(void *context, uint64_t offset, void *buffer,
                            size_t length)
{
    const struct aiff_memory *memory = context;
    if (memory == NULL || buffer == NULL || offset > (uint64_t)memory->size)
        return -1;
    if (length > memory->size - (size_t)offset)
        return -1;
    helikon_memcpy(buffer, memory->data + (size_t)offset, length);
    return 0;
}

static int aiff_source_read(void *context, uint64_t offset, void *buffer,
                            size_t length)
{
    const struct helikon_codec_source *source = context;
    if (source == NULL || source->read_at == NULL)
        return -1;
    size_t done = 0u;
    while (done < length) {
        size_t got = 0u;
        int rc = source->read_at(source->context, offset + done,
                                 (uint8_t *)buffer + done, length - done,
                                 &got);
        if (rc != HELIKON_OK || got == 0u)
            return -1;
        done += got;
    }
    return 0;
}

static int aiff_read_at(const struct helikon_aiff_reader *reader,
                        uint64_t offset, void *buffer, size_t length)
{
    if (reader == NULL || reader->read == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (reader->read(reader->context, offset, buffer, length) != 0)
        return HELIKON_ERR_IO;
    return HELIKON_OK;
}

/*
 * The 80-bit extended sample rate. The integer bit is explicit, so the value is
 * `mantissa * 2^(exponent - 16383 - 63)`; rounding the shift keeps 44100 exact
 * and 48000 exact, and a rate that needs more than 32 bits (or is zero or
 * negative) is not a sample rate and reads as 0 so the caller can refuse it.
 */
static uint32_t aiff_rate_from_extended(const uint8_t raw[10])
{
    uint16_t exponent = aiff_be16(raw);
    uint64_t mantissa = ((uint64_t)aiff_be32(raw + 2) << 32) |
                        (uint64_t)aiff_be32(raw + 6);
    if ((exponent & 0x8000u) != 0u)
        return 0u; /* negative: not a rate */
    int exponent_value = (int)(exponent & 0x7fffu) - 16383;
    if (mantissa == 0u || exponent_value < 0 || exponent_value > 31)
        return 0u;
    unsigned int shift = (unsigned int)(63 - exponent_value);
    uint64_t value = mantissa >> shift;
    if (shift > 0u) {
        uint64_t mask = (1ull << shift) - 1ull;
        uint64_t rest = mantissa & mask;
        if (rest >= (1ull << (shift - 1u)))
            value += 1u; /* round half up */
    }
    if (value == 0u || value > 1000000u)
        return 0u;
    return (uint32_t)value;
}

/*
 * COMM: channels, frame count, sample size, rate; AIFC adds a compression type
 * and a name. A compression type outside the table is UNSUPPORTED, not a
 * corrupt file, so a browser can name the codec it cannot play.
 */
static int aiff_parse_comm(const uint8_t *data, size_t size, int compressed,
                           struct helikon_aiff *out)
{
    if (size < 18u)
        return HELIKON_ERR_FORMAT;
    uint16_t channels = aiff_be16(data);
    uint32_t frames = aiff_be32(data + 2);
    uint16_t bits = aiff_be16(data + 6);
    uint32_t rate = aiff_rate_from_extended(data + 8);

    if (channels == 0u || channels > 2u)
        return HELIKON_ERR_UNSUPPORTED;
    if (rate == 0u)
        return HELIKON_ERR_FORMAT;
    if (bits != 8u && bits != 16u && bits != 24u && bits != 32u)
        return HELIKON_ERR_UNSUPPORTED;

    out->encoding = HELIKON_AIFF_PCM_BE;
    if (bits == 8u && !compressed)
        out->encoding = HELIKON_AIFF_PCM_BE; /* AIFF 8-bit is signed */

    if (compressed) {
        if (size < 22u)
            return HELIKON_ERR_FORMAT;
        const uint8_t *type = data + 18;
        if (aiff_id_is(type, "NONE") || aiff_id_is(type, "twos")) {
            out->encoding = HELIKON_AIFF_PCM_BE;
        } else if (aiff_id_is(type, "sowt")) {
            out->encoding = HELIKON_AIFF_PCM_LE;
        } else if (aiff_id_is(type, "23ni")) {
            if (bits != 24u)
                return HELIKON_ERR_FORMAT;
            out->encoding = HELIKON_AIFF_PCM_LE;
        } else if (aiff_id_is(type, "in24")) {
            if (bits != 24u)
                return HELIKON_ERR_FORMAT;
            out->encoding = HELIKON_AIFF_PCM_BE;
        } else if (aiff_id_is(type, "in32")) {
            if (bits != 32u)
                return HELIKON_ERR_FORMAT;
            out->encoding = HELIKON_AIFF_PCM_BE;
        } else if (aiff_id_is(type, "fl32") ||
                   aiff_id_is(type, "FL32")) {
            if (bits != 32u)
                return HELIKON_ERR_FORMAT;
            out->encoding = HELIKON_AIFF_FLOAT32_BE;
        } else if (aiff_id_is(type, "raw ")) {
            if (bits != 8u)
                return HELIKON_ERR_FORMAT;
            out->encoding = HELIKON_AIFF_UNSIGNED8;
        } else {
            return HELIKON_ERR_UNSUPPORTED;
        }
    }

    out->channels = channels;
    out->frames = frames;
    out->bits = bits;
    out->sample_rate = rate;
    return HELIKON_OK;
}

static size_t aiff_frame_bytes(const struct helikon_aiff *aiff)
{
    return (size_t)aiff->channels * ((size_t)aiff->bits / 8u);
}

/*
 * Walks the top-level chunks. A read that runs past the data the caller can see
 * is IO, which the probe turns into NEED_MORE when the stream continues; a walk
 * that ends with no COMM or no SSND is a malformed file, because both are
 * mandatory and neither may follow a chunk the walk refused to skip.
 */
static int aiff_parse(const struct helikon_aiff_reader *reader,
                      struct helikon_aiff *out)
{
    uint8_t header[12];
    int rc = aiff_read_at(reader, 0u, header, sizeof(header));
    if (rc != HELIKON_OK)
        return rc;
    if (!aiff_id_is(header, "FORM"))
        return HELIKON_ERR_NOT_FOUND;
    int compressed = aiff_id_is(header + 8, "AIFC");
    if (!compressed && !aiff_id_is(header + 8, "AIFF"))
        return HELIKON_ERR_NOT_FOUND;

    helikon_memset(out, 0, sizeof(*out));
    uint8_t have_comm = 0u;
    uint8_t have_ssnd = 0u;
    uint64_t offset = 12u;
    for (unsigned int chunk = 0u; chunk < HELIKON_AIFF_CHUNK_LIMIT; ++chunk) {
        uint8_t head[8];
        rc = aiff_read_at(reader, offset, head, sizeof(head));
        if (rc != HELIKON_OK)
            return rc;
        uint32_t size = aiff_be32(head + 4);
        uint64_t payload = offset + 8u;

        if (aiff_id_is(head, "COMM")) {
            uint8_t comm[22];
            size_t want = compressed ? 22u : 18u;
            if (size < want)
                return HELIKON_ERR_FORMAT;
            rc = aiff_read_at(reader, payload, comm, want);
            if (rc != HELIKON_OK)
                return rc;
            rc = aiff_parse_comm(comm, want, compressed, out);
            if (rc != HELIKON_OK)
                return rc;
            have_comm = 1u;
        } else if (aiff_id_is(head, "SSND")) {
            uint8_t sound[8];
            if (size < sizeof(sound))
                return HELIKON_ERR_FORMAT;
            rc = aiff_read_at(reader, payload, sound, sizeof(sound));
            if (rc != HELIKON_OK)
                return rc;
            uint32_t start = aiff_be32(sound);
            if ((uint64_t)size < (uint64_t)sizeof(sound) + (uint64_t)start)
                return HELIKON_ERR_FORMAT;
            out->data_offset = payload + sizeof(sound) + start;
            out->data_bytes = (uint64_t)size - sizeof(sound) - start;
            have_ssnd = 1u;
        }

        if (have_comm != 0u && have_ssnd != 0u)
            return HELIKON_OK;

        offset = payload + (uint64_t)size + (uint64_t)(size & 1u);
    }
    return HELIKON_ERR_FORMAT;
}

/*
 * A frame count larger than the sound chunk can serve is the file's problem, but
 * a decoder that believes it would read past the data. The reported total is the
 * smaller of the two, so a truncated file plays what it has and says so through
 * its duration.
 */
static uint32_t aiff_total_frames(const struct helikon_aiff *aiff)
{
    size_t frame_bytes = aiff_frame_bytes(aiff);
    if (frame_bytes == 0u)
        return 0u;
    uint64_t available = aiff->data_bytes / (uint64_t)frame_bytes;
    return (uint32_t)(available < (uint64_t)aiff->frames
                          ? available
                          : (uint64_t)aiff->frames);
}

static void aiff_fill_info(const struct helikon_aiff *aiff,
                           struct helikon_codec_info *info)
{
    helikon_memset(info, 0, sizeof(*info));
    info->sample_rate = aiff->sample_rate;
    info->total_frames = aiff_total_frames(aiff);
    if (aiff->sample_rate != 0u) {
        uint64_t ms = ((uint64_t)info->total_frames * 1000u) /
                      (uint64_t)aiff->sample_rate;
        info->duration_ms = (uint32_t)ms;
    }
    info->source_channels = aiff->channels;
    info->source_bits = aiff->bits;
    info->output_channels = 2u;
    info->output_bits = 16u;
    info->workspace_bytes = sizeof(struct aiff_state);
    info->flags = HELIKON_CODEC_FLAG_SEEK;
}

static int aiff_probe(const uint8_t *data, size_t size, uint64_t total_size,
                      struct helikon_codec_info *info)
{
    if (data == NULL || info == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (size < 12u)
        return HELIKON_ERR_NOT_FOUND;
    if (!aiff_id_is(data, "FORM"))
        return HELIKON_ERR_NOT_FOUND;
    if (!aiff_id_is(data + 8, "AIFF") && !aiff_id_is(data + 8, "AIFC"))
        return HELIKON_ERR_NOT_FOUND;

    struct aiff_memory memory = { data, size };
    const struct helikon_aiff_reader reader = { aiff_memory_read, &memory };
    struct helikon_aiff aiff;
    int rc = aiff_parse(&reader, &aiff);
    if (rc == HELIKON_ERR_IO) {
        /* The head ended before the chunks did. */
        if (total_size == 0u || total_size > (uint64_t)size)
            return HELIKON_ERR_NEED_MORE;
        return HELIKON_ERR_FORMAT;
    }
    if (rc != HELIKON_OK)
        return rc;

    aiff_fill_info(&aiff, info);
    return HELIKON_OK;
}

static int aiff_open(struct helikon_codec *codec, void *workspace,
                     size_t workspace_size)
{
    if (codec == NULL || codec->source == NULL || workspace == NULL ||
        workspace_size < sizeof(struct aiff_state))
        return HELIKON_ERR_ARGUMENT;

    struct aiff_state *state = workspace;
    helikon_memset(state, 0, sizeof(*state));
    const struct helikon_aiff_reader reader = { aiff_source_read,
                                                (void *)codec->source };
    int rc = aiff_parse(&reader, &state->aiff);
    if (rc != HELIKON_OK)
        return rc;
    aiff_fill_info(&state->aiff, &codec->info);
    codec->state = state;
    return HELIKON_OK;
}

static int16_t aiff_one_sample(const uint8_t *sample,
                               const struct helikon_aiff *aiff)
{
    switch (aiff->encoding) {
    case HELIKON_AIFF_PCM_LE:
        break;
    case HELIKON_AIFF_FLOAT32_BE: {
        union {
            uint32_t bits;
            float value;
        } converted;
        converted.bits = aiff_be32(sample);
        float scaled = converted.value * 32768.0f;
        if (scaled >= 32767.0f)
            return 32767;
        if (scaled <= -32768.0f)
            return -32768;
        return (int16_t)scaled;
    }
    case HELIKON_AIFF_UNSIGNED8:
        return (int16_t)(((int16_t)sample[0] - 128) * 256);
    case HELIKON_AIFF_PCM_BE:
    default:
        break;
    }

    int little = aiff->encoding == HELIKON_AIFF_PCM_LE;
    switch (aiff->bits) {
    case 8u:
        return (int16_t)((int16_t)(int8_t)sample[0] * 256);
    case 16u:
        return (int16_t)(little
                             ? ((uint16_t)sample[1] << 8) | sample[0]
                             : ((uint16_t)sample[0] << 8) | sample[1]);
    case 24u:
        /* Top 16 of 24: truncation, as FLAC 24-bit output does. */
        return (int16_t)(little ? ((uint16_t)sample[2] << 8) | sample[1]
                                : ((uint16_t)sample[0] << 8) | sample[1]);
    case 32u:
    default:
        return (int16_t)(little ? ((uint16_t)sample[3] << 8) | sample[2]
                                : ((uint16_t)sample[0] << 8) | sample[1]);
    }
}

static int aiff_read_pcm(struct helikon_codec *codec, int16_t *pcm,
                         size_t count)
{
    struct aiff_state *state = codec->state;
    const struct helikon_aiff *aiff = &state->aiff;
    size_t frame_bytes = aiff_frame_bytes(aiff);
    uint64_t offset = aiff->data_offset + (uint64_t)state->frame * frame_bytes;
    uint8_t raw[AIFF_BATCH_BYTES];
    size_t done = 0u;

    while (done < count) {
        size_t batch = count - done;
        if (batch > AIFF_BATCH_FRAMES)
            batch = AIFF_BATCH_FRAMES;
        size_t want = batch * frame_bytes;
        size_t got = 0u;
        int rc = codec->source->read_at(
            codec->source->context, offset + done * frame_bytes, raw, want,
            &got);
        if (rc != HELIKON_OK)
            return rc;
        size_t frames = got / frame_bytes;
        if (frames == 0u)
            break;
        for (size_t index = 0u; index < frames; ++index) {
            const uint8_t *frame = raw + index * frame_bytes;
            size_t sample_bytes = (size_t)aiff->bits / 8u;
            int16_t left = aiff_one_sample(frame, aiff);
            int16_t right = aiff->channels == 1u
                                ? left
                                : aiff_one_sample(frame + sample_bytes, aiff);
            pcm[(done + index) * 2u] = left;
            pcm[(done + index) * 2u + 1u] = right;
        }
        done += frames;
        if (frames < batch)
            break;
    }
    state->frame += (uint32_t)done;
    return HELIKON_OK;
}

static int aiff_read(struct helikon_codec *codec, int16_t *pcm,
                     size_t max_frames, size_t *frames)
{
    if (codec == NULL || codec->state == NULL || pcm == NULL ||
        frames == NULL)
        return HELIKON_ERR_ARGUMENT;
    struct aiff_state *state = codec->state;
    uint32_t total = aiff_total_frames(&state->aiff);
    *frames = 0u;
    if (state->frame >= total)
        return HELIKON_OK;
    size_t count = (size_t)(total - state->frame);
    if (count > max_frames)
        count = max_frames;
    int rc = aiff_read_pcm(codec, pcm, count);
    if (rc != HELIKON_OK)
        return rc;
    *frames = count;
    return HELIKON_OK;
}

static int aiff_seek(struct helikon_codec *codec, uint32_t frame)
{
    if (codec == NULL || codec->state == NULL)
        return HELIKON_ERR_ARGUMENT;
    struct aiff_state *state = codec->state;
    if (frame > aiff_total_frames(&state->aiff))
        return HELIKON_ERR_ARGUMENT;
    state->frame = frame;
    return HELIKON_OK;
}

static void aiff_close(struct helikon_codec *codec)
{
    if (codec != NULL)
        codec->state = NULL;
}

static const char *const aiff_extensions[] = { "aif", "aiff", "aifc", NULL };

const struct helikon_codec_ops helikon_aiff_codec_ops = {
    .name = "aiff",
    .extensions = aiff_extensions,
    .capabilities = HELIKON_CODEC_CAP_SEEK,
    .streaming = 1u,
    .stack_bytes = AIFF_BATCH_BYTES + 1024u,
    .min_clock_hz = 54000000u,
    .recommended_clock_hz = 216000000u,
    .probe = aiff_probe,
    .open = aiff_open,
    .read = aiff_read,
    .seek = aiff_seek,
    .close = aiff_close,
};
