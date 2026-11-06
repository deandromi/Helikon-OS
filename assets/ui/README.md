# UI mockups

> Purpose: host-rendered 320×240 UI mockups for the first-party UI.
> Status: host artifacts; launcher and three walls committed; index alternate;
> album/artist views proposed; no firmware UI or device panel run.
> Owner: UI lead
> Last updated: 2026-11-05

| File | Content |
|---|---|
| `helikon-os-ui-320x240.png` | Typography/menu mockup: Manrope wordmark, 17 px title, 14 px Regular menu, 14 px Medium selected row, 12 px artist/album, 11 px system text |
| `helikon-os-typography-320x240.png` | Typography proof: all sizes/weights, selected-row contrast and wordmark |
| `launcher/helikon-os-home-launcher-320x240.png` | Concept A: phone-style launcher with six frameless coloured line-icon cells on accent plinths, a mountain divider with end diamonds; no playback |
| `launcher/helikon-os-home-launcher-320x240@2x.png` | 2× viewing preview of the launcher; not a device asset |
| `index/helikon-os-home-index-320x240.png` | Concept B: neoclassical index with four Benguiat sections, counts and an hourglass; no playback |
| `index/helikon-os-home-index-320x240@2x.png` | 2× viewing preview of the index; not a device asset |
| `album-wall/helikon-os-album-wall-320x240.png` | Album browser: 4×3 cover wall with a selected-album caption and search icon; not a home candidate |
| `album-wall/helikon-os-album-wall-320x240@2x.png` | 2× viewing preview of the album wall; not a device asset |
| `podcast-wall/helikon-os-podcast-wall-320x240.png` | Podcast browser: 4×3 show wall with new-episode dots, selected-show caption and search; not a home candidate |
| `podcast-wall/helikon-os-podcast-wall-320x240@2x.png` | 2× viewing preview of the podcast wall; not a device asset |
| `audiobook-wall/helikon-os-audiobook-wall-320x240.png` | Audiobook browser: 4×3 book wall with author, duration and progress caption; not a home candidate |
| `audiobook-wall/helikon-os-audiobook-wall-320x240@2x.png` | 2× viewing preview of the audiobook wall; not a device asset |
| `album-view/helikon-os-album-view-320x240.png` | Album detail: terracotta sleeve, metadata, rating, play, tabs and track list |
| `album-view/helikon-os-album-view-320x240@2x.png` | 2× viewing preview of the album view; not a device asset |
| `artist-view/helikon-os-artist-view-320x240.png` | Artist detail: plum portrait band, name, play, tabs and overview |
| `artist-view/helikon-os-artist-view-320x240@2x.png` | 2× viewing preview of the artist view; not a device asset |

The first two are 320×240 8-bit grayscale with anti-aliased text. Their dark
background and white selected row are a neutral typography proof, not the
selected palette. The concept screens are full-colour RGB because the proposed
off-white, warm-grey and ink palette cannot be expressed as a neutral grayscale
proof. The launcher is the chosen home concept; the index remains an alternate;
the three walls are browsers, and the album and artist views are detail screens.
Each folder has its own README. None is a settled theme until a 6G panel check
records contrast and clipping.

The concept screens need the owner-supplied **ITC Benguiat Std** `.otf` files.
They are proprietary, are not committed to this repository and must not be
redistributed. Point `BENGUIAT_DIR` at the owner’s folder (the script defaults to
`~/Downloads/ITC Benguiat`). Without the files the concept step stops with a
clear message; the font is never silently substituted in the mockups. The
rendered PNGs are design artifacts, not font packages. Do not extract glyphs
from them.

Regenerate:

```sh
BENGUIAT_DIR="/path/to/ITC Benguiat" python3 tools/render-ui-mockup.py
```

The script writes the two typography mockups, the seven concept screens and
their 2× previews. It asserts that launcher labels and fixed columns fit. The
typography mockups use the licensed subsets in
[`../fonts/`](../fonts/README.md). The launcher icons, wall covers, album sleeve and artist cameo are generated
line art, not real artwork. Renderer integration remains
T-042/T-057; the bootloader splash is T-045.
