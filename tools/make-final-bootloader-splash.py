#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the final Alpha bootloader splash from the owner's green/red PNGs.

The two owner PNGs differ only in the HFS+ DETAILS label colour. The green PNG
is the base; the label is converted to an alpha mask so the bootloader can draw
it green, red or neutral at runtime. The base is packed as 16-colour 4bpp and
the exact sRGB PNG variants are kept for release/mockup use.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

WIDTH = 320
HEIGHT = 240
# Diff bbox from the owner files is x=187..257, y=205..212; add a one-pixel
# margin and keep the width even for 4bpp packing.
HFS_X = 186
HFS_Y = 204
HFS_WIDTH = 72
HFS_HEIGHT = 9
HFS_STRIDE = HFS_WIDTH // 2
INK = np.array((25, 21, 18), dtype=np.float32)
NEUTRAL = (107, 102, 92)


def rgb565(colour: tuple[int, int, int]) -> int:
    r, g, b = colour
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def pack4(indices: np.ndarray) -> bytes:
    height, width = indices.shape
    packed = np.zeros((height, (width + 1) // 2), dtype=np.uint8)
    pairs = width // 2
    if pairs:
        packed[:, :pairs] = (indices[:, 0:2 * pairs:2] << 4) | \
                            indices[:, 1:2 * pairs:2]
    if width & 1:
        packed[:, -1] = indices[:, -1] << 4
    return packed.tobytes()


def unpack4(data: bytes, width: int, height: int) -> np.ndarray:
    packed = np.frombuffer(data, dtype=np.uint8).reshape(height,
                                                        (width + 1) // 2)
    out = np.zeros((height, width), dtype=np.uint8)
    out[:, 0::2] = packed >> 4
    out[:, 1::2] = packed[:, :width // 2] & 0x0F
    if width & 1:
        out[:, -1] = packed[:, -1] >> 4
    return out


def make_palette(bg: np.ndarray) -> np.ndarray:
    ramp = np.array([bg + (INK - bg) * i / 15.0 for i in range(16)])
    return np.clip(np.rint(ramp), 0, 255).astype(np.uint8)


def quantize(image: Image.Image, palette: np.ndarray) -> np.ndarray:
    pixels = np.asarray(image, dtype=np.int32)
    pal = palette.astype(np.int32)
    distances = ((pixels[..., None, :] - pal[None, None, :, :]) ** 2).sum(
        axis=3)
    return distances.argmin(axis=2).astype(np.uint8)


def c_array(data: bytes, per_line: int = 16) -> str:
    lines = []
    for offset in range(0, len(data), per_line):
        chunk = data[offset:offset + per_line]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) +
                     ",")
    return "\n".join(lines)


def write_c(header_path: Path, source_hash: str, bg: tuple[int, int, int],
            green: tuple[int, int, int], red: tuple[int, int, int],
            palette: np.ndarray, base: bytes, mask: bytes) -> None:
    palette565 = [rgb565(tuple(int(x) for x in colour)) for colour in palette]
    palette_text = "\n".join(
        "    " + ", ".join(f"0x{value:04x}" for value in palette565[i:i + 8]) +
        "," for i in range(0, 16, 8))

    header = f'''/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_SPLASH_COLOR_H
#define HELIKON_SPLASH_COLOR_H

#include <stdint.h>

#define HELIKON_SPLASH_WIDTH {WIDTH}u
#define HELIKON_SPLASH_HEIGHT {HEIGHT}u
#define HELIKON_SPLASH_HFS_X {HFS_X}u
#define HELIKON_SPLASH_HFS_Y {HFS_Y}u
#define HELIKON_SPLASH_HFS_WIDTH {HFS_WIDTH}u
#define HELIKON_SPLASH_HFS_HEIGHT {HFS_HEIGHT}u

enum helikon_splash_hfs_status {{
    HELIKON_SPLASH_HFS_UNKNOWN = 0,
    HELIKON_SPLASH_HFS_OK = 1,
    HELIKON_SPLASH_HFS_FAIL = 2
}};

/* A 16-colour 320x240 splash. The HFS+ label is composited at runtime. */
void helikon_splash_show(void);
void helikon_splash_set_hfs(int ok);

#endif
'''

    c_source = f'''/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generated bootloader splash. Do not edit by hand.
 *
 * Source: owner final Alpha green/red PNGs
 * Source SHA-256 (green + red pair): {source_hash}
 * Version: ALPHA
 * Palette: 16 warm greys, {WIDTH}x{HEIGHT}, 4bpp
 *
 * The two owner PNGs differ only in the HFS+ DETAILS label. The label is an
 * alpha mask, composited green, red or neutral at runtime.
 */
#include "config.h"

#include "splash_color.h"

#include "lcd.h"

#define SPLASH_STRIDE (HELIKON_SPLASH_WIDTH / 2u)

static const uint16_t helikon_splash_palette[16] = {{
{palette_text}
}};

static const uint8_t helikon_splash_base[{len(base)}] = {{
{c_array(base)}
}};

static const uint8_t helikon_splash_hfs_mask[{len(mask)}] = {{
{c_array(mask)}
}};

/* The LCD framebuffer is RGB565; this staging buffer is in BSS. */
static fb_data helikon_splash_fb[HELIKON_SPLASH_WIDTH *
                                HELIKON_SPLASH_HEIGHT]
    __attribute__((aligned(4)));

static fb_data rgb565(unsigned r, unsigned g, unsigned b)
{{
    return (fb_data)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));
}}

static fb_data blend_background(unsigned r, unsigned g, unsigned b,
                                unsigned alpha)
{{
    /* The label area in the base image is the exact off-white. */
    unsigned br = {bg[0]};
    unsigned bg = {bg[1]};
    unsigned bb = {bg[2]};
    unsigned inv = 15u - alpha;
    r = (br * inv + r * alpha + 7u) / 15u;
    g = (bg * inv + g * alpha + 7u) / 15u;
    b = (bb * inv + b * alpha + 7u) / 15u;
    return rgb565(r, g, b);
}}

static void splash_draw(int status)
{{
    for (unsigned y = 0; y < HELIKON_SPLASH_HEIGHT; ++y) {{
        for (unsigned x = 0; x < HELIKON_SPLASH_WIDTH; ++x) {{
            uint8_t packed = helikon_splash_base[y * SPLASH_STRIDE + x / 2u];
            unsigned index = (x & 1u) ? (packed & 0x0fu) : (packed >> 4);
            helikon_splash_fb[y * HELIKON_SPLASH_WIDTH + x] =
                helikon_splash_palette[index];
        }}
    }}

    unsigned r = {NEUTRAL[0]};
    unsigned g = {NEUTRAL[1]};
    unsigned b = {NEUTRAL[2]};
    if (status == HELIKON_SPLASH_HFS_OK) {{
        r = {green[0]};
        g = {green[1]};
        b = {green[2]};
    }} else if (status == HELIKON_SPLASH_HFS_FAIL) {{
        r = {red[0]};
        g = {red[1]};
        b = {red[2]};
    }}

    for (unsigned y = 0; y < HELIKON_SPLASH_HFS_HEIGHT; ++y) {{
        for (unsigned x = 0; x < HELIKON_SPLASH_HFS_WIDTH; ++x) {{
            uint8_t packed =
                helikon_splash_hfs_mask[y * {HFS_STRIDE}u + x / 2u];
            unsigned alpha = (x & 1u) ? (packed & 0x0fu) : (packed >> 4);
            if (alpha == 0)
                continue;
            helikon_splash_fb[(HELIKON_SPLASH_HFS_Y + y) *
                                  HELIKON_SPLASH_WIDTH +
                              HELIKON_SPLASH_HFS_X + x] =
                blend_background(r, g, b, alpha);
        }}
    }}

    lcd_bitmap(helikon_splash_fb, 0, 0, HELIKON_SPLASH_WIDTH,
               HELIKON_SPLASH_HEIGHT);
    lcd_update();
}}

void helikon_splash_show(void)
{{
    splash_draw(HELIKON_SPLASH_HFS_UNKNOWN);
}}

void helikon_splash_set_hfs(int ok)
{{
    splash_draw(ok ? HELIKON_SPLASH_HFS_OK : HELIKON_SPLASH_HFS_FAIL);
}}
'''

    header_path.write_text(header, encoding="utf-8")
    (header_path.with_suffix(".c")).write_text(c_source, encoding="utf-8")
    print(header_path.with_suffix(".c"))
    print(header_path)


def build(green_path: Path, red_path: Path, out_dir: Path, emit_c: bool,
          header_path: Path) -> None:
    green = Image.open(green_path).convert("RGB")
    red = Image.open(red_path).convert("RGB")
    if green.size != (WIDTH, HEIGHT) or red.size != (WIDTH, HEIGHT):
        raise SystemExit("error: both final PNGs must be 320x240")
    if green.getpixel((0, 0)) != red.getpixel((0, 0)):
        raise SystemExit("error: final PNG backgrounds differ")

    bg = np.array(green.getpixel((0, 0)), dtype=np.float32)
    a = np.asarray(green).astype(np.int32)
    b = np.asarray(red).astype(np.int32)
    diff = np.abs(a - b).max(axis=2)
    ys, xs = np.where(diff > 0)
    if len(xs) == 0:
        raise SystemExit("error: the two PNGs have no label-colour difference")
    if (xs.min() < HFS_X or xs.max() >= HFS_X + HFS_WIDTH or
        ys.min() < HFS_Y or ys.max() >= HFS_Y + HFS_HEIGHT):
        raise SystemExit("error: label difference lies outside the mask area")

    label_diff = diff[HFS_Y:HFS_Y + HFS_HEIGHT,
                       HFS_X:HFS_X + HFS_WIDTH]
    alpha = label_diff / float(label_diff.max())
    alpha4 = np.rint(alpha * 15.0).astype(np.uint8)

    # Choose the full-coverage target colours from the largest difference.
    y, x = np.unravel_index(np.argmax(diff), diff.shape)
    green_colour = tuple(int(v) for v in a[y, x])
    red_colour = tuple(int(v) for v in b[y, x])

    base = green.copy()
    draw = ImageDraw.Draw(base)
    draw.rectangle((HFS_X, HFS_Y, HFS_X + HFS_WIDTH - 1,
                    HFS_Y + HFS_HEIGHT - 1),
                   fill=tuple(int(v) for v in bg))
    palette = make_palette(bg)
    indices = quantize(base, palette)
    base4 = pack4(indices)
    mask4 = pack4(alpha4)
    assert np.array_equal(unpack4(base4, WIDTH, HEIGHT), indices)
    assert np.array_equal(unpack4(mask4, HFS_WIDTH, HFS_HEIGHT), alpha4)

    out_dir.mkdir(parents=True, exist_ok=True)
    # Keep the owner PNGs exactly as the release/mockup variants.
    green.save(out_dir / "helikon-os-splash-alpha-hfs-ok.png")
    red.save(out_dir / "helikon-os-splash-alpha-hfs-fail.png")
    neutral = base.copy()
    neutral_arr = np.asarray(neutral).copy()
    for yy in range(HFS_HEIGHT):
        for xx in range(HFS_WIDTH):
            av = int(alpha4[yy, xx])
            if av == 0:
                continue
            bgc = bg
            tc = np.array(NEUTRAL, dtype=np.float32)
            neutral_arr[HFS_Y + yy, HFS_X + xx] = np.rint(
                bgc * (15 - av) / 15.0 + tc * av / 15.0)
    Image.fromarray(neutral_arr).save(
        out_dir / "helikon-os-splash-alpha-hfs-unknown.png")

    # Device preview from the packed palette.
    recon = palette[indices].astype(np.float32)
    for yy in range(HFS_HEIGHT):
        for xx in range(HFS_WIDTH):
            av = int(alpha4[yy, xx])
            if av == 0:
                continue
            tc = np.array(green_colour, dtype=np.float32)
            recon[HFS_Y + yy, HFS_X + xx] = np.rint(
                bg * (15 - av) / 15.0 + tc * av / 15.0)
    Image.fromarray(np.clip(recon, 0, 255).astype(np.uint8)).save(
        out_dir / "helikon-os-splash-alpha-quantized.png")

    if emit_c:
        source_hash = hashlib.sha256(green_path.read_bytes() +
                                     red_path.read_bytes()).hexdigest()
        write_c(header_path, source_hash,
                tuple(int(v) for v in bg), green_colour, red_colour,
                palette, base4, mask4)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--green", type=Path, required=True)
    parser.add_argument("--red", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path,
                        default=Path("assets/bootloader"))
    parser.add_argument("--emit-c", action="store_true")
    parser.add_argument("--header-out", type=Path,
                        default=Path("bootloader/splash_color.h"))
    args = parser.parse_args()
    build(args.green, args.red, args.out_dir, args.emit_c, args.header_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
