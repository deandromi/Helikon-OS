#!/usr/bin/env python3
"""Render the 320x240 host UI mockups. Host artifacts only.

The home mockup uses the owner-supplied ITC Benguiat OTFs for its wordmark and
section header. Set BENGUIAT_DIR to that folder; the fonts are not committed.
"""
import math
import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT_UI = ROOT / "assets/fonts/HelikonUI-Sans.ttf"
FONT_LOGO = ROOT / "assets/fonts/HelikonWordmark.ttf"
OUT = ROOT / "assets/ui"

WIDTH, HEIGHT = 320, 240
WHITE = 255
BLACK = 0
GRAY = 170


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
    draw.text((210, 12), "12:34   80%", font=system, fill=GRAY)

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
    draw.text((10, 214), "Artist — Album", font=meta, fill=GRAY)
    require_fit(draw, "14 / 17 / 12 / 11 px", system, 206, 216, 110,
                "system footer")
    draw.text((206, 216), "14 / 17 / 12 / 11 px", font=system, fill=GRAY)

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
    draw.text((10, 40), "Typography proof · 320x240", font=system, fill=GRAY)
    draw.line((8, 58, WIDTH - 8, 58), fill=70, width=1)

    draw.text((10, 64), "Title · 17 Semibold", font=title, fill=WHITE)
    draw.text((10, 90), "Main menu · 14 Regular", font=regular, fill=WHITE)
    draw.rectangle((4, 110, WIDTH - 5, 136), fill=WHITE)
    draw.text((12, 114), "Selected · 14 Medium", font=medium, fill=BLACK)
    draw.text((10, 144), "Artist / Album · 12 Regular", font=meta,
              fill=WHITE)
    draw.text((10, 164), "System · 11 Regular", font=system, fill=GRAY)
    draw.line((8, 190, WIDTH - 8, 190), fill=70, width=1)
    draw.text((10, 198), "Grayscale AA · 1-bit fallback in T-042", font=system,
              fill=GRAY)

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


# --- Home and library concept mockups -----------------------------------

HOME_PAPER = (239, 237, 229)
HOME_PAPER_LIGHT = (245, 243, 235)
HOME_INK = (36, 35, 31)
HOME_MUTED = (107, 102, 92)
HOME_RULE = (179, 173, 158)
HOME_FAINT = (220, 215, 202)
HOME_GREEN = (0, 191, 99)

LAUNCHER_ACCENTS = {
    "Music": (156, 63, 44),
    "Podcasts": (49, 83, 107),
    "Audiobooks": (138, 100, 32),
    "Playlists": (72, 99, 70),
    "Games": (100, 54, 76),
    "Settings": (45, 95, 88),
}

BENGUIAT_DIR = Path(
    os.environ.get("BENGUIAT_DIR", str(Path.home() / "Downloads/ITC Benguiat"))
).expanduser()

LAUNCHER = OUT / "launcher" / "helikon-os-home-launcher-320x240.png"
LAUNCHER_PREVIEW = OUT / "launcher" / "helikon-os-home-launcher-320x240@2x.png"
INDEX = OUT / "index" / "helikon-os-home-index-320x240.png"
INDEX_PREVIEW = OUT / "index" / "helikon-os-home-index-320x240@2x.png"
ALBUM_WALL = OUT / "album-wall" / "helikon-os-album-wall-320x240.png"
ALBUM_WALL_PREVIEW = OUT / "album-wall" / "helikon-os-album-wall-320x240@2x.png"
PODCAST_WALL = OUT / "podcast-wall" / "helikon-os-podcast-wall-320x240.png"
PODCAST_WALL_PREVIEW = OUT / "podcast-wall" / "helikon-os-podcast-wall-320x240@2x.png"
AUDIOBOOK_WALL = OUT / "audiobook-wall" / "helikon-os-audiobook-wall-320x240.png"
AUDIOBOOK_WALL_PREVIEW = OUT / "audiobook-wall" / "helikon-os-audiobook-wall-320x240@2x.png"
ALBUM_VIEW = OUT / "album-view" / "helikon-os-album-view-320x240.png"
ALBUM_VIEW_PREVIEW = OUT / "album-view" / "helikon-os-album-view-320x240@2x.png"
ARTIST_VIEW = OUT / "artist-view" / "helikon-os-artist-view-320x240.png"
ARTIST_VIEW_PREVIEW = OUT / "artist-view" / "helikon-os-artist-view-320x240@2x.png"


def benguiat_font(size, weight):
    path = BENGUIAT_DIR / f"BenguiatStd-{weight}.otf"
    if not path.is_file():
        raise SystemExit(
            f"home mockup: missing {path}. Set BENGUIAT_DIR to the owner-supplied "
            "ITC Benguiat folder. The font is not committed."
        )
    return ImageFont.truetype(str(path), size)


def tracked_width(text, font, tracking):
    return sum(font.getlength(char) for char in text) + tracking * (len(text) - 1)


def draw_tracked(draw, xy, text, font, fill, tracking=0.0):
    x, y = xy
    for char in text:
        draw.text((x, y), char, font=font, fill=fill, anchor="ls")
        x += font.getlength(char) + tracking
    return x - tracking


def draw_right_tracked(draw, xy, text, font, fill, tracking=0.0):
    x, y = xy
    draw_tracked(draw, (x - tracked_width(text, font, tracking), y), text,
                 font, fill, tracking)


def require_width(text, font, tracking, limit, label):
    width = tracked_width(text, font, tracking)
    if width > limit:
        raise SystemExit(f"{label}: {width:.1f}px exceeds {limit}px")


def diamond(draw, cx, cy, radius, fill):
    draw.polygon(
        [(cx, cy - radius), (cx + radius, cy),
         (cx, cy + radius), (cx - radius, cy)],
        fill=fill,
    )


def hourglass(draw, x, y, width, height, fill):
    draw.line((x, y, x + width - 1, y), fill=fill)
    draw.line((x, y + height - 1, x + width - 1, y + height - 1), fill=fill)
    cx = x + (width - 1) / 2
    middle = y + height / 2
    draw.line((x + 1, y + 1, cx, middle), fill=fill)
    draw.line((x + width - 2, y + 1, cx, middle), fill=fill)
    draw.line((x + 1, y + height - 2, cx, middle), fill=fill)
    draw.line((x + width - 2, y + height - 2, cx, middle), fill=fill)
    draw.line((cx - 1, y + height - 3, cx + 1, y + height - 3), fill=fill)


def battery(draw, x, y, width, height, level, fill):
    draw.rectangle((x, y, x + width - 4, y + height - 1), outline=fill)
    draw.rectangle((x + width - 2, y + 2, x + width - 1, y + height - 3),
                   fill=fill)
    inner = width - 6
    filled = max(1, round(inner * level))
    draw.rectangle((x + 2, y + 2, x + 2 + filled, y + height - 3), fill=fill)


def mix(first, second, amount):
    return tuple(round(first[index] * (1 - amount) + second[index] * amount)
                 for index in range(3))


def mountain_icon(size=22, height=10, colour=HOME_INK):
    scale = 4
    canvas = size * scale
    tall = height * scale
    art = Image.new("RGBA", (canvas, tall), (0, 0, 0, 0))
    draw = ImageDraw.Draw(art)
    ink = colour + (255,)
    width = scale
    draw.line((0, tall - 1, canvas - 1, tall - 1), fill=ink, width=width)
    draw.line((0, tall - 1, canvas * 0.42, tall * 0.10,
               canvas * 0.82, tall - 1), fill=ink, width=width,
              joint="curve")
    draw.ellipse((canvas * 0.76, tall * 0.06,
                  canvas * 0.94, tall * 0.34), outline=ink, width=width)
    return art.resize((size, height), Image.Resampling.LANCZOS)


def save_pair(image, path, preview):
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)
    image.resize((WIDTH * 2, HEIGHT * 2), Image.Resampling.NEAREST).save(preview)
    return path, preview


def cover_art(size, kind):
    """Generated album-wall line art; not album artwork."""
    scale = 4
    canvas = size * scale
    art = Image.new("RGB", (canvas, canvas), HOME_PAPER_LIGHT)
    draw = ImageDraw.Draw(art)
    ink = (126, 120, 106)
    faint = (160, 154, 140)
    width = scale
    c = canvas

    if kind == "mountain":
        draw.ellipse((c * 0.66, c * 0.16, c * 0.82, c * 0.32),
                     outline=ink, width=width)
        draw.line((0, c * 0.62, c * 0.28, c * 0.34, c * 0.52, c * 0.62),
                  fill=ink, width=width, joint="curve")
        draw.line((c * 0.32, c * 0.62, c * 0.58, c * 0.42, c * 0.86, c * 0.62),
                  fill=faint, width=width, joint="curve")
        draw.line((0, c * 0.64, c, c * 0.64), fill=ink, width=width)
        for line in range(1, 6):
            yy = c * 0.64 + line * c * 0.06
            draw.line((c * 0.06, yy, c * 0.94, yy), fill=faint, width=width)
    elif kind == "record":
        center = c / 2
        for fraction in (0.44, 0.36, 0.28, 0.20):
            radius = c * fraction
            draw.ellipse((center - radius, center - radius,
                          center + radius, center + radius),
                         outline=ink, width=width)
        draw.ellipse((center - c * 0.05, center - c * 0.05,
                      center + c * 0.05, center + c * 0.05), fill=ink)
        draw.line((c * 0.80, c * 0.18, c * 0.58, c * 0.42),
                  fill=faint, width=width)
        draw.ellipse((c * 0.78, c * 0.14, c * 0.84, c * 0.20), fill=faint)
    elif kind == "columns":
        draw.line((c * 0.16, c * 0.30, c * 0.50, c * 0.14,
                   c * 0.84, c * 0.30), fill=ink, width=width, joint="curve")
        draw.line((c * 0.16, c * 0.33, c * 0.84, c * 0.33),
                  fill=ink, width=width)
        for column in (0.24, 0.37, 0.50, 0.63, 0.76):
            draw.line((c * column, c * 0.37, c * column, c * 0.72),
                      fill=ink, width=width)
        draw.line((c * 0.18, c * 0.75, c * 0.82, c * 0.75),
                  fill=ink, width=width)
        draw.line((c * 0.22, c * 0.80, c * 0.78, c * 0.80),
                  fill=faint, width=width)
    elif kind == "hourglass":
        draw.line((c * 0.28, c * 0.14, c * 0.72, c * 0.14),
                  fill=ink, width=width)
        draw.line((c * 0.28, c * 0.84, c * 0.72, c * 0.84),
                  fill=ink, width=width)
        draw.line((c * 0.31, c * 0.17, c * 0.50, c * 0.48),
                  fill=ink, width=width)
        draw.line((c * 0.69, c * 0.17, c * 0.50, c * 0.48),
                  fill=ink, width=width)
        draw.line((c * 0.31, c * 0.81, c * 0.50, c * 0.52),
                  fill=ink, width=width)
        draw.line((c * 0.69, c * 0.81, c * 0.50, c * 0.52),
                  fill=ink, width=width)
        draw.line((c * 0.42, c * 0.77, c * 0.58, c * 0.77),
                  fill=faint, width=width)
    elif kind == "wave":
        for band, fraction in enumerate((0.34, 0.52, 0.70)):
            points = []
            for step in range(41):
                x = c * (0.08 + 0.84 * step / 40)
                y = c * (fraction + 0.045 * math.sin(step / 40 * math.pi * 2 + band))
                points.append((x, y))
            draw.line(points, fill=ink if band == 1 else faint,
                      width=width, joint="curve")
        draw.ellipse((c * 0.63, c * 0.10, c * 0.75, c * 0.22),
                     outline=ink, width=width)
    elif kind == "arch":
        draw.arc((c * 0.26, c * 0.18, c * 0.74, c * 0.68), 180, 360,
                 fill=ink, width=width)
        draw.line((c * 0.26, c * 0.43, c * 0.26, c * 0.82),
                  fill=ink, width=width)
        draw.line((c * 0.74, c * 0.43, c * 0.74, c * 0.82),
                  fill=ink, width=width)
        draw.line((c * 0.18, c * 0.82, c * 0.82, c * 0.82),
                  fill=ink, width=width)
        draw.line((c * 0.46, c * 0.20, c * 0.54, c * 0.20),
                  fill=faint, width=width)
    elif kind == "sun":
        draw.ellipse((c * 0.34, c * 0.34, c * 0.66, c * 0.66),
                     outline=ink, width=width)
        for angle in range(0, 360, 45):
            radians = math.radians(angle)
            x1 = c * (0.5 + 0.18 * math.cos(radians))
            y1 = c * (0.5 + 0.18 * math.sin(radians))
            x2 = c * (0.5 + 0.28 * math.cos(radians))
            y2 = c * (0.5 + 0.28 * math.sin(radians))
            draw.line((x1, y1, x2, y2), fill=ink, width=width)
        draw.line((c * 0.08, c * 0.84, c * 0.92, c * 0.84),
                  fill=faint, width=width)
    elif kind == "laurel":
        for side in (-1, 1):
            points = [(c * 0.5, c * 0.84)]
            for step in range(1, 7):
                t = step / 6
                points.append((c * (0.5 + side * (0.06 + 0.28 * t)),
                               c * (0.84 - 0.62 * t)))
            draw.line(points, fill=ink, width=width, joint="curve")
            for step in range(1, 5):
                t = step / 5
                x = c * (0.5 + side * (0.08 + 0.24 * t))
                y = c * (0.78 - 0.52 * t)
                draw.line((x, y, x + side * c * 0.08, y - c * 0.04),
                          fill=faint, width=width)
        draw.ellipse((c * 0.47, c * 0.12, c * 0.53, c * 0.18), fill=ink)
    elif kind == "obelisk":
        draw.polygon(
            [(c * 0.44, c * 0.18), (c * 0.56, c * 0.18),
             (c * 0.62, c * 0.80), (c * 0.38, c * 0.80)],
            outline=ink, width=width)
        draw.line((c * 0.30, c * 0.82, c * 0.70, c * 0.82),
                  fill=ink, width=width)
        draw.line((c * 0.26, c * 0.88, c * 0.74, c * 0.88),
                  fill=faint, width=width)
        draw.ellipse((c * 0.62, c * 0.12, c * 0.72, c * 0.22),
                     outline=ink, width=width)
    elif kind == "moon":
        draw.ellipse((c * 0.28, c * 0.18, c * 0.68, c * 0.58), fill=ink)
        draw.ellipse((c * 0.38, c * 0.12, c * 0.78, c * 0.52),
                     fill=HOME_PAPER_LIGHT)
        for x, y in ((0.72, 0.20), (0.80, 0.38), (0.66, 0.44)):
            draw.polygon(
                [(c * x, c * (y - 0.02)), (c * (x + 0.02), c * y),
                 (c * x, c * (y + 0.02)), (c * (x - 0.02), c * y)],
                fill=faint)
    elif kind == "compass":
        draw.ellipse((c * 0.18, c * 0.18, c * 0.82, c * 0.82),
                     outline=ink, width=width)
        draw.line((c * 0.5, c * 0.14, c * 0.5, c * 0.86),
                  fill=faint, width=width)
        draw.line((c * 0.14, c * 0.5, c * 0.86, c * 0.5),
                  fill=faint, width=width)
        draw.polygon(
            [(c * 0.5, c * 0.26), (c * 0.58, c * 0.5),
             (c * 0.5, c * 0.74), (c * 0.42, c * 0.5)],
            outline=ink, width=width)
        draw.ellipse((c * 0.47, c * 0.47, c * 0.53, c * 0.53), fill=ink)
    elif kind == "mic":
        draw.rounded_rectangle(
            (c * 0.38, c * 0.12, c * 0.62, c * 0.50),
            radius=c * 0.12, outline=ink, width=width)
        draw.arc((c * 0.28, c * 0.24, c * 0.72, c * 0.68), 0, 180,
                 fill=ink, width=width)
        for y in (0.22, 0.29, 0.36):
            draw.line((c * 0.42, c * y, c * 0.58, c * y),
                      fill=faint, width=width)
        draw.line((c * 0.50, c * 0.64, c * 0.50, c * 0.80),
                  fill=ink, width=width)
        draw.line((c * 0.36, c * 0.84, c * 0.64, c * 0.84),
                  fill=ink, width=width)
    elif kind == "book":
        draw.line((c * 0.12, c * 0.24, c * 0.48, c * 0.30,
                   c * 0.48, c * 0.78, c * 0.12, c * 0.70,
                   c * 0.12, c * 0.24), fill=ink, width=width, joint="curve")
        draw.line((c * 0.88, c * 0.24, c * 0.52, c * 0.30,
                   c * 0.52, c * 0.78, c * 0.88, c * 0.70,
                   c * 0.88, c * 0.24), fill=ink, width=width, joint="curve")
        draw.line((c * 0.50, c * 0.30, c * 0.50, c * 0.78),
                  fill=ink, width=width)
        draw.line((c * 0.70, c * 0.31, c * 0.70, c * 0.47),
                  fill=faint, width=width)
        for y in (0.40, 0.50, 0.60, 0.70):
            draw.line((c * 0.18, c * y, c * 0.40, c * y),
                      fill=faint, width=width)
            draw.line((c * 0.60, c * y, c * 0.82, c * y),
                      fill=faint, width=width)
    else:  # grid
        for x in (0.24, 0.44, 0.64, 0.84):
            draw.line((c * x, c * 0.16, c * x, c * 0.84),
                      fill=faint, width=width)
        for y in (0.24, 0.44, 0.64, 0.84):
            draw.line((c * 0.16, c * y, c * 0.84, c * y),
                      fill=faint, width=width)
        draw.rectangle((c * 0.24, c * 0.24, c * 0.44, c * 0.44),
                       outline=ink, width=width)
        draw.rectangle((c * 0.64, c * 0.64, c * 0.84, c * 0.84),
                       outline=ink, width=width)

    return art.resize((size, size), Image.Resampling.LANCZOS)


def launcher_icon(size, kind, accent):
    """Line icon for a launcher plate; not a font glyph."""
    scale = 4
    canvas = size * scale
    art = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(art)
    ink = accent + (255,)
    detail = mix(accent, HOME_PAPER, 0.58) + (255,)
    width = scale
    c = canvas

    def line(x1, y1, x2, y2):
        draw.line((c * x1, c * y1, c * x2, c * y2), fill=ink, width=width)

    def ellipse(x1, y1, x2, y2):
        draw.ellipse((c * x1, c * y1, c * x2, c * y2),
                     outline=ink, width=width)

    if kind == "lyre":
        line(0.22, 0.18, 0.17, 0.78)
        line(0.78, 0.18, 0.83, 0.78)
        line(0.17, 0.78, 0.83, 0.78)
        draw.line((c * 0.20, c * 0.82, c * 0.80, c * 0.82),
                  fill=detail, width=width)
        line(0.22, 0.28, 0.78, 0.28)
        for x in (0.32, 0.40, 0.48, 0.56, 0.64):
            line(x, 0.30, x, 0.74)
        ellipse(0.18, 0.10, 0.28, 0.20)
        ellipse(0.72, 0.10, 0.82, 0.20)
    elif kind == "mic":
        draw.rounded_rectangle(
            (c * 0.38, c * 0.12, c * 0.62, c * 0.52),
            radius=c * 0.12, outline=ink, width=width)
        draw.arc((c * 0.28, c * 0.26, c * 0.72, c * 0.70), 0, 180,
                 fill=ink, width=width)
        for y in (0.20, 0.27, 0.34):
            draw.line((c * 0.42, c * y, c * 0.58, c * y),
                      fill=detail, width=width)
        line(0.50, 0.66, 0.50, 0.82)
        line(0.36, 0.84, 0.64, 0.84)
    elif kind == "book":
        draw.line((c * 0.12, c * 0.24, c * 0.48, c * 0.30,
                   c * 0.48, c * 0.78, c * 0.12, c * 0.70,
                   c * 0.12, c * 0.24), fill=ink, width=width, joint="curve")
        draw.line((c * 0.88, c * 0.24, c * 0.52, c * 0.30,
                   c * 0.52, c * 0.78, c * 0.88, c * 0.70,
                   c * 0.88, c * 0.24), fill=ink, width=width, joint="curve")
        line(0.50, 0.30, 0.50, 0.78)
        line(0.70, 0.31, 0.70, 0.47)
        draw.line((c * 0.68, c * 0.47, c * 0.72, c * 0.47),
                  fill=ink, width=width)
        for y in (0.40, 0.50, 0.60, 0.70):
            line(0.18, y, 0.40, y)
            line(0.60, y, 0.82, y)
    elif kind == "list":
        draw.line((c * 0.22, c * 0.22, c * 0.22, c * 0.78),
                  fill=detail, width=width)
        for y in (0.28, 0.50, 0.72):
            draw.polygon(
                [(c * 0.14, c * (y - 0.03)), (c * 0.17, c * y),
                 (c * 0.14, c * (y + 0.03)), (c * 0.11, c * y)],
                fill=ink)
            line(0.28, y, 0.84, y)
    elif kind == "album":
        draw.rounded_rectangle(
            (c * 0.14, c * 0.14, c * 0.86, c * 0.86),
            radius=c * 0.03, outline=ink, width=width)
        ellipse(0.34, 0.34, 0.66, 0.66)
        ellipse(0.46, 0.46, 0.54, 0.54)
        draw.rectangle(
            (c * 0.47, c * 0.47, c * 0.53, c * 0.53), fill=ink)
    elif kind == "artist":
        ellipse(0.37, 0.12, 0.63, 0.38)
        line(0.18, 0.86, 0.24, 0.66)
        line(0.24, 0.66, 0.38, 0.56)
        line(0.62, 0.56, 0.76, 0.66)
        line(0.76, 0.66, 0.82, 0.86)
        line(0.18, 0.86, 0.82, 0.86)
    elif kind == "pawn":
        ellipse(0.38, 0.12, 0.62, 0.36)
        draw.polygon(
            [(c * 0.42, c * 0.38), (c * 0.58, c * 0.38),
             (c * 0.66, c * 0.66), (c * 0.34, c * 0.66)],
            outline=ink, width=width)
        line(0.42, 0.39, 0.58, 0.39)
        line(0.34, 0.66, 0.26, 0.78)
        line(0.66, 0.66, 0.74, 0.78)
        line(0.26, 0.78, 0.74, 0.78)
        line(0.22, 0.84, 0.78, 0.84)
    else:  # sliders
        for y, knob in ((0.28, 0.36), (0.50, 0.64), (0.72, 0.46)):
            line(0.16, y, 0.84, y)
            draw.line((c * 0.14, c * (y - 0.03), c * 0.14, c * (y + 0.03)),
                      fill=detail, width=width)
            draw.line((c * 0.86, c * (y - 0.03), c * 0.86, c * (y + 0.03)),
                      fill=detail, width=width)
            draw.rectangle(
                (c * (knob - 0.05), c * (y - 0.05),
                 c * (knob + 0.05), c * (y + 0.05)),
                fill=ink)

    return art.resize((size, size), Image.Resampling.LANCZOS)


def search_icon(size=12):
    scale = 4
    canvas = size * scale
    art = Image.new("RGB", (canvas, canvas), HOME_PAPER)
    draw = ImageDraw.Draw(art)
    ink = (52, 50, 44)
    width = scale
    c = canvas
    draw.ellipse((c * 0.12, c * 0.12, c * 0.66, c * 0.66),
                 outline=ink, width=width)
    draw.line((c * 0.58, c * 0.58, c * 0.86, c * 0.86),
              fill=ink, width=width)
    return art.resize((size, size), Image.Resampling.LANCZOS)


def launcher_mockup():
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    system = ui_font(11, 400)
    wordmark = benguiat_font(26, "Medium")
    label_font = benguiat_font(13, "Book")

    draw_tracked(draw, (20, 18), "ALPHA · 6G", system, HOME_MUTED, 0.8)
    draw_right_tracked(draw, (274, 18), "12:34", system, HOME_MUTED)
    battery(draw, 282, 9, 18, 9, 0.7, HOME_INK)
    diamond(draw, 160, 14, 2, HOME_RULE)

    draw.text((160, 44), "HELIKON-OS", font=wordmark, fill=HOME_INK,
              anchor="ms")
    draw.line((40, 56, 146, 56), fill=HOME_RULE, width=2)
    draw.line((174, 56, 280, 56), fill=HOME_RULE, width=2)
    diamond(draw, 40, 56, 2, HOME_RULE)
    diamond(draw, 280, 56, 2, HOME_RULE)
    mountain = mountain_icon()
    image.paste(mountain, (149, 50), mountain)

    tiles = (
        ("Music", "lyre"),
        ("Podcasts", "mic"),
        ("Audiobooks", "book"),
        ("Playlists", "list"),
        ("Games", "pawn"),
        ("Settings", "sliders"),
    )
    for index, (label, kind) in enumerate(tiles):
        column = index % 3
        row = index // 3
        x = 24 + column * 104
        y = 64 + row * 86
        selected = index == 0
        accent = LAUNCHER_ACCENTS[label]
        if selected:
            diamond(draw, x + 32, y + 6, 2, HOME_INK)
        icon = launcher_icon(40, kind, accent)
        bounds = icon.getbbox()
        if bounds is not None:
            icon_width = bounds[2] - bounds[0]
            icon_height = bounds[3] - bounds[1]
            icon_x = round(x + 32 - icon_width / 2 - bounds[0])
            icon_y = round(y + 32 - icon_height / 2 - bounds[1])
            image.paste(icon, (icon_x, icon_y), icon)
        draw.line((x + 20, y + 56, x + 44, y + 56),
                  fill=mix(accent, HOME_PAPER, 0.50))
        require_width(label, label_font, 0, 78, f"launcher label {label}")
        draw.text((x + 32, y + 78), label, font=label_font, fill=HOME_INK,
                  anchor="ms")
        if selected:
            label_width = label_font.getlength(label)
            draw.line((x + 32 - label_width / 2 - 5, y + 84,
                       x + 32 + label_width / 2 + 5, y + 84),
                      fill=HOME_INK, width=2)

    return save_pair(image, LAUNCHER, LAUNCHER_PREVIEW)


def index_mockup():
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    system = ui_font(11, 400)
    wordmark = benguiat_font(22, "Medium")
    section = benguiat_font(20, "Book")
    numeral = benguiat_font(12, "Book")

    draw.text((160, 30), "HELIKON-OS", font=wordmark, fill=HOME_INK,
              anchor="ms")
    draw_right_tracked(draw, (274, 28), "12:34", system, HOME_MUTED)
    battery(draw, 282, 19, 18, 9, 0.7, HOME_INK)
    draw.line((20, 40, 300, 40), fill=HOME_RULE)
    draw.rectangle((155, 37, 165, 43), fill=HOME_PAPER)
    diamond(draw, 160, 40, 3, HOME_INK)

    rows = (
        ("I", "LISTEN", "Music · Podcasts · Audiobooks", "1,284"),
        ("II", "BROWSE", "Artists · Albums · Genres", "86"),
        ("III", "PLAY", "Playlists · Games", "9"),
        ("IV", "SETTINGS", "Display · System · About", "—"),
    )
    for index, (number, name, description, count) in enumerate(rows):
        top = 50 + index * 40
        selected = index == 0
        draw.line((20, top, 300, top), fill=HOME_RULE)
        if selected:
            tab_right = 56 + section.getlength(name) + 8
            draw.rectangle((20, top + 2, tab_right, top + 24), fill=HOME_INK)
        draw.text((38, top + 20), number, font=numeral,
                  fill=HOME_PAPER if selected else HOME_MUTED, anchor="rs")
        draw.text((56, top + 20), name, font=section,
                  fill=HOME_PAPER if selected else HOME_INK, anchor="ls")
        draw.text((56, top + 34), description, font=system,
                  fill=HOME_INK if selected else HOME_MUTED, anchor="ls")
        draw_right_tracked(draw, (296, top + 20), count, system,
                           HOME_INK if selected else HOME_MUTED)

    draw.line((20, 210, 300, 210), fill=HOME_RULE)
    hourglass(draw, 155, 218, 10, 16, HOME_MUTED)
    return save_pair(image, INDEX, INDEX_PREVIEW)


def album_wall_mockup():
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    system = ui_font(11, 400)
    title = benguiat_font(18, "Book")

    draw.text((29, 26), "ALBUMS", font=title, fill=HOME_INK, anchor="ls")
    draw_right_tracked(draw, (270, 25), "86", system, HOME_MUTED)
    image.paste(search_icon(12), (280, 8))
    draw.line((29, 36, 291, 36), fill=HOME_RULE)

    kinds = ("mountain", "record", "columns", "hourglass",
             "wave", "arch", "sun", "grid", "laurel", "obelisk",
             "moon", "compass")
    for index in range(12):
        column = index % 4
        row = index // 4
        x = 29 + column * 70
        y = 44 + row * 60
        selected = index == 0
        draw.rectangle((x, y, x + 51, y + 51), fill=HOME_PAPER,
                       outline=HOME_INK if selected else HOME_RULE,
                       width=2 if selected else 1)
        image.paste(cover_art(46, kinds[index % len(kinds)]), (x + 3, y + 3))

    draw.text((29, 232), "Nocturnes · Helikon Trio", font=system,
              fill=HOME_INK, anchor="ls")
    draw_right_tracked(draw, (291, 232), "1 / 86", system, HOME_MUTED)
    return save_pair(image, ALBUM_WALL, ALBUM_WALL_PREVIEW)


def content_wall_mockup(title, count, caption, position, kinds, new_badges,
                        path, preview):
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    system = ui_font(11, 400)
    title_font = benguiat_font(18, "Book")

    draw.text((29, 26), title, font=title_font, fill=HOME_INK, anchor="ls")
    draw_right_tracked(draw, (270, 25), count, system, HOME_MUTED)
    image.paste(search_icon(12), (280, 8))
    draw.line((29, 36, 291, 36), fill=HOME_RULE)

    for index in range(12):
        column = index % 4
        row = index // 4
        x = 29 + column * 70
        y = 44 + row * 60
        selected = index == 0
        draw.rectangle((x, y, x + 51, y + 51), fill=HOME_PAPER,
                       outline=HOME_INK if selected else HOME_RULE,
                       width=2 if selected else 1)
        image.paste(cover_art(46, kinds[index]), (x + 3, y + 3))
        if index in new_badges:
            draw.rectangle((x + 42, y + 2, x + 49, y + 9), fill=HOME_PAPER)
            draw.ellipse((x + 45, y + 4, x + 48, y + 7), fill=HOME_GREEN)

    draw.text((29, 232), caption, font=system, fill=HOME_INK, anchor="ls")
    draw_right_tracked(draw, (291, 232), position, system, HOME_MUTED)
    return save_pair(image, path, preview)


def podcast_wall_mockup():
    kinds = ("mic", "wave", "sun", "arch", "laurel", "compass",
             "hourglass", "grid", "record", "obelisk", "mountain", "columns")
    return content_wall_mockup(
        "PODCASTS", "18", "The Long Now · Ep. 42 · 3 new", "1 / 18",
        kinds, {0, 2, 5}, PODCAST_WALL, PODCAST_WALL_PREVIEW)


def audiobook_wall_mockup():
    kinds = ("book", "columns", "laurel", "obelisk", "arch", "mountain",
             "sun", "hourglass", "grid", "compass", "record", "wave")
    return content_wall_mockup(
        "AUDIOBOOKS", "48", "The Long Now · M. Voss · 12h 40m · 34%",
        "1 / 48", kinds, set(), AUDIOBOOK_WALL, AUDIOBOOK_WALL_PREVIEW)


def diamond_outline(draw, cx, cy, radius, colour):
    draw.polygon(
        [(cx, cy - radius), (cx + radius, cy),
         (cx, cy + radius), (cx - radius, cy)],
        outline=colour,
    )


def album_sleeve(size=96):
    """Generated album sleeve; terracotta field, cream engraving."""
    scale = 4
    canvas = size * scale
    art = Image.new("RGB", (canvas, canvas), (156, 63, 44))
    draw = ImageDraw.Draw(art)
    ink = (245, 243, 235)
    faint = (226, 209, 199)
    width = scale
    c = canvas

    draw.ellipse((c * 0.62, c * 0.14, c * 0.80, c * 0.32),
                 outline=ink, width=width)
    draw.line((0, c * 0.62, c * 0.26, c * 0.32,
               c * 0.50, c * 0.62), fill=ink, width=width, joint="curve")
    draw.line((c * 0.30, c * 0.62, c * 0.56, c * 0.40,
               c * 0.88, c * 0.62), fill=faint, width=width, joint="curve")
    draw.line((0, c * 0.64, c, c * 0.64), fill=ink, width=width)
    for line in range(1, 6):
        yy = c * 0.64 + line * c * 0.06
        draw.line((c * 0.06, yy, c * 0.94, yy), fill=faint, width=width)
    draw.line((c * 0.08, c * 0.08, c * 0.92, c * 0.08),
              fill=faint, width=width)
    draw.line((c * 0.08, c * 0.90, c * 0.92, c * 0.90),
              fill=faint, width=width)
    return art.resize((size, size), Image.Resampling.LANCZOS)


def artist_hero(width=320, height=70):
    """Generated plum portrait band with a cream cameo bust."""
    scale = 2
    canvas_w = width * scale
    canvas_h = height * scale
    art = Image.new("RGB", (canvas_w, canvas_h), (100, 54, 76))
    draw = ImageDraw.Draw(art)
    cream = (239, 237, 229)
    faint = (190, 158, 171)
    dark = (48, 34, 44)
    width_px = scale * 2

    oval = (canvas_w * 0.76, canvas_h * 0.10,
            canvas_w * 0.96, canvas_h * 0.90)
    draw.ellipse(oval, fill=cream, outline=faint, width=width_px)
    # Generic classical profile; not a likeness.
    ox = canvas_w * 0.76
    oy = canvas_h * 0.10
    ow = canvas_w * 0.20
    oh = canvas_h * 0.80
    profile = (
        (0.58, 0.10), (0.44, 0.14), (0.40, 0.22), (0.36, 0.28),
        (0.28, 0.36), (0.38, 0.40), (0.35, 0.45), (0.38, 0.50),
        (0.46, 0.56), (0.42, 0.62), (0.28, 0.72), (0.18, 0.92),
        (0.88, 0.92), (0.78, 0.68), (0.80, 0.44), (0.74, 0.18),
        (0.68, 0.10),
    )
    draw.polygon([(ox + px * ow, oy + py * oh) for px, py in profile],
                 fill=dark)
    draw.arc((ox + ow * 0.06, oy + oh * 0.12,
              ox + ow * 0.90, oy + oh * 0.60),
             200, 340, fill=faint, width=scale)
    return art.resize((width, height), Image.Resampling.LANCZOS)


def play_button(draw, x, y, width=64, height=20, label="PLAY"):
    draw.rectangle((x, y, x + width - 1, y + height - 1), fill=HOME_INK)
    draw.polygon(
        [(x + 8, y + 6), (x + 8, y + 14), (x + 14, y + 10)],
        fill=HOME_PAPER)
    draw_tracked(draw, (x + 19, y + 14), label, ui_font(11, 500),
                 HOME_PAPER, 1.0)


def album_view_mockup():
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    system = ui_font(11, 400)
    meta = ui_font(12, 400)
    title = benguiat_font(22, "Book")

    draw_tracked(draw, (20, 20), "ALBUM", system, HOME_MUTED, 1.2)
    draw_right_tracked(draw, (274, 20), "12:34", system, HOME_MUTED)
    battery(draw, 282, 11, 18, 9, 0.7, HOME_INK)
    draw.line((20, 30, 300, 30), fill=HOME_RULE)

    image.paste(album_sleeve(92), (20, 38))
    draw.rectangle((20, 38, 111, 129), outline=HOME_INK)

    draw.text((124, 46), "1975", font=system, fill=HOME_MUTED, anchor="ls")
    for index in range(5):
        cx = 276 + index * 6
        if index < 4:
            diamond(draw, cx, 42, 2, HOME_INK)
        else:
            diamond_outline(draw, cx, 42, 2, HOME_RULE)

    draw.text((124, 68), "Nocturnes", font=title, fill=HOME_INK,
              anchor="ls")
    draw.text((124, 88), "Meridian Ensemble", font=meta, fill=HOME_INK,
              anchor="ls")
    draw.text((124, 104), "CHAMBER · 43 MIN", font=system, fill=HOME_MUTED,
              anchor="ls")
    play_button(draw, 124, 112)

    draw_tracked(draw, (20, 150), "TRACKS", ui_font(11, 500), HOME_INK, 0.8)
    draw_tracked(draw, (78, 150), "NOTES", system, HOME_MUTED, 0.8)
    draw_tracked(draw, (130, 150), "CREDITS", system, HOME_MUTED, 0.8)
    draw_right_tracked(draw, (300, 150), "10 TRACKS", system, HOME_MUTED)
    draw.line((20, 156, 300, 156), fill=HOME_FAINT)
    draw.line((20, 157, 62, 157), fill=HOME_INK, width=2)

    tracks = (
        ("01", "Nocturne I", "3:42"),
        ("02", "Nocturne II", "4:18"),
    )
    for index, (number, name, duration) in enumerate(tracks):
        top = 162 + index * 26
        selected = index == 0
        if selected:
            draw.rectangle((20, top, 21, top + 19), fill=HOME_INK)
        draw.text((38, top + 14), number, font=system,
                  fill=HOME_INK if selected else HOME_MUTED, anchor="rs")
        draw.text((48, top + 14), name,
                  font=ui_font(14, 500 if selected else 400),
                  fill=HOME_INK, anchor="ls")
        draw_right_tracked(draw, (300, top + 14), duration, system,
                           HOME_INK if selected else HOME_MUTED)
        if index == 0:
            draw.line((20, top + 22, 300, top + 22), fill=HOME_FAINT)

    return save_pair(image, ALBUM_VIEW, ALBUM_VIEW_PREVIEW)


def artist_view_mockup():
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    system = ui_font(11, 400)
    title = benguiat_font(22, "Book")

    draw_tracked(draw, (16, 18), "ARTIST", system, HOME_MUTED, 1.2)
    draw_right_tracked(draw, (280, 18), "12:34", system, HOME_MUTED)
    battery(draw, 288, 9, 18, 9, 0.7, HOME_INK)
    draw.line((16, 26, 304, 26), fill=HOME_RULE)

    image.paste(artist_hero(height=68), (0, 26))

    draw.text((16, 124), "Meridian Ensemble", font=title, fill=HOME_INK,
              anchor="ls")
    draw.text((16, 142), "CHAMBER · CONTEMPORARY · FORMED 1975",
              font=system, fill=HOME_MUTED, anchor="ls")
    play_button(draw, 244, 108, width=60)

    draw_tracked(draw, (16, 164), "OVERVIEW", ui_font(11, 500), HOME_INK, 0.8)
    draw_tracked(draw, (92, 164), "ALBUMS", system, HOME_MUTED, 0.8)
    draw_tracked(draw, (150, 164), "TRACKS", system, HOME_MUTED, 0.8)
    draw_right_tracked(draw, (304, 164), "4 ALBUMS", system, HOME_MUTED)
    draw.line((16, 169, 304, 169), fill=HOME_FAINT)
    draw.line((16, 170, 72, 170), fill=HOME_INK, width=2)

    bio = (
        "A chamber ensemble known for slow, engraved readings",
        "of twentieth-century works.",
    )
    for index, line in enumerate(bio):
        draw.text((16, 188 + index * 14), line, font=system,
                  fill=HOME_INK, anchor="ls")
    draw.text((16, 220), "FORMED 1975 · MINNEAPOLIS · 4 ALBUMS",
              font=system, fill=HOME_MUTED, anchor="ls")

    return save_pair(image, ARTIST_VIEW, ARTIST_VIEW_PREVIEW)


if __name__ == "__main__":
    ui = menu_mockup()
    proof = proof_mockup()
    launcher = launcher_mockup()
    index = index_mockup()
    wall = album_wall_mockup()
    podcasts = podcast_wall_mockup()
    audiobooks = audiobook_wall_mockup()
    album = album_view_mockup()
    artist = artist_view_mockup()
    for path in (ui, proof, *launcher, *index, *wall, *podcasts, *audiobooks,
                 *album, *artist):
        print(path)
