/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "flac_decoder.h"

#include <stddef.h>
#include <stdint.h>

#include "codec.h"
#include "error.h"
#include "static_assert.h"

HELIKON_STATIC_ASSERT(sizeof(struct helikon_flac_decoder) <=
                          HELIKON_CODEC_WORKSPACE_MAX,
                      flac_workspace_fits);

static int flac_probe(const uint8_t *data, size_t size,
                      struct helikon_codec_info *info)
{
    return helikon_flac_probe(data, size, info);
}

static int flac_open(struct helikon_codec *codec, void *workspace,
                     size_t workspace_size)
{
    if (codec == NULL || workspace == NULL ||
        workspace_size < sizeof(struct helikon_flac_decoder))
        return HELIKON_ERR_ARGUMENT;

    struct helikon_flac_decoder *decoder = workspace;
    int rc = helikon_flac_open(decoder, codec->data, codec->size);
    if (rc != HELIKON_OK)
        return rc;
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

const struct helikon_codec_ops helikon_flac_codec_ops = {
    .name = "flac",
    .probe = flac_probe,
    .open = flac_open,
    .read = flac_read,
    .seek = flac_seek,
    .close = flac_close,
};
