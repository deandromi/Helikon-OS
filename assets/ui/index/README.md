# Neoclassical index mockup

> Purpose: host-rendered 320×240 alternate home concept.
> Status: committed alternate concept; launcher chosen; no firmware screen and
> no device panel run.
> Owner: UI lead
> Last updated: 2026-11-05

## Files

| File | Content |
|---|---|
| `helikon-os-home-index-320x240.png` | 1× device frame: wordmark, four numbered sections, counts and hourglass |
| `helikon-os-home-index-320x240@2x.png` | Nearest-neighbour 2× viewing preview; not a device asset |

## Role

The index is an alternate home surface, not the chosen launcher. It has no
playback element, no icons, no cards and no content previews.

The four sections are `LISTEN`, `BROWSE`, `PLAY` and `SETTINGS`, with descriptions
and sample counts. The selected row uses an ink tab behind its numeral and
section name. `BROWSE` is the natural entry to the album wall.

## Layout

- 320×240, 20 px page margins.
- Masthead: 22 px Benguiat wordmark, time and battery; rule and diamond at y = 40.
- Four 40 px rows from y = 50 to 210; numerals at 12 px, section names at 20 px
  Benguiat, descriptions and counts at 11 px Plex.
- Selected row: ink tab from x = 20, height 22 px; paper text inside, ink text
  below.
- Footer rule at y = 210 and a 10 × 16 hourglass at y = 218.

## Type and colour

- Wordmark and section names: ITC Benguiat Std Medium/Book.
- Descriptions and counts: IBM Plex Sans Regular, 11 px.
- Paper `#EFEDE5`; ink `#24231F`; muted `#6B665C`; rules `#B3AD9E`.
- This screen is intentionally monochrome; the launcher carries the six icon
  accents.

## Font dependency

The wordmark, numerals and section names use the owner-supplied **ITC Benguiat
Std** `.otf` files. The font is proprietary Monotype software: it is not
committed to this repository, must not be redistributed, and must not be
extracted from the rendered PNG. The header face is provisional until the owner
confirms the scope and licence path.

## Limits

This folder contains the committed PNGs. The frame was produced by the host UI
mockup toolchain; regeneration needs the owner-supplied font and that toolchain.
All names and counts are sample content. The mockup is a design artifact, not a
firmware screen or a device result, and no support claim follows from it.
