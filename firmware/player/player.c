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

/* Output frames described by the open file, before any seek. */
static uint32_t player_total_frames(const struct helikon_player *player)
{
    if (player->compressed)
        return player->codec.info.total_frames;
    size_t frame_bytes = (size_t)player->wav.channels * 2u;
    if (frame_bytes == 0u)
        return 0u;
    return (uint32_t)(player->wav.bytes / frame_bytes);
}

static uint32_t player_stream_frames(const struct helikon_player *player)
{
    return helikon_audio_stream_position_frames(&player->audio);
}

/*
 * Starts the PCM stream at `player->start_frame`. Compressed audio pulls from
 * the codec (already positioned by the open or the seek), resident WAV hands
 * the HAL a pointer into the payload.
 */
static int player_start_stream(struct helikon_player *player)
{
    uint32_t total = player_total_frames(player);
    uint32_t start = player->start_frame < total ? player->start_frame : total;
    size_t remaining = (size_t)(total - start);
    if (remaining == 0u)
        return HELIKON_ERR_FORMAT;

    int rc;
    if (player->compressed) {
        rc = helikon_audio_stream_start_source(&player->audio,
                                               player_pcm_source, player,
                                               remaining);
    } else {
        size_t frame_bytes = (size_t)player->wav.channels * 2u;
        if (frame_bytes == 0u || player->wav.pcm == NULL)
            return HELIKON_ERR_FORMAT;
        const int16_t *pcm = (const int16_t *)(const void *)player->wav.pcm;
        pcm += (size_t)start * (size_t)player->wav.channels;
        rc = helikon_audio_stream_start(&player->audio, pcm, remaining,
                                        player->wav.channels);
    }
    if (rc != HELIKON_OK) {
        player->last_error = rc;
        player->last_stage = player->audio.stage;
        return rc;
    }
    player->started = 1;
    player->offset = (size_t)start * (size_t)player->wav.channels * 2u;
    player->last_error = HELIKON_OK;
    player->last_stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

/*
 * Brings up the audio HAL for whatever the codec/wav fields now describe.
 * Shared by the buffer and media opens so the two paths cannot drift.
 */
static int player_start_audio(struct helikon_player *player,
                              struct helikon_pmu *pmu,
                              struct helikon_clock *clock, uint32_t timeout_ms)
{
    int rc = helikon_audio_init(&player->audio, pmu, clock, timeout_ms);
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
    player->start_frame = 0u;
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

    return player_start_audio(player, pmu, clock, timeout_ms);
}

int helikon_player_open_media(struct helikon_player *player,
                              const struct helikon_codec_source *source,
                              struct helikon_pmu *pmu,
                              struct helikon_clock *clock, uint32_t timeout_ms)
{
    if (player == NULL || source == NULL || source->read_at == NULL)
        return HELIKON_ERR_ARGUMENT;

    player->opened = 0;
    player->started = 0;
    player->compressed = 0;
    player->offset = 0u;
    player->start_frame = 0u;
    player->last_error = HELIKON_OK;
    player->last_stage = HELIKON_AUDIO_STAGE_NONE;
    player->audio.stage = HELIKON_AUDIO_STAGE_NONE;

    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    int rc = helikon_codec_probe_source(source, &ops, &info);
    if (rc != HELIKON_OK) {
        player->last_error = rc;
        if (rc == HELIKON_ERR_UNSUPPORTED)
            player->last_stage = HELIKON_AUDIO_STAGE_NONE;
        return rc;
    }

    rc = helikon_codec_open_source(&player->codec, ops, source,
                                   player->codec_workspace,
                                   sizeof(player->codec_workspace));
    if (rc != HELIKON_OK) {
        player->last_error = rc;
        player->last_stage = HELIKON_AUDIO_STAGE_CODEC_DATA;
        return rc;
    }

    /* Open is authoritative: a NEED_MORE probe filled no info. */
    const struct helikon_codec_info *opened = &player->codec.info;
    player->compressed = 1;
    player->wav.pcm = NULL;
    player->wav.pcm_offset = 0u;
    player->wav.bytes = 0u;
    player->wav.sample_rate = opened->sample_rate;
    player->wav.duration_ms = opened->duration_ms;
    player->wav.channels = 2u;
    player->wav.bits = 16u;
    return player_start_audio(player, pmu, clock, timeout_ms);
}

int helikon_player_play_chunk(struct helikon_player *player,
                              uint32_t max_frames)
{
    (void)max_frames;
    if (player == NULL || !player->opened)
        return HELIKON_ERR_ARGUMENT;

    if (!player->started)
        return player_start_stream(player);

    int rc = helikon_audio_stream_service(&player->audio);
    if (rc != HELIKON_OK) {
        player->last_error = rc;
        player->last_stage = player->audio.stage;
        return rc;
    }
    size_t frame_bytes = (size_t)player->wav.channels * 2u;
    player->offset =
        ((size_t)player->start_frame + player_stream_frames(player)) *
        frame_bytes;
    player->last_error = HELIKON_OK;
    player->last_stage = HELIKON_AUDIO_STAGE_NONE;
    return 0;
}

int helikon_player_seek_ms(struct helikon_player *player, uint32_t position_ms)
{
    if (player == NULL || !player->opened || player->wav.sample_rate == 0u)
        return HELIKON_ERR_ARGUMENT;

    uint32_t total = player_total_frames(player);
    if (total == 0u)
        return HELIKON_ERR_UNSUPPORTED;

    uint64_t frame =
        ((uint64_t)position_ms * player->wav.sample_rate) / 1000u;
    if (frame >= total)
        frame = (uint64_t)total - 1u;
    uint32_t target = (uint32_t)frame;

    /*
     * Landing on the frame that is already being emitted is a no-op: a
     * restart would drop the ring for nothing.
     */
    if (player->started &&
        target == player->start_frame + player_stream_frames(player))
        return HELIKON_OK;

    if (player->compressed) {
        int rc = helikon_codec_seek(&player->codec, target);
        if (rc != HELIKON_OK) {
            player->last_error = rc;
            player->last_stage = HELIKON_AUDIO_STAGE_CODEC_DATA;
            return rc;
        }
    }

    /*
     * Before the first chunk a seek only moves the start point: the caller
     * decides when audio begins.
     */
    int was_paused = player->started && player->audio.paused;
    player->start_frame = target;
    player->offset = (size_t)target * (size_t)player->wav.channels * 2u;
    if (!player->started)
        return HELIKON_OK;

    helikon_audio_stream_stop(&player->audio);
    player->started = 0;
    int rc = player_start_stream(player);
    if (rc != HELIKON_OK)
        return rc;
    if (was_paused) {
        rc = helikon_audio_stream_pause(&player->audio);
        if (rc != HELIKON_OK) {
            player->last_error = rc;
            player->last_stage = HELIKON_AUDIO_STAGE_NONE;
            return rc;
        }
    }
    return HELIKON_OK;
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
    player->start_frame = 0u;
}

uint32_t helikon_player_position_ms(const struct helikon_player *player)
{
    if (player == NULL || player->wav.sample_rate == 0u)
        return 0u;
    uint32_t frames = player->start_frame + player_stream_frames(player);
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
