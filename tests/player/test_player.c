/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "player.h"

#include <stdio.h>
#include <string.h>

#include "audio_dma.h"
#include "audio_stream.h"
#include "error.h"
#include "s5l8702_regs.h"

static unsigned int start_calls;
static size_t start_frames;
static uint32_t start_channels;
static size_t service_step = HELIKON_AUDIO_RING_FRAMES;
static int service_error;
static uint32_t service_stage;
static unsigned int pause_calls;
static unsigned int resume_calls;
static helikon_audio_pcm_source started_source;
static void *started_source_context;
static size_t started_source_frames;

int helikon_audio_init(struct helikon_audio *audio, struct helikon_pmu *pmu,
                       struct helikon_clock *clock, uint32_t timeout_ms)
{
    (void)pmu;
    (void)clock;
    (void)timeout_ms;
    memset(audio, 0, sizeof(*audio));
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

int helikon_audio_stream_start(struct helikon_audio *audio,
                               const int16_t *pcm, size_t frames,
                               uint32_t channels)
{
    audio->stream_pcm = pcm;
    audio->stream_frames = frames;
    audio->stream_cursor = 0u;
    audio->stream_played = 0u;
    audio->stream_channels = channels;
    audio->stream_play = 0u;
    audio->stream_started = 1;
    audio->running = 1;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    start_calls += 1u;
    start_frames = frames;
    start_channels = channels;
    return HELIKON_OK;
}

int helikon_audio_stream_start_source(struct helikon_audio *audio,
                                      helikon_audio_pcm_source source,
                                      void *context, size_t frames)
{
    audio->stream_pcm = NULL;
    audio->stream_source = source;
    audio->stream_source_context = context;
    audio->stream_frames = frames;
    audio->stream_channels = 2u;
    audio->stream_started = 1;
    audio->running = 1;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    start_calls += 1u;
    start_frames = frames;
    start_channels = 2u;
    started_source = source;
    started_source_context = context;
    started_source_frames = frames;
    return HELIKON_OK;
}

int helikon_audio_stream_service(struct helikon_audio *audio)
{
    if (service_error != 0) {
        audio->stage = service_stage;
        return service_error;
    }
    size_t remaining = audio->stream_frames - audio->stream_played;
    size_t add = remaining < service_step ? remaining : service_step;
    audio->stream_played += add;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_stream_pause(struct helikon_audio *audio)
{
    audio->paused = 1;
    pause_calls += 1u;
    return HELIKON_OK;
}

int helikon_audio_stream_resume(struct helikon_audio *audio)
{
    audio->paused = 0;
    audio->running = 1;
    resume_calls += 1u;
    return HELIKON_OK;
}

void helikon_audio_stream_stop(struct helikon_audio *audio)
{
    audio->stream_started = 0;
    audio->running = 0;
    audio->paused = 0;
}

int helikon_audio_stream_finished(const struct helikon_audio *audio)
{
    return audio->stream_started && audio->stream_played >= audio->stream_frames;
}

uint32_t helikon_audio_stream_position_frames(const struct helikon_audio *audio)
{
    return (uint32_t)audio->stream_played;
}

uint32_t helikon_audio_stream_underruns(const struct helikon_audio *audio)
{
    (void)audio;
    return 0u;
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

static int expect(int condition, const char *message);

static size_t load_file(const char *path, uint8_t *buffer, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0u;
    size_t size = fread(buffer, 1u, capacity, file);
    (void)fclose(file);
    return size;
}

static int check_flac_player(void)
{
    static uint8_t flac_data[65536];
    static uint8_t raw_data[65536];
    size_t flac_size = load_file("../codec/fixtures/stereo16.flac", flac_data,
                                 sizeof(flac_data));
    size_t raw_size = load_file("../codec/fixtures/stereo16.pcm", raw_data,
                                sizeof(raw_data));
    if (expect(flac_size > 0u && raw_size == 2048u * 4u, "FLAC fixture load"))
        return 1;

    struct helikon_player flac_player;
    struct helikon_pmu flac_pmu;
    struct helikon_clock flac_clock;
    if (expect(helikon_player_open(&flac_player, flac_data, flac_size,
                                   &flac_pmu, &flac_clock, 1000u) ==
                   HELIKON_OK,
               "FLAC player open"))
        return 1;
    if (expect(flac_player.compressed &&
                   flac_player.wav.sample_rate == 44100u &&
                   flac_player.wav.channels == 2u &&
                   flac_player.wav.duration_ms == 46u,
               "FLAC player info"))
        return 1;

    start_calls = 0u;
    started_source = NULL;
    started_source_context = NULL;
    started_source_frames = 0u;
    if (expect(helikon_player_play_chunk(&flac_player, 2047u) == 0 &&
                   start_calls == 1u && start_frames == 2048u &&
                   started_source != NULL &&
                   started_source_context == &flac_player &&
                   started_source_frames == 2048u,
               "FLAC player source start"))
        return 1;

    int16_t chunk[200];
    size_t got = 0u;
    if (expect(started_source(started_source_context, chunk, 100u, &got) ==
                   HELIKON_OK &&
                   got == 100u,
               "FLAC player source read"))
        return 1;
    for (size_t frame = 0u; frame < 100u; ++frame) {
        int16_t left =
            (int16_t)(raw_data[frame * 4u] |
                      ((uint16_t)raw_data[frame * 4u + 1u] << 8));
        int16_t right =
            (int16_t)(raw_data[frame * 4u + 2u] |
                      ((uint16_t)raw_data[frame * 4u + 3u] << 8));
        if (expect(chunk[frame * 2u] == left &&
                       chunk[frame * 2u + 1u] == right,
                   "FLAC player source data"))
            return 1;
    }
    helikon_player_stop(&flac_player);
    return 0;
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
    start_calls = 0u;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 0 &&
                   start_calls == 1u && start_frames == 5000u &&
                   start_channels == 2u,
               "stream start"))
        return 1;
    if (expect(helikon_player_position_ms(&player) == 0u &&
                   !helikon_player_finished(&player),
               "stream starts at zero"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 0 &&
                   helikon_player_position_ms(&player) == 46u,
               "first stream service"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 0 &&
                   helikon_player_position_ms(&player) == 92u,
               "second stream service"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 0 &&
                   helikon_player_position_ms(&player) == 113u &&
                   helikon_player_finished(&player),
               "stream finishes"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 0,
               "stream stays finished"))
        return 1;
    helikon_player_stop(&player);

    size = make_wav(data, 1u, 100u);
    if (expect(helikon_player_open(&player, data, size, &pmu, &clock, 1000u) ==
                   HELIKON_OK,
               "mono open"))
        return 1;
    start_calls = 0u;
    if (expect(helikon_player_play_chunk(&player, 1024u) == 0 &&
                   start_calls == 1u && start_frames == 100u &&
                   start_channels == 1u,
               "mono stream start"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 1024u) == 0 &&
                   helikon_player_position_ms(&player) == 2u &&
                   helikon_player_finished(&player),
               "mono stream position"))
        return 1;
    helikon_player_stop(&player);

    size = make_wav(data, 2u, 100u);
    if (expect(helikon_player_open(&player, data, size, &pmu, &clock, 1000u) ==
                   HELIKON_OK,
               "error open"))
        return 1;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 0,
               "error stream start"))
        return 1;
    service_error = HELIKON_ERR_BUSY;
    service_stage = HELIKON_AUDIO_STAGE_STREAM_UNDERRUN;
    if (expect(helikon_player_play_chunk(&player, 2047u) ==
                   HELIKON_ERR_BUSY,
               "underrun return"))
        return 1;
    if (expect(player.last_error == HELIKON_ERR_BUSY &&
                   player.last_stage == HELIKON_AUDIO_STAGE_STREAM_UNDERRUN,
               "underrun stage"))
        return 1;
    if (expect(strcmp(helikon_player_error_text(&player), "UNDERRUN") == 0,
               "underrun label"))
        return 1;
    service_error = 0;
    service_stage = HELIKON_AUDIO_STAGE_NONE;
    if (expect(helikon_player_play_chunk(&player, 2047u) == 0,
               "retry after underrun"))
        return 1;
    if (expect(helikon_player_pause(&player) == HELIKON_OK &&
                   pause_calls == 1u && player.audio.paused,
               "pause"))
        return 1;
    if (expect(helikon_player_resume(&player) == HELIKON_OK &&
                   resume_calls == 1u && !player.audio.paused,
               "resume"))
        return 1;
    helikon_player_stop(&player);

    int16_t source[200];
    int16_t destination[4096];
    for (size_t index = 0u; index < 200u; ++index)
        source[index] = (int16_t)(index + 1u);
    size_t cursor = 0u;
    size_t copied = helikon_audio_stream_fill(destination, 2047u, source,
                                              100u, &cursor, 2u);
    if (expect(copied == 100u && cursor == 100u &&
                   destination[0] == source[0] &&
                   destination[199] == source[199],
               "stereo stream fill"))
        return 1;
    if (expect(helikon_audio_stream_fill(destination, 2047u, source, 100u,
                                         &cursor, 2u) == 0u &&
                   cursor == 100u,
               "stream fill at end"))
        return 1;

    int16_t mono[100];
    for (size_t index = 0u; index < 100u; ++index)
        mono[index] = (int16_t)(index + 10u);
    cursor = 0u;
    copied = helikon_audio_stream_fill(destination, 2047u, mono, 100u,
                                       &cursor, 1u);
    if (expect(copied == 100u && cursor == 100u &&
                   destination[0] == mono[0] &&
                   destination[1] == mono[0] &&
                   destination[2] == mono[1] &&
                   destination[3] == mono[1],
               "mono stream fill"))
        return 1;

    cursor = 0u;
    copied = helikon_audio_stream_fill(destination, 10u, source, 100u,
                                       &cursor, 2u);
    if (expect(copied == 10u && cursor == 10u,
               "stream fill capacity limit"))
        return 1;

    struct helikon_audio_lli lli;
    helikon_audio_lli_build(&lli, 0x11110000u, 0x22220000u, 0x33330000u,
                            4094u);
    if (expect(lli.source == 0x11110000u &&
                   lli.destination == 0x22220000u &&
                   lli.link == 0x33330000u &&
                   (lli.control & HELIKON_AUDIO_DMA_CONTROL_I) != 0u &&
                   (lli.control & HELIKON_AUDIO_DMA_COUNT_MASK) == 4094u,
               "LLI builder"))
        return 1;

    struct helikon_audio_lli ring[HELIKON_AUDIO_RING_BUFFERS];
    for (uint32_t index = 0u; index < HELIKON_AUDIO_RING_BUFFERS; ++index) {
        helikon_audio_lli_build(
            &ring[index], 0x1000u + index, 0x2000u + index,
            (uint32_t)(uintptr_t)&ring[(index + 1u) %
                                       HELIKON_AUDIO_RING_BUFFERS],
            2047u * 2u);
    }
    for (uint32_t index = 0u; index < HELIKON_AUDIO_RING_BUFFERS; ++index) {
        if (expect(ring[index].link ==
                       (uint32_t)(uintptr_t)&ring[(index + 1u) %
                                                   HELIKON_AUDIO_RING_BUFFERS],
                   "LLI ring link"))
            return 1;
    }

    uint32_t control = helikon_audio_dma_control(4094u);
    if (expect((control & HELIKON_AUDIO_DMA_CONTROL_I) != 0u,
               "DMA control sets terminal count enable"))
        return 1;
    if (expect((control & 0x0fffu) == 4094u, "DMA control count"))
        return 1;
    uint32_t config = helikon_audio_dma_config();
    if (expect((config & 1u) == 1u &&
                   ((config >> 6) & 0x0fu) == 0x0au &&
                   ((config >> 11) & 7u) == 1u &&
                   ((config >> 14) & 3u) == 0u,
               "DMA config"))
        return 1;
    if (expect(helikon_audio_ring_next(0u) == 1u &&
                   helikon_audio_ring_next(HELIKON_AUDIO_RING_BUFFERS - 1u) ==
                       0u,
               "ring index wrap"))
        return 1;
    if (expect(helikon_audio_ring_distance(0u, 1u) == 0u &&
                   helikon_audio_ring_distance(0u, 2u) == 1u &&
                   helikon_audio_ring_distance(0u, 3u) == 2u &&
                   helikon_audio_ring_distance(0u, 0u) == 3u,
               "ring completion distance"))
        return 1;
    if (expect(S5L_CLKCON3_ADDRESS == 0x3c50000cu,
               "S5L8702 CLKCON3/CG16_AUD0 address"))
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
    labels.last_stage = HELIKON_AUDIO_STAGE_DMA_CLOCK;
    if (expect(strcmp(helikon_player_error_text(&labels), "DMA CLOCK") == 0,
               "DMA clock label"))
        return 1;
    labels.last_stage = HELIKON_AUDIO_STAGE_DMA_CONFIG;
    if (expect(strcmp(helikon_player_error_text(&labels), "DMA CONFIG") == 0,
               "DMA config label"))
        return 1;
    labels.last_stage = HELIKON_AUDIO_STAGE_I2S_FIFO;
    if (expect(strcmp(helikon_player_error_text(&labels), "I2S FIFO") == 0,
               "I2S FIFO label"))
        return 1;
    labels.last_stage = HELIKON_AUDIO_STAGE_STREAM_UNDERRUN;
    if (expect(strcmp(helikon_player_error_text(&labels), "UNDERRUN") == 0,
               "underrun label"))
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

    if (check_flac_player())
        return 1;

    printf("PASS player\n");
    return 0;
}
