# UI mockups

> Purpose: host-rendered 320×240 typography mockups for the first-party UI.
> Status: host artifacts; no firmware UI or device panel run.
> Owner: UI lead
> Last updated: 2026-11-04

| File | Content |
|---|---|
| `helikon-os-ui-320x240.png` | Main-menu mockup: Manrope wordmark, 17 px title, 14 px Regular menu, 14 px Medium selected row, 12 px artist/album, 11 px system text |
| `helikon-os-typography-320x240.png` | Typography proof: all sizes/weights, selected-row contrast and wordmark |

Both are 320×240 8-bit grayscale with anti-aliased text. The dark background and
white selected row are a neutral mockup, not a settled palette (see
[`../../docs/ui.md`](../../docs/ui.md) §5).

Regenerate:

```sh
python3 tools/render-ui-mockup.py
```

The script asserts that text fits the canvas and its column. It uses the
licensed subsets in [`../fonts/`](../fonts/README.md); no proprietary font or
artwork is included. Renderer integration remains T-042; the bootloader splash is
T-045.
