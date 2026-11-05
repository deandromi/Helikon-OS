# Audiobook wall mockup

> Purpose: host-rendered 320×240 mockup of the audiobook browser wall.
> Status: committed design asset; no firmware screen and no device panel run.
> Owner: UI lead
> Last updated: 2026-11-05

## Files

| File | Content |
|---|---|
| `helikon-os-audiobook-wall-320x240.png` | 1× device frame: `AUDIOBOOKS` header, search, 4×3 book wall, selected-book caption |
| `helikon-os-audiobook-wall-320x240@2x.png` | Nearest-neighbour 2× viewing preview; not a device asset |

## Role

This is the audiobook browser, not a home-screen candidate. It is reached from
the `Audiobooks` launcher cell or the index. It has no playback strip, hero or
progress bar.

The wall shows twelve 52 px book covers for scanning. The header carries the
book count and a search icon; the caption names the focused book, author,
duration and reading progress.

## Layout

- 320×240, 29 px side margins, 1 px rules.
- Header: 18 px Benguiat `AUDIOBOOKS`, book count, 12 px search icon.
- Wall: four columns by three rows of 52 px covers; x = 29, 99, 169, 239;
  y = 44, 104, 164. The focused cover gets a 2 px ink frame.
- Caption: title, author, duration and percentage at 11 px; position `1 / 48`
  at the right. The percentage is reading progress, not playback.

## Type and colour

- Header: ITC Benguiat Std Book, 18 px.
- Count and caption: IBM Plex Sans Regular, 11 px.
- Paper `#EFEDE5`; ink `#24231F`; muted `#6B665C`; rules `#B3AD9E`.
- The book art is generated monochrome line art, not real audiobook artwork.
  Real covers are RGB565 images in the same 46 px art area inside the 52 px
  frame. This wall uses no status colour.

## Font dependency

The header uses the owner-supplied **ITC Benguiat Std** `.otf` files. The font is
proprietary Monotype software: it is not committed to this repository, must not
be redistributed, and must not be extracted from the rendered PNG. The header
face is provisional until the owner confirms the scope and licence path.

## Limits

This folder contains the committed PNGs. The frame was produced by the host UI
mockup toolchain; regeneration needs the owner-supplied font and that toolchain.
All titles, authors, durations and percentages are sample content. The mockup is
a design artifact, not a firmware screen or a device result, and no support
claim follows from it.
