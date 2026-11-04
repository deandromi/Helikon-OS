# UI font assets

> Purpose: licensed IBM Plex Sans and Manrope subsets for the first-party UI.
> Status: `implemented` for the host mockup; renderer integration is T-042.
> Owner: UI lead
> Last updated: 2026-11-04

## Files

| File | Source | Size | SHA-256 |
|---|---|---:|---|
| `HelikonUI-Sans.ttf` | Subset of IBM Plex Sans variable (wght 100–700, wdth 100), Latin-1 + UI punctuation | 139,716 bytes | `865c929cb8c51f612168c17077bcb65d813bcc9d2cb291afc22f3cf134bde39f` |
| `HelikonWordmark.ttf` | Subset of Manrope variable (wght 200–800), `HELIKON-OS` glyphs only | 4,436 bytes | `a9365702c51b15ecd4439aaa42ae74bed29eef0e9c45bfc6761c66f19553f883` |
| `OFL-IBMPlexSans.txt` | SIL Open Font License 1.1 for IBM Plex Sans | 4,362 bytes | — |
| `OFL-Manrope.txt` | SIL Open Font License 1.1 for Manrope | 4,386 bytes | — |

Upstream sources, downloaded 2026-11-04:

- IBM Plex Sans variable: <https://github.com/google/fonts/raw/main/ofl/ibmplexsans/IBMPlexSans%5Bwdth,wght%5D.ttf>
  - full upstream SHA-256 `3b031aa4216174205bd8471f88a49b91f093169e9e87bd5262242bc5967fe2e3`
- Manrope variable: <https://github.com/google/fonts/raw/main/ofl/manrope/Manrope%5Bwght%5D.ttf>
  - full upstream SHA-256 `3ae11c49db0455a3cc33e37d380f20fdb8c7f8b41dc07625c177e3d87a9d6ae6`

The subsets are renamed because IBM Plex has a Reserved Font Name. The license
texts and copyright notices remain with the files. These are derivatives under
the SIL Open Font License 1.1, not the upstream font names.

## Axes

- `HelikonUI-Sans.ttf`: `wght` 100–700, default 400; `wdth` fixed at 100.
  Regular = 400, Medium = 500, Semibold = 600.
- `HelikonWordmark.ttf`: `wght` 200–800, default 200; Bold = 700.
  Use only for the Helikon-OS wordmark, never body/menu/title text.

## Scope

- The font subset covers Latin-1 and common UI punctuation. A future firmware
  subset may add the Unicode names the library needs; do not claim full
  coverage until that subset is measured on the device.
- No proprietary font, theme, artwork or metadata is included.
- The bootloader splash is a separate asset and rendering path (T-045).
- The renderer (T-042) must use precomputed glyph masks/atlases, not rasterize a
  full font per frame, and keep a readable 1-bit fallback.

## Regeneration

The subsets were generated from the upstream variable fonts with:

```sh
pyftsubset IBMPlexSans[wght].ttf \
  --output-file=IBMPlexSans-UI.ttf \
  --unicodes='U+0020-007E,U+00A0-00FF,U+2013,U+2014,U+2018,U+2019,U+201C,U+201D,U+2022,U+2026,U+20AC' \
  --layout-features='*' --name-IDs='*' --name-languages='*'

pyftsubset Manrope[wght].ttf \
  --output-file=Manrope-Logo.ttf \
  --text='HELIKON-OS' \
  --layout-features='*' --name-IDs='*' --name-languages='*'
```

The internal names were changed to `Helikon UI Sans` and `Helikon Wordmark`
before committing, so the IBM Plex Reserved Font Name is not used for the
modified subset.
