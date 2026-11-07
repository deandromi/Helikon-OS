#!/usr/bin/env python3
"""Render the 320x240 host UI mockups. Host artifacts only.

The home mockup uses the owner-supplied ITC Benguiat OTFs for its wordmark and
section header. Set BENGUIAT_DIR to that folder; the fonts are not committed.
"""
import math
import os
import sys
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


CLIP_REPORT = []


def clip_entry(label, detail):
    CLIP_REPORT.append(f"{label}: {detail}")


def require_fit(draw, text, font, x, y, width, label, anchor=None, report=False):
    box = draw.textbbox((x, y), text, font=font, anchor=anchor)
    if box[0] < 0 or box[1] < 0 or box[2] > WIDTH or box[3] > HEIGHT:
        raise SystemExit(f"{label}: text outside canvas: {box}")
    if box[2] - x > width:
        raise SystemExit(f"{label}: text clips its {width}px column: {box}")
    if report:
        clip_entry(label, f"box={box} fits {width}px")
    return box


def require_rect(x, y, width, height, label, report=True):
    if x < 0 or y < 0 or x + width > WIDTH or y + height > HEIGHT:
        raise SystemExit(f"{label}: rect outside canvas: {(x, y, width, height)}")
    if report:
        clip_entry(label, f"rect={(x, y, width, height)} inside 320x240")


def require_points(points, label, report=True):
    for px, py in points:
        if px < 0 or py < 0 or px > WIDTH or py > HEIGHT:
            raise SystemExit(f"{label}: point outside canvas: {(px, py)}")
    if report:
        clip_entry(label, f"{len(points)} points inside 320x240")


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
HOME_ERROR = (254, 49, 49)

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
NOW_PLAYING = OUT / "now-playing"
NOW_PLAYING_STATES = (
    "playing",
    "paused",
    "opening",
    "error-retry",
    "error-terminal",
    "finished",
    "missing-art",
    "long-metadata",
)


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


def text_width(draw, text, font):
    return draw.textlength(text, font=font)


def fit_line(draw, text, font, limit, label, report=True):
    """Return one line that fits, with a visible ellipsis when cut."""
    text = " ".join(text.split())
    if text_width(draw, text, font) <= limit:
        if report:
            clip_entry(label, f"fits {limit}px ({text_width(draw, text, font):.1f}px)")
        return text, False
    ellipsis = "\u2026"
    words = text.split(" ")
    candidate = ""
    while words:
        trial = " ".join(words).strip()
        if trial and text_width(draw, trial + ellipsis, font) <= limit:
            candidate = trial
            break
        words.pop()
    if not candidate:
        candidate = text
        while candidate and text_width(draw, candidate + ellipsis, font) > limit:
            candidate = candidate[:-1]
        candidate = candidate.rstrip()
    result = (candidate + ellipsis) if candidate else ellipsis
    if text_width(draw, result, font) > limit:
        raise SystemExit(f"{label}: ellipsis still clips {limit}px")
    if report:
        clip_entry(
            label,
            f"truncated with U+2026 to {text_width(draw, result, font):.1f}px",
        )
    return result, True


def split_word(draw, word, font, limit):
    head = word
    while head and text_width(draw, head, font) > limit:
        head = head[:-1]
    if not head:
        head = word[:1]
    return head, word[len(head):]


def wrap_title(draw, text, font, limit, max_lines, label):
    """Word-wrap to at most max_lines; the last line gets U+2026 if cut."""
    text = " ".join(text.split())
    words = text.split(" ")
    lines = []
    cursor = 0
    while cursor < len(words) and len(lines) < max_lines:
        if len(lines) == max_lines - 1:
            line, _ = fit_line(draw, " ".join(words[cursor:]), font, limit,
                               f"{label} line {len(lines) + 1}")
            lines.append(line)
            cursor = len(words)
            break
        line = words[cursor]
        cursor += 1
        if text_width(draw, line, font) > limit:
            line, tail = split_word(draw, line, font, limit)
            if tail:
                words.insert(cursor, tail)
        while cursor < len(words):
            trial = f"{line} {words[cursor]}"
            if text_width(draw, trial, font) <= limit:
                line = trial
                cursor += 1
            else:
                break
        lines.append(line)
    if cursor < len(words) and lines:
        line, _ = fit_line(draw, " ".join(words[cursor:]), font, limit,
                           f"{label} line {len(lines)}")
        lines[-1] = line
    if not lines:
        lines = [text]
    for index, line in enumerate(lines):
        if text_width(draw, line, font) > limit:
            raise SystemExit(f"{label} line {index + 1}: clips {limit}px")
    return lines


def sleeve_placeholder(size=96):
    """Generated no-artwork mark: quiet geometry, never a broken-image icon."""
    scale = 4
    canvas = size * scale
    art = Image.new("RGB", (canvas, canvas), HOME_PAPER_LIGHT)
    draw = ImageDraw.Draw(art)
    ink = HOME_MUTED
    width = scale
    c = canvas

    draw.ellipse((c * 0.64, c * 0.16, c * 0.80, c * 0.32),
                 outline=ink, width=width)
    draw.line((c * 0.08, c * 0.62, c * 0.34, c * 0.34,
               c * 0.58, c * 0.62), fill=ink, width=width, joint="curve")
    draw.line((0, c * 0.64, c, c * 0.64), fill=ink, width=width)
    return art.resize((size, size), Image.Resampling.LANCZOS)


def format_time(milliseconds):
    seconds = max(0, int(milliseconds) // 1000)
    hours, remainder = divmod(seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{seconds:02d}"
    return f"{minutes}:{seconds:02d}"


def now_playing_context(track, total, disc, disc_total):
    parts = []
    if disc and disc_total and disc_total > 1:
        parts.append(f"DISC {disc} OF {disc_total}")
    if track and total:
        parts.append(f"TRACK {track} OF {total}")
    elif track:
        parts.append(f"TRACK {track}")
    return " \u00b7 ".join(parts)


def draw_now_footer(draw, actions, arrows, label):
    font = ui_font(11, 400)
    text = "  ".join(actions)
    start = 40 if arrows else 20
    require_fit(draw, text, font, start, 230, 300 - start, f"{label} text",
                anchor="ls", report=True)
    if arrows:
        left = [(25, 222), (25, 229), (20, 226)]
        right = [(30, 222), (30, 229), (35, 226)]
        require_points(left + right, f"{label} arrows")
        draw.polygon(left, fill=HOME_MUTED)
        draw.polygon(right, fill=HOME_MUTED)
    draw.text((start, 230), text, font=font, fill=HOME_MUTED, anchor="ls")
    return text_width(draw, text, font) + (start - 20)


def draw_top_bar(draw, label="NOW PLAYING"):
    system = ui_font(11, 400)
    require_width(label, system, 1.2, 150, "top label width")
    label_width = tracked_width(label, system, 1.2)
    draw_tracked(draw, (20, 20), label, system, HOME_MUTED, 1.2)
    require_width("12:34", system, 0.0, 60, "top time width")
    time_width = tracked_width("12:34", system, 0.0)
    if 20 + label_width > 274 - time_width:
        raise SystemExit("top bar: label and time collide")
    clip_entry("top label", f"{label_width:.1f}px, clear of time")
    clip_entry("top time", f"{time_width:.1f}px, right edge 274")
    draw_right_tracked(draw, (274, 20), "12:34", system, HOME_MUTED)
    require_rect(282, 11, 18, 9, "battery glyph")
    battery(draw, 282, 11, 18, 9, 0.7, HOME_INK)
    require_rect(20, 30, 280, 1, "top rule")
    draw.line((20, 30, 300, 30), fill=HOME_RULE)


def draw_now_metadata(draw, spec):
    title_font = ui_font(17, 600)
    meta_font = ui_font(12, 400)
    system = ui_font(11, 400)

    lines = wrap_title(draw, spec["title"], title_font, 168, 3, "title")
    for index, line in enumerate(lines):
        baseline = 58 + index * 20
        require_fit(draw, line, title_font, 132, baseline, 168,
                    f"title line {index + 1}", anchor="ls", report=True)
        draw.text((132, baseline), line, font=title_font, fill=HOME_INK,
                  anchor="ls")

    artist_baseline = 58 + len(lines) * 20 + 6
    artist, _ = fit_line(draw, spec["artist"], meta_font, 168, "artist")
    require_fit(draw, artist, meta_font, 132, artist_baseline, 168, "artist",
                anchor="ls", report=True)
    draw.text((132, artist_baseline), artist, font=meta_font, fill=HOME_INK,
              anchor="ls")

    album_baseline = artist_baseline + 16
    album, _ = fit_line(draw, spec["album"], meta_font, 168, "album")
    require_fit(draw, album, meta_font, 132, album_baseline, 168, "album",
                anchor="ls", report=True)
    draw.text((132, album_baseline), album, font=meta_font, fill=HOME_MUTED,
              anchor="ls")

    context = now_playing_context(spec.get("track"), spec.get("total"),
                                  spec.get("disc"), spec.get("disc_total"))
    if context:
        require_fit(draw, context, system, 20, 208, 280, "track context",
                    anchor="ls", report=True)
        draw.text((20, 208), context, font=system, fill=HOME_MUTED,
                  anchor="ls")
    else:
        clip_entry("track context", "omitted: no track or disc facts")


def draw_progress(draw, position, duration):
    require_rect(20, 152, 280, 4, "progress track")
    draw.rectangle((20, 152, 299, 155), fill=HOME_RULE)
    if duration > 0 and position > 0:
        width = max(1, round(280 * min(position, duration) / duration))
        require_rect(20, 152, width, 4, "progress fill")
        draw.rectangle((20, 152, 19 + width, 155), fill=HOME_INK)
    else:
        clip_entry("progress fill", "empty at zero or unknown duration")

    system = ui_font(11, 400)
    elapsed = format_time(position)
    total = format_time(duration) if duration > 0 else "--:--"
    combined = f"{elapsed} / {total}"
    require_fit(draw, combined, system, 20, 170, 150, "elapsed / total",
                anchor="ls", report=True)
    draw.text((20, 170), elapsed, font=system, fill=HOME_INK, anchor="ls")
    split = 20 + text_width(draw, elapsed, system)
    draw.text((split, 170), f" / {total}", font=system, fill=HOME_MUTED,
              anchor="ls")


def draw_state(draw, state):
    label = {
        "playing": "PLAYING",
        "paused": "PAUSED",
        "opening": "OPENING",
        "finished": "FINISHED",
    }[state]
    font = ui_font(14, 500)
    x = 20
    if state == "playing":
        points = [(20, 181), (20, 191), (28, 186)]
        require_points(points, "playing icon")
        draw.polygon(points, fill=HOME_INK)
        x = 36
    elif state == "paused":
        require_rect(20, 181, 3, 10, "paused icon left")
        require_rect(26, 181, 3, 10, "paused icon right")
        draw.rectangle((20, 181, 22, 190), fill=HOME_INK)
        draw.rectangle((26, 181, 28, 190), fill=HOME_INK)
        x = 36
    elif state == "finished":
        require_rect(21, 182, 8, 8, "finished icon")
        draw.rectangle((21, 182, 28, 189), fill=HOME_INK)
        x = 36
    require_fit(draw, label, font, x, 190, 200, "state label", anchor="ls",
                report=True)
    draw.text((x, 190), label, font=font, fill=HOME_INK, anchor="ls")


def draw_error(draw, spec):
    system = ui_font(11, 400)
    meta = ui_font(12, 400)
    headline = ui_font(17, 600)

    reason = spec["reason"]
    if len(reason) > 44:
        raise SystemExit(f"error reason: {len(reason)} chars exceeds 44")
    clip_entry("error reason length", f"{len(reason)} chars <= 44")

    require_rect(20, 44, 280, 2, "error status rule")
    draw.rectangle((20, 44, 299, 45), fill=HOME_ERROR)

    title = "AUDIO PLAYBACK FAILED"
    require_fit(draw, title, headline, 20, 70, 280, "error headline",
                anchor="ls", report=True)
    draw.text((20, 70), title, font=headline, fill=HOME_INK, anchor="ls")

    reason, _ = fit_line(draw, reason, meta, 280, "error reason")
    require_fit(draw, reason, meta, 20, 94, 280, "error reason",
                anchor="ls", report=True)
    draw.text((20, 94), reason, font=meta, fill=HOME_INK, anchor="ls")

    guidance, _ = fit_line(draw, spec["guidance"], meta, 280, "error guidance")
    require_fit(draw, guidance, meta, 20, 116, 280, "error guidance",
                anchor="ls", report=True)
    draw.text((20, 116), guidance, font=meta, fill=HOME_MUTED, anchor="ls")

    identity, _ = fit_line(draw, spec["identity"], system, 280,
                           "error track identity")
    require_fit(draw, identity, system, 20, 142, 280, "error track identity",
                anchor="ls", report=True)
    draw.text((20, 142), identity, font=system, fill=HOME_MUTED, anchor="ls")


def now_playing_mockup(name, spec):
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)

    draw_top_bar(draw)

    if spec["state"].startswith("error"):
        draw_error(draw, spec)
        actions = (["SELECT ALBUM", "MENU BACK"] if spec["terminal"]
                   else ["SELECT RETRY", "MENU BACK"])
        draw_now_footer(draw, actions, False, f"{name} footer")
    else:
        require_rect(20, 40, 96, 96, "sleeve frame")
        if spec.get("art", True):
            image.paste(album_sleeve(96), (20, 40))
            frame = HOME_INK
        else:
            image.paste(sleeve_placeholder(96), (20, 40))
            frame = HOME_RULE
        draw.rectangle((20, 40, 115, 135), outline=frame)

        draw_now_metadata(draw, spec)

        if spec["state"] == "opening":
            draw_progress(draw, 0, spec["duration"])
        else:
            draw_progress(draw, spec["position"], spec["duration"])
        draw_state(draw, spec["state"])

        if spec["state"] == "opening":
            actions = ["MENU CANCEL"]
        elif spec["state"] == "finished":
            actions = ["SELECT ALBUM", "PLAY AGAIN", "MENU BACK"]
        else:
            actions = ["TRACK", "SELECT ALBUM",
                       "PLAY PAUSE" if spec["state"] == "playing"
                       else "PLAY RESUME", "MENU BACK"]
        draw_now_footer(draw, actions,
                        spec["state"] in ("playing", "paused"),
                        f"{name} footer")

    require_rect(20, 216, 280, 1, "footer rule")
    draw.line((20, 216, 300, 216), fill=HOME_FAINT)

    path = NOW_PLAYING / f"helikon-os-now-playing-{name}-320x240.png"
    preview = NOW_PLAYING / f"helikon-os-now-playing-{name}-320x240@2x.png"
    return save_pair(image, path, preview)


def now_playing_mockups():
    base = {
        "title": "Nocturne I",
        "artist": "Meridian Ensemble",
        "album": "Nocturnes",
        "track": 1,
        "total": 10,
        "disc": 1,
        "disc_total": 1,
        "duration": 238000,
    }
    states = {
        "playing": dict(base, state="playing", position=102000),
        "paused": dict(base, state="paused", position=102000),
        "opening": dict(base, state="opening", position=0),
        "finished": dict(base, state="finished", position=238000,
                         track=10),
        "missing-art": dict(base, state="playing", position=37000,
                            title="Nocturne II", track=2, total=5,
                            disc=1, disc_total=2, art=False),
        "long-metadata": dict(
            base, state="playing", position=126000, duration=301000,
            track=7, total=12,
            title="The Long Night of the Meridian Ensemble, Part One: "
                  "A Winter Sequence",
            artist="The Meridian Chamber Ensemble of the Upper Midwest",
            album="Collected Nocturnes for a Long Winter",
        ),
        "error-retry": dict(
            base, state="error-retry", reason="THE AUDIO DEVICE DID NOT START",
            guidance="Retry, or return and choose another track.",
            identity="Nocturne I \u2014 Meridian Ensemble", terminal=False,
        ),
        "error-terminal": dict(
            base, state="error-terminal", reason="UNSUPPORTED WAV FORMAT",
            guidance="Choose another track, or return to the library.",
            identity="Nocturne IV \u2014 Meridian Ensemble", terminal=True,
        ),
    }
    paths = []
    for name in NOW_PLAYING_STATES:
        paths.extend(now_playing_mockup(name, states[name]))

    # Width checks for footers that do not get their own PNG.
    scratch = ImageDraw.Draw(Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER))
    variants = (
        (["TRACK", "SELECT ALBUM", "PLAY PAUSE", "MENU BACK"], True,
         "footer variant playing+prev/next"),
        (["TRACK", "SELECT ALBUM", "PLAY RESUME", "MENU BACK"], True,
         "footer variant paused+prev/next"),
        (["SELECT ALBUM", "PLAY PAUSE", "MENU BACK"], False,
         "footer variant single-track"),
        (["PLAY PAUSE", "MENU BACK"], False,
         "footer variant single-track no album"),
        (["TRACK", "PLAY PAUSE", "MENU BACK"], True,
         "footer variant playing no album"),
        (["TRACK", "PLAY RESUME", "MENU BACK"], True,
         "footer variant paused no album"),
        (["MENU CANCEL"], False, "footer variant opening"),
        (["SELECT ALBUM", "PLAY AGAIN", "MENU BACK"], False,
         "footer variant finished"),
        (["PLAY AGAIN", "MENU BACK"], False,
         "footer variant finished no album"),
        (["SELECT RETRY", "MENU BACK"], False, "footer variant error retry"),
        (["SELECT ALBUM", "MENU BACK"], False,
         "footer variant error terminal"),
        (["MENU BACK"], False, "footer variant error terminal no album"),
    )
    for actions, arrows, label in variants:
        draw_now_footer(scratch, actions, arrows, label)

    context_font = ui_font(11, 400)
    context_variants = (
        ("DISC 1 OF 2 \u00b7 TRACK 3 OF 10", "context variant multi-disc"),
        ("DISC 2 OF 2 \u00b7 TRACK 10 OF 10", "context variant last disc"),
        ("TRACK 3", "context variant track only"),
        ("DISC 1 OF 2", "context variant disc only"),
    )
    for text, label in context_variants:
        require_fit(scratch, text, context_font, 20, 208, 280, label,
                    anchor="ls", report=True)
    return paths


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
    now_playing_only = "--now-playing-only" in sys.argv[1:]
    if now_playing_only:
        now_playing = now_playing_mockups()
        for path in now_playing:
            print(path.relative_to(ROOT))
        print(f"CLIP CHECK: {len(CLIP_REPORT)} Now Playing text/geometry checks")
        for line in CLIP_REPORT:
            print(f"  PASS  {line}")
        print("CLIP SUMMARY: all checks passed")
        raise SystemExit(0)

    ui = menu_mockup()
    proof = proof_mockup()
    launcher = launcher_mockup()
    index = index_mockup()
    wall = album_wall_mockup()
    podcasts = podcast_wall_mockup()
    audiobooks = audiobook_wall_mockup()
    album = album_view_mockup()
    artist = artist_view_mockup()
    now_playing = now_playing_mockups()
    for path in (ui, proof, *launcher, *index, *wall, *podcasts, *audiobooks,
                 *album, *artist, *now_playing):
        print(path.relative_to(ROOT))
    print(f"CLIP CHECK: {len(CLIP_REPORT)} Now Playing text/geometry checks")
    for line in CLIP_REPORT:
        print(f"  PASS  {line}")
    print("CLIP SUMMARY: all checks passed")
