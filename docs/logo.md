# Helikon-OS logo — mountain dot-matrix concept

> Purpose: define the provisional Helikon-OS mark and lockup.
> Status: stub — provisional branding, not final
> Owner: project owner
> Last updated: 2026-11-03
> Source: product identity D-17, paper Appendix D; text/dot-matrix only

**This is a design proposal, not a generated image.** The logo is a text dot
matrix, so it can be copied into a terminal, a README, an installer screen or a
build-time asset without a raster image. No image generator was used, and no
photo pixels are included.

## Concept

The primary mark is a **landscape**, not a device portrait. A dot-field mountain
ridge rises from the lower edge; an iPod emerges from behind it. The mountain is
the mask: only the top/mid frame, the entire screen and the top arc of the click
wheel are visible. The lower frame and the rest of the wheel are deliberately
absent.

The screen carries the **lyre-H**: two uprights read as the letter **H**, the top
forms a peak for **Mount Helikon**, and the base/crossbar suggest a lyre and a
waveform. A 5×7 `HELIKON-OS` wordmark sits in the sky at top right.

Three dot weights make the scene readable:

- `●` — lit dots: iPod frame, screen mark and wordmark.
- `○` — hollow dots: the screen bezel, so the screen reads as a separate surface.
- `·` — dim dots: the mountain field below the ridge.

## Primary lockup

Source: [`assets/logo/helikon-os-primary.txt`](../assets/logo/helikon-os-primary.txt)

```text
                               ●   ● ●●●●● ●     ●●●●● ●   ●  ●●●  ●   ●        ●●●   ●●●●
                               ●   ● ●     ●       ●   ●  ●  ●   ● ●●  ●       ●   ● ●
                               ●   ● ●     ●       ●   ● ●   ●   ● ●●  ●       ●   ● ●
                               ●●●●● ●●●●  ●       ●   ●●    ●   ● ● ● ● ●●●●● ●   ●  ●●●
                               ●   ● ●     ●       ●   ● ●   ●   ● ●  ●●       ●   ●     ●
                               ●   ● ●     ●       ●   ●  ●  ●   ● ●  ●●       ●   ●     ●
                               ●   ● ●●●●● ●●●●● ●●●●● ●   ●  ●●●  ●   ●        ●●●  ●●●●

          ●●●●●●●●●●●●●●●●●●●●●●●●
          ●                      ●
          ●○○○○○○○○○○○○○○○○○○○○○○●
          ●○                    ○●
          ●○                    ○●
          ●○         ●          ○●
          ●○        ● ●         ○●
          ●○       ●   ●        ○●             ·
          ●○       ●●●●●        ○●            ·········
          ●○       ●   ●        ○●          ···················
          ●○       ●   ●        ○●        ··························
          ●○        ●●●         ○●   ···································
          ●○                    ○●········································
          ●○○○○○○○○○○○○○○○○○○○○○○●···········································
          ●        ●●●●●●       ···············································
          ●      ●●      ●●    ····················································
          ●    ●●          ●●  ·························································
          ●   ●●         ···································································
···       ●   ●      ·······································································
········  ●  ●●·············································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
····························································································
```

## Composition rule

The mountain ridge is the mask. Draw the iPod only where it is **above** the
ridge:

- Keep the entire screen visible above the ridge.
- Keep the top and side frame visible down to the ridge.
- Keep the top arc of the click wheel visible; stop the arc at the ridge.
- Omit the lower frame, the rest of the wheel and the bottom of the device.
- If the ridge changes, re-mask the iPod. Do not draw a complete iPod on top of
  the mountain and fade it out; the concealment must follow the landform.

## Standalone mark

Source: [`assets/logo/helikon-os-mark.txt`](../assets/logo/helikon-os-mark.txt)

```text
    ●
   ● ●
  ●   ●
 ●     ●
 ●  ●  ●
 ●●●●●●●
 ●     ●
 ●     ●
  ●   ●
   ●●●
```

Use the mark alone when the full landscape would be too small: favicon, app icon,
boot splash, installer badge, or a single-colour stamp.

## Palette

These are proposed, not final. The design is monochrome-first; colour is an
accent, not a requirement.

| Token | Hex | Use |
|---|---|---|
| Ink | `#0B0D10` | Background on light contexts |
| Parchment | `#F2E9D8` | Primary dots on dark backgrounds |
| Brass | `#C8A96B` | Screen mark / accent |
| Slate | `#7A8B99` | Secondary UI accents |

The mark must work in one colour. Do not rely on the brass accent to make the
shape readable.

## Usage rules

- **Primary lockup:** at least 92 dots wide on screen; below that use the
  standalone mark.
- **Standalone mark:** at least 9 dots wide.
- **Clear space:** one click-wheel diameter around the lockup; one dot around
  the mark.
- **Backgrounds:** use parchment dots on ink, or ink dots on parchment. Invert
  the matrix for light backgrounds; do not add a gradient.
- **Scaling:** scale by whole dots only. Do not interpolate, blur or anti-alias
  the matrix into mush.
- **Screen:** the screen contains only the lyre-H mark. Do not add Apple UI,
  menu text, album art or a second logo.
- **Wordmark:** keep `HELIKON-OS` as one unit. Do not abbreviate it to `H-OS` or
  add `Rockbox`/`Rockpod` to the lockup. The wordmark may be removed for a
  scene-only mark; keep the top-right sky clear if it is.

## Don'ts

- Do not use the Apple logo, Apple wordmark or an exact Apple product outline.
- Do not add a photo-realistic screen, reflections, shadows or gradients.
- Do not stretch the device or change the screen/wheel proportions.
- Do not use low-contrast dot colours or a dotted background behind the dots.
- Do not draw the iPod below the mountain ridge or "fade" the lower body; the
  mountain must conceal it.
- Do not put the mark inside another badge, circle or rounded square in the
  primary lockup; the landscape is the container.
- Do not claim Apple affiliation, certification or endorsement.

## Reference note

The composition is inspired by the attached **Mount Helicon (1,749 m) at
sunset, Central Greece** reference. The photo is not copied into this repository;
the dot matrix approximates the ridge silhouette only. The iPod is an abstract
Classic-like device, not an Apple product image.

## Open questions

- **D-17:** trademark/domain/package clearance for the Helikon-OS name is still
  open. This logo is provisional until that is recorded.
- Final palette and whether brass is the primary accent.
- Whether the wordmark stays in the lockup or becomes a separate wordmark asset.
- Whether a phosphor-green terminal variant is wanted.
- Whether the ridge should be simplified further for very small sizes.

## Files

- [`assets/logo/helikon-os-primary.txt`](../assets/logo/helikon-os-primary.txt)
  — mountain lockup.
- [`assets/logo/helikon-os-mark.txt`](../assets/logo/helikon-os-mark.txt)
  — standalone lyre-H mark.
- No PNG, SVG, icon-font or generated image is part of this proposal.
