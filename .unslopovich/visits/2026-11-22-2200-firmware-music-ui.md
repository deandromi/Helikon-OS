# Dr. Unslopovich — firmware Music flow, follow-up

```text
Verdict: The hub was reading a 12:34 that never ticks off a battery nothing measures,
         and half the flow was still set in the diagnostic face, so the paper-and-ink
         typography stopped at whichever screen was migrated last. Both are gone.
Stack: freestanding C99 firmware for ARM7TDMI (S5L8702), 320x240 grayscale framebuffer,
       RGB565; renders are host builds of the same firmware/ui sources the device runs.
Reviewed: Music hub, album wall, Tracks list, artists list, artist page (overview,
          albums, tracks, empty), album page (playing, play-all, empty, corrupt, no-art),
          Now Playing (opening, playing, finished, error), the 1-bit fallback render.
          Rendered from code at 320x240; no device, no click wheel.
Before -> after: 17 -> 20 / 24 (same six categories, 4 each)
```

| Category | Before | After | Why |
|---|---|---|---|
| Task path | 3 | 3 | Unchanged: hub → wall → album → Now Playing is direct; the artist tab row is still a gesture nothing announces (owner, recorded) |
| Truth | 3 | 4 | The hub's baked `12:34` and full battery are masked and replaced by the honest outline; the wall header's magnifier is gone because there is no search; every number left is real |
| Native fit | 3 | 3 | One footer grammar now, and it is the atlas on every screen; the wheel grammar did not change |
| States | 3 | 3 | Empty, corrupt, unreadable, opening, finished and error states all still reachable and honest |
| Access | 2 | 3 | The smallest text used to be 5 px tall solid glyphs in the same band as 11 px anti-aliased text; a footer is now 11 px Plex at 5.00:1 everywhere |
| Specificity | 3 | 4 | No screen is set in the diagnostic face any more: the type is the product's own from the hub to the error page |

## What was wrong

- `firmware/ui/music.c:718` (hub) blitted `helikon_launcher_background` as its
  masthead. That tile is owner artwork and carries a mockup clock and a full
  battery, so the hub displayed a time that cannot pass and a charge level
  nothing reads. #62 removed the same claim from the drawn top bar and the tile
  walked around it; the pixel probe that should have caught it was pointed at
  x 150–200, which is not the slot.
- `firmware/ui/music.c` drew its footers, wall captions, wall counts and the
  entire Tracks list with `helikon_screen_text`, the 3×5 diagnostic face, while
  the artists list, album pages and Now Playing used the Plex atlas. Two
  products, one footer band.
- The generated wall headers baked a magnifier at x 280–291 from the mockup. The
  firmware has no search: SELECT opens the row, the wheel scrolls.

## What changed

- `mask_baked_hub_clock()` clears the tile's corner and `draw_battery()` draws
  the same outline the other screens use. The launcher keeps its own artwork
  untouched (T-129 asks the owner about that one).
- `draw_text`/`draw_text_right` now place a baseline inside the band the caller
  already used and give the atlas a pixel limit derived from the old character
  budget, so no layout moved. Every direct `helikon_screen_text` call in
  `music.c` is gone; the `·` in "Mixtape · Various Artists" survives because the
  text now reaches the atlas as UTF-8 instead of being flattened to ASCII.
- `tools/make-launcher-assets.py` no longer draws the search mark into the wall
  strips, and the two committed strips were cleared to match.
- `tests/ui/test_music.c` now probes the hub's actual corner (no ink but the
  outline, interior not filled, outline present), the wall's corner, and the
  colour count of the footer and caption bands — anti-aliased atlas text has 16
  and 17 distinct colours where the 3×5 face has exactly two, so the typography
  cannot quietly regress.

## Ask first

- `firmware/ui/music.c:718` launcher masthead still carries the mockup clock and
  full battery. It is the owner's artwork, so it stays until the owner decides
  (T-129): regenerate the tile without them, or record the exception in the
  chart.
- `firmware/ui/music.c` "NO CREDITS LISTED" fills the album's metadata band when
  no track carries a composer. It is true, and HDB v1 has the field, so it was
  left alone. Removing the line instead of stating a negative is a one-line
  change if the owner wants the band silent.
- `firmware/ui/music.c` `▶ PLAYING` in the Now Playing state line is still a
  transport glyph on a device with a play button. Cosmetic, previously recorded,
  still undecided.

## Worth keeping

- The paper-and-ink palette, the Benguiat wall headers and the rule-and-diamond
  folio: the hub is recognisably this product and not a list template.
- The empty, corrupt and unreadable states say what is missing and where
  ("1 TRACK UNREADABLE", "TRACK DATA MISSING") instead of "Oops".
- The time slot stays empty when the atlas is healthy and says `FONT FALLBACK`
  when it is not: the one thing that belongs in a clock's place.

## Not checked

- No device, no click wheel, no panel: contrast is measured on RGB565, not on
  glass, and the renders are 1:1 host builds.
- Performance is not profiled. The change removes per-glyph `fill_rect` calls
  from four screens and adds one 71×17 fill on the hub, but that is an argument,
  not a measurement.
- The detector (`scripts/detect.js`) reads web and mobile sources; it scanned no
  files here. The C was reviewed by hand.
- The companion and the boot splash were not revisited in this pass; their last
  visit is 2026-11-21-1605.
