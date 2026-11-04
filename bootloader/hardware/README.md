# Diagnostic bootloader hardware glue

> Purpose: integrate the Helikon diagnostic core with the iPod Classic 6G
> S5L8702 bootloader.
> Status: `in progress` — image built, no device run.
> Owner: boot lead
> Last updated: 2026-11-03

## Files

| File | Purpose |
|---|---|
| `helikon_diag_hw.c` | ATA read callback, donor partition scan, splash packing/blit, report screen, button wait and power-off |
| `helikon_write_diag_hw.c` | Inner-image write diagnostic: ATA read/write/flush, cut hook, splash, report and power-off |
| `helikon_outer_diag_hw.c` | Outer-volume write diagnostic: ATA read/write/flush, splash, report and power-off |
| `donor-bootloader.patch` | Removes the donor firmware-loading/USB path, adds the Helikon sources and calls the diagnostic after `storage_init()` |
| `donor-write-diag.patch` | Inner-image write diagnostic integration |
| `donor-outer-diag.patch` | Outer-volume write diagnostic integration |

The build script is [`../../tools/build-diagnostic-bootloader.sh`](../../tools/build-diagnostic-bootloader.sh).
It exports the donor at commit `55603bd4bc2524ded900fa17feae99a223b04349`,
copies this file plus the Helikon core into `bootloader/helikon/`, applies the
patch, and builds with `--thumb`.

## Why the patch removes code

The donor bootloader normally mounts the HFS+ overlay, loads `rockbox.ipod` and
enters a firmware-loading path. This image must not do any of that. The patch:

- removes `usb_mode()` and the `ERR_RB` USB branch;
- removes `filesystem_init()`, `disk_mount_all()` and `load_firmware()`;
- leaves the early Menu/DFU button path and the OF launch path intact;
- calls `helikon_diagnostic()` after `storage_init()` and before any mount.

As a result the ELF has no `ata_write_sectors`, `storage_write_sectors`,
`hfsplus_create_file` or `fat_write` symbol.

## Known limits

- The donor partition scanner is used read-only; no HFS+ mount or overlay is
  linked.
- The diagnostic probes `/rockbox.ipod`. A missing file is a `read: FAIL`
  report, not a volume failure.
- The first hardware boot, splash, report, button wait and power-off are
  unproven. Record the result in [`../../docs/device-matrix.md`](../../docs/device-matrix.md).
- Do not add a write path, manifest bypass or `--single` here.
