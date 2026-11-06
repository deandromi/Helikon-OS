# Album view mockup

> Purpose: host-rendered 320×240 album detail page.
> Status: proposed design; no firmware screen and no device panel run.
> Owner: UI lead
> Last updated: 2026-11-06

## Files

| File | Content |
|---|---|
| `helikon-os-album-view-320x240.png` | 1× device frame: sleeve, metadata, play, tabs and first three tracks |
| `helikon-os-album-view-320x240@2x.png` | Nearest-neighbour 2× viewing preview; not a device asset |

## Role

The album page is a detail screen, not a home screen or a wall. It is opened from
the album wall or a track row. It has no sidebar and no desktop-style player bar;
the hardware Menu button returns.

## Layout

- 320×240, 20 px side margins, 1 px rules.
- Top bar at y = 8: `ALBUM`, time, battery; rule at y = 30.
- 92 px sleeve at x = 20, y = 38, with a 1 px ink frame.
- Text column at x = 124: year and five-diamond rating; 22 px Benguiat title;
  12 px artist; 11 px genre/length; compact ink `PLAY` button.
- Tabs at y = 150: `TRACKS`, `NOTES`, `CREDITS`; selected tab underlined 2 px.
  Track count at the right.
- Two visible track rows of 26 px from y = 162: number, 14 px title, duration.
  The focused row has a 2 px ink bar and a 14 px Medium title. The rest scroll.

## Information

| Tab | Content |
|---|---|
| Tracks | Numbered track list with title and duration; the first two are visible, the rest scroll |
| Notes | Description, length, rating and format facts |
| Credits | Composer, performers, producer, engineer and recording date |

The page keeps the Roon-style information hierarchy — sleeve, title, year,
rating, genres, tracks, notes, credits — without copying its sidebar, photo,
buttons or player bar.

## Type and colour

- Album title: ITC Benguiat Std Book, 22 px.
- Artist: IBM Plex Sans Regular, 12 px. Track rows: 14 px. Metadata: 11 px.
- Paper `#EFEDE5`; ink `#24231F`; muted `#6B665C`; rules `#B3AD9E`.
- The sleeve is a generated terracotta `#9C3F2C` placeholder with cream
  engraving, not real album artwork. A real cover replaces it at the same 96 px.

## Limits

The mockup is a host artifact. It does not prove a firmware renderer, scrolling,
focus model or 6G panel result. The Benguiat font is owner-supplied, proprietary
and not committed; the header face remains provisional. All names, years,
ratings and durations are sample content.
