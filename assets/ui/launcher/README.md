# Launcher home mockup

> Purpose: host-rendered 320×240 mockup of the chosen phone-style launcher home.
> Status: committed launcher concept; no firmware screen and no device panel run.
> Owner: UI lead
> Last updated: 2026-11-05

## Files

| File | Content |
|---|---|
| `helikon-os-home-launcher-320x240.png` | 1× device frame: top folio, Benguiat wordmark, mountain divider, 3×2 icon grid |
| `helikon-os-home-launcher-320x240@2x.png` | Nearest-neighbour 2× viewing preview; not a device asset |

## Role

The launcher is the home screen. It has no playback element, no now-playing
strip, no content cards and no footer. The six cells are the only decisions:
Music, Podcasts, Audiobooks, Playlists, Games and Settings.

## Layout

- 320×240; 20 px side margins.
- Top folio at y = 8: `ALPHA · 6G`, centred diamond, time and battery.
- `HELIKON-OS` in ITC Benguiat Std Medium, 26 px, raised 4 px above the divider.
- Divider at y = 56: two 2 px rules with small end diamonds, flanking a small
  ink mountain crest. The wordmark moved; the divider did not.
- Grid: three columns at x = 24, 128, 232; two rows at y = 64, 150.
- Each cell is 64 px, frameless, with a 40 px icon. A 24 px plinth in a 50%
  paper mix of the icon accent sits under the icon. Labels are 13 px Benguiat.
- Focus: a small apex diamond plus a 2 px underline beneath the label.

## Icon accents

| Cell | Accent | Contrast on paper |
|---|---|---|
| Music | `#9C3F2C` | 5.7:1 |
| Podcasts | `#31536B` | 6.9:1 |
| Audiobooks | `#8A6420` | 4.6:1 |
| Playlists | `#486346` | 5.7:1 |
| Games | `#64364C` | 8.2:1 |
| Settings | `#2D5F58` | 6.2:1 |

The page remains off-white `#EFEDE5`; there is no fill or wash behind the icons.
Secondary icon lines use a 58% paper mix of the same accent. The mountain and
focus marks are ink; labels remain ink for contrast.

## Font dependency

The wordmark, labels, divider and wall headers use the owner-supplied **ITC
Benguiat Std** `.otf` files. The font is proprietary Monotype software: it is not
committed to this repository, must not be redistributed, and must not be
extracted from the rendered PNG. The header face is provisional until the owner
confirms the scope and licence path.

## Limits

This folder contains the committed PNGs. The frame was produced by the host UI
mockup toolchain; regeneration needs the owner-supplied font and that toolchain.
All names, counts and times are sample content. The mockup is a design artifact,
not a firmware screen or a device result, and no support claim follows from it.
