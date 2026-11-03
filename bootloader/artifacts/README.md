# Helikon-OS diagnostic bootloader — artifact

> Status: **development image, unproven on hardware.**
> This is not a release and not a support claim.
> Owner: boot/release lead
> Last updated: 2026-11-03

## File

| Field | Value |
|---|---|
| Image | `bootloader-ipod6g.ipod` |
| Size | 66,920 bytes |
| SHA-256 | `bd0ebc92f58ad9549cad43120cb63566d6c20ae6255a530a17bafd8d832ed6c0` |
| Target | iPod Classic 6G (`ipod6g`, `ip6g`, target 71) |
| Load | `_movestart` `0x22020800`, `_moveend` `0x22030d60` |
| `MOVE_AREA` | `0x22020800`–`0x22040000`, headroom 62,112 bytes |
| Build | Thumb, `arm-elf-eabi-gcc` 9.5.0, binutils 2.38 |
| Donor base | `hfsplus-rockboxed-ipod` commit `55603bd4bc2524ded900fa17feae99a223b04349` |
| Installer | `mks5lboot` 3.2.0; `--mkdfu-inst` accepted the image as `Classic 6G` |

The committed `SHA256SUMS` is authoritative for this file. A rebuild may
produce a different hash if the toolchain or build environment changes; verify
the hash printed by the build script in that case.

## What it does

- Shows the owner-supplied 320×240 splash.
- Reads the MBR/GPT/APM partition table through the donor scanner.
- Probes the native HFS+ volume header and journal.
- Opens the catalog B-tree.
- Reads the first 32 bytes of `/rockbox.ipod` through inline or extents-overflow
  extents.
- Reports `HFS+ write: NOT IMPLEMENTED`.
- Waits for a button, asks for USB to be unplugged if needed, then powers off.

## What it does not do

- It does not load firmware.
- It does not write the volume, the journal or NOR.
- It does not implement a signed manifest, boot state or rollback.
- It does not use the donor overlay, `/rockpod-rw`, or any write-capable HFS+
  path. The ELF contains no `ata_write_sectors`, `storage_write_sectors`,
  `hfsplus_create_file` or `fat_write` symbol.
- It does not replace the Apple NOR path. `mks5lboot --single` is forbidden.

## Rebuild

From the repository root:

```sh
DONOR_DIR=/path/to/hfsplus-rockboxed-ipod \
  ./tools/build-diagnostic-bootloader.sh
```

The script pins the donor commit, exports a clean donor tree with
`git archive`, copies the Helikon sources, applies
`bootloader/hardware/donor-bootloader.patch`, builds with `--thumb`, verifies
`_moveend` against `MOVE_AREA`, copies the image here and prints its SHA-256.

The install and backup procedure is in [`../DFU.md`](../DFU.md).

## Evidence status

Host evidence only:

- 41/41 host tests pass, including ASan/UBSan.
- The splash packing test verifies the row-major source is converted to
  Rockbox's vertical mono-bitmap layout without pixel loss.
- The donor partition/HFS+ tests pass in the donor checkout; they are reference
  evidence, not Helikon-OS validation.

**No device run exists.** The first hardware boot, the splash, the report, the
button wait and power-off are unproven until recorded in
[`../../docs/device-matrix.md`](../../docs/device-matrix.md).
