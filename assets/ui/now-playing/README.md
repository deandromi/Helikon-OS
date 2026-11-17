# Now Playing mockups

> Purpose: host-rendered 320×240 states for the cleaned Now Playing surface.
> Status: binding design mockups for `docs/ui.md` §6.7–§6.10; host only, no 6G
> panel run.
> Owner: UI lead
> Last updated: 2026-11-08
> Source: owner Music-flow brief, 2026-11-08; D-23, D-24; `main` `2420c64`;
> `firmware/player/wav.h`; `firmware/hal/6g/audio.h`;
> [`../../../docs/ui.md`](../../../docs/ui.md#6-music-flow--artists-artist-album-and-now-playing-binding-design)

## Design rules

- IBM Plex Sans only; 17/600 title, 12/400 artist/album, 14/500 state, 11/400
  metadata. 11 px floor; no 3×5 fallback; mixed case except state labels.
- Paper/ink palette; error red `#FE3131` is a 2 px rule only, never text. No
  green, cards, shadows, gradients, sidebar or player bar.
- 96×96 cover or generated paper-light placeholder; 96×16 twelve-bar
  sample-peak visualizer; 288×4 progress bar.
- Sound quality is honest: `SRC WAV · PCM · 16-BIT · 44.1 kHz` and
  `1411 kbps · OUT 44.1 kHz · STEREO`; unknown fields show `—`.
- Controls shown are real: PLAY pause/resume/replay, MENU back, SELECT album
  when resolvable. No shuffle, repeat, queue, volume, seek or next/previous.
- `OPENING` shows `PLEASE WAIT`; it does not claim cancel while the current
  player open is synchronous. Error reasons use the existing `AUDIO` stage
  labels, including `AUDIO UNDERRUN`.
- Binding/proposed split and the full design-gate checklist are in
  `docs/ui.md` §6.1 and §6.11; every checklist box is binding before merge.

## Files

All are 320×240 RGB; `@2x` is a nearest-neighbour viewing preview.

| State | 1× | 2× |
|---|---|---|
| Playing | `helikon-os-now-playing-playing-320x240.png` | `helikon-os-now-playing-playing-320x240@2x.png` |
| Paused | `helikon-os-now-playing-paused-320x240.png` | `helikon-os-now-playing-paused-320x240@2x.png` |
| Opening | `helikon-os-now-playing-opening-320x240.png` | `helikon-os-now-playing-opening-320x240@2x.png` |
| Finished | `helikon-os-now-playing-finished-320x240.png` | `helikon-os-now-playing-finished-320x240@2x.png` |
| Error, retryable | `helikon-os-now-playing-error-retry-320x240.png` | `helikon-os-now-playing-error-retry-320x240@2x.png` |
| Error, terminal | `helikon-os-now-playing-error-terminal-320x240.png` | `helikon-os-now-playing-error-terminal-320x240@2x.png` |
| Missing art | `helikon-os-now-playing-no-art-320x240.png` | `helikon-os-now-playing-no-art-320x240@2x.png` |
| Long metadata | `helikon-os-now-playing-long-text-320x240.png` | `helikon-os-now-playing-long-text-320x240@2x.png` |

## Provenance

The cover and no-art mark are project-owned generated line art. Track, artist
and album names are sample content. No real album artwork or copied layout is
included. No Benguiat font is used or required; the Plex subset and licence are
in [`../../fonts/README.md`](../../fonts/README.md).

## Regenerate and check

```sh
python3 tools/render-ui-mockup.py --only music-flow
```

This writes the eight states at 1× and 2× and
[`clipping-check.txt`](clipping-check.txt). The report records every text box
and frame inside its column/canvas. The visualizer bars are a host drawing;
the CPU/RAM/frame budgets in `docs/ui.md` §6.11 are targets, not measurements.
The report is host geometry only: not a 6G panel, contrast, frame-time or
playback claim.
