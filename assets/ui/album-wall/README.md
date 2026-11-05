# Album wall mockup

> Purpose: host-rendered 320×240 mockup of the album browser wall.
> Status: committed design asset; no firmware screen and no device panel run.
> Owner: UI lead
> Last updated: 2026-11-05

## Files

| File | Content |
|---|---|
| `helikon-os-album-wall-320x240.png` | 1× device frame: `ALBUMS` header, search, 4×3 cover wall, selected-album caption |
| `helikon-os-album-wall-320x240@2x.png` | Nearest-neighbour 2× viewing preview; not a device asset |

## Role

This is the **album browser**, not a home-screen candidate. It is reached from
the `BROWSE` section of the neoclassical index or the `Music` plate of the
launcher. It has no playback strip, hero, progress bar or green playback dot.

The wall is intentionally denser than a home screen: twelve 52 px covers are
visible so the user can scan by artwork. The header carries the album count and
a search icon; the footer caption names the focused album and shows its position.

## Layout

- 320×240, 29 px side margins, 1 px rules.
- Header: 18 px Benguiat `ALBUMS`, album count, 12 px search icon.
- Wall: four columns by three rows of 52 px covers; x = 29, 99, 169, 239;
  y = 44, 104, 164. The focused cover gets a 2 px ink frame.
- Caption: album and artist at 11 px, position `1 / 86` at the right.

## Type and colour

- Header: ITC Benguiat Std Book, 18 px.
- Count and caption: IBM Plex Sans Regular, 11 px.
- Paper `#EFEDE5`; ink `#24231F`; muted `#6B665C`; rules `#B3AD9E`.
- The cover art is generated monochrome line art, not album artwork. Real covers
  are RGB565 images in the same 46 px art area inside the 52 px frame.

## Font dependency

The header uses the owner-supplied **ITC Benguiat Std** `.otf` files. The font is
proprietary Monotype software: it is not committed to this repository, must not
be redistributed, and must not be extracted from the rendered PNG. Set
`BENGUIAT_DIR` to the owner’s folder when regenerating the mockup. The header
face is provisional; it is not an accepted default until the owner confirms the
scope and the licence path.

## Regeneration and limits

The frame was produced by the host UI mockup toolchain; this folder contains the
two committed PNGs. Regeneration needs the owner-supplied font and the working
toolchain. The PNGs are design artifacts, not a firmware screen or a device
result. All names, counts and durations are sample content. No support or panel
claim follows from this mockup.
