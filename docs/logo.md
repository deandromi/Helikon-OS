# Helikon-OS logo — iPod overlay on the mountain

> Purpose: define the provisional Helikon-OS iPod overlay and H/OS mark.
> Status: stub — provisional branding, not final
> Owner: project owner
> Last updated: 2026-11-03
> Source: product identity D-17, paper Appendix D; text/dot-matrix only

**This is an overlay, not a generated image.** The mountain photo stays original.
Only the iPod is dot-matrixed. The overlay is transparent everywhere except the
iPod, so it can be placed over the reference photo without redrawing the
landscape. No image generator was used, and no photo pixels are included.

## Preview

Open [`preview.html`](../assets/logo/preview.html) in a browser and choose your
local mountain photo. The page loads the image locally, draws the overlay on a
canvas, and never uploads or saves the image. The preview is a design tool, not a
generated image asset.

## Concept

The iPod is a rounded Classic-like device emerging from the mountain ridge. The
mountain is the mask: only the top/mid frame, the entire screen and the top arc
of the click wheel are drawn. The lower frame and the rest of the wheel are
absent, not faded.

The screen carries the stacked **H / OS** mark:

- a 5×5 `H` on top;
- a one-row gap;
- `OS` in 4×5 dot letters underneath.

There is no separate `HELIKON-OS` wordmark in the overlay. The identity is the
H/OS screen mark plus the mountain composition. The overlay uses only `●` for
lit iPod dots and `○` for the screen bezel; the mountain is never dotted.

## iPod overlay

Source: [`assets/logo/helikon-os-ipod-overlay.txt`](../assets/logo/helikon-os-ipod-overlay.txt)

```text
   ●●●●●●●●●●●●●●●●●●●●
  ●●                  ●●
 ●●○○○○○○○○○○○○○○○○○○○○●●
 ● ○      ●   ●       ○ ●
 ● ○      ●   ●       ○ ●
 ● ○      ●●●●●       ○ ●
 ● ○      ●   ●       ○ ●
 ● ○      ●   ●       ○ ●
 ● ○                  ○ ●
 ● ○    ●●●● ●●●●     ○ ●
 ● ○    ●  ● ●        ○ ●
 ● ○    ●  ● ●●●      ○ ●
 ● ○    ●  ●    ●     ○ ●
 ● ○    ●●●● ●●●●     ○ ●
 ● ○○○○○○○○○○○○○○○○○○○○ ●
 ●        ●●●●●●
 ●      ●●      ●●
 ●    ●●          ●●
 ●   ●●
 ●   ●
```

## Placement

The overlay is a 26×20-cell crop of a 92×52 placement grid. It is pre-masked to
the ridge; do not add mountain dots. If you move it, re-mask the lower body and
wheel to the new ridge.

For the 736×413 reference photo at 8 pixels per cell:

| Element | Grid cells | Pixel position |
|---|---|---|
| Overlay crop origin | x=8, y=9 | (64, 72) |
| Body top | y=9 | y=72 |
| Screen | x=11..30, y=11..23 | x=88..240, y=88..184 |
| Wheel top | y=24 | y=192 |
| Mountain ridge | photo-dependent | not drawn |

The screen is intentionally kept fully visible even where the ridge crosses
close to its lower edge. The body sides and wheel stop at the ridge; the lower
device is concealed by the original mountain.

## Standalone H/OS mark

Source: [`assets/logo/helikon-os-mark.txt`](../assets/logo/helikon-os-mark.txt)

```text
  ●   ●
  ●   ●
  ●●●●●
  ●   ●
  ●   ●

●●●● ●●●●
●  ● ●
●  ● ●●●
●  ●    ●
●●●● ●●●●
```

Use the mark where the full overlay would be too small: favicon, app icon, boot
splash, installer badge or a single-colour stamp.

## Palette

These are proposed, not final. The design is monochrome-first; colour is an
accent, not a requirement.

| Token | Hex | Use |
|---|---|---|
| Ink | `#0B0D10` | Background on light contexts |
| Parchment | `#F2E9D8` | Primary dots on dark backgrounds |
| Brass | `#C8A96B` | Screen/H-OS accent |
| Slate | `#7A8B99` | Secondary UI accents |

The mark must work in one colour. Do not rely on the brass accent to make the
shape readable.

## Usage rules

- **Overlay:** place at the documented crop origin; keep the photo's ridge as the
  mask. Do not draw the lower iPod.
- **Standalone mark:** at least 9 dots wide.
- **Clear space:** one click-wheel diameter around the overlay; one dot around
  the mark.
- **Backgrounds:** use parchment dots on a dark photo, or ink dots on a light
  photo. Invert the dots, not the mountain.
- **Scaling:** scale by whole dots only. Do not interpolate or blur the matrix.
- **Screen:** the screen contains only `H` over `OS`. Do not add Apple UI, menu
  text, album art or a second logo.
- **Wordmark:** the H/OS mark is the identity. Do not add `Rockbox`/`Rockpod` or a
  second wordmark inside the overlay.

## Don'ts

- Do not dot the mountain; it stays the original photo.
- Do not draw a complete iPod and fade the bottom out; omit the concealed parts.
- Do not use the Apple logo, Apple wordmark or an exact Apple product outline.
- Do not square off the iPod corners; the body is rounded.
- Do not add gradients, reflections, shadows or a photo-realistic screen.
- Do not stretch the device or change the screen/wheel proportions.
- Do not claim Apple affiliation, certification or endorsement.

## Reference note

The composition is inspired by the attached **Mount Helicon (1,749 m) at
sunset, Central Greece** reference. The photo is not copied into this repository;
the overlay uses the ridge silhouette for masking only. The iPod is an abstract
Classic-like device, not an Apple product image.

## Open questions

- **D-17:** trademark/domain/package clearance for the Helikon-OS name is still
  open. This logo is provisional until that is recorded.
- Final palette and whether brass is the primary accent.
- Whether the H/OS mark should be optically larger inside the screen.
- Whether a separate horizontal `HELIKON-OS` wordmark is wanted outside the
  overlay.
- Whether the ridge mask should be simplified for very small sizes.

## Files

- [`assets/logo/helikon-os-ipod-overlay.txt`](../assets/logo/helikon-os-ipod-overlay.txt)
  — transparent iPod overlay, pre-masked to the ridge.
- [`assets/logo/helikon-os-mark.txt`](../assets/logo/helikon-os-mark.txt)
  — standalone H/OS mark.
- [`assets/logo/preview.html`](../assets/logo/preview.html) — local HTML preview;
  choose the photo, no upload, no image generation.
- No PNG, SVG, icon-font or generated image is part of this proposal.
