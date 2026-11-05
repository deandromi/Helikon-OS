#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the firmware launcher tiles from the committed UI mockup code.

The launcher layout and icon line art come from tools/render-ui-mockup.py so
the firmware and the design sources cannot drift. The HELIKON-OS wordmark is
rasterized from the owner-supplied ITC Benguiat Medium face, at the owner's
direction; the .otf file is not committed. Labels use the tracked IBM Plex
Sans fallback.

Outputs firmware/ui/launcher_assets.c and launcher_assets.h. Every tile is an
opaque RGB565 rectangle on the paper background; the firmware draws only the
focus diamond and underline dynamically.
"""
from __future__ import annotations

import importlib.util
import os
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parent.parent
MOCK = ROOT / "tools" / "render-ui-mockup.py"
OUT_C = ROOT / "firmware" / "ui" / "launcher_assets.c"
OUT_H = ROOT / "firmware" / "ui" / "launcher_assets.h"
BENGUIAT_DIR = Path(os.environ.get(
    "BENGUIAT_DIR", str(Path.home() / "Downloads" / "ITC Benguiat"))).expanduser()

spec = importlib.util.spec_from_file_location("helikon_mockup", MOCK)
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)

PAPER = mock.HOME_PAPER
INK = mock.HOME_INK
MUTED = mock.HOME_MUTED
RULE = mock.HOME_RULE

CELLS = (
    ("Music", "lyre"),
    ("Podcasts", "mic"),
    ("Audiobooks", "book"),
    ("Playlists", "list"),
    ("Games", "pawn"),
    ("Settings", "sliders"),
)


def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def benguiat(size, weight):
    path = BENGUIAT_DIR / f"BenguiatStd-{weight}.otf"
    if not path.is_file():
        raise SystemExit(
            f"launcher wordmark: missing {path}. Set BENGUIAT_DIR to the "
            "owner-supplied ITC Benguiat folder. The font is not committed.")
    return mock.ImageFont.truetype(str(path), size)


def background_tile():
    """320x64: folio, wordmark and divider exactly as the launcher mockup."""
    image = mock.Image.new("RGB", (mock.WIDTH, 64), PAPER)
    draw = mock.ImageDraw.Draw(image)
    system = mock.ui_font(11, 400)
    wordmark = benguiat(26, "Medium")

    mock.draw_tracked(draw, (20, 18), "ALPHA · 6G", system, MUTED, 0.8)
    mock.draw_right_tracked(draw, (274, 18), "12:34", system, MUTED)
    mock.battery(draw, 282, 9, 18, 9, 0.7, INK)
    mock.diamond(draw, 160, 14, 2, RULE)
    draw.text((160, 44), "HELIKON-OS", font=wordmark, fill=INK, anchor="ms")
    draw.line((40, 56, 146, 56), fill=RULE, width=2)
    draw.line((174, 56, 280, 56), fill=RULE, width=2)
    mock.diamond(draw, 40, 56, 2, RULE)
    mock.diamond(draw, 280, 56, 2, RULE)
    mountain = mock.mountain_icon()
    image.paste(mountain, (149, 50), mountain)
    return image, 0, 0


def label_tile(text, font, anchor_x, anchor_y):
    probe = mock.Image.new("RGB", (1, 1), PAPER)
    probe_draw = mock.ImageDraw.Draw(probe)
    box = probe_draw.textbbox((anchor_x, anchor_y), text, font=font,
                              anchor="ms")
    pad = 1
    width = box[2] - box[0] + pad * 2
    height = box[3] - box[1] + pad * 2
    tile = mock.Image.new("RGB", (width, height), PAPER)
    draw = mock.ImageDraw.Draw(tile)
    draw.text((anchor_x - box[0] + pad, anchor_y - box[1] + pad), text,
              font=font, fill=INK, anchor="ms")
    return tile, box[0] - pad, box[1] - pad


def icon_tile(kind, accent, x, y):
    icon = mock.launcher_icon(40, kind, accent)
    bounds = icon.getbbox()
    if bounds is None:
        raise SystemExit(f"launcher icon {kind} is empty")
    icon_width = bounds[2] - bounds[0]
    icon_height = bounds[3] - bounds[1]
    icon_x = round(x + 32 - icon_width / 2 - bounds[0])
    icon_y = round(y + 32 - icon_height / 2 - bounds[1])
    tile = mock.Image.new("RGB", (40, 40), PAPER)
    tile.paste(icon, (0, 0), icon)
    return tile, icon_x, icon_y


def emit_pixels(path, name, image):
    width, height = image.size
    pixels = list(image.getdata())
    path.write(f"static const uint16_t {name}_pixels[{width}u * {height}u] = {{\n")
    for index in range(0, len(pixels), 12):
        row = pixels[index:index + 12]
        path.write("    " + ", ".join(f"0x{rgb565(p):04x}" for p in row)
                   + ",\n")
    path.write("};\n")


def main():
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    tiles = []
    background, bx, by = background_tile()
    tiles.append(("background", background, bx, by))

    label_font = mock.ui_font(13, 500)
    labels = []
    icons = []
    plinths = []
    for index, (label, kind) in enumerate(CELLS):
        column = index % 3
        row = index // 3
        x = 24 + column * 104
        y = 64 + row * 86
        accent = mock.LAUNCHER_ACCENTS[label]

        tile, tx, ty = label_tile(label, label_font, x + 32, y + 78)
        labels.append((label.lower(), tile, tx, ty))
        icon, ix, iy = icon_tile(kind, accent, x, y)
        icons.append((f"{label.lower()}_icon", icon, ix, iy))
        plinths.append(rgb565(mock.mix(accent, PAPER, 0.50)))

    with OUT_C.open("w", encoding="utf-8") as path:
        path.write("/* SPDX-License-Identifier: GPL-2.0-or-later */\n")
        path.write("/* Generated by tools/make-launcher-assets.py. Do not edit. */\n")
        path.write('#include "launcher_assets.h"\n\n')
        for name, image, tx, ty in tiles:
            emit_pixels(path, name, image)
            path.write("\n")
        for name, image, tx, ty in labels + icons:
            emit_pixels(path, name, image)
            path.write("\n")

        path.write("const struct helikon_launcher_bitmap "
                   "helikon_launcher_background = {\n")
        path.write("    background_pixels, 320u, 64u, 0, 0\n};\n\n")
        path.write("const struct helikon_launcher_bitmap "
                   "helikon_launcher_labels[6] = {\n")
        for name, image, tx, ty in labels:
            width, height = image.size
            path.write(f"    {{ {name}_pixels, {width}u, {height}u, "
                       f"{tx}, {ty} }},\n")
        path.write("};\n\n")
        path.write("const struct helikon_launcher_bitmap "
                   "helikon_launcher_icons[6] = {\n")
        for name, image, tx, ty in icons:
            path.write(f"    {{ {name}_pixels, 40u, 40u, {tx}, {ty} }},\n")
        path.write("};\n\n")
        path.write("const uint16_t helikon_launcher_plinths[6] = {\n    ")
        path.write(", ".join(f"0x{value:04x}" for value in plinths))
        path.write("\n};\n")

    with OUT_H.open("w", encoding="utf-8") as path:
        path.write("/* SPDX-License-Identifier: GPL-2.0-or-later */\n")
        path.write("/* Generated by tools/make-launcher-assets.py. Do not edit. */\n")
        path.write("#ifndef HELIKON_UI_LAUNCHER_ASSETS_H\n")
        path.write("#define HELIKON_UI_LAUNCHER_ASSETS_H\n\n")
        path.write("#include <stdint.h>\n\n")
        path.write("struct helikon_launcher_bitmap {\n")
        path.write("    const uint16_t *pixels;\n")
        path.write("    uint16_t width;\n    uint16_t height;\n")
        path.write("    int16_t x;\n    int16_t y;\n};\n\n")
        path.write("extern const struct helikon_launcher_bitmap "
                   "helikon_launcher_background;\n")
        path.write("extern const struct helikon_launcher_bitmap "
                   "helikon_launcher_labels[6];\n")
        path.write("extern const struct helikon_launcher_bitmap "
                   "helikon_launcher_icons[6];\n")
        path.write("extern const uint16_t helikon_launcher_plinths[6];\n\n")
        path.write("#endif\n")

    print(OUT_C)
    print(OUT_H)


if __name__ == "__main__":
    main()
