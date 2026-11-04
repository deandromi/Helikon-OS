# Bootloader splash assets

> Purpose: source, variants and regeneration of the Helikon-OS bootloader splash.
> Status: Alpha design built into the loader test image; no device run.
> Owner: project owner / boot lead
> Last updated: 2026-11-04
> Source: owner-supplied `bootloader-design.jpg`; D-23/D-24; T-045

## Files

| File | Purpose |
|---|---|
| `bootloader-design.jpg` | Owner-supplied 320×240 source artwork. Do not edit. |
| `helikon-os-splash-alpha-hfs-ok.png` | Exact sRGB mockup, HFS+ label green. |
| `helikon-os-splash-alpha-hfs-fail.png` | Exact sRGB mockup, HFS+ label red. |
| `helikon-os-splash-alpha-hfs-unknown.png` | Exact sRGB mockup, neutral label while probing. |
| `helikon-os-splash-alpha-quantized.png` | 16-colour preview of the device representation. |

Only two areas differ from the source:

1. the `VERSION ...` dots are replaced by `VERSION Alpha`;
2. the `HFS+ DETAILS` ink is recoloured green, red or neutral.

The title, ornament, illustration and off-white background are copied from the
source. The PNG files are lossless sRGB; the illustration is not redrawn or
cropped.

## Device representation

The 6G LCD is RGB565 and the bootloader `MOVE_AREA` is size-limited, so
`bootloader/splash_color.c` stores the base as a 16-colour (4bpp) image
(38,400 bytes) plus a 4bpp alpha mask for `HFS+ DETAILS` (592 bytes). The
illustration is therefore quantised to 16 warm greys on the device; the PNG
mockups preserve the full source colours. The label mask is composited at
runtime in green, red or neutral grey.

## Font

The labels use **ITC Benguiat Std Book** at 9 px. The owner supplied the font
files under `/Users/deandromink/Downloads/ITC Benguiat/`; they are **not
committed**. ITC Benguiat is a Monotype trademark and font software is licensed
separately. Do not add the `.otf` files to this repository or redistribute them.
The owner is responsible for the font licence used for the raster artwork.

The existing `assets/fonts/` IBM Plex/Manrope assets are unrelated.

## Regenerate

From the repository root, with the licensed font available:

```sh
./tools/make-bootloader-splash.py \
  --font '/path/to/BenguiatStd-Book.otf' \
  --font-size 9 \
  --version Alpha \
  --emit-c
```

This rewrites the three PNG variants, the quantized preview, and
`bootloader/splash_color.c/.h`. Change `--version` for a later build. The script
refuses a non-Benguiat font unless `--allow-non-benguiat` is passed for a layout
mockup only.

Source image SHA-256:
`e4fbb980df441db4e754037118c7673c375845ea0536bc1bd137c3eda73ba001`.

## Provenance and limits

- The source artwork is owner-supplied; do not redistribute the images
  separately without owner approval.
- The bootloader splash does not write the filesystem.
- The loader test image shows the neutral label during storage/probe, then green
  when the HFS+ volume and catalog open, or red on an HFS+ probe/catalog error.
- No device-run evidence exists yet; the loader image remains `unproven`.
