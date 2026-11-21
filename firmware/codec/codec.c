/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "codec.h"

#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include "mem.h"
#include "mp3/mp3.h"
#include "wav.h"

struct wav_state {
    struct helikon_wav wav;
    uint64_t frame;
};

static uint32_t wav_total_frames(const struct helikon_wav *wav)
{
    size_t frame_bytes = (size_t)wav->channels * 2u;
    if (frame_bytes == 0u)
        return 0u;
    return (uint32_t)(wav->bytes / frame_bytes);
}

static void wav_fill_info(const struct helikon_wav *wav,
                          struct helikon_codec_info *info)
{
    helikon_memset(info, 0, sizeof(*info));
    info->sample_rate = wav->sample_rate;
    info->total_frames = wav_total_frames(wav);
    info->duration_ms = wav->duration_ms;
    info->source_channels = wav->channels;
    info->source_bits = 16u;
    info->output_channels = 2u;
    info->output_bits = 16u;
    info->workspace_bytes = sizeof(struct wav_state);
    info->flags = HELIKON_CODEC_FLAG_SEEK;
}

static int wav_probe(const uint8_t *data, size_t size, uint64_t total_size,
                     struct helikon_codec_info *info)
{
    if (data == NULL || info == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (size < 4u || data[0] != 'R' || data[1] != 'I' || data[2] != 'F' ||
        data[3] != 'F')
        return HELIKON_ERR_NOT_FOUND;

    struct helikon_wav wav;
    int rc = helikon_wav_parse_prefix(data, size, total_size, &wav);
    if (rc == HELIKON_ERR_FORMAT && total_size != size)
        return HELIKON_ERR_NEED_MORE;
    if (rc != HELIKON_OK)
        return rc;

    wav_fill_info(&wav, info);
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
    int rc = helikon_wav_parse_source(codec->source, &state->wav);
    if (rc != HELIKON_OK)
        return rc;
    /* Open is authoritative: a NEED_MORE probe filled no info. */
    wav_fill_info(&state->wav, &codec->info);
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

    size_t available = total - (size_t)state->frame;
    size_t count = available < max_frames ? available : max_frames;
    size_t frame_bytes = (size_t)state->wav.channels * 2u;
    uint64_t offset = state->wav.pcm_offset + state->frame * frame_bytes;

    /*
     * RIFF PCM is little-endian; the 6G CPU and the host are both
     * little-endian, so the payload becomes int16 samples by copy.
     */
    if (state->wav.channels == 2u) {
        /*
         * A media adapter may answer with less than asked (a short read, a
         * chunked test source, a sector-aligned window). Keep pulling until
         * the request is met or the source reports nothing left; offsets are
         * absolute, so a trailing partial frame is simply read again.
         */
        size_t done = 0u;
        while (done < count) {
            size_t got = 0u;
            int rc = codec->source->read_at(codec->source->context,
                                            offset + done * frame_bytes,
                                            pcm + done * 2u,
                                            (count - done) * frame_bytes, &got);
            if (rc != HELIKON_OK)
                return rc;
            size_t batch = got / frame_bytes;
            if (batch == 0u)
                break;
            done += batch;
        }
        count = done;
    } else {
        int16_t mono[256];
        size_t done = 0u;
        while (done < count) {
            size_t batch = count - done;
            if (batch > sizeof(mono) / sizeof(mono[0]))
                batch = sizeof(mono) / sizeof(mono[0]);
            size_t got = 0u;
            int rc = codec->source->read_at(codec->source->context,
                                            offset + done * 2u, mono,
                                            batch * 2u, &got);
            if (rc != HELIKON_OK)
                return rc;
            batch = got / 2u;
            for (size_t index = 0u; index < batch; ++index) {
                pcm[(done + index) * 2u] = mono[index];
                pcm[(done + index) * 2u + 1u] = mono[index];
            }
            done += batch;
            if (batch == 0u)
                break;
        }
        count = done;
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

static const char *const wav_extensions[] = { "wav", NULL };

const struct helikon_codec_ops helikon_wav_codec_ops = {
    .name = "wav",
    .extensions = wav_extensions,
    .capabilities = HELIKON_CODEC_CAP_SEEK,
    .streaming = 1u,
    .stack_bytes = 2048u,
    .min_clock_hz = 54000000u,
    .recommended_clock_hz = 216000000u,
    .probe = wav_probe,
    .open = wav_open,
    .read = wav_read,
    .seek = wav_seek,
    .close = wav_close,
};

static const struct helikon_codec_ops *const codec_registry[] = {
    &helikon_wav_codec_ops,
    &helikon_flac_codec_ops,
    &helikon_mp3_codec_ops,
};

int helikon_codec_matches_extension(const struct helikon_codec_ops *ops,
                                    const char *path)
{
    if (ops == NULL || ops->extensions == NULL || path == NULL)
        return 0;
    int have_dot = 0;
    const char *dot = path;
    for (; *path != '\0'; ++path) {
        if (*path == '.') {
            dot = path + 1;
            have_dot = 1;
        }
    }
    if (!have_dot)
        return 0;
    for (const char *const *ext = ops->extensions; *ext != NULL; ++ext) {
        const char *candidate = *ext;
        const char *remaining = dot;
        while (*candidate != '\0' && *remaining != '\0') {
            char left = *candidate;
            if (left >= 'A' && left <= 'Z')
                left = (char)(left - 'A' + 'a');
            char right = *remaining;
            if (right >= 'A' && right <= 'Z')
                right = (char)(right - 'A' + 'a');
            if (left != right)
                break;
            ++candidate;
            ++remaining;
        }
        if (*candidate == '\0' && *remaining == '\0')
            return 1;
    }
    return 0;
}

unsigned int helikon_codec_registry(const struct helikon_codec_ops **ops,
                                    unsigned int max_ops)
{
    const size_t count =
        sizeof(codec_registry) / sizeof(codec_registry[0]);
    if (ops != NULL) {
        for (size_t index = 0;
             index < count && (unsigned int)index < max_ops; ++index)
            ops[index] = codec_registry[index];
    }
    return (unsigned int)count;
}

/*
 * The memory adapter keeps every existing whole-buffer caller and test
 * working unchanged: it reports the buffer as one contiguous span, which is
 * exactly what a whole-buffer decoder needs. Its context is the codec, so the
 * adapter lives as long as the open codec and never holds a stack pointer.
 */
static int memory_read_at(void *context, uint64_t offset, void *buffer,
                          size_t length, size_t *read)
{
    const struct helikon_codec *codec = context;
    if (codec == NULL || read == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    *read = 0u;
    if (offset > codec->size)
        return HELIKON_ERR_FORMAT;
    size_t available = codec->size - (size_t)offset;
    size_t count = available < length ? available : length;
    if (count != 0u)
        helikon_memcpy(buffer, codec->data + (size_t)offset, count);
    *read = count;
    return HELIKON_OK;
}

int helikon_codec_probe(const uint8_t *data, size_t size,
                        const struct helikon_codec_ops **ops,
                        struct helikon_codec_info *info)
{
    if (data == NULL || ops == NULL || info == NULL || size == 0u)
        return HELIKON_ERR_ARGUMENT;

    for (size_t index = 0u;
         index < sizeof(codec_registry) / sizeof(codec_registry[0]); ++index) {
        int rc = codec_registry[index]->probe(data, size, size, info);
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

int helikon_codec_probe_source(const struct helikon_codec_source *source,
                               const struct helikon_codec_ops **ops,
                               struct helikon_codec_info *info)
{
    if (source == NULL || source->read_at == NULL || ops == NULL ||
        info == NULL)
        return HELIKON_ERR_ARGUMENT;

    static uint8_t head[HELIKON_CODEC_PROBE_BYTES];
    uint64_t limit = source->size != 0u && source->size < sizeof(head)
                         ? source->size
                         : sizeof(head);
    size_t got = 0u;
    int rc = source->read_at(source->context, 0u, head, (size_t)limit, &got);
    if (rc != HELIKON_OK)
        return rc;
    if (got == 0u)
        return HELIKON_ERR_UNSUPPORTED;

    for (size_t index = 0u;
         index < sizeof(codec_registry) / sizeof(codec_registry[0]); ++index) {
        const struct helikon_codec_ops *candidate = codec_registry[index];
        rc = candidate->probe(head, got, source->size, info);
        if (rc == HELIKON_ERR_NOT_FOUND)
            continue;
        if (rc == HELIKON_ERR_NEED_MORE) {
            if (!candidate->streaming)
                continue;
            helikon_memset(info, 0, sizeof(*info));
            *ops = candidate;
            return HELIKON_OK;
        }
        if (rc == HELIKON_OK) {
            *ops = candidate;
            return HELIKON_OK;
        }
        return rc;
    }
    return HELIKON_ERR_UNSUPPORTED;
}

int helikon_codec_memory_span(const struct helikon_codec *codec,
                              const uint8_t **data, size_t *size)
{
    if (codec == NULL || data == NULL || size == NULL)
        return 0;
    if (!codec->memory)
        return 0;
    *data = codec->data;
    *size = codec->size;
    return 1;
}

static int codec_begin(struct helikon_codec *codec,
                       const struct helikon_codec_ops *ops,
                       const struct helikon_codec_source *source,
                       const uint8_t *data, size_t size, int memory,
                       void *workspace, size_t workspace_size)
{
    if (codec == NULL || ops == NULL || source == NULL || workspace == NULL ||
        ops->open == NULL || codec->state != NULL)
        return HELIKON_ERR_ARGUMENT;

    codec->ops = ops;
    codec->source = source;
    codec->data = data;
    codec->size = size;
    codec->memory = memory;
    codec->workspace = workspace;
    codec->workspace_size = workspace_size;

    /* `source->size` is 0 when the media cannot report a length. */
    int rc = ops->probe(data, size, source->size, &codec->info);
    if (rc == HELIKON_ERR_NEED_MORE) {
        /* A streaming decoder resolves the head during open. */
        if (!ops->streaming)
            return HELIKON_ERR_UNSUPPORTED;
        helikon_memset(&codec->info, 0, sizeof(codec->info));
    } else if (rc == HELIKON_ERR_NOT_FOUND) {
        /* The explicitly requested descriptor does not recognize the head. */
        return HELIKON_ERR_UNSUPPORTED;
    } else if (rc != HELIKON_OK) {
        return rc;
    }

    if (workspace_size < codec->info.workspace_bytes)
        return HELIKON_ERR_BUFFER;
    rc = ops->open(codec, workspace, workspace_size);
    if (rc != HELIKON_OK) {
        codec->state = NULL;
        return rc;
    }
    if (codec->state == NULL)
        return HELIKON_ERR_FORMAT;
    return HELIKON_OK;
}

int helikon_codec_open_source(struct helikon_codec *codec,
                              const struct helikon_codec_ops *ops,
                              const struct helikon_codec_source *source,
                              void *workspace, size_t workspace_size)
{
    if (codec == NULL || ops == NULL || source == NULL ||
        source->read_at == NULL || workspace == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(codec, 0, sizeof(*codec));

    /*
     * A whole-buffer decoder is refused a pull source rather than handed a
     * header prefix it would mistake for the stream.
     */
    if (!ops->streaming)
        return HELIKON_ERR_UNSUPPORTED;

    uint64_t limit = source->size != 0u && source->size < HELIKON_CODEC_PROBE_BYTES
                         ? source->size
                         : HELIKON_CODEC_PROBE_BYTES;
    size_t got = 0u;
    int rc = source->read_at(source->context, 0u, codec->prefix,
                             (size_t)limit, &got);
    if (rc != HELIKON_OK)
        return rc;
    if (got == 0u)
        return HELIKON_ERR_UNSUPPORTED;

    return codec_begin(codec, ops, source, codec->prefix, got, 0, workspace,
                       workspace_size);
}

int helikon_codec_open(struct helikon_codec *codec,
                       const struct helikon_codec_ops *ops,
                       const uint8_t *data, size_t size, void *workspace,
                       size_t workspace_size)
{
    if (codec == NULL || ops == NULL || data == NULL || workspace == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(codec, 0, sizeof(*codec));

    codec->data = data;
    codec->size = size;
    codec->memory = 1;
    codec->source_storage.read_at = memory_read_at;
    codec->source_storage.size = size;
    codec->source_storage.context = codec;

    /* The caller's buffer outlives the codec; decoders keep this pointer. */
    return codec_begin(codec, ops, &codec->source_storage, data, size, 1,
                       workspace, workspace_size);
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
    codec->source = NULL;
    codec->memory = 0;
    codec->data = NULL;
    codec->size = 0u;
}
