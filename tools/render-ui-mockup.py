#!/usr/bin/env python3
"""Render the 320x240 host typography mockups. Host artifact only."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT_UI = ROOT / "assets/fonts/HelikonUI-Sans.ttf"
FONT_LOGO = ROOT / "assets/fonts/HelikonWordmark.ttf"
OUT = ROOT / "assets/ui"

WIDTH, HEIGHT = 320, 240
WHITE = 255
BLACK = 0
MUTED = 170


def ui_font(size, weight):
    font = ImageFont.truetype(str(FONT_UI), size)
    font.set_variation_by_axes([weight, 100])
    return font


def logo_font(size, weight=700):
    font = ImageFont.truetype(str(FONT_LOGO), size)
    font.set_variation_by_axes([weight])
    return font


def require_fit(draw, text, font, x, y, width, label):
    box = draw.textbbox((x, y), text, font=font)
    if box[0] < 0 or box[1] < 0 or box[2] > WIDTH or box[3] > HEIGHT:
        raise SystemExit(f"{label}: text outside canvas: {box}")
    if box[2] - x > width:
        raise SystemExit(f"{label}: text clips its {width}px column: {box}")
    return box


def menu_mockup():
    image = Image.new("L", (WIDTH, HEIGHT), BLACK)
    draw = ImageDraw.Draw(image)

    system = ui_font(11, 400)
    title = ui_font(17, 600)
    regular = ui_font(14, 400)
    medium = ui_font(14, 500)
    meta = ui_font(12, 400)
    logo = logo_font(20, 700)

    require_fit(draw, "HELIKON", logo, 10, 8, 150, "wordmark")
    draw.text((10, 8), "HELIKON", font=logo, fill=WHITE)
    require_fit(draw, "12:34   80%", system, 210, 12, 105, "status")
    draw.text((210, 12), "12:34   80%", font=system, fill=MUTED)

    draw.line((8, 36, WIDTH - 8, 36), fill=70, width=1)
    require_fit(draw, "Music", title, 10, 44, 180, "title")
    draw.text((10, 44), "Music", font=title, fill=WHITE)

    rows = [
        ("Now Playing", False),
        ("Artists", True),
        ("Albums", False),
        ("Songs", False),
        ("Playlists", False),
        ("Settings", False),
    ]
    y = 70
    for text, selected in rows:
        if selected:
            draw.rectangle((4, y - 3, WIDTH - 5, y + 17), fill=WHITE)
            require_fit(draw, text, medium, 12, y, 280, f"selected {text}")
            draw.text((12, y), text, font=medium, fill=BLACK)
        else:
            require_fit(draw, text, regular, 12, y, 280, text)
            draw.text((12, y), text, font=regular, fill=WHITE)
        y += 24

    draw.line((8, 208, WIDTH - 8, 208), fill=70, width=1)
    require_fit(draw, "Artist — Album", meta, 10, 214, 200, "artist/album")
    draw.text((10, 214), "Artist — Album", font=meta, fill=MUTED)
    require_fit(draw, "14 / 17 / 12 / 11 px", system, 206, 216, 110,
                "system footer")
    draw.text((206, 216), "14 / 17 / 12 / 11 px", font=system, fill=MUTED)

    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / "helikon-os-ui-320x240.png"
    image.save(path)
    return path


def proof_mockup():
    image = Image.new("L", (WIDTH, HEIGHT), BLACK)
    draw = ImageDraw.Draw(image)
    system = ui_font(11, 400)
    title = ui_font(17, 600)
    regular = ui_font(14, 400)
    medium = ui_font(14, 500)
    meta = ui_font(12, 400)
    logo = logo_font(24, 700)

    require_fit(draw, "HELIKON-OS", logo, 10, 8, 250, "wordmark")
    draw.text((10, 8), "HELIKON-OS", font=logo, fill=WHITE)
    require_fit(draw, "Typography proof · 320x240", system, 10, 40, 300,
                "proof label")
    draw.text((10, 40), "Typography proof · 320x240", font=system, fill=MUTED)
    draw.line((8, 58, WIDTH - 8, 58), fill=70, width=1)

    draw.text((10, 64), "Title · 17 Semibold", font=title, fill=WHITE)
    draw.text((10, 90), "Main menu · 14 Regular", font=regular, fill=WHITE)
    draw.rectangle((4, 110, WIDTH - 5, 136), fill=WHITE)
    draw.text((12, 114), "Selected · 14 Medium", font=medium, fill=BLACK)
    draw.text((10, 144), "Artist / Album · 12 Regular", font=meta,
              fill=WHITE)
    draw.text((10, 164), "System · 11 Regular", font=system, fill=MUTED)
    draw.line((8, 190, WIDTH - 8, 190), fill=70, width=1)
    draw.text((10, 198), "Grayscale AA · 1-bit fallback in T-042", font=system,
              fill=MUTED)

    for label, font in (
        ("title", title),
        ("main", regular),
        ("selected", medium),
        ("meta", meta),
        ("system", system),
        ("wordmark", logo),
    ):
        box = draw.textbbox((0, 0), "HELIKON-OS", font=font)
        if box[2] - box[0] > WIDTH - 16:
            raise SystemExit(f"{label}: sample width {box[2] - box[0]}")

    path = OUT / "helikon-os-typography-320x240.png"
    image.save(path)
    return path


if __name__ == "__main__":
    ui = menu_mockup()
    proof = proof_mockup()
    print(ui)
    print(proof)
