#!/usr/bin/env python3
"""Generate the Helikon Companion source icon.

The output is a 1024 px PNG with transparent corners. The build script turns it
into an .icns with iconutil; this script needs Pillow only when regenerating.
"""
from pathlib import Path
from PIL import Image, ImageDraw

SIZE = 1024
PAPER = (243, 240, 231, 255)
INK = (35, 34, 30, 255)
RUST = (156, 63, 44, 255)
RULE = (213, 207, 193, 255)

out = Path(__file__).resolve().parent.parent / "Resources" / "AppIcon.png"
out.parent.mkdir(parents=True, exist_ok=True)

img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)
draw.rounded_rectangle((32, 32, SIZE - 32, SIZE - 32), radius=228, fill=INK)

# A thin inner folio rule.
draw.rounded_rectangle((118, 118, SIZE - 118, SIZE - 118), radius=168, outline=(243, 240, 231, 48), width=4)

# Mountain crest, drawn as a wide open line.
points = [
    (150, 700),
    (340, 410),
    (452, 584),
    (604, 314),
    (874, 700),
]
draw.line(points, fill=PAPER, width=34, joint="curve")

# Sun, slightly off the crest.
draw.ellipse((668, 238, 790, 360), fill=RUST)

# A small horizon rule under the crest.
draw.line((250, 790, 774, 790), fill=(243, 240, 231, 120), width=8)
draw.ellipse((490, 774, 522, 806), fill=RULE)

img.save(out)
print(out)
