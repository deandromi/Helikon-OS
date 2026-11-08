/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "codec.h"

#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include "mem.h"
#include "wav.h"

struct wav_state {
    struct helikon_wav wav;
    size_t frame;
};

static uint32_t wav_total_frames(const struct helikon_wav *wav)
{
    size_t frame_bytes = (size_t)wav->channels * 2u;
    if (frame_bytes == 0u)
        return 0u;
    return (uint32_t)(wav->bytes / frame_bytes);
}

static int wav_probe(const uint8_t *data, size_t size,
                     struct helikon_codec_info *info)
{
    if (data == NULL || info == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (size < 12u || data[0] != 'R' || data[1] != 'I' || data[2] != 'F' ||
        data[3] != 'F')
        return HELIKON_ERR_NOT_FOUND;

    struct helikon_wav wav;
    int rc = helikon_wav_parse(data, size, &wav);
    if (rc != HELIKON_OK)
        return rc;

    uint32_t frames = wav_total_frames(&wav);
    helikon_memset(info, 0, sizeof(*info));
    info->sample_rate = wav.sample_rate;
    info->total_frames = frames;
    info->duration_ms = wav.duration_ms;
    info->source_channels = wav.channels;
    info->source_bits = 16u;
    info->output_channels = 2u;
    info->output_bits = 16u;
    info->workspace_bytes = sizeof(struct wav_state);
    info->flags = HELIKON_CODEC_FLAG_SEEK;
    return HELIKON_OK;
}

static int wav_open(struct helikon_codec *codec, void *workspace,
                    size_t workspace_size)
{
    if (codec == NULL || workspace == NULL ||
        workspace_size < sizeof(struct wav_state))
        return HELIKON_ERR_ARGUMENT;

    struct wav_state *state = workspace;
    helikon_memset(state, 0, sizeof(*state));
    int rc = helikon_wav_parse(codec->data, codec->size, &state->wav);
    if (rc != HELIKON_OK)
        return rc;
    codec->state = state;
    return HELIKON_OK;
}

static int wav_read(struct helikon_codec *codec, int16_t *pcm,
                    size_t max_frames, size_t *frames)
{
    if (codec == NULL || codec->state == NULL || pcm == NULL ||
        frames == NULL)
        return HELIKON_ERR_ARGUMENT;

    struct wav_state *state = codec->state;
    uint32_t total = wav_total_frames(&state->wav);
    if (state->frame >= total) {
        *frames = 0u;
        return HELIKON_OK;
    }

    size_t available = total - state->frame;
    size_t count = available < max_frames ? available : max_frames;
    const int16_t *source =
        (const int16_t *)(const void *)(state->wav.pcm +
                                        state->frame * state->wav.channels * 2u);
    if (state->wav.channels == 2u) {
        for (size_t index = 0u; index < count * 2u; ++index)
            pcm[index] = source[index];
    } else {
        for (size_t index = 0u; index < count; ++index) {
            pcm[index * 2u] = source[index];
            pcm[index * 2u + 1u] = source[index];
        }
    }
    state->frame += count;
    *frames = count;
    return HELIKON_OK;
}

static int wav_seek(struct helikon_codec *codec, uint32_t frame)
{
    if (codec == NULL || codec->state == NULL)
        return HELIKON_ERR_ARGUMENT;
    struct wav_state *state = codec->state;
    if (frame > wav_total_frames(&state->wav))
        return HELIKON_ERR_ARGUMENT;
    state->frame = frame;
    return HELIKON_OK;
}

static void wav_close(struct helikon_codec *codec)
{
    if (codec != NULL)
        codec->state = NULL;
}

const struct helikon_codec_ops helikon_wav_codec_ops = {
    .name = "wav",
    .probe = wav_probe,
    .open = wav_open,
    .read = wav_read,
    .seek = wav_seek,
    .close = wav_close,
};

static const struct helikon_codec_ops *const codec_registry[] = {
    &helikon_wav_codec_ops,
    &helikon_flac_codec_ops,
};

int helikon_codec_probe(const uint8_t *data, size_t size,
                        const struct helikon_codec_ops **ops,
                        struct helikon_codec_info *info)
{
    if (data == NULL || ops == NULL || info == NULL)
        return HELIKON_ERR_ARGUMENT;

    for (size_t index = 0u;
         index < sizeof(codec_registry) / sizeof(codec_registry[0]); ++index) {
        int rc = codec_registry[index]->probe(data, size, info);
        if (rc == HELIKON_ERR_NOT_FOUND)
            continue;
        if (rc == HELIKON_OK) {
            *ops = codec_registry[index];
            return HELIKON_OK;
        }
        return rc;
    }
    return HELIKON_ERR_UNSUPPORTED;
}

int helikon_codec_open(struct helikon_codec *codec,
                       const struct helikon_codec_ops *ops,
                       const uint8_t *data, size_t size, void *workspace,
                       size_t workspace_size)
{
    if (codec == NULL || ops == NULL || data == NULL || workspace == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(codec, 0, sizeof(*codec));
    codec->ops = ops;
    codec->data = data;
    codec->size = size;
    codec->workspace = workspace;
    codec->workspace_size = workspace_size;

    int rc = ops->probe(data, size, &codec->info);
    if (rc != HELIKON_OK)
        return rc;
    if (workspace_size < codec->info.workspace_bytes)
        return HELIKON_ERR_BUFFER;
    return ops->open(codec, workspace, workspace_size);
}

int helikon_codec_read(struct helikon_codec *codec, int16_t *pcm,
                       size_t max_frames, size_t *frames)
{
    if (codec == NULL || codec->ops == NULL || codec->state == NULL)
        return HELIKON_ERR_ARGUMENT;
    return codec->ops->read(codec, pcm, max_frames, frames);
}

int helikon_codec_seek(struct helikon_codec *codec, uint32_t frame)
{
    if (codec == NULL || codec->ops == NULL || codec->state == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (codec->ops->seek == NULL)
        return HELIKON_ERR_UNSUPPORTED;
    return codec->ops->seek(codec, frame);
}

void helikon_codec_close(struct helikon_codec *codec)
{
    if (codec == NULL)
        return;
    if (codec->ops != NULL && codec->ops->close != NULL)
        codec->ops->close(codec);
    codec->state = NULL;
    codec->ops = NULL;
}
