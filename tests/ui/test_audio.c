/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host render and logic test for the WAV playback and Now Playing path. */
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
    memset(audio, 0, sizeof(*audio));
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
    return audio->stream_started &&
           audio->stream_played >= audio->stream_frames;
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

void helikon_audio_peak_levels(const struct helikon_audio *audio,
                               uint8_t *levels, uint32_t count)
{
    (void)audio;
    for (uint32_t index = 0u; index < count; ++index)
        levels[index] = 0u;
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

static int pixel(uint32_t x, uint32_t y)
{
    return framebuffer[y * HELIKON_SCREEN_WIDTH + x];
}

static int any_ink(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    for (uint32_t row = 0u; row < height; ++row) {
        for (uint32_t column = 0u; column < width; ++column) {
            if (pixel(x + column, y + row) == MUSIC_INK)
                return 1;
        }
    }
    return 0;
}


static int any_color(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                     int color)
{
    for (uint32_t row = 0u; row < height; ++row) {
        for (uint32_t column = 0u; column < width; ++column) {
            if (pixel(x + column, y + row) == color)
                return 1;
        }
    }
    return 0;
}

static int any_not_paper(uint32_t x, uint32_t y, uint32_t width,
                         uint32_t height)
{
    for (uint32_t row = 0u; row < height; ++row) {
        for (uint32_t column = 0u; column < width; ++column) {
            if (pixel(x + column, y + row) != MUSIC_PAPER)
                return 1;
        }
    }
    return 0;
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

static size_t make_wav(uint8_t *data, size_t frames)
{
    size_t data_bytes = frames * 4u;
    size_t riff_size = 36u + data_bytes;
    memset(data, 0, 44u + data_bytes);
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
    printf("preview: %s\n", path);
    return 0;
}


static int expect_text(const char *actual, const char *expected,
                       const char *label)
{
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "%s: got '%s' expected '%s'\n", label, actual,
                expected);
        return 1;
    }
    return 0;
}

static int check_formatting(void)
{
    char text[128];
    struct now_playing_data data;
    memset(&data, 0, sizeof(data));
    data.source_known = 1;
    data.source_rate = 44100u;
    data.source_channels = 2u;
    data.source_bits = 16u;
    data.source_bitrate_kbps = 1411u;
    data.output_rate = 44100u;
    data.container = "WAV";
    data.codec = "PCM";
    format_source_line(text, sizeof(text), &data);
    if (expect_text(text, "WAV \xc2\xb7 PCM \xc2\xb7 16-BIT \xc2\xb7 44.1 kHz",
                    "source line"))
        return 1;
    format_output_line(text, sizeof(text), &data);
    if (expect_text(text, "1411 kbps \xc2\xb7 OUT 44.1 kHz \xc2\xb7 STEREO",
                    "output line"))
        return 1;
    data.source_known = 0;
    format_source_line(text, sizeof(text), &data);
    if (expect_text(text, "SRC \xe2\x80\x94", "unknown source"))
        return 1;
    format_output_line(text, sizeof(text), &data);
    if (expect_text(text, "OUT \xe2\x80\x94", "unknown output"))
        return 1;

    format_rate(text, sizeof(text), 48000u);
    if (expect_text(text, "48 kHz", "48k rate"))
        return 1;
    format_rate(text, sizeof(text), 44100u);
    if (expect_text(text, "44.1 kHz", "44.1k rate"))
        return 1;
    format_rate(text, sizeof(text), 0u);
    if (expect_text(text, "\xe2\x80\x94", "unknown rate"))
        return 1;
    format_time(5000u, text, sizeof(text));
    if (expect_text(text, "0:05", "time"))
        return 1;
    format_duration_short(210000u, text, sizeof(text));
    if (expect_text(text, "3:30", "track duration"))
        return 1;
    format_position(text, sizeof(text), 1u, 4u, 78u);
    if (expect_text(text, "1\xe2\x80\x93" "4 / 78", "position"))
        return 1;

    struct hdb_artist artist;
    memset(&artist, 0, sizeof(artist));
    artist.album_count = 1u;
    artist.track_count = 2u;
    format_artist_counts(text, sizeof(text), &artist);
    if (expect_text(text, "1 ALBUM \xc2\xb7 2 TRACKS", "artist counts"))
        return 1;
    artist.album_count = 0u;
    artist.track_count = 0u;
    format_artist_counts(text, sizeof(text), &artist);
    if (expect_text(text, "NO ALBUMS", "zero albums"))
        return 1;
    artist.album_count = HDB_NO_ID;
    format_artist_counts(text, sizeof(text), &artist);
    if (expect_text(text, "COUNTS UNAVAILABLE", "missing counts"))
        return 1;

    format_track_line(text, sizeof(text), 0u, 20000u);
    if (expect_text(text, "TRACK 1 OF 2 \xc2\xb7 0:20", "track line"))
        return 1;

    int16_t silence[2] = { 0, 0 };
    int16_t low[2] = { 2048, -2048 };
    int16_t high[2] = { 32767, -32768 };
    int16_t mid[2] = { 7168, 0 };
    if (helikon_audio_peak_level(silence, 2u) != 0u ||
        helikon_audio_peak_level(low, 2u) != 1u ||
        helikon_audio_peak_level(high, 2u) != 15u ||
        helikon_audio_peak_level(mid, 2u) != 3u) {
        fprintf(stderr, "peak level mapping is incorrect\n");
        return 1;
    }
    return 0;
}

static void base_data(struct now_playing_data *data,
                      enum now_playing_state state, uint32_t position,
                      uint32_t duration)
{
    memset(data, 0, sizeof(*data));
    data->state = state;
    data->position_ms = position;
    data->duration_ms = duration;
    data->source_known = 1;
    data->source_rate = 44100u;
    data->source_channels = 2u;
    data->source_bits = 16u;
    data->source_bitrate_kbps = 1411u;
    data->output_rate = 44100u;
    data->container = "WAV";
    data->codec = "PCM";
}

int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : "../../build/ui/audio";
    struct helikon_screen target;
    const struct helikon_display_ops ops = { .present = present_stub };
    helikon_screen_init(&target, framebuffer, HELIKON_SCREEN_WIDTH,
                        HELIKON_SCREEN_HEIGHT, &ops, NULL);
    helikon_text_init();
    screen = &target;

    const struct hdb_io io = { .read = blob_read, .context = NULL };
    if (hdb_open(&db, &io) != HELIKON_OK) {
        fprintf(stderr, "fixture HDB did not open\n");
        return 1;
    }
    if (check_formatting() != 0)
        return 1;

    uint8_t peaks[12] = { 2u, 5u, 9u, 6u, 12u, 4u, 8u, 14u, 7u, 10u,
                          3u, 7u };
    struct now_playing_data data;
    char path[256];

    base_data(&data, NOW_PLAYING_PLAYING, 5000u, 20000u);
    data.peaks = peaks;
    data.peaks_active = 1;
    draw_now_playing_view(&target, 0u, 0u, &data, 0u);
    snprintf(path, sizeof(path), "%s-now-playing.ppm", prefix);
    (void)write_ppm(path);
    if (pixel(20u, 166u) != MUSIC_INK) {
        fprintf(stderr, "playing progress fill is missing\n");
        return 1;
    }
    if (!any_ink(16u, 140u, 96u, 16u)) {
        fprintf(stderr, "playing visualizer is missing\n");
        return 1;
    }
    if (!any_not_paper(16u, 200u, 288u, 14u)) {
        fprintf(stderr, "playing source quality is missing\n");
        return 1;
    }

    base_data(&data, NOW_PLAYING_PAUSED, 5000u, 20000u);
    data.peaks = peaks;
    data.peaks_active = 1;
    draw_now_playing_view(&target, 0u, 0u, &data, 0u);
    snprintf(path, sizeof(path), "%s-paused.ppm", prefix);
    (void)write_ppm(path);
    if (pixel(20u, 166u) != MUSIC_INK) {
        fprintf(stderr, "paused progress fill is missing\n");
        return 1;
    }

    base_data(&data, NOW_PLAYING_PLAYING, 5000u, 20000u);
    data.peaks = peaks;
    data.peaks_active = 1;
    data.visualizer_mode = 1u;
    draw_now_playing_view(&target, 0u, 0u, &data, 0u);
    snprintf(path, sizeof(path), "%s-reduced.ppm", prefix);
    (void)write_ppm(path);
    if (!any_ink(16u, 140u, 96u, 16u)) {
        fprintf(stderr, "reduced visualizer is missing\n");
        return 1;
    }

    base_data(&data, NOW_PLAYING_PLAYING, 5000u, 20000u);
    data.peaks = peaks;
    data.peaks_active = 1;
    data.visualizer_mode = 2u;
    draw_now_playing_view(&target, 0u, 0u, &data, 0u);
    snprintf(path, sizeof(path), "%s-viz-off.ppm", prefix);
    (void)write_ppm(path);
    if (any_ink(16u, 140u, 96u, 16u)) {
        fprintf(stderr, "off visualizer still draws bars\n");
        return 1;
    }

    base_data(&data, NOW_PLAYING_OPENING, 0u, 0u);
    data.source_known = 0;
    draw_now_playing_view(&target, 0u, 0u, &data, 0u);
    snprintf(path, sizeof(path), "%s-opening.ppm", prefix);
    (void)write_ppm(path);
    if (pixel(18u, 150u) != MUSIC_PAPER) {
        fprintf(stderr, "opening visualizer is not inactive\n");
        return 1;
    }
    if (!any_not_paper(16u, 200u, 288u, 14u)) {
        fprintf(stderr, "opening source line is missing\n");
        return 1;
    }

    base_data(&data, NOW_PLAYING_FINISHED, 20000u, 20000u);
    draw_now_playing_view(&target, 0u, 0u, &data, 0u);
    snprintf(path, sizeof(path), "%s-finished.ppm", prefix);
    (void)write_ppm(path);
    if (pixel(300u, 166u) != MUSIC_INK) {
        fprintf(stderr, "finished progress bar is not full\n");
        return 1;
    }

    base_data(&data, NOW_PLAYING_ERROR, 0u, 0u);
    data.error_reason = "AUDIO UNDERRUN";
    data.retryable = 1;
    data.stopped_ms = 5000u;
    draw_now_playing_view(&target, 0u, 0u, &data, 0u);
    snprintf(path, sizeof(path), "%s-error.ppm", prefix);
    (void)write_ppm(path);
    if (pixel(16u, 40u) != MUSIC_ERROR) {
        fprintf(stderr, "error status rule is missing\n");
        return 1;
    }
    if (pixel(16u, 38u) != MUSIC_PAPER) {
        fprintf(stderr, "error state still draws a cover\n");
        return 1;
    }
    if (!any_ink(16u, 42u, 288u, 30u)) {
        fprintf(stderr, "error headline is missing\n");
        return 1;
    }

    base_data(&data, NOW_PLAYING_ERROR, 0u, 0u);
    data.error_reason = "AUDIO CODEC/I2C";
    data.retryable = 0;
    data.stopped_ms = 0u;
    draw_now_playing_view(&target, 0u, 0u, &data, 0u);
    snprintf(path, sizeof(path), "%s-error-terminal.ppm", prefix);
    (void)write_ppm(path);
    if (pixel(16u, 40u) != MUSIC_ERROR) {
        fprintf(stderr, "terminal error rule is missing\n");
        return 1;
    }
    if (!any_not_paper(16u, 218u, 288u, 16u)) {
        fprintf(stderr, "terminal error footer is missing\n");
        return 1;
    }

    base_data(&data, NOW_PLAYING_PLAYING, 5000u, 20000u);
    data.peaks = peaks;
    data.peaks_active = 1;
    draw_now_playing_view(&target, 0u, 0u, &data, MUSIC_PREVIEW_NO_ART);
    snprintf(path, sizeof(path), "%s-no-art.ppm", prefix);
    (void)write_ppm(path);
    if (!any_color(17u, 39u, 94u, 94u, MUSIC_PAPER_LIGHT)) {
        fprintf(stderr, "now playing no-art placeholder is missing\n");
        return 1;
    }

    draw_now_playing_view(&target, 0u, 0u, &data, MUSIC_PREVIEW_LONG);
    snprintf(path, sizeof(path), "%s-long-text.ppm", prefix);
    (void)write_ppm(path);
    if (!any_ink(124u, 72u, 180u, 20u)) {
        fprintf(stderr, "now playing long second title line is missing\n");
        return 1;
    }

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
