/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "error.h"
#include "screen.h"

#include "test_check.h"

static int present_calls;
static uint32_t present_width;
static uint32_t present_height;

static int fake_present(void *context, const uint16_t *pixels, uint32_t stride,
                        uint32_t x, uint32_t y, uint32_t width,
                        uint32_t height)
{
    (void)context;
    (void)pixels;
    (void)stride;
    (void)x;
    (void)y;
    ++present_calls;
    present_width = width;
    present_height = height;
    return HELIKON_OK;
}

static const struct helikon_display_ops display_ops = {
    .present = fake_present
};

int main(void)
{
    uint16_t framebuffer[64 * 32];
    struct helikon_screen screen;

    helikon_screen_init(&screen, framebuffer, 64, 32, &display_ops, NULL);
    helikon_screen_clear(&screen);
    for (unsigned int i = 0; i < 64 * 32; ++i)
        CHECK(framebuffer[i] == HELIKON_COLOR_BACKGROUND);

    helikon_screen_set_foreground(&screen, HELIKON_COLOR_SUCCESS);
    helikon_screen_text(&screen, 0, 0, 1, "I");
    CHECK(framebuffer[0] == HELIKON_COLOR_SUCCESS);
    CHECK(framebuffer[1] == HELIKON_COLOR_SUCCESS);
    CHECK(framebuffer[2] == HELIKON_COLOR_SUCCESS);
    CHECK(framebuffer[64] == HELIKON_COLOR_BACKGROUND);
    CHECK(framebuffer[65] == HELIKON_COLOR_SUCCESS);

    CHECK_EQ(HELIKON_OK, helikon_screen_present_all(&screen));
    CHECK_EQ(1, present_calls);
    CHECK_EQ(64, present_width);
    CHECK_EQ(32, present_height);

    /* Clipping must not write outside the framebuffer. */
    helikon_screen_text(&screen, 62, 30, 2, "HELIKON-OS");
    CHECK_EQ(HELIKON_OK, helikon_screen_present(&screen, 60, 28, 8, 8));
    CHECK_EQ(2, present_calls);

    return helikon_test_report("screen");
}
