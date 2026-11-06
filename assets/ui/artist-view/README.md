# Artist view mockup

> Purpose: host-rendered 320×240 artist detail page.
> Status: implemented in `firmware/ui/music.c` with host previews; no device
> panel run yet.
> Owner: UI lead
> Last updated: 2026-11-06

## Files

| File | Content |
|---|---|
| `helikon-os-artist-view-320x240.png` | 1× device frame: portrait band, artist name, play, tabs and overview |
| `helikon-os-artist-view-320x240@2x.png` | Nearest-neighbour 2× viewing preview; not a device asset |

## Role

The artist page is a detail screen, not a home screen or a wall. It is opened from
an artist row or a track credit. It has no sidebar and no desktop-style player
bar; the hardware Menu button returns.

## Layout

- 320×240, 16 px side margins, 1 px rules.
- Top bar at y = 8: `ARTIST`, time, battery; rule at y = 26.
- Full-width 320 × 68 portrait band in plum, with a cream cameo at the right.
  The cameo is a generic classical profile, not a likeness.
- Artist name in 22 px Benguiat at y = 124; genre line at 142.
- Compact ink `PLAY` button at the right of the name block.
- Tabs at y = 164: `OVERVIEW`, `ALBUMS`, `TRACKS`; selected tab underlined 2 px.
  Album count at the right.
- Overview: two-line 11 px biography and a formed/origin/count line.

## Information

| Tab | Content |
|---|---|
| Overview | Two-line biography, formed/origin facts, album count |
| Albums | The artist’s album wall, using the committed album-wall tiles |
| Tracks | Top tracks with duration and album context |

The page borrows the Roon artist hierarchy — portrait, name, genres, actions,
biography, facts, discography and tracks — without copying its sidebar, photo,
button cluster or player bar.

## Type and colour

- Artist name: ITC Benguiat Std Book, 22 px.
- Genre and facts: IBM Plex Sans Regular, 11 px. Biography: 11 px.
- Paper `#EFEDE5`; ink `#24231F`; muted `#6B665C`; rules `#B3AD9E`.
- The portrait band is plum `#64364C` with a cream `#EFEDE5` cameo. It is a
  generated placeholder; a real artist image replaces the band at the same size.

## Limits

The mockup is a host artifact. The firmware renders the layout from generated
top-bar, portrait-band, play and tab tiles; dynamic names, genre, biography and
facts use the 3×5 fallback until the Plex/Benguiat atlas (T-042/T-057). The
left/right buttons switch tabs, the wheel moves album/track selection, SELECT
opens the focused item and MENU returns. It does not prove 6G panel contrast or
frame time. The Benguiat font is owner-supplied, proprietary and not committed;
the header face remains provisional. All names, dates and counts are sample
content.
