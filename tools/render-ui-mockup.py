#!/usr/bin/env python3
"""Render the 320x240 host UI mockups. Host artifacts only.

The legacy home/wall mockups use the owner-supplied ITC Benguiat OTFs for their
wordmarks and headers. Set BENGUIAT_DIR to that folder; the fonts are not
committed. The cleaned Music flow and Now Playing are Plex-only and can be
rendered with `--only music-flow`.
"""
import argparse
import hashlib
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


# --- Music-flow geometry and clipping report -----------------------------

CLIP_REPORT = []


def clip_entry(label, detail):
    CLIP_REPORT.append(f"{label}: {detail}")


def require_rect(x, y, width, height, label, report=True):
    if width <= 0 or height <= 0:
        raise SystemExit(f"{label}: empty rect {(x, y, width, height)}")
    if x < 0 or y < 0 or x + width > WIDTH or y + height > HEIGHT:
        raise SystemExit(f"{label}: rect outside canvas: {(x, y, width, height)}")
    if report:
        clip_entry(label, f"rect {(x, y, width, height)} inside 320x240")
    return (x, y, width, height)


def require_points(points, label, report=True):
    if not points:
        raise SystemExit(f"{label}: empty point set")
    for px, py in points:
        if px < 0 or py < 0 or px > WIDTH or py > HEIGHT:
            raise SystemExit(f"{label}: point outside canvas: {(px, py)}")
    if report:
        clip_entry(label, f"{len(points)} points inside 320x240")


def require_text(draw, text, font, x, baseline, region, label, anchor="ls",
                 report=True):
    box = draw.textbbox((x, baseline), text, font=font, anchor=anchor)
    rx, ry, rw, rh = region
    if (box[0] < rx - 0.5 or box[1] < ry - 0.5 or
            box[2] > rx + rw + 0.5 or box[3] > ry + rh + 0.5):
        raise SystemExit(
            f"{label}: text box {tuple(round(v, 1) for v in box)} "
            f"clips region {(rx, ry, rw, rh)} for {text!r}"
        )
    if box[0] < 0 or box[1] < 0 or box[2] > WIDTH or box[3] > HEIGHT:
        raise SystemExit(f"{label}: text outside canvas: {box}")
    if report:
        clip_entry(
            label,
            f"text {tuple(round(v, 1) for v in box)} inside "
            f"{(rx, ry, rw, rh)}",
        )
    return box


def draw_tracked_fit(draw, x, baseline, text, font, tracking, region, label,
                     fill):
    width = tracked_width(text, font, tracking)
    rx, ry, rw, rh = region
    if x < rx or x + width > rx + rw:
        raise SystemExit(f"{label}: width {width:.1f} clips {(rx, ry, rw, rh)}")
    box = draw.textbbox((x, baseline), "Hg", font=font, anchor="ls")
    if box[1] < ry or box[3] > ry + rh:
        raise SystemExit(f"{label}: height {box} clips {(rx, ry, rw, rh)}")
    draw_tracked(draw, (x, baseline), text, font, fill, tracking)
    clip_entry(label, f"tracked width {width:.1f}px inside {(rx, ry, rw, rh)}")


def draw_right_tracked_fit(draw, right, baseline, text, font, tracking, region,
                           label, fill):
    width = tracked_width(text, font, tracking)
    rx, ry, rw, rh = region
    start = right - width
    if start < rx or right > rx + rw:
        raise SystemExit(
            f"{label}: right-aligned width {width:.1f} clips "
            f"{(rx, ry, rw, rh)}"
        )
    box = draw.textbbox((start, baseline), "Hg", font=font, anchor="ls")
    if box[1] < ry or box[3] > ry + rh:
        raise SystemExit(f"{label}: height {box} clips {(rx, ry, rw, rh)}")
    draw_right_tracked(draw, (right, baseline), text, font, fill, tracking)
    clip_entry(
        label,
        f"tracked width {width:.1f}px ending at {right} inside "
        f"{(rx, ry, rw, rh)}",
    )


def text_width(draw, text, font):
    return draw.textlength(text, font=font)


def fit_line(draw, text, font, limit, label, report=True):
    """Return one line that fits, with a visible U+2026 when cut."""
    text = " ".join(text.split())
    if text_width(draw, text, font) <= limit:
        if report:
            clip_entry(
                label,
                f"fits {limit}px ({text_width(draw, text, font):.1f}px)",
            )
        return text
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
    return result


def wrap_lines(draw, text, font, limit, max_lines, label):
    """Word-wrap to at most max_lines; the last line gets U+2026 when cut."""
    text = " ".join(text.split())
    words = text.split(" ")
    lines = []
    cursor = 0
    while cursor < len(words) and len(lines) < max_lines:
        if len(lines) == max_lines - 1:
            lines.append(
                fit_line(
                    draw, " ".join(words[cursor:]), font, limit,
                    f"{label} line {len(lines) + 1}",
                )
            )
            cursor = len(words)
            break
        line = words[cursor]
        cursor += 1
        if text_width(draw, line, font) > limit:
            head = line
            while head and text_width(draw, head, font) > limit:
                head = head[:-1]
            if not head:
                head = line[:1]
            tail = line[len(head):]
            if tail:
                words.insert(cursor, tail)
            line = head
        while cursor < len(words):
            trial = f"{line} {words[cursor]}"
            if text_width(draw, trial, font) <= limit:
                line = trial
                cursor += 1
            else:
                break
        lines.append(line)
    if cursor < len(words) and lines:
        lines[-1] = fit_line(
            draw, " ".join(words[cursor:]), font, limit,
            f"{label} line {len(lines)}",
        )
    if not lines:
        lines = [text]
    for index, line in enumerate(lines):
        if text_width(draw, line, font) > limit:
            raise SystemExit(f"{label} line {index + 1}: clips {limit}px")
    return lines


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

MUSIC_FLOW = OUT / "music-flow"
NOW_PLAYING = OUT / "now-playing"
HOME_ERROR = (254, 49, 49)

MUSIC_FLOW_SCREENS = (
    "artists-list",
    "artists-long",
    "artist-albums",
    "artist-overview",
    "artist-overview-empty",
    "artist-tracks",
    "album-view",
    "album-long",
    "album-empty",
    "album-corrupt",
)

NOW_PLAYING_STATES = (
    "play",
    "pause",
    "open",
    "end",
    "error-retry",
    "error-terminal",
    "no-art",
    "long-text",
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


# --- Cleaned Music flow (Plex only, binding) -----------------------------

def write_clip_report(directory, title, paths):
    font_hash = hashlib.sha256(FONT_UI.read_bytes()).hexdigest()
    lines = [
        f"Helikon-OS {title} — host clipping and geometry check",
        "Date: 2026-11-08",
        "Renderer: tools/render-ui-mockup.py --only music-flow",
        "Canvas: 320x240 RGB",
        f"Plex subset: assets/fonts/HelikonUI-Sans.ttf sha256={font_hash}",
        "Benguiat: not used for this group",
        "",
    ]
    lines.extend(CLIP_REPORT)
    lines.extend([
        "",
        f"Screens rendered: {len(paths)}",
        "Result: PASS — every recorded text box is inside its column and the "
        "320x240 canvas; every frame is inside the canvas.",
        "Limits: host geometry only. No 6G panel, contrast, frame-time or "
        "device claim.",
        "",
        "Files:",
    ])
    lines.extend(str(path.relative_to(ROOT)) for path in paths)
    report = directory / "clipping-check.txt"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(report)
    return report


def render_music_flow():
    CLIP_REPORT.clear()
    (MUSIC_FLOW / "clipping-check.txt").unlink(missing_ok=True)
    paths = []
    for long_names in (False, True):
        paths.extend(draw_artists_list(long_names=long_names))
    for tab in ("albums", "overview", "tracks"):
        paths.extend(draw_artist_view(tab))
    paths.extend(draw_artist_view("overview", empty_overview=True))
    for mode in ("normal", "long", "empty", "corrupt"):
        paths.extend(draw_album_view(mode=mode))
    write_clip_report(MUSIC_FLOW, "Music flow", paths)
    return paths


def render_now_playing():
    CLIP_REPORT.clear()
    (NOW_PLAYING / "clipping-check.txt").unlink(missing_ok=True)
    paths = []
    for state in ("playing", "paused", "opening", "finished",
                  "error-retry", "error-terminal", "no-art", "long-text"):
        paths.extend(draw_now_playing(state))
    write_clip_report(NOW_PLAYING, "Now Playing", paths)
    return paths


def render_legacy():
    paths = []
    paths.extend(menu_mockup())
    paths.extend(proof_mockup())
    paths.extend(launcher_mockup())
    paths.extend(index_mockup())
    paths.extend(album_wall_mockup())
    paths.extend(podcast_wall_mockup())
    paths.extend(audiobook_wall_mockup())
    paths.extend(album_view_mockup())
    paths.extend(artist_view_mockup())
    return paths


def main():
    parser = argparse.ArgumentParser(
        description="Render 320x240 Helikon-OS host mockups."
    )
    parser.add_argument(
        "--only",
        choices=("all", "music-flow", "now-playing", "legacy"),
        default="all",
        help="render one group; 'music-flow' includes Now Playing",
    )
    args = parser.parse_args()

    paths = []
    if args.only in ("all", "music-flow"):
        paths.extend(render_music_flow())
    if args.only in ("all", "music-flow", "now-playing"):
        paths.extend(render_now_playing())
    if args.only in ("all", "legacy"):
        paths.extend(render_legacy())
    for path in paths:
        print(path)
    return 0


# --- Cleaned Music flow (Plex only, binding) -----------------------------

def music_top_bar(draw, label):
    system = ui_font(11, 400)
    draw_tracked_fit(
        draw, 16, 20, label, system, 1.2, (16, 8, 200, 14), f"{label} top",
        HOME_MUTED,
    )
    draw_right_tracked_fit(
        draw, 274, 20, "12:34", system, 0.0, (214, 8, 60, 14),
        f"{label} time", HOME_MUTED,
    )
    require_rect(282, 11, 18, 9, f"{label} battery")
    battery(draw, 282, 11, 18, 9, 0.7, HOME_INK)
    require_rect(16, 30, 288, 1, f"{label} top rule")
    draw.line((16, 30, 303, 30), fill=HOME_RULE)


def put_text(draw, text, font, x, baseline, region, label, fill,
             anchor="ls"):
    require_text(draw, text, font, x, baseline, region, label, anchor=anchor)
    draw.text((x, baseline), text, font=font, fill=fill, anchor=anchor)


def put_fitted(draw, text, font, x, baseline, limit, region, label, fill,
               anchor="ls"):
    line = fit_line(draw, text, font, limit, label)
    put_text(draw, line, font, x, baseline, region, label, fill, anchor=anchor)
    return line


def put_right_fitted(draw, text, font, right, baseline, limit, region, label,
                     fill):
    line = fit_line(draw, text, font, limit, label)
    put_text(draw, line, font, right, baseline, region, label, fill,
             anchor="rs")
    return line


def format_time(milliseconds):
    seconds = max(0, int(milliseconds) // 1000)
    hours, remainder = divmod(seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    if hours:
        return f"{hours}:{minutes:02d}:{seconds:02d}"
    return f"{minutes}:{seconds:02d}"


def monogram_image(size, name):
    scale = 4
    canvas = size * scale
    art = Image.new("RGB", (canvas, canvas), HOME_PAPER_LIGHT)
    draw = ImageDraw.Draw(art)
    draw.rectangle((0, 0, canvas - 1, canvas - 1), outline=HOME_RULE,
                   width=scale)
    initials = "".join(part[0] for part in name.split()[:2]).upper()
    if not initials:
        initials = "?"
    font = ui_font(11 * scale, 500)
    if font.getlength(initials) > canvas - 8 * scale:
        initials = initials[:1]
    draw.text((canvas / 2, canvas / 2), initials, font=font, fill=HOME_INK,
              anchor="mm")
    return art.resize((size, size), Image.Resampling.LANCZOS)


def sleeve_placeholder(size):
    """Generated no-art mark; quiet geometry, never a broken-image icon."""
    scale = 4
    canvas = size * scale
    art = Image.new("RGB", (canvas, canvas), HOME_PAPER_LIGHT)
    draw = ImageDraw.Draw(art)
    width = scale
    c = canvas
    ink = HOME_MUTED
    draw.rectangle((0, 0, c - 1, c - 1), outline=HOME_RULE, width=width)
    draw.ellipse((c * 0.64, c * 0.16, c * 0.80, c * 0.32),
                 outline=ink, width=width)
    draw.line((c * 0.08, c * 0.66, c * 0.34, c * 0.38,
               c * 0.58, c * 0.66), fill=ink, width=width, joint="curve")
    draw.line((c * 0.06, c * 0.68, c * 0.94, c * 0.68),
              fill=ink, width=width)
    return art.resize((size, size), Image.Resampling.LANCZOS)


def artist_portrait(size, name, has_art):
    if not has_art:
        return monogram_image(size, name)
    art = Image.new("RGB", (size, size), HOME_PAPER_LIGHT)
    icon = launcher_icon(size, "artist", HOME_MUTED)
    art.paste(icon, (0, 0), icon)
    return art


def draw_rating(draw, right, baseline, rating):
    filled = 0 if rating <= 0 else max(1, min(5, round(rating / 20)))
    start = right - 34
    for index in range(5):
        cx = start + 3 + index * 7
        points = [
            (cx, baseline - 3), (cx + 3, baseline),
            (cx, baseline + 3), (cx - 3, baseline),
        ]
        require_points(points, f"rating diamond {index + 1}")
        if index < filled:
            draw.polygon(points, fill=HOME_INK)
        else:
            draw.polygon(points, outline=HOME_RULE)


def draw_play_album(draw, x, y, width, height, focused):
    require_rect(x, y, width, height, "PLAY ALBUM plate")
    if focused:
        draw.rectangle((x, y, x + width - 1, y + height - 1), fill=HOME_INK)
        ink = HOME_PAPER
    else:
        draw.rectangle((x, y, x + width - 1, y + height - 1),
                       outline=HOME_INK, width=1)
        ink = HOME_INK
    triangle = [(x + 7, y + 5), (x + 7, y + height - 6),
                (x + 14, y + height // 2)]
    require_points(triangle, "PLAY ALBUM triangle")
    draw.polygon(triangle, fill=ink)
    font = ui_font(11, 500)
    put_text(draw, "PLAY ALL", font, x + 20, y + height - 5,
             (x + 18, y + 2, width - 20, height - 2), "PLAY ALL label", ink)


def draw_artists_list(long_names=False):
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    music_top_bar(draw, "MUSIC")

    title_font = ui_font(17, 600)
    system = ui_font(11, 400)
    put_text(draw, "ARTISTS", title_font, 16, 50, (16, 34, 200, 22),
             "artists title", HOME_INK)
    put_right_fitted(draw, "78 ARTISTS", system, 304, 50, 90,
                     (214, 36, 90, 18), "artists count", HOME_MUTED)
    require_rect(16, 58, 288, 1, "artists title rule")
    draw.line((16, 58, 303, 58), fill=HOME_RULE)

    put_text(draw, "A", ui_font(11, 500), 16, 72, (16, 60, 20, 14),
             "artists section letter", HOME_INK)
    put_right_fitted(draw, "SORT: NAME", system, 304, 72, 100,
                     (204, 60, 100, 14), "artists sort", HOME_MUTED)

    names = (
        ("Aurora Strings", "12 ALBUMS · 148 TRACKS", True),
        ("Bellweather Trio", "4 ALBUMS · 42 TRACKS", True),
        ("Cinder Choir", "NO ALBUMS", False),
        ("Dunmore Ensemble", "7 ALBUMS · 63 TRACKS", True),
    )
    if long_names:
        names = (
            ("The Extraordinarily Long Name Of A Contemporary Chamber Ensemble",
             "NO ALBUMS", False),
            ("A Band With A Very Long Name That Must Be Truncated In The List",
             "COUNTS UNAVAILABLE", False),
            ("Cinder Choir", "NO ALBUMS", False),
            ("Dunmore Ensemble", "7 ALBUMS · 63 TRACKS", True),
        )

    for index, (name, counts, has_art) in enumerate(names):
        y = 78 + index * 36
        selected = index == 0
        if selected:
            require_rect(16, y, 2, 34, f"artist row {index + 1} focus bar")
            draw.rectangle((16, y, 17, y + 33), fill=HOME_INK)
        require_rect(24, y + 3, 28, 28, f"artist row {index + 1} art")
        draw.rectangle((24, y + 3, 51, y + 30),
                       outline=HOME_INK if selected else HOME_RULE, width=1)
        art = artist_portrait(26, name, has_art)
        image.paste(art, (25, y + 4))
        title_font = ui_font(14, 500 if selected else 400)
        put_fitted(draw, name, title_font, 60, y + 14, 244,
                   (60, y + 1, 244, 16), f"artist row {index + 1} name",
                   HOME_INK)
        put_fitted(draw, counts, system, 60, y + 28, 244,
                   (60, y + 15, 244, 14), f"artist row {index + 1} counts",
                   HOME_INK if selected else HOME_MUTED)

    require_rect(16, 226, 288, 1, "artists footer rule")
    draw.line((16, 226, 303, 226), fill=HOME_FAINT)
    put_text(draw, "WHEEL SCROLL", system, 16, 236, (16, 228, 120, 12),
             "artists footer left", HOME_MUTED)
    put_right_fitted(draw, "1–4 / 78", system, 304, 236, 80,
                     (224, 228, 80, 12), "artists footer position",
                     HOME_MUTED)

    name = "artists-long" if long_names else "artists-list"
    return save_pair(image, MUSIC_FLOW / f"helikon-os-{name}-320x240.png",
                     MUSIC_FLOW / f"helikon-os-{name}-320x240@2x.png")


def draw_artist_view(tab, empty_overview=False):
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    music_top_bar(draw, "ARTISTS")

    title_font = ui_font(17, 600)
    meta_font = ui_font(12, 400)
    system = ui_font(11, 400)
    name = "Aurora Strings" if not empty_overview else "Cinder Choir"
    put_fitted(draw, name, title_font, 16, 48, 228, (16, 32, 228, 20),
               "artist name", HOME_INK)
    put_fitted(draw, "CHAMBER · CONTEMPORARY", meta_font, 16, 64, 228,
               (16, 48, 228, 18), "artist genre", HOME_MUTED)
    put_fitted(draw, "4 ALBUMS · 42 TRACKS" if not empty_overview
               else "NO ALBUMS", system, 16, 78, 228,
               (16, 64, 228, 16), "artist counts", HOME_MUTED)

    require_rect(256, 32, 44, 44, "artist portrait")
    draw.rectangle((256, 32, 299, 75), outline=HOME_RULE, width=1)
    portrait = artist_portrait(42, name, not empty_overview)
    image.paste(portrait, (257, 33))

    require_rect(16, 84, 288, 1, "artist identity rule")
    draw.line((16, 84, 303, 84), fill=HOME_RULE)

    labels = (("ALBUMS", 16), ("OVERVIEW", 84), ("TRACKS", 168))
    selected_index = {"albums": 0, "overview": 1, "tracks": 2}[tab]
    for index, (label, x) in enumerate(labels):
        selected = index == selected_index
        draw_tracked_fit(
            draw, x, 98, label, ui_font(11, 500 if selected else 400),
            0.8, (x, 86, 76, 14), f"artist tab {label}",
            HOME_INK if selected else HOME_MUTED,
        )
    put_right_fitted(draw, "4 ALBUMS" if not empty_overview else "0 ALBUMS",
                     system, 304, 98, 90, (214, 86, 90, 14),
                     "artist tab count", HOME_MUTED)
    require_rect(16, 101, 288, 1, "artist tab rule")
    draw.line((16, 101, 303, 101), fill=HOME_FAINT)
    tab_x = labels[selected_index][1]
    tab_width = tracked_width(labels[selected_index][0],
                              ui_font(11, 500), 0.8)
    require_rect(tab_x, 102, round(tab_width), 2, "artist tab underline")
    draw.rectangle((tab_x, 102, tab_x + round(tab_width), 103), fill=HOME_INK)

    if tab == "albums":
        put_right_fitted(draw, "1–3 / 4" if not empty_overview else "0 / 0",
                         system, 304, 112, 80, (224, 104, 80, 12),
                         "artist album position", HOME_MUTED)
        albums = (
            ("Nocturnes", "1975", True, "mountain"),
            ("The Long Now", "1981", True, "record"),
            ("The Extraordinarily Long Album Title", "1994", False, "columns"),
        ) if not empty_overview else ()
        if not albums:
            put_text(draw, "NO ALBUMS IN LIBRARY", system, 16, 128,
                     (16, 108, 288, 16), "artist empty albums", HOME_MUTED)
        for index, (album, year, has_art, kind) in enumerate(albums):
            x = 16 + index * 108
            draw_artist_album_cell(image, draw, x, 118, 72, album, year,
                                   index == 0, has_art, kind)
    elif tab == "overview":
        if empty_overview:
            put_text(draw, "NO BIOGRAPHY AVAILABLE", system, 16, 122,
                     (16, 108, 288, 16), "artist empty bio", HOME_MUTED)
            put_text(draw, "NO CATALOG FACTS", system, 16, 138,
                     (16, 124, 288, 16), "artist empty facts", HOME_MUTED)
        else:
            bio = (
                "A chamber ensemble known for slow, engraved readings of "
                "twentieth-century works, recorded in single rooms."
            )
            bio_font = ui_font(12, 400)
            lines = wrap_lines(draw, bio, bio_font, 288, 4, "artist bio")
            for index, line in enumerate(lines):
                put_text(draw, line, bio_font, 16, 122 + index * 16,
                         (16, 108, 288, 64),
                         f"artist bio line {index + 1}", HOME_INK)
            require_rect(16, 182, 288, 1, "artist overview rule")
            draw.line((16, 182, 303, 182), fill=HOME_FAINT)
            put_text(draw, "4 ALBUMS · 42 TRACKS", system, 16, 198,
                     (16, 184, 288, 16), "artist facts", HOME_INK)
            put_fitted(draw, "FIRST  Nocturnes (1975)", system, 16, 212,
                       288, (16, 198, 288, 18), "artist first album",
                       HOME_MUTED)
            put_fitted(draw, "LATEST  Paper Rooms (1994)", system, 16, 226,
                       288, (16, 212, 288, 18), "artist latest album",
                       HOME_MUTED)
    else:
        put_right_fitted(draw, "1–4 / 24", system, 304, 112, 80,
                         (224, 104, 80, 12), "artist track position",
                         HOME_MUTED)
        tracks = (
            ("Nocturne I", "Nocturnes", "3:42"),
            ("Nocturne II", "Nocturnes", "4:18"),
            ("Paper Rooms", "Paper Rooms", "5:06"),
            ("The Long Now", "The Long Now", "6:12"),
        )
        for index, (track, album, duration) in enumerate(tracks):
            y = 118 + index * 28
            selected = index == 0
            if selected:
                require_rect(16, y, 2, 26, f"artist track {index + 1} bar")
                draw.rectangle((16, y, 17, y + 25), fill=HOME_INK)
            put_fitted(draw, track, ui_font(14, 500 if selected else 400),
                       24, y + 12, 220, (24, y, 220, 16),
                       f"artist track {index + 1} title", HOME_INK)
            put_fitted(draw, album, system, 24, y + 24, 220,
                       (24, y + 12, 220, 14),
                       f"artist track {index + 1} album", HOME_MUTED)
            put_text(draw, duration, system, 304, y + 12,
                     (258, y, 46, 16), f"artist track {index + 1} duration",
                     HOME_MUTED, anchor="rs")

    name = {
        "albums": "artist-albums",
        "overview": "artist-overview-empty" if empty_overview
        else "artist-overview",
        "tracks": "artist-tracks",
    }[tab]
    return save_pair(image, MUSIC_FLOW / f"helikon-os-{name}-320x240.png",
                     MUSIC_FLOW / f"helikon-os-{name}-320x240@2x.png")


def draw_level_meter(draw, x, y, width, height, peaks, active):
    require_rect(x, y, width, height, "visualizer")
    draw.line((x, y + height, x + width - 1, y + height), fill=HOME_FAINT)
    if not active:
        clip_entry("visualizer bars", "inactive: baseline only")
        return
    bars = 12
    bar_width = 4
    gap = 4
    for index in range(bars):
        bx = x + 2 + index * (bar_width + gap)
        if bx + bar_width > x + width:
            raise SystemExit("visualizer: bar clips its box")
        peak = peaks[index % len(peaks)]
        bar_height = max(1, min(height - 1, round(peak * (height - 1))))
        draw.rectangle((bx, y + height - bar_height, bx + bar_width - 1,
                        y + height - 1), fill=HOME_INK)
        require_rect(bx, y + height - bar_height, bar_width, bar_height,
                     f"visualizer bar {index + 1}", report=False)
    clip_entry("visualizer bars", f"{bars} bars in {width}x{height}")


def draw_state_icon(draw, state, x, baseline):
    if state in ("opening", "finished"):
        return x
    if state == "playing":
        points = [(x, baseline - 9), (x, baseline + 1), (x + 8, baseline - 4)]
        require_points(points, "playing icon")
        draw.polygon(points, fill=HOME_INK)
        return x + 12
    points = [(x, baseline - 10), (x, baseline), (x + 2, baseline)]
    require_points(points, "pause bar left")
    draw.rectangle((x, baseline - 10, x + 2, baseline), fill=HOME_INK)
    draw.rectangle((x + 6, baseline - 10, x + 8, baseline), fill=HOME_INK)
    require_rect(x + 6, baseline - 10, 3, 11, "pause bar right")
    return x + 14


def draw_now_playing(state):
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    music_top_bar(draw, "NOW PLAYING")

    title_font = ui_font(17, 600)
    meta_font = ui_font(12, 400)
    system = ui_font(11, 400)

    is_error = state.startswith("error")
    has_art = state != "no-art"
    is_long = state == "long-text"
    title = ("An Extraordinarily Long Track Title That Needs Two Lines And An "
             "Ellipsis") if is_long else "Nocturne III"
    artist = ("The Extraordinarily Long Artist Name That Must Be Truncated"
              if is_long else "Aurora Strings")
    album = ("The Extraordinarily Long Album Title That Must Be Truncated"
             if is_long else "Nocturnes")

    if is_error:
        reason = "AUDIO UNDERRUN" if state == "error-retry" else "AUDIO CODEC/I2C"
        if len(reason) > 44:
            raise SystemExit("error reason exceeds 44 characters")
        clip_entry("error reason length", f"{len(reason)} <= 44 characters")
        guidance = ("Press SELECT to retry." if state == "error-retry"
                    else "Return to the album.")
        require_rect(16, 40, 288, 2, "error status rule")
        draw.rectangle((16, 40, 303, 41), fill=HOME_ERROR)
        put_fitted(draw, "AUDIO PLAYBACK FAILED", title_font, 16, 68, 288,
                   (16, 42, 288, 30), "error headline", HOME_INK)
        put_fitted(draw, reason, meta_font, 16, 92, 288,
                   (16, 74, 288, 22), "error reason", HOME_INK)
        put_fitted(draw, guidance, meta_font, 16, 114, 288,
                   (16, 96, 288, 22), "error guidance", HOME_MUTED)
        put_fitted(draw, "Nocturne III — Aurora Strings", system, 16, 138,
                   288, (16, 120, 288, 20), "error identity", HOME_MUTED)
        require_rect(16, 150, 288, 1, "error detail rule")
        draw.line((16, 150, 303, 150), fill=HOME_FAINT)
        put_fitted(draw, "SOURCE WAV · PCM · 16-BIT · 44.1 kHz", system, 16,
                   164, 288, (16, 152, 288, 14), "error source quality",
                   HOME_MUTED)
        put_text(draw, "OUT — · PLAYBACK STOPPED", system, 16, 178,
                 (16, 166, 288, 14), "error output quality", HOME_MUTED)
        stopped = "0:00" if state == "error-terminal" else "1:24"
        put_text(draw, f"STOPPED AT {stopped} · TRACK 3 OF 10", system, 16,
                 192, (16, 180, 288, 14), "error stop position", HOME_MUTED)
        if state == "error-retry":
            footer = "SELECT RETRY · MENU BACK"
        else:
            footer = "SELECT ALBUM · MENU BACK"
        put_fitted(draw, footer, system, 16, 232, 288,
                   (16, 220, 288, 14), "error footer", HOME_MUTED)
        return save_pair(
            image,
            NOW_PLAYING / f"helikon-os-now-playing-{state}-320x240.png",
            NOW_PLAYING / f"helikon-os-now-playing-{state}-320x240@2x.png",
        )

    require_rect(16, 38, 96, 96, "now playing cover")
    draw.rectangle((16, 38, 111, 133), outline=HOME_INK, width=1)
    cover = cover_art(94, "mountain") if has_art else sleeve_placeholder(94)
    image.paste(cover, (17, 39))

    title_lines = wrap_lines(draw, title, title_font, 180, 2,
                             "now playing title")
    for index, line in enumerate(title_lines):
        put_text(draw, line, title_font, 124, 56 + index * 20,
                 (124, 38, 180, 44), f"now playing title {index + 1}",
                 HOME_INK)
    put_fitted(draw, artist, meta_font, 124, 98, 180,
               (124, 82, 180, 18), "now playing artist", HOME_INK)
    put_fitted(draw, album, meta_font, 124, 114, 180,
               (124, 100, 180, 16), "now playing album", HOME_MUTED)
    track_label = ("TRACK 3 OF 10 · 4:18" if state != "opening"
                   else "TRACK 3 OF 10")
    put_fitted(draw, track_label, system, 124, 130, 180,
               (124, 116, 180, 14), "now playing track line", HOME_MUTED)

    if state in ("playing", "long-text", "no-art"):
        state_label = "PLAYING"
    elif state == "paused":
        state_label = "PAUSED"
    elif state == "opening":
        state_label = "OPENING"
    else:
        state_label = "FINISHED"
    state_x = draw_state_icon(draw, state_label.lower(), 124, 146)
    put_text(draw, state_label, ui_font(14, 500), state_x, 146,
             (124, 132, 180, 18), "now playing state", HOME_INK)

    peaks = (0.18, 0.42, 0.70, 0.52, 0.86, 0.36, 0.62, 0.94, 0.48,
             0.72, 0.28, 0.56)
    draw_level_meter(draw, 16, 140, 96, 16, peaks,
                     state_label in ("PLAYING", "PAUSED"))

    duration = 0 if state == "opening" else 258000
    position = {
        "playing": 42000, "long-text": 42000, "no-art": 42000,
        "paused": 84000, "opening": 0, "finished": 258000,
    }[state]
    require_rect(16, 166, 288, 4, "progress track")
    draw.rectangle((16, 166, 303, 169), fill=HOME_RULE)
    if duration > 0 and position > 0:
        fill_width = max(1, round(288 * min(position, duration) / duration))
        require_rect(16, 166, fill_width, 4, "progress fill")
        draw.rectangle((16, 166, 15 + fill_width, 169), fill=HOME_INK)
    else:
        clip_entry("progress fill", "empty at zero or unknown duration")
    elapsed = format_time(position)
    total = format_time(duration) if duration > 0 else "--:--"
    put_text(draw, f"{elapsed} / {total}", system, 16, 184,
             (16, 172, 150, 14), "now playing time", HOME_INK)

    require_rect(16, 198, 288, 1, "details rule")
    draw.line((16, 198, 303, 198), fill=HOME_FAINT)
    source_line = ("SRC WAV · PCM · 16-BIT · 44.1 kHz" if state != "opening"
                   else "SRC —")
    output_line = ("1411 kbps · OUT 44.1 kHz · STEREO" if state != "opening"
                   else "OUT —")
    put_fitted(draw, source_line, system, 16, 210, 288,
               (16, 200, 288, 14), "now playing source quality", HOME_MUTED)
    put_fitted(draw, output_line, system, 16, 224, 288,
               (16, 214, 288, 14), "now playing output quality", HOME_MUTED)

    if state_label == "PLAYING":
        footer = "PLAY PAUSE · SELECT ALBUM · MENU BACK"
    elif state_label == "PAUSED":
        footer = "PLAY RESUME · SELECT ALBUM · MENU BACK"
    elif state_label == "OPENING":
        footer = "PLEASE WAIT"
    else:
        footer = "PLAY REPLAY · SELECT ALBUM · MENU BACK"
    put_fitted(draw, footer, system, 16, 238, 288,
               (16, 226, 288, 14), "now playing footer", HOME_MUTED)

    return save_pair(
        image,
        NOW_PLAYING / f"helikon-os-now-playing-{state}-320x240.png",
        NOW_PLAYING / f"helikon-os-now-playing-{state}-320x240@2x.png",
    )


def draw_album_view(mode="normal"):
    image = Image.new("RGB", (WIDTH, HEIGHT), HOME_PAPER)
    draw = ImageDraw.Draw(image)
    music_top_bar(draw, "ALBUM")

    title_font = ui_font(17, 600)
    meta_font = ui_font(12, 400)
    system = ui_font(11, 400)

    has_art = mode != "long"
    require_rect(16, 36, 72, 72, "album cover")
    draw.rectangle((16, 36, 87, 107), outline=HOME_RULE, width=1)
    cover = cover_art(70, "mountain") if has_art else sleeve_placeholder(70)
    image.paste(cover, (17, 37))

    if mode != "empty":
        draw_play_album(draw, 16, 114, 72, 18, focused=False)

    put_text(draw, "1975" if mode != "long" else "YEAR —", system, 100, 48,
             (100, 36, 80, 14), "album year", HOME_MUTED)
    draw_rating(draw, 304, 48, 80 if mode != "long" else 0)

    if mode == "long":
        album_title = ("The Extraordinarily Long Album Title That Needs "
                       "Two Lines")
        artist = "The Extraordinarily Long Artist Name"
    else:
        album_title = "Nocturnes"
        artist = "Aurora Strings"
    title_lines = wrap_lines(draw, album_title, title_font, 204, 2,
                             "album title")
    for index, line in enumerate(title_lines):
        put_text(draw, line, title_font, 100, 68 + index * 20,
                 (100, 36, 204, 56), f"album title line {index + 1}",
                 HOME_INK)
    put_fitted(draw, artist, meta_font, 100, 104, 204,
               (100, 90, 204, 16), "album artist", HOME_INK)
    genre = "CHAMBER · 43 MIN" if mode != "long" else "GENRE — · LENGTH —"
    put_fitted(draw, genre, system, 100, 118, 204,
               (100, 104, 204, 14), "album genre", HOME_MUTED)

    require_rect(16, 138, 288, 1, "album body rule")
    draw.line((16, 138, 303, 138), fill=HOME_RULE)
    put_text(draw, "TRACKS", ui_font(11, 500), 16, 150,
             (16, 140, 100, 14), "album tracks label", HOME_INK)
    track_count = "0 / 0" if mode == "empty" else "1–3 / 10"
    put_right_fitted(draw, track_count, system, 304, 150, 80,
                     (224, 140, 80, 14), "album track position", HOME_MUTED)

    if mode == "empty":
        put_text(draw, "NO TRACKS IN ALBUM", system, 16, 168,
                 (16, 156, 288, 16), "album empty tracks", HOME_MUTED)
    else:
        tracks = (
            ("01", "Nocturne I", "3:42"),
            ("02", "Nocturne II", "4:18"),
            ("03", "Nocturne III", "5:06"),
        )
        if mode == "long":
            tracks = (
                ("01", "An Extraordinarily Long Track Title That Needs An Ellipsis",
                 "3:42"),
                ("02", "Nocturne II", "4:18"),
                ("03", "Nocturne III", "5:06"),
            )
        if mode == "corrupt":
            tracks = (
                ("01", "Nocturne I", "3:42"),
                ("—", "TRACK DATA MISSING", ""),
                ("03", "Nocturne III", "5:06"),
            )
        for index, (number, track, duration) in enumerate(tracks):
            draw_album_track(draw, 16, 158 + index * 18, 288, number, track,
                             duration, index == 0,
                             unavailable=(number == "—"))

    require_rect(16, 216, 288, 1, "album colophon rule")
    draw.line((16, 216, 303, 216), fill=HOME_FAINT)
    if mode not in ("long", "empty", "corrupt"):
        put_text(draw, "NOTES", ui_font(11, 500), 16, 226,
                 (16, 216, 40, 14), "album notes label", HOME_INK)
        put_fitted(draw, "Recorded in one room over three nights.", system,
                   58, 226, 246, (56, 216, 248, 14),
                   "album notes excerpt", HOME_MUTED)
        put_text(draw, "CREDITS", ui_font(11, 500), 16, 236,
                 (16, 226, 50, 14), "album credits label", HOME_INK)
        put_fitted(draw, "A. Voss, producer · M. Rill, engineer", system,
                   68, 236, 236, (66, 226, 238, 14),
                   "album credits excerpt", HOME_MUTED)
    elif mode == "empty":
        put_text(draw, "NO CREDITS LISTED", system, 16, 228,
                 (16, 218, 288, 12), "album empty credits", HOME_MUTED)
    elif mode == "corrupt":
        put_text(draw, "1 TRACK UNREADABLE", system, 16, 228,
                 (16, 218, 288, 12), "album corrupt note", HOME_MUTED)

    name = {"normal": "album-view", "long": "album-long",
            "empty": "album-empty", "corrupt": "album-corrupt"}[mode]
    return save_pair(image, MUSIC_FLOW / f"helikon-os-{name}-320x240.png",
                     MUSIC_FLOW / f"helikon-os-{name}-320x240@2x.png")


def draw_album_track(draw, x, y, width, number, title, duration, selected,
                     unavailable=False):
    if selected:
        require_rect(x, y, 2, 18, f"track {number} focus bar")
        draw.rectangle((x, y, x + 1, y + 17), fill=HOME_INK)
    number_font = ui_font(11, 400)
    title_font = ui_font(14, 500 if selected else 400)
    duration_font = ui_font(11, 400)
    put_text(draw, number, number_font, x + 4, y + 13,
             (x + 2, y, 22, 18), f"track {number} number",
             HOME_MUTED if not selected else HOME_INK)
    title_colour = HOME_MUTED if unavailable else HOME_INK
    put_fitted(draw, title, title_font, x + 22, y + 13, width - 60,
               (x + 20, y, width - 58, 18), f"track {number} title",
               title_colour)
    if duration:
        put_text(draw, duration, duration_font, x + width, y + 13,
                 (x + width - 46, y, 46, 18), f"track {number} duration",
                 HOME_MUTED if not selected else HOME_INK, anchor="rs")


def draw_artist_album_cell(image, draw, x, y, size, title, year, selected,
                           has_art, kind):
    frame_colour = HOME_INK if selected else HOME_RULE
    require_rect(x, y, size, size, f"album cell {title}")
    draw.rectangle((x, y, x + size - 1, y + size - 1),
                   outline=frame_colour, width=2 if selected else 1)
    art = cover_art(size - 4, kind) if has_art else sleeve_placeholder(size - 4)
    image.paste(art, (x + 2, y + 2))
    title_font = ui_font(12, 500 if selected else 400)
    lines = wrap_lines(draw, title, title_font, size, 2, f"cell {title} title")
    for index, line in enumerate(lines):
        put_text(draw, line, title_font, x, y + size + 14 + index * 14,
                 (x, y + size, size, 34), f"cell {title} line {index + 1}",
                 HOME_INK)
    put_text(draw, year, ui_font(11, 400), x, y + size + 42,
             (x, y + size + 26, size, 20), f"cell {title} year", HOME_MUTED)


if __name__ == "__main__":
    raise SystemExit(main())
