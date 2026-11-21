/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host check of the bootloader splash label contrast.
 *
 * The label colours in `bootloader/splash_label.h` are composited over the
 * off-white label band and shown on an RGB565 panel. Small text needs 4.5:1
 * against its background; the owner green `#00BF63` measured 2.10:1 and the
 * owner red `#FE3131` 3.14:1, both unreadable at 9 px. This test fails if a
 * label colour drops below the floor again, before the change can reach a
 * device.
 *
 * Host test only: it reads the constants, it does not exercise the panel.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "splash_label.h"

#define FLOOR 4.5

static int check(int condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", what);
        return 1;
    }
    printf("ok   %s\n", what);
    return 0;
}

static double channel(double value)
{
    value /= 255.0;
    if (value <= 0.04045)
        return value / 12.92;
    return pow((value + 0.055) / 1.055, 2.4);
}

static double luminance(double r, double g, double b)
{
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

/* Luminance ratio as WCAG 2.1 defines it for sRGB. */
static double contrast(double r1, double g1, double b1, double r2, double g2,
                       double b2)
{
    double a = luminance(r1, g1, b1);
    double b = luminance(r2, g2, b2);
    double hi = a > b ? a : b;
    double lo = a > b ? b : a;
    return (hi + 0.05) / (lo + 0.05);
}

/* The panel drops the low bits of each channel; check what it shows. */
static void quantize(unsigned raw_r, unsigned raw_g, unsigned raw_b,
                     double *r, double *g, double *b)
{
    unsigned red = (raw_r & 0xf8u) | (raw_r >> 5);
    unsigned green = (raw_g & 0xfcu) | (raw_g >> 6);
    unsigned blue = (raw_b & 0xf8u) | (raw_b >> 5);
    *r = (double)red;
    *g = (double)green;
    *b = (double)blue;
}

int main(void)
{
    double bg_r = 0.0, bg_g = 0.0, bg_b = 0.0;
    double fg_r = 0.0, fg_g = 0.0, fg_b = 0.0;
    double ratio = 0.0;
    int failures = 0;

    quantize(HELIKON_SPLASH_LABEL_BG_R, HELIKON_SPLASH_LABEL_BG_G,
             HELIKON_SPLASH_LABEL_BG_B, &bg_r, &bg_g, &bg_b);

    quantize(HELIKON_SPLASH_LABEL_UNKNOWN_R, HELIKON_SPLASH_LABEL_UNKNOWN_G,
             HELIKON_SPLASH_LABEL_UNKNOWN_B, &fg_r, &fg_g, &fg_b);
    ratio = contrast(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b);
    printf("unknown label: %.2f:1\n", ratio);
    failures += check(ratio >= FLOOR, "neutral label clears 4.5:1");

    quantize(HELIKON_SPLASH_LABEL_OK_R, HELIKON_SPLASH_LABEL_OK_G,
             HELIKON_SPLASH_LABEL_OK_B, &fg_r, &fg_g, &fg_b);
    ratio = contrast(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b);
    printf("hfs ok label: %.2f:1\n", ratio);
    failures += check(ratio >= FLOOR, "green label clears 4.5:1");
    /* Keep it green: the blue channel must stay below green. */
    failures += check(HELIKON_SPLASH_LABEL_OK_G > HELIKON_SPLASH_LABEL_OK_R &&
                          HELIKON_SPLASH_LABEL_OK_G >
                              HELIKON_SPLASH_LABEL_OK_B,
                      "green label stays green");

    quantize(HELIKON_SPLASH_LABEL_FAIL_R, HELIKON_SPLASH_LABEL_FAIL_G,
             HELIKON_SPLASH_LABEL_FAIL_B, &fg_r, &fg_g, &fg_b);
    ratio = contrast(fg_r, fg_g, fg_b, bg_r, bg_g, bg_b);
    printf("hfs fail label: %.2f:1\n", ratio);
    failures += check(ratio >= FLOOR, "red label clears 4.5:1");
    failures += check(HELIKON_SPLASH_LABEL_FAIL_R >
                          HELIKON_SPLASH_LABEL_FAIL_G,
                      "red label stays red");

    /*
     * Both labels are tuned to the same contrast against the paper, so their
     * luminance is nearly identical and a luminance ratio cannot tell them
     * apart. The hue has to. That is also the weakness worth knowing: colour is
     * the only cue distinguishing a readable volume from an unreadable one,
     * which a red/green colour-blind reader cannot use. Changing the words
     * requires new owner masks, so it stays an open issue.
     */
    failures += check(
        HELIKON_SPLASH_LABEL_OK_G >= HELIKON_SPLASH_LABEL_OK_R + 60u &&
            HELIKON_SPLASH_LABEL_OK_G >= HELIKON_SPLASH_LABEL_OK_B + 40u,
        "green label is green enough to read as pass");
    failures += check(
        HELIKON_SPLASH_LABEL_FAIL_R >= HELIKON_SPLASH_LABEL_FAIL_G + 120u &&
            HELIKON_SPLASH_LABEL_FAIL_R >= HELIKON_SPLASH_LABEL_FAIL_B + 120u,
        "red label is red enough to read as fail");

    if (failures != 0) {
        fprintf(stderr, "%d splash label check(s) failed\n", failures);
        return 1;
    }
    printf("PASS splash label contrast\n");
    return 0;
}
