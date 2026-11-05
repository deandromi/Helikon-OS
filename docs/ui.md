# UI design — default theme and typography

> Purpose: the first-party Helikon-OS UI look and the default typography scale.
> Status: living document; typography owner-settled, font subsets and host mockups implemented, launcher renderer implemented on host, 6G panel run pending.
> Owner: project owner; UI lead
> Last updated: 2026-11-05
> Source: owner-supplied typography table, 2026-11-03; owner home-screen brief,
> 2026-11-05; design paper §14, §28.3.4; D-10, D-15, D-23

This is the default first-party UI. It is not a skin format and not a promise
that a Rockbox WPS theme will render identically. Skins follow D-10 and D-15;
they may not make the first-party UI unusable.

## 1. Default typography

The iPod Classic 6G screen is 320×240. These values are for the default theme
at 1× scale.

| Element | Typeface | Size |
|---|---|---|
| Main menu | IBM Plex Sans Regular | 14 px |
| Selected item | IBM Plex Sans Medium | 14 px |
| Screen titles | IBM Plex Sans Semibold | 17 px |
| Artist / album | IBM Plex Sans Regular | 12 px |
| System information | IBM Plex Sans Regular | 11 px |
| Helikon-OS logo | Manrope Bold | Variable |

Weight mapping:

- IBM Plex Sans Regular = 400
- IBM Plex Sans Medium = 500
- IBM Plex Sans Semibold = 600
- Manrope Bold = 700 on the variable weight axis

`Variabel` in the owner table is recorded as “variable font / layout-dependent
logo size”. Confirm whether it means the variable font, a variable logo size, or
both. The logo is a wordmark, not a UI text style.

## 2. Rules

- IBM Plex Sans is the single UI family. Do not add a second UI family without an
  owner decision.
- UI text and icons use grayscale anti-aliasing. Precompute glyph masks or a
  glyph atlas; do not rasterize a full font per frame. Keep a readable 1-bit
  bitmap fallback if a font fails to load.
- Anti-aliased output must be checked on the 6G panel for contrast and clipping,
  not only on the host.
- Manrope Bold is only for the Helikon-OS logo/wordmark. It is not body, menu or
  title text.
- The selected item uses Medium at the same 14 px size as the main menu. The
  selection must be visible by weight/contrast, not by a size jump.
- Screen titles are 17 px Semibold. They are the largest UI text unless the logo
  is on screen.
- Artist/album metadata is 12 px. System information is 11 px.
- 11 px is the minimum for this default scale. Text must not clip, overlap or
  require horizontal scrolling. A large-text option remains an accessibility
  requirement.
- If a font fails to load or a glyph is missing, fall back to a readable system
  bitmap font and report the failure; the UI must remain usable. Do not silently
  substitute a proprietary font.
- The first-party UI must remain usable if a skin fails or is rejected.

## 3. Font assets and licensing

- IBM Plex Sans and Manrope are published under the SIL Open Font License.
  The licensed subsets, OFL texts, upstream sources and hashes are recorded in
  [`../assets/fonts/README.md`](../assets/fonts/README.md).
- `HelikonUI-Sans.ttf` is a renamed Latin-1 subset with the variable weight axis
  (Regular 400, Medium 500, Semibold 600). `HelikonWordmark.ttf` is a Manrope
  subset for `HELIKON-OS` at weight 700. Do not bundle the full upstream
  families; regenerate a subset only with a measured glyph list.
- The subsets are renamed because IBM Plex has a Reserved Font Name. Keep the
  OFL texts and copyright notices with the files.
- The owner-supplied mountain logo reference has an open licence/source; see
  `assets/logo/README.md`. The Manrope wordmark font is separate from that image.
- Do not copy proprietary themes, artwork, fonts or metadata.

## 4. Bootloader splash

The bootloader splash is the owner's final **green/red Alpha artwork**, not the
earlier 1-bit image. The source PNGs, the 16-colour 4bpp device representation,
the HFS+ label mask and the `#00BF63`/`#FE3131`/`#6B665C` label colours are
recorded in [`../assets/bootloader/README.md`](../assets/bootloader/README.md).
The final-splash image `6bc6927c…` was device-checked on one 6G/iFlash SD at
6.5 s startup with a clean `fsck_hfs -fn`.

The firmware first frame should continue this identity: off-white `#EFEDE5`
background, the same wordmark and type scale, and no contradictory HFS+ state.
The intended handoff is described in
[`firmware-architecture.md`](firmware-architecture.md) §9.10; it is a design
intent, not a firmware result. Do not replace the splash or the reference image
without owner approval.

## 5. Home and album-browser concepts (proposed)

> Status: owner-requested concept set; host mockups only. No renderer, 6G panel
> run or support claim. The owner will choose a home concept. The album wall is
> the album browser, not a home candidate.

The home screen has no playback element. Earlier attempts failed because a
now-playing hero or strip made the surface read as Now Playing. These concepts
keep home as a launch or index surface; playback is a separate screen. The album
wall is the one exception in role: it is the album browser, not a home.

All three use the same paper/ink palette, 1 px rules and Benguiat headers. All
names, counts and durations are sample content, not device data.

### 5.1 Palette (proposed)

| Token | Value | Use | Contrast on paper |
|---|---|---|---|
| Paper | `#EFEDE5` | page ground | — |
| Paper light | `#F5F3EB` | cover art ground | — |
| Ink | `#24231F` | primary text, selected marks, rules | 13.4:1 |
| Muted | `#6B665C` | secondary text, counts, descriptions | 4.9:1 |
| Rule | `#B3AD9E` | frames, hairlines, separators | 1.9:1 (decorative) |
| Faint | `#DCD7CA` | leader rules and inner detail | decorative |
| Live green | `#00BF63` | reserved for system status; not used here | 2.1:1 (non-text) |
| Error red | `#FE3131` | reserved for error screens | not used here |

The page stays off-white. Colour appears only in the six launcher icon accents;
the index and album wall remain monochrome. Green is a non-text status accent
and is deliberately absent from these three surfaces: none of them claims
playback or HFS+ state. Red never appears on a healthy screen.

#### Launcher accents (proposed)

| Icon | Accent | Contrast on paper |
|---|---|---|
| Music | `#9C3F2C` | 5.7:1 |
| Podcasts | `#31536B` | 6.9:1 |
| Audiobooks | `#8A6420` | 4.6:1 |
| Playlists | `#486346` | 5.7:1 |
| Games | `#64364C` | 8.2:1 |
| Settings | `#2D5F58` | 6.2:1 |

The accents are icon-line colours, not text colours. Secondary icon lines use a
58% paper mix of the same accent. No fill or wash is used behind the icons; the
page ground remains `#EFEDE5`. The divider mountain crest is ink, not an accent.

### 5.2 Concept A — phone-style launcher

![Launcher concept](../assets/ui/launcher/helikon-os-home-launcher-320x240@2x.png)

A ceremonial but conventional home screen: a status folio, a larger centred
Benguiat masthead, and six frameless coloured line-icon cells. It has no
now-playing element, no content cards and no footer.

| Region | Frame (x, y, w, h) | Content |
|---|---|---|
| Top folio | 20, 8, 280, 10 | edition mark, centred diamond, time, battery |
| Masthead | 20, 26, 280, 30 | 26 px Benguiat wordmark, raised 4 px; divider unchanged |
| Divider | 40, 50, 240, 12 | two 2 px rules with end diamonds, flanking a small mountain crest |
| Grid | 24, 64, 272, 164 | two rows by three 64 px icon cells |
| Icon cell | 64 × 64 | frameless, 40 px coloured icon on a 24 px accent plinth; selected cell has an apex diamond and a 2 px label underline |
| Label | 78 × 14 below cell | 13 px Benguiat, centred |

Cells sit at x = 24, 128, 232 and y = 64, 150. The labels are `Music`,
`Podcasts`, `Audiobooks`, `Playlists`, `Games` and `Settings`. Focus runs
row-major: Music → Podcasts → Audiobooks → Playlists → Games → Settings.

The full-width rule is gone: two wider 2 px rules with small end diamonds now
flank a mountain crest under the wordmark. The wordmark sits 4 px higher; the
divider and crest did not move. The arches were removed rather than re-shaped
again; the grid is frameless. Each icon rests on a short accent plinth, and the
selected cell is marked by a small apex diamond and a 2 px underline beneath its
label.

This is the most literal “home screen” reading. It is a launcher: the six
cells are the only decisions on the surface. It does not show what is playing.
The Music cell opens the library root; the album wall can be one of its views.

#### Firmware launcher renderer (implemented on host)

`firmware/ui/launcher.c` composes the launcher from precomputed RGB565 tiles;
`tools/make-launcher-assets.py` generates them from the same
`tools/render-ui-mockup.py` icon and layout code. The HELIKON-OS wordmark is
rasterized from the owner-supplied ITC Benguiat Medium face at the owner's
direction; the `.otf` is not committed. Labels use the tracked IBM Plex Sans
fallback. The folio, wordmark, divider, six icons and six labels are opaque
tiles. The accent plinths, the selected cell's apex diamond and the 2 px label
underline are drawn at runtime.

The wheel and left/right move the focus row-major through the six cells with
wrap. SELECT or PLAY opens a labelled placeholder app screen; MENU or SELECT
returns to the launcher, and MENU on the launcher exits. The diagnostic image
powers off after 60 s idle.

```sh
make -C firmware BOARD=6g VERSION=10 BOOTSTATE_STATE=good UI=1 \
  EXTRA_CFLAGS='-DHELIKON_DIAG_LAUNCHER' \
  BUILD=../build/firmware/6g-launcher image
```

The image stops after the LCD stage and does not touch storage.
`make -C tests/ui preview` renders the real firmware buffer to
`build/ui/launcher.png` and checks the focus marks, icon presence and
framebuffer bounds. No 6G panel run yet.

### 5.3 Concept B — neoclassical index

![Index concept](../assets/ui/index/helikon-os-home-index-320x240@2x.png)

A typographic contents page: wordmark, four numbered sections and a printer’s
hourglass. It has no icons, no cards and no playback.

| Region | Frame (x, y, w, h) | Content |
|---|---|---|
| Masthead | 20, 8, 280, 32 | 22 px Benguiat wordmark, time, battery |
| Index rows | 20, 50, 280, 160 | four rows, 40 px each |
| Row | 280 × 40 | Roman numeral, 20 px Benguiat section, 11 px description, right count |
| Colophon | 155, 218, 10, 16 | centred hourglass |

The sections are:

| Numeral | Section | Description | Count |
|---|---|---|---|
| I | LISTEN | Music · Podcasts · Audiobooks | 1,284 |
| II | BROWSE | Artists · Albums · Genres | 86 |
| III | PLAY | Playlists · Games | 9 |
| IV | SETTINGS | Display · System · About | — |

“BROWSE” is used instead of “READ” because audiobooks and podcasts are audio.
The owner can rename the sections without changing the layout.

The selected row gets an ink tab behind its numeral and section name. The
description and count stay below the tab in ink. The tab is the only dark block
on the screen; there is no row-height change and no icon.

This is the calmest and most distinctive surface. It is an index, not a
dashboard: four large rooms and no content previews. BROWSE is the natural entry
to the album wall.

### 5.4 Concept C — content walls (album, podcast, audiobook browsers)

The three walls are browsers, not home-screen candidates. The owner asked for
the music album wall first and then matching podcast and audiobook walls.

![Album wall](../assets/ui/album-wall/helikon-os-album-wall-320x240@2x.png)

![Podcast wall](../assets/ui/podcast-wall/helikon-os-podcast-wall-320x240@2x.png)

![Audiobook wall](../assets/ui/audiobook-wall/helikon-os-audiobook-wall-320x240@2x.png)

They share one layout:

| Region | Frame (x, y, w, h) | Content |
|---|---|---|
| Header | 29, 8, 262, 28 | 18 px Benguiat title, count, search icon |
| Wall | 29, 44, 262, 172 | four columns by three rows of 52 px covers |
| Caption | 29, 220, 262, 16 | focused item and position |

| Wall | Header | Count | Caption sample | Status |
|---|---|---|---:|---|
| Album | `ALBUMS` | 86 | Nocturnes · Helikon Trio | — |
| Podcast | `PODCASTS` | 18 | The Long Now · Ep. 42 · 3 new | green dot on new-show covers |
| Audiobook | `AUDIOBOOKS` | 48 | The Long Now · M. Voss · 12h 40m · 34% | — |

Covers sit at x = 29, 99, 169, 239 and y = 44, 104, 164. The selected cover gets
a 2 px ink frame; the caption updates to its item. The search icon in the header
is a focus stop and opens search. Each wall scrolls vertically; the count and the
`1 / N` caption show position. None has a playback bar.

The podcast wall uses the splash green `#00BF63` only as a small new-episode dot.
The audiobook caption includes author, duration and reading progress; the
percentage is not playback. The walls are intentionally denser than either home
concept: browsing cover art benefits from more covers per screen. They are not a
model for the home layout.

The album wall is reached from BROWSE; the podcast wall from Podcasts; the
audiobook wall from Audiobooks. The hardware Menu button returns to the previous
screen.

### 5.5 Headers and type

- ITC Benguiat Std is the display face for every wordmark and header on these
  screens: launcher wordmark 26 px Medium; launcher labels 13 px Book; index
  wordmark 22 px Medium; index section names 20 px Book; index numerals 12 px
  Book; album-wall title 18 px Book.
- IBM Plex Sans is the body face: 12 px for album and track titles; 11 px for
  descriptions, counts, captions and time. The 11 px minimum is unchanged.
- These home surfaces do not use the 14 px selected-row rule from D-23. That
  rule remains for list screens. Selection here is an apex diamond plus a 2 px
  label underline (launcher), a 2 px cover frame (album wall) or an ink tab
  (index), never a size change.
- The launcher icons use the six muted accents in §5.1. No fill or wash sits
  behind them; secondary icon lines are a lighter wash of the accent, and each
  icon sits on a 24 px plinth at a 50% paper mix. Labels stay ink and the page
  stays paper.
- D-52 records the proposed exception to D-23. D-23 currently accepts IBM Plex
  Sans as the single UI family; these concepts use Benguiat because the owner
  directed it. Until D-52 is accepted, they are proposals, not settled defaults.
- Licensing: ITC Benguiat is proprietary Monotype font software. The owner
  supplied the `.otf` files locally. They are not committed, redistributed or
  extracted from the rendered mockups. `BENGUIAT_DIR` must point to the owner’s
  folder; another machine cannot regenerate the screens without it.
- If the firmware cannot ship Benguiat, the fallback is a tracked IBM Plex Sans
  Semibold treatment, not another proprietary face and not a silent
  substitution.
- The splash title is owner bitmap artwork in Benguiat. These headers continue
  that display voice; the transition from splash to home is a content change,
  not a typeface change.

### 5.6 Focus and states

- Launcher: a focused cell gets a small apex diamond and a 2 px underline
  beneath its label. The icon and label do not grow or move. Focus is row-major
  and wraps.
- Index: a focused row gets the ink tab behind its numeral and name; the
  description and count change from muted to ink. No row-height change.
- Album wall: a focused cover gets a 2 px ink frame; the caption updates. The
  search icon is reachable from the wall and opens search.
- None of the three shows a playback state. There is no green dot, progress bar
  or now-playing strip on a home concept. If the owner later wants a playback
  cue, it must be a small status mark, not a strip or hero.
- The album wall may show an empty state (“No albums”) and a search-empty state;
  the home concepts show their sections even when a section is empty, with a
  muted count.
- A read-only HFS+ volume is explained in System Info; none of these screens
  paints an HFS+ claim. Red is reserved for error screens.
- Large text: the launcher becomes a two-column paged grid; the index drops to
  two rows per page; the album wall becomes three columns. No clipping or
  horizontal scrolling. This remains T-074 work.

### 5.7 Motion and rendering

- Focus changes are instant; no sliding highlight, parallax or shadow.
- Rules and frames are 1 px; text and icons use grayscale anti-aliasing with the
  1-bit fallback from D-24.
- Launcher icons are 40 px line art in the six accent colours, centred by
  ink bounds inside 64 px frameless cells and resting on 24 px accent plinths.
  There is no arch or fill; focus is the apex diamond and label underline.
  Album-wall covers are 46 px art inside 52 px frames. The index has no bitmap
  artwork.
- The album wall scrolls; the two home concepts are static.
- Content is read before the frame is drawn; no per-frame library query.

### 5.8 Open questions

- Which home concept does the owner choose: launcher, index, or a hybrid?
- Does “BROWSE” stay, or should the index use “READ” as originally sketched?
- D-52 scope: home headers only, or all first-party headers? Does the wordmark
  change globally?
- Can ITC Benguiat be embedded in firmware, or must headers be pre-rendered
  bitmaps/atlases under the font licence?
- Are the six launcher icons the right set, and do the line drawings read on the
  6G panel?
- Are the three walls dense enough for searching, or should they use 3 × 2
  with larger covers?
- Should the three walls share one search, sort and filter model?
- Do the six icon accents hold on the 6G panel, or should the launcher use one
  accent plus ink?
- Does the frameless icon grid hold on the 6G panel, or does it need a quiet
  cell frame?
- 11 px captions, 13 px labels and 20 px index names on the 6G panel, not only
  on the host.
- Large-text and high-contrast layouts.

## 6. Not specified yet

These are open questions, not implied values. Do not invent and record them as
settled:

- line height and baseline spacing beyond the proposed home concepts;
- colour palette and selected-item treatment on screens other than the home;
- list row height, separators and status-bar layout outside the home screen;
- whether 11 px is comfortable on the 6G panel or needs a larger default;
- large-text mode and high-contrast mode;
- exact wordmark size, lockup and clear space beyond the proposed screen-specific sizes;
- Manrope variable-axis settings for the earlier typography proof;
- whether all screen headers use Benguiat or only the home concepts (D-52);
- ITC Benguiat licensing and embedding for the firmware, not only the host;
- subpixel rendering, gamma correction, hinting and bitmap fallback metrics;
- whether the firmware first frame continues the splash with a stage line or a
  clean content change.

The earlier dark menu mockup uses provisional values (24 px menu row pitch,
10–12 px margins, black/white neutral palette, 20 px wordmark). It illustrates
the type scale, not the selected palette. The concepts in §5 propose the
off-white launcher, the neoclassical index and the album wall, all with the
Benguiat header treatment; none is a settled theme until the owner chooses a
home concept and a panel check records the result. List screens, the main menu
and the theme format remain open.

## 7. Verification

Before firmware UI code depends on this table:

1. Render a 320×240 host mockup with the exact weights and sizes. Done:
   [`../assets/ui/helikon-os-ui-320x240.png`](../assets/ui/helikon-os-ui-320x240.png)
   and
   [`../assets/ui/helikon-os-typography-320x240.png`](../assets/ui/helikon-os-typography-320x240.png).
2. Check every row for clipping, overlap and readable contrast. Done by
   `tools/render-ui-mockup.py`; the menu rows, title, selected row, footer and
   wordmark fit the 320×240 canvas.
3. Check the selected row at 14 px Medium against the 14 px Regular menu. Done
   in the menu mockup.
4. Record font source, licence and subset hash for the Plex/Manrope subsets.
   Done in [`../assets/fonts/README.md`](../assets/fonts/README.md). The
   external Benguiat display face is recorded there separately; it is not
   committed.
5. Render and check the three concepts at 320×240. Done:
   [`../assets/ui/launcher/helikon-os-home-launcher-320x240.png`](../assets/ui/launcher/helikon-os-home-launcher-320x240.png),
   [`../assets/ui/index/helikon-os-home-index-320x240.png`](../assets/ui/index/helikon-os-home-index-320x240.png)
   and
   [`../assets/ui/album-wall/helikon-os-album-wall-320x240.png`](../assets/ui/album-wall/helikon-os-album-wall-320x240.png),
   each with a `@2x` viewing preview. `tools/render-ui-mockup.py` asserts that
   every launcher label and fixed column fits; the frames are checked for
   clipping and overlap on the host. The Benguiat headers come from the
   owner-supplied font via `BENGUIAT_DIR`; no font file is committed. The home
   choice, palette and album-wall density still need owner acceptance.
6. Resolve D-52 (scope and font licence) before firmware headers depend on
   Benguiat; record the fallback if the font cannot ship.
7. Only then implement the renderer and theme (T-042).
8. Compare anti-aliased and 1-bit fallback glyphs on the 6G panel, including the
   11 px descriptions/captions, 13 px launcher labels and 20 px index names.

The mockups are host artifacts, not device runs. A device run must still record
the panel result in `docs/device-matrix.md`.
