# Cleaned Music-flow mockups

> Purpose: host-rendered 320×240 mockups for the Artists list, Artist view and
> single-page Album view.
> Status: binding design mockups for `docs/ui.md` §6; host only, no 6G panel run.
> Owner: UI lead
> Last updated: 2026-11-08
> Source: owner Music-flow brief, 2026-11-08; D-23, D-24; `main` `2420c64`;
> [`../../../docs/ui.md`](../../../docs/ui.md#6-music-flow--artists-artist-album-and-now-playing-binding-design)

## Design rules

- IBM Plex Sans only. Sizes are D-23 roles: 17/600 titles, 14/500 selected and
  state, 14/400 list and track rows, 12/400 body, 11/400 metadata, 11/500 labels.
- 11 px floor; no 3×5 fallback; mixed case; visible U+2026 truncation.
- Paper `#EFEDE5`, paper light `#F5F3EB`, ink `#24231F`, muted `#6B665C`, rule
  `#B3AD9E`, faint `#DCD7CA`. No green; no cards, shadows, gradients or sidebar.
- Artists list: sort-name order, sticky section letter, 28 px artist art or
  two-letter monogram, album/track counts, 2 px selected bar, footer range.
- Artist view: `ALBUMS` first and default; `OVERVIEW` second; `TRACKS` third.
  The 72 px album shelf is visible before biography. There is no artist-level
  `PLAY` plate.
- Album view: one page, no separate track-selection screen. Cover, identity,
  rating, `PLAY ALL`, three visible inline track rows, notes/credits excerpts.
- The older `album-view/` and `artist-view/` folders are superseded history.
- Binding/proposed split and the full design-gate checklist are in
  `docs/ui.md` §6.1 and §6.11; every checklist box is binding before merge.

## Files

All are 320×240 RGB; `@2x` is a nearest-neighbour viewing preview.

| Screen | 1× | 2× |
|---|---|---|
| Artists list | `helikon-os-artists-list-320x240.png` | `helikon-os-artists-list-320x240@2x.png` |
| Artists list, long/missing | `helikon-os-artists-long-320x240.png` | `helikon-os-artists-long-320x240@2x.png` |
| Artist — Albums | `helikon-os-artist-albums-320x240.png` | `helikon-os-artist-albums-320x240@2x.png` |
| Artist — Overview | `helikon-os-artist-overview-320x240.png` | `helikon-os-artist-overview-320x240@2x.png` |
| Artist — Overview empty | `helikon-os-artist-overview-empty-320x240.png` | `helikon-os-artist-overview-empty-320x240@2x.png` |
| Artist — Tracks | `helikon-os-artist-tracks-320x240.png` | `helikon-os-artist-tracks-320x240@2x.png` |
| Album — normal | `helikon-os-album-view-320x240.png` | `helikon-os-album-view-320x240@2x.png` |
| Album — long/missing art | `helikon-os-album-long-320x240.png` | `helikon-os-album-long-320x240@2x.png` |
| Album — empty | `helikon-os-album-empty-320x240.png` | `helikon-os-album-empty-320x240@2x.png` |
| Album — corrupt track | `helikon-os-album-corrupt-320x240.png` | `helikon-os-album-corrupt-320x240@2x.png` |

## Provenance

The album covers, artist portraits and no-art placeholders are project-owned
generated line art, not real album or artist artwork. All names, counts, years,
durations, biographies and notes are sample content. No Benguiat font is used or
required. The Plex subset and licence are in
[`../../fonts/README.md`](../../fonts/README.md).

## Regenerate and check

```sh
python3 tools/render-ui-mockup.py --only music-flow
```

This writes the ten screens at 1× and 2× and
[`clipping-check.txt`](clipping-check.txt). The report records every text box
and frame inside its column/canvas. It is host geometry only: not a 6G panel,
contrast, frame-time or device claim.
