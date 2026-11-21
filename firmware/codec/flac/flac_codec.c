/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "flac_decoder.h"

#include <stddef.h>
#include <stdint.h>

#include "codec.h"
#include "error.h"
#include "mem.h"
#include "static_assert.h"

HELIKON_STATIC_ASSERT(sizeof(struct helikon_flac_decoder) <=
                          HELIKON_CODEC_WORKSPACE_MAX,
                      flac_workspace_fits);

static int flac_probe(const uint8_t *data, size_t size, uint64_t total_size,
                      struct helikon_codec_info *info)
{
    int rc = helikon_flac_probe(data, size, info);
    /*
     * A head that ends inside the metadata cannot decide: open parses the
     * stream and reports the real error (including an unsupported profile).
     */
    if (rc != HELIKON_OK && rc != HELIKON_ERR_NOT_FOUND && rc != HELIKON_ERR_ARGUMENT &&
        total_size != size)
        return HELIKON_ERR_NEED_MORE;
    if (rc == HELIKON_ERR_ARGUMENT && total_size != size)
        return HELIKON_ERR_NEED_MORE;
    return rc;
}

static int flac_open(struct helikon_codec *codec, void *workspace,
                     size_t workspace_size)
{
    if (codec == NULL || workspace == NULL ||
        workspace_size < sizeof(struct helikon_flac_decoder))
        return HELIKON_ERR_ARGUMENT;

    struct helikon_flac_decoder *decoder = workspace;
    const uint8_t *data = NULL;
    size_t size = 0u;
    int rc;
    if (helikon_codec_memory_span(codec, &data, &size)) {
        rc = helikon_flac_open(decoder, data, size);
    } else {
        rc = helikon_flac_open_source(decoder, codec->source);
    }
    if (rc != HELIKON_OK)
        return rc;

    /* Open is authoritative: a NEED_MORE probe filled no info. */
    helikon_memset(&codec->info, 0, sizeof(codec->info));
    codec->info.sample_rate = decoder->sample_rate;
    codec->info.total_frames = decoder->total_frames;
    codec->info.duration_ms =
        (uint32_t)(((uint64_t)decoder->total_frames * 1000u) /
                   decoder->sample_rate);
    codec->info.source_channels = decoder->channels;
    codec->info.source_bits = decoder->bits;
    codec->info.output_channels = 2u;
    codec->info.output_bits = 16u;
    codec->info.workspace_bytes = sizeof(struct helikon_flac_decoder);
    codec->info.flags = HELIKON_CODEC_FLAG_SEEK;
    codec->state = decoder;
    return HELIKON_OK;
}

static int flac_read(struct helikon_codec *codec, int16_t *pcm,
                     size_t max_frames, size_t *frames)
{
    if (codec == NULL || codec->state == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_flac_read(codec->state, pcm, max_frames, frames);
}

static int flac_seek(struct helikon_codec *codec, uint32_t frame)
{
    if (codec == NULL || codec->state == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_flac_seek(codec->state, frame);
}

static void flac_close(struct helikon_codec *codec)
{
    if (codec == NULL)
        return;
    if (codec->state != NULL)
        helikon_flac_close(codec->state);
    codec->state = NULL;
}

static const char *const flac_extensions[] = { "flac", NULL };

const struct helikon_codec_ops helikon_flac_codec_ops = {
    .name = "flac",
    .extensions = flac_extensions,
    .capabilities = HELIKON_CODEC_CAP_SEEK | HELIKON_CODEC_CAP_GAPLESS,
    .streaming = 1u,
    .stack_bytes = 4096u,
    .min_clock_hz = 54000000u,
    .recommended_clock_hz = 216000000u,
    .probe = flac_probe,
    .open = flac_open,
    .read = flac_read,
    .seek = flac_seek,
    .close = flac_close,
};
