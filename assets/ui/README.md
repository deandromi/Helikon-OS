# UI mockups

> Purpose: host-rendered 320×240 UI mockups for the first-party UI.
> Status: host artifacts; cleaned Music-flow and Now Playing binding mockups
> added; launcher, three walls and old detail views on host; index alternate;
> no device panel run.
> Owner: UI lead
> Last updated: 2026-11-08

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
| `album-view/helikon-os-album-view-320x240.png` | **Superseded** by `music-flow/helikon-os-album-view-320x240.png`; retained as design history |
| `album-view/helikon-os-album-view-320x240@2x.png` | 2× viewing preview of the superseded album view; not a device asset |
| `artist-view/helikon-os-artist-view-320x240.png` | **Superseded** by the `music-flow/helikon-os-artist-*` mockups; retained as design history |
| `artist-view/helikon-os-artist-view-320x240@2x.png` | 2× viewing preview of the superseded artist view; not a device asset |

### Music flow and Now Playing (binding host mockups)

These are Plex-only and do not require `BENGUIAT_DIR`. The specification is
[`../../docs/ui.md` §6](../../docs/ui.md#6-music-flow--artists-artist-album-and-now-playing-binding-design).
All images are 320×240; the `@2x` files are nearest-neighbour viewing previews,
not device assets. Sample names, counts, biographies and durations are
illustrative.

| Screen / state | 1× | 2× |
|---|---|---|
| Artists list | `music-flow/helikon-os-artists-list-320x240.png` | `music-flow/helikon-os-artists-list-320x240@2x.png` |
| Artists list — long names / missing art | `music-flow/helikon-os-artists-long-320x240.png` | `music-flow/helikon-os-artists-long-320x240@2x.png` |
| Artist — Albums (default) | `music-flow/helikon-os-artist-albums-320x240.png` | `music-flow/helikon-os-artist-albums-320x240@2x.png` |
| Artist — Overview | `music-flow/helikon-os-artist-overview-320x240.png` | `music-flow/helikon-os-artist-overview-320x240@2x.png` |
| Artist — Overview empty | `music-flow/helikon-os-artist-overview-empty-320x240.png` | `music-flow/helikon-os-artist-overview-empty-320x240@2x.png` |
| Artist — Tracks | `music-flow/helikon-os-artist-tracks-320x240.png` | `music-flow/helikon-os-artist-tracks-320x240@2x.png` |
| Album — normal | `music-flow/helikon-os-album-view-320x240.png` | `music-flow/helikon-os-album-view-320x240@2x.png` |
| Album — long title / missing art | `music-flow/helikon-os-album-long-320x240.png` | `music-flow/helikon-os-album-long-320x240@2x.png` |
| Album — empty | `music-flow/helikon-os-album-empty-320x240.png` | `music-flow/helikon-os-album-empty-320x240@2x.png` |
| Album — corrupt track | `music-flow/helikon-os-album-corrupt-320x240.png` | `music-flow/helikon-os-album-corrupt-320x240@2x.png` |
| Now Playing — playing | `now-playing/helikon-os-now-playing-playing-320x240.png` | `now-playing/helikon-os-now-playing-playing-320x240@2x.png` |
| Now Playing — paused | `now-playing/helikon-os-now-playing-paused-320x240.png` | `now-playing/helikon-os-now-playing-paused-320x240@2x.png` |
| Now Playing — opening | `now-playing/helikon-os-now-playing-opening-320x240.png` | `now-playing/helikon-os-now-playing-opening-320x240@2x.png` |
| Now Playing — finished | `now-playing/helikon-os-now-playing-finished-320x240.png` | `now-playing/helikon-os-now-playing-finished-320x240@2x.png` |
| Now Playing — error, retryable | `now-playing/helikon-os-now-playing-error-retry-320x240.png` | `now-playing/helikon-os-now-playing-error-retry-320x240@2x.png` |
| Now Playing — error, terminal | `now-playing/helikon-os-now-playing-error-terminal-320x240.png` | `now-playing/helikon-os-now-playing-error-terminal-320x240@2x.png` |
| Now Playing — missing art | `now-playing/helikon-os-now-playing-no-art-320x240.png` | `now-playing/helikon-os-now-playing-no-art-320x240@2x.png` |
| Now Playing — long metadata | `now-playing/helikon-os-now-playing-long-text-320x240.png` | `now-playing/helikon-os-now-playing-long-text-320x240@2x.png` |

The clipping reports are
[`music-flow/clipping-check.txt`](music-flow/clipping-check.txt) and
[`now-playing/clipping-check.txt`](now-playing/clipping-check.txt). They record
host geometry only; they are not 6G panel results.

The first two are 320×240 8-bit grayscale with anti-aliased text. Their dark
background and white selected row are a neutral typography proof, not the
selected palette. The concept screens are full-colour RGB because the proposed
off-white, warm-grey and ink palette cannot be expressed as a neutral grayscale
proof. The launcher is the chosen home concept; the index remains an alternate;
the three walls are browsers. The cleaned Music-flow list/detail screens and
Now Playing are the binding host mockups in §6 of `docs/ui.md`; the older
`album-view/` and `artist-view/` folders are superseded history.
Each folder has its own README. None is a settled theme until a 6G panel check
records contrast and clipping.

The legacy home/wall and old detail concept screens need the owner-supplied
**ITC Benguiat Std** `.otf` files. They are proprietary, are not committed to
this repository and must not be redistributed. Point `BENGUIAT_DIR` at the
owner’s folder (the script defaults to `~/Downloads/ITC Benguiat`). Without the
files the legacy concept step stops with a clear message; the font is never
silently substituted in the mockups. The rendered PNGs are design artifacts,
not font packages. Do not extract glyphs from them. The cleaned Music flow and
Now Playing do not need Benguiat.

Regenerate the cleaned Music flow and Now Playing (Plex-only, no Benguiat):

```sh
python3 tools/render-ui-mockup.py --only music-flow
```

Regenerate every legacy concept (this needs the owner-supplied Benguiat files):

```sh
BENGUIAT_DIR="/path/to/ITC Benguiat" python3 tools/render-ui-mockup.py --only all
```

The `--only music-flow` run writes the ten Music-flow screens and eight Now
Playing states at 1× and 2×, plus both `clipping-check.txt` reports. The
`--only all` run also writes the two typography mockups and the seven home/wall
concept screens, which still use the owner-supplied ITC Benguiat face. The
script asserts that every recorded text box and frame is inside its column and
the 320×240 canvas. The typography mockups use the licensed subsets in
[`../fonts/`](../fonts/README.md). The launcher icons, wall covers, album
placeholder and artist portrait are generated line art, not real artwork.
Renderer integration remains T-042/T-057; the bootloader splash is T-045.
