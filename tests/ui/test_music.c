/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host render of the firmware music browser against the fixture HDB. */
#define HELIKON_HDB_EMBEDDED
#include "../../firmware/ui/music.c"

#include <stdio.h>
#include <stdlib.h>

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

void helikon_clock_delay_us(struct helikon_clock *clock_arg, uint32_t microseconds)
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

static int write_ppm(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return -1;
    fprintf(file, "P6\n%u %u\n255\n", HELIKON_SCREEN_WIDTH,
            HELIKON_SCREEN_HEIGHT);
    for (uint32_t i = 0; i < HELIKON_SCREEN_WIDTH * HELIKON_SCREEN_HEIGHT; ++i) {
        uint16_t value = framebuffer[i];
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

int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : "../../build/ui/music";
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

    char path[256];
    draw_hub(&target, 0u);
    snprintf(path, sizeof(path), "%s-hub.ppm", prefix);
    (void)write_ppm(path);
    if (framebuffer[0u] != MUSIC_PAPER) {
        fprintf(stderr, "hub background is not paper\n");
        return 1;
    }
    if (framebuffer[94u * HELIKON_SCREEN_WIDTH + 56u] != MUSIC_INK) {
        fprintf(stderr, "hub focus diamond is missing\n");
        return 1;
    }

    draw_wall(&target, WALL_ALBUMS, 0u);
    snprintf(path, sizeof(path), "%s-albums.ppm", prefix);
    (void)write_ppm(path);
    if (framebuffer[36u * HELIKON_SCREEN_WIDTH + 100u] != MUSIC_RULE) {
        fprintf(stderr, "album wall header rule is missing\n");
        return 1;
    }
    if (framebuffer[44u * HELIKON_SCREEN_WIDTH + 29u] != MUSIC_INK) {
        fprintf(stderr, "album wall selected frame is not ink\n");
        return 1;
    }
    if (framebuffer[50u * HELIKON_SCREEN_WIDTH + 40u] == MUSIC_PAPER) {
        fprintf(stderr, "album wall cell has no art or fallback\n");
        return 1;
    }

    draw_wall(&target, WALL_ARTISTS, 0u);
    snprintf(path, sizeof(path), "%s-artists.ppm", prefix);
    (void)write_ppm(path);
    if (framebuffer[44u * HELIKON_SCREEN_WIDTH + 29u] != MUSIC_INK) {
        fprintf(stderr, "artist wall selected frame is not ink\n");
        return 1;
    }
    if (framebuffer[50u * HELIKON_SCREEN_WIDTH + 40u] == MUSIC_PAPER) {
        fprintf(stderr, "artist wall cell has no art or fallback\n");
        return 1;
    }

    struct list_spec tracks = { .kind = LIST_SONGS, .parent = 0u };
    draw_list(&target, &tracks, 0u, 0u, list_count(&tracks));
    snprintf(path, sizeof(path), "%s-tracks.ppm", prefix);
    (void)write_ppm(path);
    if (framebuffer[36u * HELIKON_SCREEN_WIDTH + 100u] != MUSIC_RULE) {
        fprintf(stderr, "tracks header rule is missing\n");
        return 1;
    }
    if (framebuffer[45u * HELIKON_SCREEN_WIDTH + 10u] != MUSIC_INK) {
        fprintf(stderr, "tracks selected row is not ink\n");
        return 1;
    }

    draw_artist_page(0u);
    snprintf(path, sizeof(path), "%s-artist.ppm", prefix);
    (void)write_ppm(path);

    draw_album_page(0u);
    snprintf(path, sizeof(path), "%s-album.ppm", prefix);
    (void)write_ppm(path);

    draw_track_page(0u);
    snprintf(path, sizeof(path), "%s-track.ppm", prefix);
    (void)write_ppm(path);

    draw_monogram(&target, 0u, 0u, "Lighthouse");
    int monogram = 0;
    for (uint32_t y = 0u; y < MUSIC_TILE_SIZE; ++y) {
        for (uint32_t x = 0u; x < MUSIC_TILE_SIZE; ++x) {
            if (framebuffer[y * HELIKON_SCREEN_WIDTH + x] == MUSIC_MUTED)
                monogram = 1;
        }
    }
    if (!monogram) {
        fprintf(stderr, "artist monogram fallback did not draw\n");
        return 1;
    }

    printf("PASS music\n");
    return 0;
}
