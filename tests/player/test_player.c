/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "player.h"

#include <stdio.h>
#include <string.h>

#include "audio_dma.h"
#include "error.h"

static int16_t captured[8192];
static size_t captured_frames;
static int play_calls;
static int fail_on_call;
static int fail_rc;
static uint32_t fail_stage;

int helikon_audio_init(struct helikon_audio *audio, struct helikon_pmu *pmu,
                       struct helikon_clock *clock, uint32_t timeout_ms)
{
    (void)pmu;
    (void)clock;
    (void)timeout_ms;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    audio->initialized = 1;
    return HELIKON_OK;
}

int helikon_audio_set_rate(struct helikon_audio *audio, uint32_t sample_rate)
{
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return (sample_rate == 44100u || sample_rate == 48000u)
               ? HELIKON_OK
               : HELIKON_ERR_UNSUPPORTED;
}

int helikon_audio_play(struct helikon_audio *audio, const int16_t *pcm,
                       size_t frames)
{
    captured_frames = frames;
    size_t samples = frames * 2u;
    if (samples <= sizeof(captured) / sizeof(captured[0]))
        memcpy(captured, pcm, samples * sizeof(captured[0]));
    play_calls += 1;
    if (fail_on_call != 0 && play_calls == fail_on_call) {
        audio->stage = fail_stage;
        return fail_rc;
    }
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_pause(struct helikon_audio *audio)
{
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_resume(struct helikon_audio *audio)
{
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_stop(struct helikon_audio *audio)
{
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

static void put16(uint8_t *data, uint32_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value & 0xffu);
    data[offset + 1u] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *data, uint32_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value & 0xffu);
    data[offset + 1u] = (uint8_t)((value >> 8) & 0xffu);
    data[offset + 2u] = (uint8_t)((value >> 16) & 0xffu);
    data[offset + 3u] = (uint8_t)(value >> 24);
}

static size_t make_wav(uint8_t *data, uint16_t channels, size_t frames)
{
    size_t data_bytes = frames * channels * 2u;
    size_t riff_size = 36u + data_bytes;
    memcpy(data, "RIFF", 4u);
    put32(data, 4u, (uint32_t)riff_size);
    memcpy(data + 8u, "WAVE", 4u);
    memcpy(data + 12u, "fmt ", 4u);
    put32(data, 16u, 16u);
    put16(data, 20u, 1u);
    put16(data, 22u, channels);
    put32(data, 24u, 44100u);
    put32(data, 28u, 44100u * channels * 2u);
    put16(data, 32u, (uint16_t)(channels * 2u));
    put16(data, 34u, 16u);
    memcpy(data + 36u, "data", 4u);
    put32(data, 40u, (uint32_t)data_bytes);
    for (size_t index = 0u; index < frames; ++index) {
        for (uint16_t channel = 0u; channel < channels; ++channel)
            put16(data, (uint32_t)(44u + (index * channels + channel) * 2u),
                  (uint16_t)(index + channel));
    }
    return 44u + data_bytes;
}

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", message);
        return 1;
    }
    return 0;
}

int main(void)
{
    uint8_t data[32768];
    struct helikon_player player;
    struct helikon_pmu pmu;
    struct helikon_clock clock;

    size_t size = make_wav(data, 2u, 5000u);
    if (expect(helikon_player_open(&player, data, size, &pmu, &clock, 1000u) ==
                   HELIKON_OK,
               "stereo open"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 2047,
               "stereo first chunk"))
        return 1;
    if (expect(captured_frames == 2047u && captured[0] == 0 &&
                   captured[1] == 1 && captured[2] == 1 && captured[3] == 2,
               "stereo PCM copy"))
        return 1;
    if (expect(helikon_player_position_ms(&player) == 46u,
               "stereo position after first chunk"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 2047,
               "stereo second chunk"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 906,
               "stereo final chunk"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 0,
               "stereo finished"))
        return 1;
    if (expect(helikon_player_position_ms(&player) == 113u,
               "stereo final position"))
        return 1;
    if (expect(helikon_player_finished(&player), "stereo finished flag"))
        return 1;
    helikon_player_stop(&player);

    size = make_wav(data, 1u, 100u);
    if (expect(helikon_player_open(&player, data, size, &pmu, &clock, 1000u) ==
                   HELIKON_OK,
               "mono open"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 1024u) == 100,
               "mono chunk"))
        return 1;
    if (expect(captured_frames == 100u && captured[0] == 0 &&
                   captured[1] == 0 && captured[2] == 1 && captured[3] == 1,
               "mono expansion"))
        return 1;
    if (expect(helikon_player_position_ms(&player) == 2u,
               "mono position"))
        return 1;
    helikon_player_stop(&player);

    size = make_wav(data, 2u, 100u);
    if (expect(helikon_player_open(&player, data, size, &pmu, &clock, 1000u) ==
                   HELIKON_OK,
               "error open"))
        return 1;
    play_calls = 0;
    fail_on_call = 1;
    fail_rc = HELIKON_ERR_BUSY;
    fail_stage = HELIKON_AUDIO_STAGE_DMA_WAIT;
    if (expect(helikon_player_play_chunk(&player, 2047u) ==
                   HELIKON_ERR_BUSY,
               "DMA timeout return"))
        return 1;
    if (expect(player.offset == 0u &&
                   player.last_error == HELIKON_ERR_BUSY &&
                   player.last_stage == HELIKON_AUDIO_STAGE_DMA_WAIT,
               "DMA timeout leaves the cursor and stage"))
        return 1;
    if (expect(strcmp(helikon_player_error_text(&player), "DMA TIMEOUT") == 0,
               "DMA timeout label"))
        return 1;
    fail_on_call = 0;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 100,
               "retry after DMA timeout"))
        return 1;
    if (expect(player.offset == 400u && player.last_error == HELIKON_OK,
               "retry advances the cursor"))
        return 1;
    helikon_player_stop(&player);

    uint32_t control = helikon_audio_dma_control(4094u);
    if (expect((control & HELIKON_AUDIO_DMA_CONTROL_I) != 0u,
               "DMA control sets terminal count enable"))
        return 1;
    if (expect((control & HELIKON_AUDIO_DMA_COUNT_MASK) == 4094u,
               "DMA control count"))
        return 1;
    if (expect(((control >> 28) & 7u) == 7u, "DMA control protection"))
        return 1;
    if (expect(((control >> 26) & 1u) == 1u, "DMA source increment"))
        return 1;
    if (expect(((control >> 27) & 1u) == 0u, "DMA destination fixed"))
        return 1;
    if (expect(((control >> 18) & 7u) == 1u &&
                   ((control >> 21) & 7u) == 1u,
               "DMA 16-bit widths"))
        return 1;
    if (expect(((control >> 12) & 7u) == 2u &&
                   ((control >> 15) & 7u) == 1u,
               "DMA burst sizes"))
        return 1;

    uint32_t config = helikon_audio_dma_config();
    if (expect((config & 1u) == 1u, "DMA channel enable"))
        return 1;
    if (expect(((config >> 6) & 0xfu) == 0x0au,
               "DMA destination peripheral IIS0_TX"))
        return 1;
    if (expect(((config >> 11) & 7u) == 1u, "DMA memory-to-peripheral flow"))
        return 1;
    if (expect(((config >> 14) & 3u) == 0u,
               "DMA interrupts stay masked for polling"))
        return 1;

    struct helikon_player labels;
    memset(&labels, 0, sizeof(labels));
    if (expect(strcmp(helikon_player_error_text(NULL), "PLAYER") == 0,
               "null error label"))
        return 1;
    labels.last_error = HELIKON_ERR_IO;
    labels.last_stage = HELIKON_AUDIO_STAGE_CODEC;
    if (expect(strcmp(helikon_player_error_text(&labels), "CODEC/I2C") == 0,
               "codec error label"))
        return 1;
    labels.last_stage = HELIKON_AUDIO_STAGE_DMA_WAIT;
    if (expect(strcmp(helikon_player_error_text(&labels), "DMA ERROR") == 0,
               "DMA error label"))
        return 1;
    labels.last_error = HELIKON_ERR_UNSUPPORTED;
    labels.last_stage = HELIKON_AUDIO_STAGE_NONE;
    if (expect(strcmp(helikon_player_error_text(&labels), "WAV PROFILE") == 0,
               "WAV profile label"))
        return 1;
    labels.last_error = HELIKON_ERR_FORMAT;
    if (expect(strcmp(helikon_player_error_text(&labels), "WAV FORMAT") == 0,
               "WAV format label"))
        return 1;

    printf("PASS player\n");
    return 0;
}
