/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "player.h"

#include <stddef.h>

#include "error.h"

#define PLAYER_MAX_FRAMES 2047u

static int16_t mono_buffer[PLAYER_MAX_FRAMES * 2u];

int helikon_player_open(struct helikon_player *player, const uint8_t *data,
                        size_t size, struct helikon_pmu *pmu,
                        struct helikon_clock *clock, uint32_t timeout_ms)
{
    if (player == NULL)
        return HELIKON_ERR_ARGUMENT;

    player->opened = 0;
    player->offset = 0u;
    int rc = helikon_wav_parse(data, size, &player->wav);
    if (rc != HELIKON_OK)
        return rc;

    rc = helikon_audio_init(&player->audio, pmu, clock, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    rc = helikon_audio_set_rate(&player->audio, player->wav.sample_rate);
    if (rc != HELIKON_OK) {
        (void)helikon_audio_stop(&player->audio);
        return rc;
    }
    player->opened = 1;
    return HELIKON_OK;
}

static uint32_t remaining_frames(const struct helikon_player *player)
{
    size_t frame_bytes = (size_t)player->wav.channels * 2u;
    if (player->offset >= player->wav.bytes || frame_bytes == 0u)
        return 0u;
    return (uint32_t)((player->wav.bytes - player->offset) / frame_bytes);
}

int helikon_player_play_chunk(struct helikon_player *player,
                              uint32_t max_frames)
{
    if (player == NULL || !player->opened)
        return HELIKON_ERR_ARGUMENT;
    if (max_frames == 0u || max_frames > PLAYER_MAX_FRAMES)
        max_frames = PLAYER_MAX_FRAMES;

    uint32_t frames = remaining_frames(player);
    if (frames == 0u)
        return 0;
    if (frames > max_frames)
        frames = max_frames;

    int rc;
    if (player->wav.channels == 2u) {
        rc = helikon_audio_play(&player->audio,
                                (const int16_t *)(player->wav.pcm +
                                                  player->offset),
                                frames);
    } else {
        const int16_t *source =
            (const int16_t *)(player->wav.pcm + player->offset);
        for (uint32_t index = 0u; index < frames; ++index) {
            mono_buffer[index * 2u] = source[index];
            mono_buffer[index * 2u + 1u] = source[index];
        }
        rc = helikon_audio_play(&player->audio, mono_buffer, frames);
    }
    if (rc != HELIKON_OK)
        return rc;

    player->offset += (size_t)frames * player->wav.channels * 2u;
    return (int)frames;
}

int helikon_player_pause(struct helikon_player *player)
{
    if (player == NULL || !player->opened)
        return HELIKON_ERR_ARGUMENT;
    return helikon_audio_pause(&player->audio);
}

int helikon_player_resume(struct helikon_player *player)
{
    if (player == NULL || !player->opened)
        return HELIKON_ERR_ARGUMENT;
    return helikon_audio_resume(&player->audio);
}

void helikon_player_stop(struct helikon_player *player)
{
    if (player == NULL || !player->opened)
        return;
    (void)helikon_audio_stop(&player->audio);
    player->opened = 0;
}

uint32_t helikon_player_position_ms(const struct helikon_player *player)
{
    if (player == NULL || player->wav.sample_rate == 0u)
        return 0u;
    size_t frame_bytes = (size_t)player->wav.channels * 2u;
    uint32_t frames = frame_bytes == 0u
                          ? 0u
                          : (uint32_t)(player->offset / frame_bytes);
    return (uint32_t)(((uint64_t)frames * 1000u) /
                      player->wav.sample_rate);
}

uint32_t helikon_player_duration_ms(const struct helikon_player *player)
{
    return player == NULL ? 0u : player->wav.duration_ms;
}

int helikon_player_finished(const struct helikon_player *player)
{
    return player == NULL || remaining_frames(player) == 0u;
}
