# Diagnostic bootloader core

> Purpose: read-only HFS+ diagnostics and the 320×240 splash for the first
> bootloader screen.
> Status: in progress — host-testable core; not a flashable bootloader image
> Owner: boot lead
> Last updated: 2026-11-03
> Source: design paper §5, §24, §27; `docs/state.md`

## What this is

This is the platform-independent core of a diagnostic bootloader:

- `diagnostics.c` probes the HFS+ volume, opens the catalog, looks up a probe
  path, and reads the file through inline or extents-overflow extents.
- `report.c` formats the on-screen report.
- `splash.c` contains the owner-supplied 320×240 image as a 1-bit bitmap
  (`1 = ink`, `0 = background`, 40-byte stride).
- `tests/bootloader/test_diag.c` exercises the core on a synthetic HFS+ image.

It is **not** a flashable bootloader. The S5L8702 HAL, ATA init, LCD init,
button handling, image manifest verification and the boot jump are not wired
here yet.

## Safety boundary

- The diagnostic is **read-only**. It never writes to the HFS+ volume.
- `HFS+ write` is always reported as `NOT IMPLEMENTED`; the HFS+ transaction
  engine does not exist yet. Do not turn this into a write test on a real
  library.
- There is no bootloader/NOR write path, no DFU action and no `--single`.
- The splash is an image asset; displaying it does not imply a working firmware
  or a hardware-validated boot.

## Integration order

1. HAL/clock/PMU/GPIO/I2C/SPI bring-up.
2. ATA/block read-only bring-up.
3. Provide `hfs_read_fn` backed by the block layer.
4. Call `boot_diag_run()` with catalog and extents scratch buffers.
5. Blit `helikon_splash_bits[]` to the 320×240 panel.
6. Render `boot_diag_format()` lines.
7. Power off or enter recovery; never write the volume.
8. Only then add manifest/boot-state verification and the jump.

## Report

Example:

```text
HELIKON-OS DIAGNOSTIC
HFS+ volume: PASS
HFS+ catalog: PASS
HFS+ read: PASS (4 bytes)
HFS+ write: NOT IMPLEMENTED
Journal: NOT PRESENT
Splash: PASS
```

A dirty or invalid volume reports `FAIL`, `Journal: DIRTY`, and does not
continue into catalog or file reads. It never auto-repairs.

## Test

```sh
make -C tests/bootloader test
make -C tests/bootloader sanitize
```

The synthetic fixture is a 64 KiB HFS+ image with a catalog file at
`/.firmware/current.img`; no device or disk image is involved.

## Not done

- Hardware entry point and display driver.
- Manifest/signature verification and boot state (D-01/D-02).
- Any write diagnostic or write path.
- Any device run.
