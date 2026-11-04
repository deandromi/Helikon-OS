# Bootloader splash assets

> Purpose: source, variants and regeneration of the Helikon-OS bootloader splash.
> Status: final Alpha green/red artwork built into the loader image and
> device-checked; 6.5 s startup and clean `fsck_hfs -fn`.
> Owner: project owner / boot lead
> Last updated: 2026-11-04
> Source: owner final `Bootloader design Version Alpha Green/Red.png`; T-045

## Files

| File | Purpose |
|---|---|
| `helikon-os-splash-alpha-hfs-ok.png` | Exact owner green PNG; HFS+ label green. |
| `helikon-os-splash-alpha-hfs-fail.png` | Exact owner red PNG; HFS+ label red. |
| `helikon-os-splash-alpha-hfs-unknown.png` | Neutral label while probing. |
| `helikon-os-splash-alpha-quantized.png` | 16-colour preview of the device representation. |
| `bootloader-design.jpg` | Earlier design source; provenance only, not the final source. |

The version line is **`VERSION ALPHA`** (uppercase) in the owner artwork. The
illustration, title, ornament and off-white background are the owner PNGs; only
the HFS+ label is composited at runtime.

## Device representation

The 6G LCD is RGB565 and the bootloader `MOVE_AREA` is size-limited, so
`bootloader/splash_color.c` stores the base as a 16-colour 4bpp image
(38,400 bytes) plus a 4bpp alpha mask for `HFS+ DETAILS` (324 bytes). The label
is drawn at runtime in:

- green `#00BF63` (`0, 191, 99`);
- red `#FE3131` (`254, 49, 49`);
- neutral grey `#6B665C` (`107, 102, 92`).

The off-white base is `#EFEDE5` (`239, 237, 229`). The PNGs preserve the full
source colours; the device uses 16 warm greys because of the LCD/framebuffer
limit.

## Font

The labels use **ITC Benguiat Std** in the owner PNGs. The font files were
supplied by the owner and are **not committed**. ITC Benguiat is a Monotype
trademark and font software is licensed separately. Do not add the `.otf` files
to this repository or redistribute them.

## Regenerate

From the repository root, with the two owner PNGs available:

```sh
./tools/make-final-bootloader-splash.py \
  --green '/path/to/Bootloader design Version Alpha Green.png' \
  --red '/path/to/Bootloader design Version Alpha Red.png' \
  --emit-c
```

This rewrites the three PNG variants, the quantized preview and
`bootloader/splash_color.c/.h`. The red/green pair is verified to differ only in
the HFS+ label before the mask is generated.

The earlier `tools/make-bootloader-splash.py` remains for the pre-final design
and font mockups; it is not used for the final artwork.

## Provenance and limits

- The source artwork is owner-supplied; do not redistribute the images
  separately without owner approval.
- The bootloader splash does not write the filesystem.
- The loader image shows the neutral label during probe, then green when the
  HFS+ volume/catalog open, or red on an HFS+ error.
- The final-splash `6bc6927c…` image passed its device check with 6.5 s startup
  and a clean `fsck_hfs -fn`. One 6G/iFlash SD only.
- No firmware boot or support claim follows from the splash.
