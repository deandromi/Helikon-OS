#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the bounded IBM Plex Sans text atlas for the Music flow.

The atlas is a 4-bit alpha mask set for the D-23 roles used by docs/ui.md 6:
11/12/14/17 px at weights 400/500/600. It is generated from the licensed
assets/fonts/HelikonUI-Sans.ttf subset and committed as C so the firmware never
rasterizes a font at runtime.

Run from the repository root:

    python3 tools/make-ui-font-atlas.py

The generated atlas is read-only .rodata. The renderer owns no allocation and
no per-frame rasterization.
"""
from __future__ import annotations

import hashlib
from pathlib import Path

from PIL import ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets/fonts/HelikonUI-Sans.ttf"
OUT_C = ROOT / "firmware" / "ui" / "font_atlas.c"
OUT_H = ROOT / "firmware" / "ui" / "font_atlas.h"

SIZES = (11, 12, 14, 17)
WEIGHTS = (400, 500, 600)
LINE_HEIGHTS = {11: 14, 12: 16, 14: 18, 17: 20}

# ASCII printable, Latin-1, and the UI punctuation used by the binding design.
CODEPOINTS = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + [
    0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
]


def face(size: int, weight: int) -> ImageFont.FreeTypeFont:
    font = ImageFont.truetype(str(FONT), size)
    font.set_variation_by_axes([weight, 100])
    return font


def glyph(font: ImageFont.FreeTypeFont, codepoint: int):
    text = chr(codepoint)
    advance = int(round(font.getlength(text)))
    try:
        mask, offset = font.getmask2(text, mode="L", anchor="ls")
    except Exception:
        return None
    width, height = mask.size
    if width == 0 or height == 0:
        if advance == 0:
            return None
        return advance, 0, 0, 0, 0, bytes(), 0
    if advance <= 0:
        advance = width
    pixels = bytes(mask)
    packed = bytearray()
    stride = (width + 1) // 2
    for row in range(height):
        base = row * width
        for column in range(0, width, 2):
            high = pixels[base + column] >> 4
            low = pixels[base + column + 1] >> 4 if column + 1 < width else 0
            packed.append((high << 4) | low)
    return advance, offset[0], offset[1], width, height, bytes(packed), stride


def fallback_glyph(font: ImageFont.FreeTypeFont, codepoint: int):
    text = chr(codepoint)
    advance = int(round(font.getlength(text)))
    try:
        mask, offset = font.getmask2(text, mode="L", anchor="ls")
    except Exception:
        return None
    width, height = mask.size
    if width == 0 or height == 0:
        if advance == 0:
            return None
        return advance, 0, 0, 0, 0, bytes(), 0
    if advance <= 0:
        advance = width
    pixels = bytes(mask)
    packed = bytearray()
    stride = (width + 7) // 8
    for row in range(height):
        base = row * width
        for column in range(0, width, 8):
            value = 0
            for bit in range(8):
                if column + bit < width and pixels[base + column + bit] >= 80:
                    value |= 0x80 >> bit
            packed.append(value)
    return advance, offset[0], offset[1], width, height, bytes(packed), stride


def main() -> int:
    styles = []
    glyphs = []
    bits = bytearray()
    fallback_glyphs = []
    fallback_bits = bytearray()
    fallback_ascii = list(range(0x20, 0x7F)) + [
        0x00B7, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
    ]
    for size in SIZES:
        for weight in WEIGHTS:
            font = face(size, weight)
            first = len(glyphs)
            first_fallback = len(fallback_glyphs)
            for codepoint in CODEPOINTS:
                entry = glyph(font, codepoint)
                if entry is None:
                    continue
                advance, bearing_x, bearing_y, width, height, data, stride = entry
                glyphs.append((codepoint, advance, bearing_x, bearing_y,
                               width, height, len(bits), stride))
                bits.extend(data)
            for codepoint in fallback_ascii:
                entry = fallback_glyph(font, codepoint)
                if entry is None:
                    continue
                advance, bearing_x, bearing_y, width, height, data, stride = entry
                fallback_glyphs.append((codepoint, advance, bearing_x,
                                        bearing_y, width, height,
                                        len(fallback_bits), stride))
                fallback_bits.extend(data)
            styles.append((size, weight, first, len(glyphs) - first,
                           first_fallback,
                           len(fallback_glyphs) - first_fallback,
                           LINE_HEIGHTS[size]))

    font_hash = hashlib.sha256(FONT.read_bytes()).hexdigest()
    total_glyphs = len(glyphs)
    max_style = max(style[3] for style in styles)
    c = []
    c.append("/* SPDX-License-Identifier: GPL-2.0-or-later */")
    c.append("/*")
    c.append(" * Generated by tools/make-ui-font-atlas.py. Do not edit.")
    c.append(" *")
    c.append(" * IBM Plex Sans subset, SIL Open Font License 1.1.")
    c.append(" * Source: assets/fonts/HelikonUI-Sans.ttf")
    c.append(f" * Source SHA-256: {font_hash}")
    c.append(" * Styles: 11/12/14/17 px at 400/500/600, 4-bit alpha masks.")
    c.append(" * Atlas is read-only .rodata; no runtime rasterization.")
    c.append(" */")
    c.append("")
    c.append('#include "font_atlas.h"')
    c.append("")
    c.append(f"#define ATLAS_GLYPH_COUNT {total_glyphs}u")
    c.append(f"#define ATLAS_BITS_BYTES {len(bits)}u")
    c.append("")
    c.append("const struct helikon_font_style helikon_font_styles[] = {")
    for size, weight, first, count, fb_first, fb_count, line_height in styles:
        c.append("    { %du, %du, %du, %du, %du, %du, %du },"
                 % (size, weight, first, count, fb_first, fb_count,
                    line_height))
    c.append("};")
    c.append("")
    c.append("const uint32_t helikon_font_style_count =")
    c.append(f"    sizeof(helikon_font_styles) / sizeof(helikon_font_styles[0]);")
    c.append("")
    c.append("const struct helikon_font_glyph helikon_font_glyphs[] = {")
    for codepoint, advance, bearing_x, bearing_y, width, height, offset, stride in glyphs:
        c.append(
            "    { 0x%04xu, %du, %d, %d, %du, %du, %du, %du },"
            % (codepoint, advance, bearing_x, bearing_y, width, height,
               offset, stride))
    c.append("};")
    c.append("")
    c.append("const uint32_t helikon_font_glyph_count =")
    c.append("    sizeof(helikon_font_glyphs) / sizeof(helikon_font_glyphs[0]);")
    c.append("")
    c.append("const uint8_t helikon_font_bits[] = {")
    for index in range(0, len(bits), 16):
        chunk = bits[index:index + 16]
        c.append("    " + ", ".join("0x%02x" % value for value in chunk) + ",")
    c.append("};")
    c.append("")
    c.append("const uint32_t helikon_font_bits_bytes =")
    c.append("    sizeof(helikon_font_bits) / sizeof(helikon_font_bits[0]);")
    c.append("")
    c.append("const struct helikon_font_glyph helikon_font_fallback_glyphs[] = {")
    for codepoint, advance, bearing_x, bearing_y, width, height, offset, stride in fallback_glyphs:
        c.append(
            "    { 0x%04xu, %du, %d, %d, %du, %du, %du, %du },"
            % (codepoint, advance, bearing_x, bearing_y, width, height,
               offset, stride))
    c.append("};")
    c.append("")
    c.append("const uint32_t helikon_font_fallback_glyph_count =")
    c.append("    sizeof(helikon_font_fallback_glyphs) /")
    c.append("    sizeof(helikon_font_fallback_glyphs[0]);")
    c.append("")
    c.append("const uint8_t helikon_font_fallback_bits[] = {")
    for index in range(0, len(fallback_bits), 16):
        chunk = fallback_bits[index:index + 16]
        c.append("    " + ", ".join("0x%02x" % value for value in chunk) + ",")
    c.append("};")
    c.append("")
    c.append("const uint32_t helikon_font_fallback_bits_bytes =")
    c.append("    sizeof(helikon_font_fallback_bits) /")
    c.append("    sizeof(helikon_font_fallback_bits[0]);")
    c.append("")
    OUT_C.write_text("\n".join(c).rstrip("\n") + "\n")

    h = []
    h.append("/* SPDX-License-Identifier: GPL-2.0-or-later */")
    h.append("/*")
    h.append(" * Generated by tools/make-ui-font-atlas.py. Do not edit.")
    h.append(" *")
    h.append(" * IBM Plex Sans subset, SIL Open Font License 1.1.")
    h.append(" * Source: assets/fonts/HelikonUI-Sans.ttf")
    h.append(f" * Source SHA-256: {font_hash}")
    h.append(" */")
    h.append("#ifndef HELIKON_UI_FONT_ATLAS_H")
    h.append("#define HELIKON_UI_FONT_ATLAS_H")
    h.append("")
    h.append("#include <stdint.h>")
    h.append("")
    h.append("#define HELIKON_FONT_ATLAS_VERSION 1u")
    h.append("")
    h.append("enum helikon_font_style_id {")
    for index, (size, weight, _, _, _, _, _) in enumerate(styles):
        h.append("    HELIKON_FONT_%d_%d = %du," % (size, weight, index))
    h.append("    HELIKON_FONT_STYLE_COUNT")
    h.append("};")
    h.append("")
    h.append("struct helikon_font_style {")
    h.append("    uint8_t size;")
    h.append("    uint16_t weight;")
    h.append("    uint16_t first_glyph;")
    h.append("    uint16_t glyph_count;")
    h.append("    uint16_t first_fallback;")
    h.append("    uint16_t fallback_count;")
    h.append("    uint8_t line_height;")
    h.append("};")
    h.append("")
    h.append("struct helikon_font_glyph {")
    h.append("    uint16_t codepoint;")
    h.append("    uint8_t advance;")
    h.append("    int8_t bearing_x;")
    h.append("    int8_t bearing_y;")
    h.append("    uint8_t width;")
    h.append("    uint8_t height;")
    h.append("    uint32_t offset;")
    h.append("    uint16_t stride;")
    h.append("};")
    h.append("")
    h.append("extern const struct helikon_font_style helikon_font_styles[];")
    h.append("extern const uint32_t helikon_font_style_count;")
    h.append("extern const struct helikon_font_glyph helikon_font_glyphs[];")
    h.append("extern const uint32_t helikon_font_glyph_count;")
    h.append("extern const uint8_t helikon_font_bits[];")
    h.append("extern const uint32_t helikon_font_bits_bytes;")
    h.append("extern const struct helikon_font_glyph helikon_font_fallback_glyphs[];")
    h.append("extern const uint32_t helikon_font_fallback_glyph_count;")
    h.append("extern const uint8_t helikon_font_fallback_bits[];")
    h.append("extern const uint32_t helikon_font_fallback_bits_bytes;")
    h.append("")
    h.append("#endif")
    h.append("")
    OUT_H.write_text("\n".join(h).rstrip("\n") + "\n")

    print("font:", FONT)
    print("sha256:", font_hash)
    print("styles:", len(styles), "glyphs:", total_glyphs,
          "max/style:", max_style)
    print("bits:", len(bits), "bytes")
    print("fallback bits:", len(fallback_bits), "bytes",
          "fallback glyphs:", len(fallback_glyphs))
    print("generated:", OUT_C, OUT_H)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
