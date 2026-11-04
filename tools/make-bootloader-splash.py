#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Helikon-OS bootloader splash and its C data.

The owner artwork is 320x240. Only two areas may change:
  * the version dots are replaced by the version word;
  * the HFS+ DETAILS text is recoloured at runtime.

The illustration, title, ornament and off-white background are copied from the
source. The HFS+ label is represented by an alpha mask, so the bootloader can
composite it in green, red or neutral without storing three full images.

The bootloader stores the base as a 16-colour (4bpp) image because the 6G LCD
is RGB565 and the bootloader region is size-limited. The PNG variants are
lossless sRGB mockups; the C data is the device representation.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

BACKGROUND = np.array((240, 237, 230), dtype=np.float32)
INK = np.array((25, 21, 18), dtype=np.float32)

# The source is 320x240. These are the only editable areas.
VERSION_AREA = (118, 202, 136, 211)  # dots only; x0, y0, x1, y1 exclusive
VERSION_ORIGIN = (121, 207)          # left baseline
HFS_AREA = (180, 197, 253, 213)      # x0, y0, x1, y1 exclusive
HFS_TEXT_AREA = (183, 199, 249, 208)

COLOURS = {
    "ok": (47, 107, 63),        # muted green
    "fail": (163, 58, 46),      # muted brick red
    "unknown": (107, 102, 92),  # neutral warm grey
}

SPLASH_WIDTH = 320
SPLASH_HEIGHT = 240
HFS_WIDTH = HFS_AREA[2] - HFS_AREA[0]
HFS_HEIGHT = HFS_AREA[3] - HFS_AREA[1]
HFS_STRIDE = (HFS_WIDTH + 1) // 2


def font_family(path: Path) -> str:
    font = TTFont(path, lazy=True, fontNumber=0)
    names = font["name"]
    return names.getDebugName(1) or names.getDebugName(4) or ""


def load_font(path: Path, size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(path), size=size)


def erase_version_area(image: Image.Image) -> None:
    """Remove only the dark dots, leaving every background pixel intact."""
    x0, y0, x1, y1 = VERSION_AREA
    region = np.asarray(image.crop((x0, y0, x1, y1)), dtype=np.uint8).copy()
    luminance = region.astype(np.float32).mean(axis=2)
    mask = luminance < 225.0
    region[mask] = BACKGROUND.astype(np.uint8)
    image.paste(Image.fromarray(region), (x0, y0))


def draw_version(image: Image.Image, font: ImageFont.FreeTypeFont,
                 version: str) -> None:
    draw = ImageDraw.Draw(image)
    draw.text(VERSION_ORIGIN, version, font=font,
              fill=tuple(int(v) for v in INK), anchor="ls")


def hfs_alpha(image: Image.Image) -> np.ndarray:
    x0, y0, x1, y1 = HFS_AREA
    region = np.asarray(image.crop((x0, y0, x1, y1)), dtype=np.float32)
    luminance = region.mean(axis=2)
    bg_l = float(BACKGROUND.mean())
    ink_l = float(INK.mean())
    return np.clip((bg_l - luminance) / (bg_l - ink_l), 0.0, 1.0)


def recolour_hfs(image: Image.Image, colour: tuple[int, int, int]) -> None:
    """Blend the HFS+ label ink with `colour`, preserving background pixels."""
    x0, y0, x1, y1 = HFS_AREA
    region = np.asarray(image.crop((x0, y0, x1, y1)), dtype=np.float32)
    alpha = hfs_alpha(image)
    target = np.array(colour, dtype=np.float32)
    out = region * (1.0 - alpha[..., None]) + target * alpha[..., None]
    image.paste(Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)),
                (x0, y0))


def blank_hfs(image: Image.Image) -> None:
    draw = ImageDraw.Draw(image)
    draw.rectangle((HFS_AREA[0], HFS_AREA[1],
                    HFS_AREA[2] - 1, HFS_AREA[3] - 1),
                   fill=tuple(int(v) for v in BACKGROUND))


def make_palette() -> np.ndarray:
    """16 warm greys from the exact off-white to the exact ink."""
    ramp = np.array([BACKGROUND + (INK - BACKGROUND) * i / 15.0
                     for i in range(16)])
    return np.clip(np.rint(ramp), 0, 255).astype(np.uint8)


def quantize(image: Image.Image, palette: np.ndarray) -> np.ndarray:
    pixels = np.asarray(image, dtype=np.int32)
    pal = palette.astype(np.int32)
    distances = ((pixels[..., None, :] - pal[None, None, :, :]) ** 2).sum(
        axis=3)
    return distances.argmin(axis=2).astype(np.uint8)


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


def rgb565(colour: tuple[int, int, int]) -> int:
    r, g, b = colour
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def c_array(data: bytes, per_line: int = 16) -> str:
    lines = []
    for offset in range(0, len(data), per_line):
        chunk = data[offset:offset + per_line]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) +
                     ",")
    return "\n".join(lines)


def write_c(header_path: Path, source_path: Path, source_hash: str,
            family: str, font_size: int, version: str, palette: np.ndarray,
            base: bytes, mask: bytes) -> None:
    palette565 = [rgb565(tuple(int(x) for x in colour)) for colour in palette]
    palette_text = "\n".join(
        "    " + ", ".join(f"0x{value:04x}" for value in palette565[i:i + 8]) +
        "," for i in range(0, 16, 8))

    header = f'''/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_SPLASH_COLOR_H
#define HELIKON_SPLASH_COLOR_H

#include <stdint.h>

#define HELIKON_SPLASH_WIDTH {SPLASH_WIDTH}u
#define HELIKON_SPLASH_HEIGHT {SPLASH_HEIGHT}u
#define HELIKON_SPLASH_HFS_X {HFS_AREA[0]}u
#define HELIKON_SPLASH_HFS_Y {HFS_AREA[1]}u
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
 * Source: {source_path.name}
 * Source SHA-256: {source_hash}
 * Font: {family}, {font_size}px
 * Version: {version}
 * Palette: 16 warm greys, {SPLASH_WIDTH}x{SPLASH_HEIGHT}, 4bpp
 *
 * The HFS+ DETAILS text is an alpha mask so the bootloader can draw it green,
 * red or neutral without storing three full images. The illustration, title,
 * ornament and off-white background are the owner artwork.
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
    unsigned br = {int(BACKGROUND[0])};
    unsigned bg = {int(BACKGROUND[1])};
    unsigned bb = {int(BACKGROUND[2])};
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

    unsigned r = {COLOURS['unknown'][0]};
    unsigned g = {COLOURS['unknown'][1]};
    unsigned b = {COLOURS['unknown'][2]};
    if (status == HELIKON_SPLASH_HFS_OK) {{
        r = {COLOURS['ok'][0]};
        g = {COLOURS['ok'][1]};
        b = {COLOURS['ok'][2]};
    }} else if (status == HELIKON_SPLASH_HFS_FAIL) {{
        r = {COLOURS['fail'][0]};
        g = {COLOURS['fail'][1]};
        b = {COLOURS['fail'][2]};
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
    c_source_path = header_path.with_suffix(".c")
    c_source_path.write_text(c_source, encoding="utf-8")
    print(c_source_path)
    print(header_path)


def build(source: Path, font_path: Path, font_size: int, version: str,
          out_dir: Path, allow_non_benguiat: bool, emit_c: bool,
          header_path: Path) -> None:
    family = font_family(font_path)
    if "benguiat" not in family.lower() and not allow_non_benguiat:
        raise SystemExit(
            f"error: {font_path} is '{family}', not ITC Benguiat.\n"
            "       Pass --allow-non-benguiat only for a layout mockup.")

    base = Image.open(source).convert("RGB")
    if base.size != (SPLASH_WIDTH, SPLASH_HEIGHT):
        raise SystemExit(f"error: expected 320x240, got {base.size}")

    font = load_font(font_path, font_size)
    erase_version_area(base)
    draw_version(base, font, version)

    out_dir.mkdir(parents=True, exist_ok=True)
    for name, colour in COLOURS.items():
        variant = base.copy()
        recolour_hfs(variant, colour)
        path = out_dir / f"helikon-os-splash-{version.lower()}-hfs-{name}.png"
        variant.save(path)
        print(path)

    if not emit_c:
        return

    # The C image has no HFS+ text; the mask carries the glyphs.
    base_image = base.copy()
    blank_hfs(base_image)
    alpha = hfs_alpha(base)
    alpha4 = np.rint(alpha * 15.0).astype(np.uint8)
    palette = make_palette()
    indices = quantize(base_image, palette)
    base_4bpp = pack4(indices)
    mask_4bpp = pack4(alpha4)
    assert np.array_equal(unpack4(base_4bpp, SPLASH_WIDTH, SPLASH_HEIGHT),
                          indices)
    assert np.array_equal(unpack4(mask_4bpp, HFS_WIDTH, HFS_HEIGHT), alpha4)

    # Verify the packed representation before writing it.
    reconstructed = palette[indices].astype(np.float32)
    for y in range(HFS_HEIGHT):
        for x in range(HFS_WIDTH):
            a = int(alpha4[y, x])
            if a == 0:
                continue
            target = np.array(COLOURS["ok"], dtype=np.float32)
            reconstructed[HFS_AREA[1] + y, HFS_AREA[0] + x] = \
                BACKGROUND * (15 - a) / 15.0 + target * a / 15.0
    preview = Image.fromarray(np.clip(reconstructed, 0, 255).astype(np.uint8))
    preview.save(out_dir / f"helikon-os-splash-{version.lower()}-quantized.png")

    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    write_c(header_path, source, source_hash, family, font_size, version,
            palette, base_4bpp, mask_4bpp)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=Path("assets/bootloader/bootloader-design.jpg"))
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--font-size", type=int, default=9)
    parser.add_argument("--version", default="Alpha")
    parser.add_argument("--out-dir", type=Path,
                        default=Path("assets/bootloader"))
    parser.add_argument("--allow-non-benguiat", action="store_true")
    parser.add_argument("--emit-c", action="store_true")
    parser.add_argument("--header-out", type=Path,
                        default=Path("bootloader/splash_color.h"))
    args = parser.parse_args()
    build(args.source, args.font, args.font_size, args.version,
          args.out_dir, args.allow_non_benguiat, args.emit_c,
          args.header_out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
