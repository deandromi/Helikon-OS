# Diagnostic bootloader

> Purpose: read-only HFS+ diagnostics and the 320×240 splash for the first
> bootloader screen.
> Status: **diagnostic image built and unproven; loader test image built and
> unproven.** Not a release and not a firmware loader.
> Owner: boot lead
> Last updated: 2026-11-04
> Source: design paper §5, §24, §27; `docs/state.md`; D-21

## Firmware-loading loader (host core)

The production firmware-loading bootloader is separate from this read-only
diagnostic image. The host core now implements:

- `sha256.c` — image hashing;
- `manifest.c` — manifest parsing and split verification; the signature is
  checked before any image byte is read;
- `bootstate.c` — two-slot boot-state selection;
- `ed25519.c` — verification-only TweetNaCl 20140427 derivative, CC0, no key
  generation/signing/random source (RFC 8032 tested; see `NOTICE`);
- `keys.c` — public-key table and signature-hook adapter;
- `firmware_loader.c` — read-only manifest/image/bootstate loader;
- `hfs_loader.c` — HFS+ file-read adapter.

Formats and paths are fixed in [`LOADER.md`](LOADER.md). The host tests, the
macOS HFS+ fixture and the exact-artifact check pass. A separate DFU-only
[`LOADER_TEST.md`](LOADER_TEST.md) image builds and executes a signed test
payload; production handoff is disabled. No firmware image exists and no device
run is recorded, so no firmware boot is claimed.

## Bootloader splash

The loader test image uses the owner-supplied Alpha design in
[`../assets/bootloader/`](../assets/bootloader/README.md): a 320×240 off-white
engraving with `VERSION Alpha` and an `HFS+ DETAILS` label that is green when the
HFS+ volume and catalog open, red on an HFS+ probe/catalog error, and neutral
while probing. The source illustration and title are preserved.

The device stores the base as a 16-colour 4bpp image (38,400 bytes) plus a 4bpp
alpha mask (592 bytes) and renders it to RGB565. The exact sRGB variants are in
`assets/bootloader/`; the 4bpp representation is the device limit. The label
font is ITC Benguiat Std Book 9px, provided by the owner and **not committed**;
see the asset README for the licence note and regeneration command.

`splash_color.c/.h` implement the loader splash. `splash.c` remains the 1-bit
splash for the separate write diagnostics. The diagnostic read-only image still
uses the old splash until it is rebuilt and re-tested.

## What this is

- `diagnostics.c` probes the HFS+ volume, opens the catalog, looks up a probe
  path, and reads the file through inline or extents-overflow extents.
- `report.c` formats the on-screen report.
- `splash.c` contains the owner-supplied 320×240 image as a 1-bit bitmap
  (`1 = light/background`, `0 = ink`, 40-byte stride).
- `hardware/helikon_diag_hw.c` is the S5L8702/Pocket-6G glue: ATA read callback,
  partition scan, splash blit, report rendering, button wait and power-off.
- `hardware/donor-bootloader.patch` removes the donor firmware-loading path and
  wires the diagnostic into a pinned donor bootloader tree.
- `artifacts/bootloader-ipod6g.ipod` is the built image:
  `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c`
  (67,144 bytes, `_moveend` `0x22030e40`, 61,888 bytes below `MOVE_AREA`).

The hardware bring-up (clocking, PMU, ATA, LCD, buttons) is donor-derived. The
HFS+ reader, diagnostic core, report and splash are Helikon-OS code. The donor
overlay, FAT mount and write-capable HFS+ paths are not linked into the image.

## Safety boundary

- This image is **read-only**. It never writes to the HFS+ volume. Two separate
  DFU-only write diagnostics exist: [`WRITE_DIAG.md`](WRITE_DIAG.md) writes only
  inside a preallocated inner image file, and [`OUTER_DIAG.md`](OUTER_DIAG.md)
  writes the real outer volume for one dedicated test folder after an opt-in
  file and a full backup. Neither is this image.
- `HFS+ write` is always reported as `NOT IMPLEMENTED`; the HFS+ transaction
  engine does not exist yet. Do not turn this into a write test on a real
  library.
- There is no bootloader/NOR write path in the image, no DFU action and no
  `--single`. The image contains no `ata_write_sectors`,
  `storage_write_sectors`, `hfsplus_create_file` or `fat_write` symbol.
- The splash is an image asset; displaying it does not imply a working firmware
  or a hardware-validated boot.
- The first build booted and splashed; its report formatting and read probe
  failed. The fixed image has not been rerun.

## Build

```sh
DONOR_DIR=/path/to/hfsplus-rockboxed-ipod \
  ./tools/build-diagnostic-bootloader.sh
```

The script pins donor commit `55603bd4bc2524ded900fa17feae99a223b04349`,
exports a clean tree with `git archive`, applies the patch, builds with
`--thumb`, verifies `_moveend` against `MOVE_AREA`, copies the image to
`artifacts/` and prints its SHA-256.

## How it works

1. The donor bootloader initializes clocks, PMU, buttons, LCD and backlight.
2. `helikon_splash_show()` converts the row-major splash to Rockbox's vertical
   mono-bitmap layout and blits it.
3. `helikon_diagnostic()` scans MBR/GPT/APM with the donor partition scanner,
   picks the largest HFS+ partition and calls `boot_diag_run()`.
4. The HFS+ read callback is `ata_read_sectors()` only.
5. `boot_diag_format()` is rendered line by line.
6. The diagnostic waits for a button, asks for USB to be unplugged if needed,
   and calls `power_off()`.

## Report

Example:

```text
HELIKON-OS DIAGNOSTIC
HFS+ volume: PASS
HFS+ catalog: PASS
HFS+ read: PASS (32 bytes)
HFS+ write: NOT IMPLEMENTED
Journal: CLEAN (or NOT PRESENT)
Splash: PASS
Partition: <size> MiB
Press any button to power off.
Nothing was written to the disk.
```

A dirty or invalid volume reports `FAIL`, `Journal: DIRTY`, and does not
continue into catalog or file reads. It never auto-repairs.

## Test

```sh
make -C tests/hfsplus test
make -C tests/hfsplus sanitize
make -C tests/bootloader test
make -C tests/bootloader sanitize
```

The bootloader test includes a round-trip check of the Rockbox mono-bitmap
packing and the inner-image extent mapping/metadata guard. The synthetic HFS+
fixture is 64 KiB; no device or disk image is involved. These are host tests
only. The separate write diagnostic also has a macOS outer/inner image test:

```sh
make -C tests/image write-diag-test
```

## DFU install

The backup and install procedure is in [`DFU.md`](DFU.md). The image hash and
the `MOVE_AREA` headroom are recorded there. Read the hard preconditions before
flashing.

## Not done

- A rerun of the fixed image; the first build's report formatting and read
  probe failed.
- Manifest/signature verification and boot state (D-01/D-02).
- Firmware loading.
- The separate write diagnostic is built (`bootloader-ipod6g-write-diag.ipod`)
  and has one owner-reported 6G run with every stage PASS and a clean inner
  `fsck_hfs`. The read-only image remains the production diagnostic.
