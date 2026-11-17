# Music-flow Plex text atlas

> Purpose: record the bounded IBM Plex Sans atlas used by the T-088 Music flow.
> Status: implemented for the 6G firmware Music screens; host previews only,
> no 6G panel run.
> Owner: UI lead. Last updated: 2026-11-08.

## Source and licence

The atlas is generated from `assets/fonts/HelikonUI-Sans.ttf`, the licensed IBM
Plex Sans variable subset documented in `assets/fonts/README.md`.

- Source SHA-256: `865c929cb8c51f612168c17077bcb65d813bcc9d2cb291afc22f3cf134bde39f`
- Licence: SIL Open Font License 1.1, `assets/fonts/OFL-IBMPlexSans.txt`
- No ITC Benguiat glyph is used or required by this flow.
- The generated bitmap data is a derivative of the licensed font; the OFL
  notice remains with the font asset.

## Styles and coverage

The generator emits every D-23 role in the flow:

| Size | Weights | Line height |
|---:|---|---:|
| 11 px | 400, 500, 600 | 14 |
| 12 px | 400, 500, 600 | 16 |
| 14 px | 400, 500, 600 | 18 |
| 17 px | 400, 500, 600 | 20 |

Coverage per style is ASCII printable, Latin-1, and UI punctuation:
U+2013, U+2014, U+2018, U+2019, U+201C, U+201D, U+2022 and U+2026. Unsupported
code points use the visible `?` fallback; the flow never silently drops them.

The renderer preserves mixed case, supports U+2026 truncation at a word
boundary, and never shrinks below 11 px. There is no 3×5 path on these screens.

## Budget

Measured from the generated C at compile time:

| Item | Bytes |
|---|---:|
| 4-bit alpha masks, 12 styles × 198 glyphs | 96,770 |
| Glyph records (16 bytes each) | 38,016 |
| 1-bit fallback masks, 12 styles × 103 glyphs | 14,651 |
| Fallback glyph records (16 bytes each) | 19,776 |
| Style table and counters | ~180 |
| **Total generated .rodata** | **~169,400** |

The atlas is read-only `.rodata`. It adds no RAM, no heap and no per-frame
rasterization. `ui_text.c` keeps one fallback flag and draws directly into the
existing RGB565 framebuffer. There is no allocation in the draw path.

## Fallback

A separate 1-bit mask set at the same sizes and weights is generated from the
same Plex face. If the atlas is unavailable, the renderer uses that set and
`helikon_text_fallback_active()` becomes true. The top bar then shows
`FONT FALLBACK` instead of the unknown time so the substitution is visible.
Host tests can force the fallback with `helikon_text_set_fallback(1)`.

## Regeneration

```sh
python3 tools/make-ui-font-atlas.py
```

The script rewrites `firmware/ui/font_atlas.c` and `font_atlas.h`. It records
the source hash in both files. The generated C is committed so the firmware
build never needs a font rasterizer.

Host previews are in `build/ui/music-*.png` and `build/ui/audio-*.png` after
`make -C tests/ui test`. They are host artifacts, not device evidence.
