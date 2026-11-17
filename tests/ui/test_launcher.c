/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host render of the firmware launcher; writes a PPM preview and checks the
 * focus marks, icon presence and framebuffer bounds. */
#include <stdint.h>
#include <stdio.h>

#include "launcher.h"
#include "launcher_assets.h"
#include "screen.h"

#define WIDTH 320u
#define HEIGHT 240u
#define GUARD 4u

static uint16_t pixels[WIDTH * HEIGHT + GUARD];
static int failures;

static int present_stub(void *context, const uint16_t *pixels_arg,
                        uint32_t stride, uint32_t x, uint32_t y,
                        uint32_t width, uint32_t height)
{
    (void)context;
    (void)pixels_arg;
    (void)stride;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return 0;
}

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

static void write_ppm(const char *path, uint32_t width, uint32_t height)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "FAIL: cannot write %s\n", path);
        ++failures;
        return;
    }
    fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (uint32_t i = 0; i < width * height; ++i) {
        uint16_t value = pixels[i];
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
}

int main(int argc, char **argv)
{
    for (uint32_t i = 0; i < WIDTH * HEIGHT + GUARD; ++i)
        pixels[i] = 0xdeadu;
    for (uint32_t i = 0; i < WIDTH * HEIGHT; ++i)
        pixels[i] = 0xffffu;

    const struct helikon_display_ops ops = { .present = present_stub };
    struct helikon_screen screen;
    helikon_screen_init(&screen, pixels, WIDTH, HEIGHT, &ops, NULL);
    helikon_launcher_draw(&screen, 0u);

    check(pixels[0] == HELIKON_LAUNCHER_PAPER, "top-left is paper");
    check(pixels[(HEIGHT - 1u) * WIDTH + (WIDTH - 1u)] ==
              HELIKON_LAUNCHER_PAPER,
          "bottom-right is paper");

    const struct helikon_launcher_bitmap *label = &helikon_launcher_labels[0];
    uint32_t center = (uint32_t)label->x + (uint32_t)label->width / 2u;
    uint32_t width = (uint32_t)label->width + 10u;
    uint32_t left = center - width / 2u;
    uint32_t underline_y = 64u + 84u;
    check(pixels[underline_y * WIDTH + left] == HELIKON_LAUNCHER_INK,
          "selected underline starts in ink");
    check(pixels[underline_y * WIDTH + left + width - 1u] ==
              HELIKON_LAUNCHER_INK,
          "selected underline ends in ink");
    check(pixels[underline_y * WIDTH + 160u] == HELIKON_LAUNCHER_PAPER,
          "unselected row has no underline");

    uint32_t icon_pixels = 0u;
    for (uint32_t y = 78u; y < 118u; ++y)
        for (uint32_t x = 36u; x < 76u; ++x)
            if (pixels[y * WIDTH + x] != HELIKON_LAUNCHER_PAPER)
                ++icon_pixels;
    check(icon_pixels > 100u, "music icon is drawn");

    check(helikon_launcher_move(0u, 1, 0) == 1u, "right moves Music to Podcasts");
    check(helikon_launcher_move(0u, -1, 0) == 2u, "left wraps to Audiobooks");
    check(helikon_launcher_move(0u, 0, 1) == 3u, "down moves to Playlists");
    check(helikon_launcher_move(3u, 0, -1) == 0u, "up wraps to Music");
    check(helikon_launcher_move(5u, 1, 0) == 3u, "right wraps on bottom row");
    check(helikon_launcher_step(0u, 1) == 1u, "wheel steps to Podcasts");
    check(helikon_launcher_step(0u, -1) == 5u, "wheel wraps back to Settings");
    check(helikon_launcher_step(5u, 1) == 0u, "wheel wraps to Music");

    for (uint32_t i = 0; i < GUARD; ++i)
        check(pixels[WIDTH * HEIGHT + i] == 0xdeadu,
              "framebuffer guard is intact");

    if (argc > 1)
        write_ppm(argv[1], WIDTH, HEIGHT);

    if (failures != 0) {
        fprintf(stderr, "%d launcher checks failed\n", failures);
        return 1;
    }
    printf("PASS launcher\n");
    return 0;
}
