# Now Playing mockups

> Purpose: host-rendered 320×240 design frames for the Helikon-OS Now Playing
> screen.
> Status: binding design specification with host mockups; no firmware screen,
> no 6G panel run and no playback claim.
> Owner: UI lead
> Last updated: 2026-11-07
> Specification: [`docs/ui.md`](../../../docs/ui.md) §7
> Base: `main` at `cf78410` (first PCM/WAV playback path)

## Files

| File | Content |
|---|---|
| `helikon-os-now-playing-playing-320x240.png` | Playing: sleeve, title, artist, album, progress, elapsed/total, state, context and footer |
| `helikon-os-now-playing-paused-320x240.png` | Paused: frozen fill and time, pause bars, `PLAY RESUME` |
| `helikon-os-now-playing-opening-320x240.png` | Opening: empty progress, `OPENING`, `MENU CANCEL` |
| `helikon-os-now-playing-error-retry-320x240.png` | Retryable error: `AUDIO PLAYBACK FAILED`, reason, guidance, `SELECT RETRY` |
| `helikon-os-now-playing-error-terminal-320x240.png` | Terminal error: `UNSUPPORTED WAV FORMAT`, `SELECT ALBUM` |
| `helikon-os-now-playing-finished-320x240.png` | Finished: full progress, `PLAY AGAIN` |
| `helikon-os-now-playing-missing-art-320x240.png` | Missing-art placeholder; playback is unaffected |
| `helikon-os-now-playing-long-metadata-320x240.png` | Three-line title plus ellipsis on artist and album |
| `*@2x.png` | Nearest-neighbour 640×480 viewing previews; not device assets |
| `clipping-check.txt` | Recorded output of the Now Playing clipping/geometry check |

Each 1× PNG is exactly 320×240 RGB. Each 2× preview is exactly 640×480 and is
scaled with nearest-neighbour sampling, not smoothing.

## Provenance

- Renderer: [`tools/render-ui-mockup.py`](../../../tools/render-ui-mockup.py),
  function `now_playing_mockups()`. No other drawing tool was used.
- Typeface: IBM Plex Sans from the project’s licensed Latin-1 subset
  [`assets/fonts/HelikonUI-Sans.ttf`](../../fonts/HelikonUI-Sans.ttf), weights
  400/500/600. The OFL text and source record are in
  [`assets/fonts/README.md`](../../fonts/README.md).
- No ITC Benguiat. Now Playing uses Plex only, so the proposed D-52 exception
  and its uncommitted proprietary font are not dependencies.
- Sleeve: `album_sleeve(96)` in the renderer, generated terracotta and cream
  line geometry. It is not real album artwork and is not copied from a release.
- Missing art: `sleeve_placeholder(96)`, generated paper-light geometry with a
  muted mountain-and-sun mark. It is not a broken-image icon.
- All names, durations, track counts, times and the battery level are fictional
  sample content: `Nocturne I`, `Meridian Ensemble`, `Nocturnes`, `1:42 / 3:58`,
  `12:34`, 70%. They are not device data, a measurement or a claim about a
  library.
- The mockups are host artifacts. No 6G panel, audio path, player, VFS or
  HelikonDB reader was exercised.

## Regenerate

```sh
python3 tools/render-ui-mockup.py --now-playing-only
```

This renders the eight 1× states, writes the eight 2× previews, and prints the
clipping report. It does not need `BENGUIAT_DIR`; the older concept screens are
skipped. To regenerate every UI asset including the Benguiat concept screens:

```sh
BENGUIAT_DIR="/path/to/ITC Benguiat" python3 tools/render-ui-mockup.py
```

## What the clipping check covers

`clipping-check.txt` records the output of the command above. It checks:

- every text box against the 320×240 canvas and its declared column width;
- the top label/time collision and the battery glyph bounds;
- title wrapping to at most three lines, with U+2026 when cut;
- artist and album one-line ellipsis;
- every footer variant, including single-track, no-album, opening, finished and
  both error paths;
- sleeve, rules, progress track and progress fill bounds.

It does **not** prove 6G panel contrast, anti-aliased glyph quality, 1-bit
fallback readability, frame time or memory use.

## State and provenance limits

- The specification in [`docs/ui.md`](../../../docs/ui.md) §7 is binding for the
  implementation PR. This README records the mockup provenance, not a second
  specification.
- The error reason strings are illustrative. F5 owns the final retryable and
  terminal error taxonomy; the design fixes only the one-line reason slot.
- Background playback across MENU is a design requirement, not an implemented
  result.
- The current firmware progress view in `firmware/ui/music.c` uses 3×5 text and
  is not this design. Do not cite it as evidence for this screen.
- No shuffle, repeat, queue, volume overlay or visualizer is implied.
- No device support, playback, audio-output or frame-time claim is made.
