/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host renders and geometry checks for the cleaned Music flow. */
#define HELIKON_HDB_EMBEDDED
#include "../../firmware/ui/music.c"

#include <stdio.h>

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

static int write_ppm(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return -1;
    fprintf(file, "P6\n%u %u\n255\n", HELIKON_SCREEN_WIDTH,
            HELIKON_SCREEN_HEIGHT);
    for (uint32_t i = 0; i < HELIKON_SCREEN_WIDTH * HELIKON_SCREEN_HEIGHT;
         ++i) {
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

static int check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", message);
        return 1;
    }
    return 0;
}

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


static uint8_t fake_string[] = {
    0x00u, 0x00u, 0x0du, 0x00u, 'H', 'e', 'l', 'l', 'o', ' ', 'W',
    'o', 'r', 'l', 'd', '!', '!',
};

static const uint8_t *fake_source;
static uint32_t fake_source_size;

static int fake_read(void *context, uint32_t offset, void *buffer,
                     uint32_t length)
{
    (void)context;
    if (offset > fake_source_size ||
        length > fake_source_size - offset)
        return -1;
    memcpy(buffer, fake_source + offset, length);
    return 0;
}

static int check_long_string(void)
{
    struct hdb saved = db;
    fake_source = fake_string;
    fake_source_size = sizeof(fake_string);
    db.io.read = fake_read;
    db.io.context = NULL;
    db.string_offset = 0u;
    db.string_bytes = sizeof(fake_string);
    char text[8];
    int rc = read_string(2u, text, sizeof(text));
    db = saved;
    if (rc != HELIKON_OK)
        return 1;
    if ((uint8_t)text[4] != 0xe2u || (uint8_t)text[5] != 0x80u ||
        (uint8_t)text[6] != 0xa6u)
        return 1;
    return 0;
}


static uint8_t fake_utf8[] = {
    0x00u, 0x00u, 0x07u, 0x00u, 'A', 'B', 0xc3u, 0xa9u, 'C', 'D', 'E',
};

static int check_long_utf8(void)
{
    struct hdb saved = db;
    fake_source = fake_utf8;
    fake_source_size = sizeof(fake_utf8);
    db.io.read = fake_read;
    db.io.context = NULL;
    db.string_offset = 0u;
    db.string_bytes = sizeof(fake_utf8);
    char text[7];
    int rc = read_string(2u, text, sizeof(text));
    db = saved;
    if (rc != HELIKON_OK)
        return 1;
    if (text[0] != 'A' || text[1] != 'B')
        return 1;
    if ((uint8_t)text[2] != 0xe2u || (uint8_t)text[3] != 0x80u ||
        (uint8_t)text[4] != 0xa6u)
        return 1;
    return 0;
}

int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : "../../build/ui/music";
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

    char path[256];
    draw_hub(&target, 0u);
    snprintf(path, sizeof(path), "%s-hub.ppm", prefix);
    (void)write_ppm(path);
    if (check(pixel(0u, 0u) == MUSIC_PAPER, "hub background"))
        return 1;
    if (check(pixel(56u, 94u) == MUSIC_INK, "hub focus diamond"))
        return 1;

    draw_wall(&target, WALL_ALBUMS, 0u);
    snprintf(path, sizeof(path), "%s-albums.ppm", prefix);
    (void)write_ppm(path);
    if (check(pixel(100u, 36u) == MUSIC_RULE, "album wall header rule"))
        return 1;
    if (check(pixel(29u, 44u) == MUSIC_INK, "album wall selected frame"))
        return 1;

    struct list_spec tracks = { .kind = LIST_SONGS, .parent = 0u };
    draw_list(&target, &tracks, 0u, 0u, list_count(&tracks));
    snprintf(path, sizeof(path), "%s-tracks.ppm", prefix);
    (void)write_ppm(path);
    if (check(pixel(100u, 36u) == MUSIC_RULE, "tracks header rule"))
        return 1;

    draw_artists_list_screen(&target, 0u, 0u, 0u);
    snprintf(path, sizeof(path), "%s-artists.ppm", prefix);
    (void)write_ppm(path);
    if (check(pixel(16u, 78u) == MUSIC_INK, "artists selected bar"))
        return 1;
    if (check(pixel(100u, 30u) == MUSIC_RULE, "artists top rule"))
        return 1;
    if (check(pixel(16u, 226u) == MUSIC_FAINT, "artists footer rule"))
        return 1;

    draw_artists_list_screen(&target, 0u, 0u, MUSIC_PREVIEW_LONG);
    snprintf(path, sizeof(path), "%s-artists-long.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_ink(60u, 78u, 244u, 16u), "artists long name"))
        return 1;
    if (check(any_not_paper(60u, 94u, 244u, 14u),
              "artists long missing counts"))
        return 1;

    draw_artists_list_screen(&target, 0u, 0u, MUSIC_PREVIEW_EMPTY);
    snprintf(path, sizeof(path), "%s-artists-empty.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_not_paper(16u, 82u, 288u, 16u), "artists empty message"))
        return 1;
    if (check(any_not_paper(240u, 228u, 64u, 12u), "artists empty range"))
        return 1;

    draw_artist_page(&target, 0u, 0u, 0u, 0u, 0u);
    snprintf(path, sizeof(path), "%s-artist-albums.ppm", prefix);
    (void)write_ppm(path);
    if (check(pixel(16u, 102u) == MUSIC_INK, "artist albums underline"))
        return 1;
    if (check(pixel(84u, 102u) != MUSIC_INK,
              "artist overview underline is not active"))
        return 1;
    if (check(pixel(16u, 118u) == MUSIC_INK, "artist album cell frame"))
        return 1;

    draw_artist_page(&target, 0u, 0u, 0u, 0u, MUSIC_PREVIEW_EMPTY);
    snprintf(path, sizeof(path), "%s-artist-albums-empty.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_not_paper(16u, 120u, 288u, 16u), "artist empty albums"))
        return 1;

    draw_artist_page(&target, 0u, 1u, 0u, 0u, 0u);
    snprintf(path, sizeof(path), "%s-artist-overview.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_ink(16u, 106u, 288u, 64u), "artist biography"))
        return 1;
    if (check(pixel(16u, 150u) == MUSIC_PAPER,
              "artist overview has no dead play plate"))
        return 1;

    draw_artist_page(&target, 2u, 1u, 0u, 0u, MUSIC_PREVIEW_EMPTY);
    snprintf(path, sizeof(path), "%s-artist-overview-empty.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_not_paper(16u, 106u, 288u, 16u), "artist empty biography"))
        return 1;
    if (check(any_not_paper(16u, 126u, 288u, 16u), "artist empty facts"))
        return 1;

    draw_artist_page(&target, 0u, 2u, 0u, 0u,
                     MUSIC_PREVIEW_ALL_PLAYABLE);
    snprintf(path, sizeof(path), "%s-artist-tracks.ppm", prefix);
    (void)write_ppm(path);
    if (check(pixel(16u, 118u) == MUSIC_INK, "artist track focus bar"))
        return 1;
    if (check(any_ink(24u, 118u, 220u, 16u), "artist track title"))
        return 1;

    draw_album_page(&target, 0u, 0, 0u, MUSIC_PREVIEW_ALL_PLAYABLE);
    snprintf(path, sizeof(path), "%s-album.ppm", prefix);
    (void)write_ppm(path);
    if (check(pixel(16u, 36u) == MUSIC_RULE, "album cover frame"))
        return 1;
    if (check(pixel(16u, 114u) == MUSIC_INK, "album PLAY ALL outline"))
        return 1;
    if (check(pixel(16u, 158u) == MUSIC_INK, "album track focus bar"))
        return 1;
    if (check(any_ink(100u, 50u, 204u, 40u), "album title"))
        return 1;

    draw_album_page(&target, 0u, 0, 0u,
                    MUSIC_PREVIEW_ALL_PLAYABLE | MUSIC_PREVIEW_LONG |
                        MUSIC_PREVIEW_CREDITS);
    snprintf(path, sizeof(path), "%s-album-long.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_ink(100u, 72u, 204u, 20u), "album long second title line"))
        return 1;
    if (check(any_ink(58u, 218u, 246u, 14u), "album long notes"))
        return 1;

    draw_album_page(&target, 0u, -1, 0u, MUSIC_PREVIEW_ALL_PLAYABLE);
    snprintf(path, sizeof(path), "%s-album-playall.ppm", prefix);
    (void)write_ppm(path);
    if (check(pixel(16u, 114u) == MUSIC_INK, "album PLAY ALL focus fill"))
        return 1;
    if (check(any_color(16u, 114u, 72u, 18u, MUSIC_PAPER),
              "album PLAY ALL focused label"))
        return 1;

    draw_album_page(&target, 0u, -1, 0u,
                    MUSIC_PREVIEW_EMPTY | MUSIC_PREVIEW_CREDITS);
    snprintf(path, sizeof(path), "%s-album-empty.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_not_paper(16u, 160u, 288u, 16u), "album empty tracks"))
        return 1;
    if (check(pixel(16u, 114u) == MUSIC_PAPER, "album empty hides PLAY ALL"))
        return 1;

    draw_album_page(&target, 0u, 0, 0u,
                    MUSIC_PREVIEW_ALL_PLAYABLE | MUSIC_PREVIEW_CORRUPT);
    snprintf(path, sizeof(path), "%s-album-corrupt.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_not_paper(38u, 176u, 220u, 16u), "album corrupt row"))
        return 1;
    if (check(any_not_paper(16u, 222u, 288u, 14u), "album corrupt status"))
        return 1;

    draw_album_page(&target, 0u, 0, 0u,
                    MUSIC_PREVIEW_ALL_PLAYABLE | MUSIC_PREVIEW_NO_ART);
    snprintf(path, sizeof(path), "%s-album-no-art.ppm", prefix);
    (void)write_ppm(path);
    if (check(any_color(17u, 37u, 70u, 70u, MUSIC_PAPER_LIGHT),
              "album no-art placeholder ground"))
        return 1;

    helikon_text_set_fallback(1);
    draw_artists_list_screen(&target, 0u, 0u, 0u);
    snprintf(path, sizeof(path), "%s-artists-fallback.ppm", prefix);
    (void)write_ppm(path);
    if (check(helikon_text_fallback_active(), "font fallback reported"))
        return 1;
    if (check(any_ink(16u, 34u, 220u, 22u), "font fallback title"))
        return 1;
    helikon_text_set_fallback(0);

    if (check(check_long_string() == 0, "long string ellipsis"))
        return 1;
    if (check(check_long_utf8() == 0, "long UTF-8 safe ellipsis"))
        return 1;

    printf("PASS music\n");
    return 0;
}
