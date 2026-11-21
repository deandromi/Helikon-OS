# Chart

Stack: freestanding C99 firmware for ARM7TDMI (S5L8702), custom 1/2/4-bpp framebuffer
UI drawn from `firmware/ui/`; SwiftUI companion for macOS 13+ in
`tools/desktop/Sources/HelikonCompanion/`; HTML/CSS docs site and brand assets.
Platforms: iPod Classic 6G (fixed 320x240 grayscale TFT, click wheel), macOS desktop,
docs/brand pages in a browser. No tablet, no phone, no dark-mode variant.
Visits: on

## Top tasks
1. Pick something to play: Music hub -> Artists/Albums -> Artist -> Album -> Now Playing
   (`firmware/ui/music.c`).
2. Understand what the player is doing: Now Playing state line, progress, error screen.
3. Check and move files from a Mac: companion shell (`RootView.swift`), then unplug and
   trust that nothing was overwritten.

## Users and context
One owner in a pre-release alpha, often at a desk with the iPod docked, one thumb on a
click wheel when walking. No touch, no keyboard, no text scaling, no network on device.
Everything on screen is read at arm's length on a 2.5-inch panel.

## Brand
- Color: paper `#EFEFE6` / ink `#23221E` (firmware RGB565 `0xef7c` / `0x2103`),
  companion `#F3F0E7` / `#23221E`; the palette and its contrast floors are set by
  `docs/decisions.md` D-23 and the UI paper.
- Type: IBM Plex Sans for UI text, Manrope Bold for the logo, Benguiat-style display
  face for the boot wordmark only (D-23; Benguiat embedding licence still a gate).
- Keep: grayscale, anti-aliased text with a 1-bit fallback (D-24); the paper-and-ink
  look is the product, not a placeholder; the ruler-line footer and all-caps labels.

## Accepted
- 11 px all-caps text with wide tracking on a fixed 320x240 panel: there is no
  scaling path in the design; the panel is the constraint (owner, 2026-11-21).
- `MUSIC_MUTED` at 5.00:1 and `MUSIC_RULE` at 1.91:1: rules are decoration, not text;
  muted is above the 4.5:1 text floor (owner, 2026-11-21).
- No dark mode, no text-size setting, no reduced-motion query on device: no such
  setting exists in the firmware and none is planned for 6G (owner, 2026-11-21).
- Boot splash artwork comes from the owner as a 4bpp mask; the status words are baked
  into the mask and only the label colour is composited (`assets/bootloader/README.md`).
  Do not redraw the artwork without the owner's PNGs (owner, 2026-11-21).
- Artist and album pages have no footer hint row: the bands are full and the wheel
  grammar is taught on the list pages (owner, 2026-11-21).
- `SORT: NAME` on list headers: inert, truthful metadata the owner wants to keep even
  though the wheel cannot change it yet (owner, 2026-11-21).
- Companion window is light-only: the palette has no dark variant, so the window keeps
  `preferredColorScheme(.light)` while the app no longer overrides the system
  appearance for sheets and menus (owner, 2026-11-21).
- The artist tab row is a focus stop entered by scrolling up past the first body item,
  with no footer hint row. Accepted for now because the body bands are full; revisit if
  a device run shows the panes are unfindable (owner, 2026-11-21).
- Boot splash label colours are deep green `#007A3D` and deep red `#C81E1E` rather than
  the owner artwork's `#00BF63` / `#FE3131`, so the 9 px label clears 4.5:1. The mask and
  illustration are untouched (owner, 2026-11-21).

## Off limits
- The root `README.md` and `docs/design-paper.md`: owner-authored product intent.
- Album wall, launcher and boot screens: the owner supplies the artwork.
- Any write path, the transaction engine, and Disk Mode copy semantics: fixed by
  `docs/invariants.md`, not a UI decision.
