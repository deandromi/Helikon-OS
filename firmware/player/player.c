/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "player.h"

#include <stddef.h>

#include "error.h"

static int player_pcm_source(void *context, int16_t *pcm, size_t max_frames,
                             size_t *frames)
{
    struct helikon_player *player = context;
    int rc = helikon_codec_read(&player->codec, pcm, max_frames, frames);
    if (rc != HELIKON_OK) {
        player->last_error = rc;
        player->last_stage = HELIKON_AUDIO_STAGE_CODEC_DATA;
    }
    return rc;
}

int helikon_player_open(struct helikon_player *player, const uint8_t *data,
                        size_t size, struct helikon_pmu *pmu,
                        struct helikon_clock *clock, uint32_t timeout_ms)
{
    if (player == NULL)
        return HELIKON_ERR_ARGUMENT;

    player->opened = 0;
    player->started = 0;
    player->compressed = 0;
    player->offset = 0u;
    player->last_error = HELIKON_OK;
    player->last_stage = HELIKON_AUDIO_STAGE_NONE;
    player->audio.stage = HELIKON_AUDIO_STAGE_NONE;

    int rc = helikon_wav_parse(data, size, &player->wav);
    if (rc != HELIKON_OK) {
        const struct helikon_codec_ops *ops = NULL;
        struct helikon_codec_info info;
        rc = helikon_codec_probe(data, size, &ops, &info);
        if (rc != HELIKON_OK) {
            player->last_error = rc;
            if (size >= 4u && data[0] == 'f' && data[1] == 'L' &&
                data[2] == 'a' && data[3] == 'C')
                player->last_stage = HELIKON_AUDIO_STAGE_CODEC_DATA;
            return rc;
        }
        rc = helikon_codec_open(&player->codec, ops, data, size,
                                player->codec_workspace,
                                sizeof(player->codec_workspace));
        if (rc != HELIKON_OK) {
            player->last_error = rc;
            player->last_stage = HELIKON_AUDIO_STAGE_CODEC_DATA;
            return rc;
        }
        player->compressed = 1;
        player->wav.sample_rate = info.sample_rate;
        player->wav.duration_ms = info.duration_ms;
        player->wav.channels = 2u;
        player->wav.bits = 16u;
        player->wav.pcm = NULL;
        player->wav.bytes = 0u;
    }

    rc = helikon_audio_init(&player->audio, pmu, clock, timeout_ms);
    if (rc != HELIKON_OK) {
        player->last_error = rc;
        player->last_stage = player->audio.stage;
        if (player->compressed)
            helikon_codec_close(&player->codec);
        return rc;
    }
    rc = helikon_audio_set_rate(&player->audio, player->wav.sample_rate);
    if (rc != HELIKON_OK) {
        player->last_error = rc;
        player->last_stage = player->audio.stage;
        helikon_audio_stream_stop(&player->audio);
        if (player->compressed)
            helikon_codec_close(&player->codec);
        return rc;
    }
    player->opened = 1;
    return HELIKON_OK;
}

int helikon_player_play_chunk(struct helikon_player *player,
                              uint32_t max_frames)
{
    (void)max_frames;
    if (player == NULL || !player->opened)
        return HELIKON_ERR_ARGUMENT;

    if (!player->started) {
        int rc;
        if (player->compressed) {
            rc = helikon_audio_stream_start_source(
                &player->audio, player_pcm_source, player,
                player->codec.info.total_frames);
        } else {
            size_t frame_bytes = (size_t)player->wav.channels * 2u;
            if (frame_bytes == 0u)
                return HELIKON_ERR_FORMAT;
            size_t frames = player->wav.bytes / frame_bytes;
            rc = helikon_audio_stream_start(
                &player->audio,
                (const int16_t *)(const void *)player->wav.pcm, frames,
                player->wav.channels);
        }
        if (rc != HELIKON_OK) {
            player->last_error = rc;
            player->last_stage = player->audio.stage;
            return rc;
        }
        player->started = 1;
        player->offset = 0u;
        player->last_error = HELIKON_OK;
        player->last_stage = HELIKON_AUDIO_STAGE_NONE;
        return 0;
    }

    int rc = helikon_audio_stream_service(&player->audio);
    if (rc != HELIKON_OK) {
        player->last_error = rc;
        player->last_stage = player->audio.stage;
        return rc;
    }
    size_t frame_bytes = (size_t)player->wav.channels * 2u;
    player->offset = (size_t)helikon_audio_stream_position_frames(
                         &player->audio) *
                     frame_bytes;
    player->last_error = HELIKON_OK;
    player->last_stage = HELIKON_AUDIO_STAGE_NONE;
    return 0;
}

int helikon_player_pause(struct helikon_player *player)
{
    if (player == NULL || !player->opened)
        return HELIKON_ERR_ARGUMENT;
    return helikon_audio_stream_pause(&player->audio);
}

int helikon_player_resume(struct helikon_player *player)
{
    if (player == NULL || !player->opened)
        return HELIKON_ERR_ARGUMENT;
    return helikon_audio_stream_resume(&player->audio);
}

void helikon_player_stop(struct helikon_player *player)
{
    if (player == NULL || !player->opened)
        return;
    helikon_audio_stream_stop(&player->audio);
    if (player->compressed)
        helikon_codec_close(&player->codec);
    player->compressed = 0;
    player->started = 0;
    player->opened = 0;
}

uint32_t helikon_player_position_ms(const struct helikon_player *player)
{
    if (player == NULL || player->wav.sample_rate == 0u)
        return 0u;
    uint32_t frames = helikon_audio_stream_position_frames(&player->audio);
    return (uint32_t)(((uint64_t)frames * 1000u) /
                      player->wav.sample_rate);
}

uint32_t helikon_player_duration_ms(const struct helikon_player *player)
{
    return player == NULL ? 0u : player->wav.duration_ms;
}

int helikon_player_finished(const struct helikon_player *player)
{
    return player == NULL || (player->started &&
                              helikon_audio_stream_finished(&player->audio));
}

const char *helikon_player_error_text(const struct helikon_player *player)
{
    if (player == NULL)
        return "PLAYER";

    switch (player->last_stage) {
    case HELIKON_AUDIO_STAGE_CODEC:
        return "CODEC/I2C";
    case HELIKON_AUDIO_STAGE_RATE:
        return "CODEC RATE";
    case HELIKON_AUDIO_STAGE_I2S:
        return "I2S/CLOCK";
    case HELIKON_AUDIO_STAGE_DMA_SETUP:
        return "DMA SETUP";
    case HELIKON_AUDIO_STAGE_DMA_WAIT:
        return player->last_error == HELIKON_ERR_IO ? "DMA ERROR"
                                                    : "DMA TIMEOUT";
    case HELIKON_AUDIO_STAGE_DMA_ACTIVE:
        return "DMA ACTIVE";
    case HELIKON_AUDIO_STAGE_CACHE:
        return "CACHE";
    case HELIKON_AUDIO_STAGE_BUFFER:
        return "BUFFER";
    case HELIKON_AUDIO_STAGE_DMA_CLOCK:
        return "DMA CLOCK";
    case HELIKON_AUDIO_STAGE_DMA_CONFIG:
        return "DMA CONFIG";
    case HELIKON_AUDIO_STAGE_I2S_FIFO:
        return "I2S FIFO";
    case HELIKON_AUDIO_STAGE_STREAM_UNDERRUN:
        return "UNDERRUN";
    case HELIKON_AUDIO_STAGE_CODEC_DATA:
        switch (player->last_error) {
        case HELIKON_ERR_FORMAT:
            return "FLAC FORMAT";
        case HELIKON_ERR_UNSUPPORTED:
            return "FLAC PROFILE";
        case HELIKON_ERR_BUFFER:
            return "FLAC BUFFER";
        default:
            return "FLAC DATA";
        }
    case HELIKON_AUDIO_STAGE_NONE:
    default:
        break;
    }

    switch (player->last_error) {
    case HELIKON_ERR_UNSUPPORTED:
        return "WAV PROFILE";
    case HELIKON_ERR_FORMAT:
        return "WAV FORMAT";
    case HELIKON_ERR_ARGUMENT:
    case HELIKON_ERR_BUFFER:
        return "BUFFER";
    case HELIKON_ERR_BUSY:
        return "DMA TIMEOUT";
    case HELIKON_ERR_IO:
        return "AUDIO I/O";
    default:
        return "PLAYBACK";
    }
}
