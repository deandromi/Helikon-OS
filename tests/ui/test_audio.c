/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host render and logic test for the WAV playback path. */
#define HELIKON_HDB_EMBEDDED
#define HELIKON_AUDIO_EMBEDDED
#define HELIKON_AUDIO_TRACK 0
#include "../../firmware/ui/music.c"

#include <stdio.h>
#include <string.h>

const uint8_t helikon_audio_start[1] = { 0u };
const uint8_t helikon_audio_end[1] = { 0u };

int helikon_audio_init(struct helikon_audio *audio, struct helikon_pmu *pmu,
                       struct helikon_clock *clock_arg, uint32_t timeout_ms)
{
    (void)pmu;
    (void)clock_arg;
    (void)timeout_ms;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    audio->stream_started = 0;
    audio->stream_played = 0u;
    audio->stream_frames = 0u;
    return HELIKON_OK;
}

int helikon_audio_set_rate(struct helikon_audio *audio, uint32_t sample_rate)
{
    (void)audio;
    return (sample_rate == 44100u || sample_rate == 48000u)
               ? HELIKON_OK
               : HELIKON_ERR_UNSUPPORTED;
}

int helikon_audio_stream_start(struct helikon_audio *audio,
                               const int16_t *pcm, size_t frames,
                               uint32_t channels)
{
    (void)pcm;
    (void)channels;
    audio->stream_started = 1;
    audio->stream_frames = frames;
    audio->stream_played = 0u;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_stream_start_source(struct helikon_audio *audio,
                                      helikon_audio_pcm_source source,
                                      void *context, size_t frames)
{
    (void)source;
    (void)context;
    audio->stream_started = 1;
    audio->stream_frames = frames;
    audio->stream_played = 0u;
    audio->stage = HELIKON_AUDIO_STAGE_NONE;
    return HELIKON_OK;
}

int helikon_audio_stream_service(struct helikon_audio *audio)
{
    audio->stream_played = audio->stream_frames;
    return HELIKON_OK;
}

int helikon_audio_stream_pause(struct helikon_audio *audio)
{
    audio->paused = 1;
    return HELIKON_OK;
}

int helikon_audio_stream_resume(struct helikon_audio *audio)
{
    audio->paused = 0;
    return HELIKON_OK;
}

void helikon_audio_stream_stop(struct helikon_audio *audio)
{
    audio->stream_started = 0;
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

static int present_stub(void *context, const uint16_t *pixels, uint32_t stride,
                        uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    (void)context;
    (void)pixels;
    (void)stride;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return HELIKON_OK;
}

uint64_t helikon_clock_now_us(struct helikon_clock *clock_arg)
{
    (void)clock_arg;
    return 0u;
}

int helikon_clock_expired(struct helikon_clock *clock_arg, uint64_t start_us,
                          uint32_t timeout_ms)
{
    (void)clock_arg;
    (void)start_us;
    (void)timeout_ms;
    return 0;
}

void helikon_clock_delay_us(struct helikon_clock *clock_arg,
                            uint32_t microseconds)
{
    (void)clock_arg;
    (void)microseconds;
}

int helikon_buttons_poll(struct helikon_buttons *buttons_arg, uint32_t *events,
                         int32_t *wheel_delta, int *hold, uint32_t timeout_ms)
{
    (void)buttons_arg;
    (void)timeout_ms;
    if (events != NULL)
        *events = 0u;
    if (wheel_delta != NULL)
        *wheel_delta = 0;
    if (hold != NULL)
        *hold = 0;
    return HELIKON_OK;
}

static uint16_t framebuffer[HELIKON_SCREEN_WIDTH * HELIKON_SCREEN_HEIGHT];

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

static size_t make_wav(uint8_t *data, size_t frames)
{
    size_t data_bytes = frames * 4u;
    size_t riff_size = 36u + data_bytes;
    memcpy(data, "RIFF", 4u);
    put32(data, 4u, (uint32_t)riff_size);
    memcpy(data + 8u, "WAVE", 4u);
    memcpy(data + 12u, "fmt ", 4u);
    put32(data, 16u, 16u);
    put16(data, 20u, 1u);
    put16(data, 22u, 2u);
    put32(data, 24u, 44100u);
    put32(data, 28u, 44100u * 4u);
    put16(data, 32u, 4u);
    put16(data, 34u, 16u);
    memcpy(data + 36u, "data", 4u);
    put32(data, 40u, (uint32_t)data_bytes);
    for (size_t index = 0u; index < frames * 2u; ++index)
        put16(data, (uint32_t)(44u + index * 2u), (uint16_t)index);
    return 44u + data_bytes;
}

static int write_ppm(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return -1;
    fprintf(file, "P6\n%u %u\n255\n", HELIKON_SCREEN_WIDTH,
            HELIKON_SCREEN_HEIGHT);
    for (uint32_t index = 0u;
         index < HELIKON_SCREEN_WIDTH * HELIKON_SCREEN_HEIGHT; ++index) {
        uint16_t value = framebuffer[index];
        uint8_t red = (uint8_t)((value >> 11) & 0x1fu);
        uint8_t green = (uint8_t)((value >> 5) & 0x3fu);
        uint8_t blue = (uint8_t)(value & 0x1fu);
        uint8_t rgb[3] = {
            (uint8_t)((red << 3) | (red >> 2)),
            (uint8_t)((green << 2) | (green >> 4)),
            (uint8_t)((blue << 3) | (blue >> 2)),
        };
        (void)fwrite(rgb, 1u, sizeof(rgb), file);
    }
    (void)fclose(file);
    return 0;
}

int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : "../../build/ui/audio";
    struct helikon_screen target;
    const struct helikon_display_ops ops = { .present = present_stub };
    helikon_screen_init(&target, framebuffer, HELIKON_SCREEN_WIDTH,
                        HELIKON_SCREEN_HEIGHT, &ops, NULL);
    screen = &target;

    const struct hdb_io io = { .read = blob_read, .context = NULL };
    if (hdb_open(&db, &io) != HELIKON_OK) {
        fprintf(stderr, "fixture HDB did not open\n");
        return 1;
    }

    draw_track_page_view(&target, 0u, 1, 1, 0, 5000u, 20000u);
    char path[256];
    snprintf(path, sizeof(path), "%s-now-playing.ppm", prefix);
    (void)write_ppm(path);
    if (framebuffer[176u * HELIKON_SCREEN_WIDTH + 20u] != MUSIC_INK) {
        fprintf(stderr, "playing progress bar is missing\n");
        return 1;
    }

    draw_track_page_view(&target, 0u, 1, 1, 1, 5000u, 20000u);
    snprintf(path, sizeof(path), "%s-paused.ppm", prefix);
    (void)write_ppm(path);

    uint8_t wav[1024];
    size_t wav_size = make_wav(wav, 100u);
    struct helikon_player test_player;
    struct helikon_pmu pmu;
    struct helikon_clock test_clock;
    if (helikon_player_open(&test_player, wav, wav_size, &pmu, &test_clock,
                            1000u) != HELIKON_OK) {
        fprintf(stderr, "player open failed\n");
        return 1;
    }
    if (helikon_player_play_chunk(&test_player, 1024u) != 0 ||
        helikon_player_position_ms(&test_player) != 0u ||
        helikon_player_finished(&test_player)) {
        fprintf(stderr, "player stream start incorrect\n");
        return 1;
    }
    if (helikon_player_play_chunk(&test_player, 1024u) != 0 ||
        helikon_player_position_ms(&test_player) != 2u ||
        !helikon_player_finished(&test_player)) {
        fprintf(stderr, "player stream position incorrect\n");
        return 1;
    }
    if (helikon_player_play_chunk(&test_player, 1024u) != 0) {
        fprintf(stderr, "player stream did not stay finished\n");
        return 1;
    }
    helikon_player_stop(&test_player);

    printf("PASS audio\n");
    return 0;
}
