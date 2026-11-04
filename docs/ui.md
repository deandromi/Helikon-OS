# UI design — default theme and typography

> Purpose: the first-party Helikon-OS UI look and the default typography scale.
> Status: living document; typography owner-settled, font subsets and host mockup implemented, renderer not started.
> Owner: project owner; UI lead
> Last updated: 2026-11-04
> Source: owner-supplied typography table, 2026-11-03; design paper §14,
> §28.3.4; D-10, D-15, D-23

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

## 5. Not specified yet

These are open questions, not implied values. Do not invent and record them as
settled:

- line height and baseline spacing;
- colour palette, contrast ratios and selected-item background;
- margins, list row height, separators and status-bar layout;
- whether 11 px is comfortable on the 6G panel or needs a larger default;
- large-text mode and high-contrast mode;
- exact logo size, lockup and clear space;
- Manrope variable-axis settings beyond Bold;
- subpixel rendering, gamma correction, hinting and bitmap fallback metrics;
- whether the firmware first frame continues the splash with a stage line or a
  clean content change.

The host mockup uses provisional values (24 px menu row pitch, 10–12 px margins,
black/white neutral palette, 20 px wordmark). They illustrate the type scale and
are not settled layout or colour decisions.

## 6. Verification

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
4. Record font source, licence and subset hash. Done in
   [`../assets/fonts/README.md`](../assets/fonts/README.md).
5. Only then implement the renderer and theme (T-042).
6. Compare anti-aliased and 1-bit fallback glyphs on the 6G panel.

The mockup is a host artifact, not a device run. A device run must still record
the panel result in `docs/device-matrix.md`.
