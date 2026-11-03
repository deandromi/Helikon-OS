# Helikon-OS logo — dot-matrix concept

> Purpose: define the provisional Helikon-OS mark and lockup.
> Status: stub — provisional branding, not final
> Owner: project owner
> Last updated: 2026-11-03
> Source: product identity D-17, paper Appendix D; text/dot-matrix only

**This is a design proposal, not a generated image.** The logo is a text dot
matrix, so it can be copied into a terminal, a README, an installer screen or a
build-time asset without a raster image. No image generator was used.

## Concept

The mark is a **lyre-H**: two uprights read as the letter **H**, the top forms a
mountain peak for **Mount Helikon**, and the curved base and centre crossbar
suggest a lyre and a waveform. It is deliberately simple enough to survive a
9×10 dot grid.

The primary lockup places that mark on the screen of an abstract iPod Classic
silhouette. The device has a 4:3 screen, a click wheel, a hold switch and a dock
connector, but **no Apple logo, wordmark or trade dress**. The wordmark
`HELIKON-OS` is set in a 5×7 dot font below the device.

## Primary lockup

Source: [`assets/logo/helikon-os-primary.txt`](../assets/logo/helikon-os-primary.txt)

```text
                                    ●●●●
                     ●●●●●●●●●●●●●●●●●●●●●●
                   ●●●                    ●●●
                   ●                        ●
                  ●●                        ●●
                  ●    ●●●●●●●●●●●●●●●●●●    ●
                  ●   ●●                ●●   ●
                  ●   ●                  ●   ●
                  ●   ●        ●         ●   ●
                  ●   ●       ● ●        ●   ●
                  ●   ●      ●   ●       ●   ●
                  ●   ●     ●     ●      ●   ●
                  ●   ●     ●  ●  ●      ●   ●
                  ●   ●     ●●●●●●●      ●   ●
                  ●   ●     ●     ●      ●   ●
                  ●   ●     ●     ●      ●   ●
                  ●   ●      ●   ●       ●   ●
                  ●   ●       ●●●        ●   ●
                  ●   ●                  ●   ●
                  ●   ●                  ●   ●
                  ●   ●●                ●●   ●
                  ●    ●●●●●●●●●●●●●●●●●●    ●
                  ●                          ●
                  ●                          ●
                  ●                          ●
                  ●                          ●
                  ●                          ●
                  ●                          ●
                  ●                          ●
                  ●        ●●●●●●●●●●        ●
                  ●       ●●        ●●       ●
                  ●     ●●            ●●     ●
                  ●    ●●              ●●    ●
                  ●    ●                ●    ●
                  ●   ●                  ●   ●
                  ●   ●         ●        ●   ●
                  ●  ●●      ●●●●●●      ●●  ●
                  ●  ●      ●●    ●●      ●  ●
                  ●  ●      ●      ●      ●  ●
                  ●  ●      ●      ●      ●  ●
                  ●  ●      ●      ●      ●  ●
                  ●  ●      ●●    ●●      ●  ●
                  ●  ●●      ●●●●●●      ●●  ●
                  ●   ●         ●        ●   ●
                  ●   ●                  ●   ●
                  ●    ●                ●    ●
                  ●    ●●              ●●    ●
                  ●     ●●            ●●     ●
                  ●       ●●        ●●       ●
                  ●        ●●●●●●●●●●        ●
                  ●                          ●
                  ●                          ●
                  ●●                        ●●
                   ●                        ●
                   ●●●                    ●●●
                     ●●●●●●●●●●●●●●●●●●●●●●
                            ●●●●●●●●



  ●   ● ●●●●● ●     ●●●●● ●   ●  ●●●  ●   ●        ●●●   ●●●●
  ●   ● ●     ●       ●   ●  ●  ●   ● ●●  ●       ●   ● ●
  ●   ● ●     ●       ●   ● ●   ●   ● ●●  ●       ●   ● ●
  ●●●●● ●●●●  ●       ●   ●●    ●   ● ● ● ● ●●●●● ●   ●  ●●●
  ●   ● ●     ●       ●   ● ●   ●   ● ●  ●●       ●   ●     ●
  ●   ● ●     ●       ●   ●  ●  ●   ● ●  ●●       ●   ●     ●
  ●   ● ●●●●● ●●●●● ●●●●● ●   ●  ●●●  ●   ●        ●●●  ●●●●
```

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

Use the mark alone when the full lockup would be too small: favicon, app icon,
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

- **Primary lockup:** at least 64 dots wide on screen; below that use the
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
  add `Rockbox`/`Rockpod` to the lockup.

## Don'ts

- Do not use the Apple logo, Apple wordmark or an exact Apple product outline.
- Do not add a photo-realistic screen, reflections, shadows or gradients.
- Do not stretch the device or change the screen/wheel proportions.
- Do not use low-contrast dot colours or a dotted background behind the dots.
- Do not put the mark inside another badge, circle or rounded square in the
  primary lockup; the iPod body is the container.
- Do not claim Apple affiliation, certification or endorsement.

## Open questions

- **D-17:** trademark/domain/package clearance for the Helikon-OS name is still
  open. This logo is provisional until that is recorded.
- Final palette and whether brass is the primary accent.
- Whether the wordmark stays in the lockup or becomes a separate wordmark asset.
- Whether a phosphor-green terminal variant is wanted.
- Whether a 7G/port descriptor is ever added (recommended: no, keep one mark).

## Files

- [`assets/logo/helikon-os-primary.txt`](../assets/logo/helikon-os-primary.txt)
  — primary lockup.
- [`assets/logo/helikon-os-mark.txt`](../assets/logo/helikon-os-mark.txt)
  — standalone lyre-H mark.
- No PNG, SVG, icon-font or generated image is part of this proposal.
