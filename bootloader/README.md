# Diagnostic bootloader

> Purpose: read-only HFS+ diagnostics and the 320×240 splash for the first
> bootloader screen.
> Status: **image built; unproven on hardware.** Not a release and not a
> firmware loader.
> Owner: boot lead
> Last updated: 2026-11-03
> Source: design paper §5, §24, §27; `docs/state.md`; D-21

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

- The diagnostic is **read-only**. It never writes to the HFS+ volume.
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
packing. The synthetic HFS+ fixture is 64 KiB; no device or disk image is
involved. These are host tests only.

## DFU install

The backup and install procedure is in [`DFU.md`](DFU.md). The image hash and
the `MOVE_AREA` headroom are recorded there. Read the hard preconditions before
flashing.

## Not done

- A rerun of the fixed image; the first build's report formatting and read
  probe failed.
- Manifest/signature verification and boot state (D-01/D-02).
- Firmware loading.
- Any HFS+ write diagnostic or write path (T-030).
