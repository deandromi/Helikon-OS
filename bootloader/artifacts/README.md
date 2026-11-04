# Helikon-OS diagnostic bootloader — artifact

> Status: **development image, unproven on hardware.**
> This is not a release and not a support claim.
> Owner: boot/release lead
> Last updated: 2026-11-03

## File

This is the **Helikon-OS** diagnostic image. It is not the donor
`bootloader-ipod6g.ipod` from `hfsplus-rockboxed-ipod`. The donor checkout
supplies `mks5lboot` and the build-time S5L8702/ATA/LCD bring-up only; the
installed image is built from this repository's diagnostic core, HFS+ reader,
report and splash.

| Field | Value |
|---|---|
| Image | `bootloader-ipod6g.ipod` |
| Size | 67,144 bytes |
| SHA-256 | `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c` |
| Target | iPod Classic 6G (`ipod6g`, `ip6g`, target 71) |
| Load | `_movestart` `0x22020800`, `_moveend` `0x22030e40` |
| `MOVE_AREA` | `0x22020800`–`0x22040000`, headroom 61,888 bytes |
| Build | Thumb, `arm-elf-eabi-gcc` 9.5.0, binutils 2.38 |
| Donor base | `hfsplus-rockboxed-ipod` commit `55603bd4bc2524ded900fa17feae99a223b04349` |
| Installer | `mks5lboot` 3.2.0; `--mkdfu-inst` accepted the image as `Classic 6G` |

The committed `SHA256SUMS` is authoritative for this file. A rebuild may
produce a different hash if the toolchain or build environment changes; verify
the hash printed by the build script in that case.

## Two images

This directory now contains three separate artifacts:

| Image | Purpose | Status |
|---|---|---|
| `bootloader-ipod6g.ipod` | Read-only HFS+ diagnostic + splash | unproven on hardware |
| `bootloader-ipod6g-write-diag.ipod` | Separate write diagnostic inside a preallocated inner image file | host-tested, hardware-run on 2026-11-03 |
| `bootloader-ipod6g-outer-diag.ipod` | Separate outer-volume write diagnostic | host-tested, no device run |

The production diagnostic remains read-only. The inner-image write image is a
separate, DFU-only experiment governed by D-22 and
[`../WRITE_DIAG.md`](../WRITE_DIAG.md); the outer-volume image is governed by
D-25 and [`../OUTER_DIAG.md`](../OUTER_DIAG.md). Neither is ever installed with
`--single`.

### Write diagnostic artifact

| Field | Value |
|---|---|
| Image | `bootloader-ipod6g-write-diag.ipod` |
| Image size | 110,920 bytes |
| Image SHA-256 | `81350f7821d91c6e2672f7d477cb2e5a079b70748b1c128fe6aa4784d19b691f` |
| DFU installer | `bootloader-ipod6g-write-diag.dfu` |
| DFU size | 119,056 bytes |
| DFU SHA-256 | `ce356e248b51b1a1efc1900c7c866c680be122cea18fc39ea3bf6ed1d5c2b03f` |
| Uninstaller DFU | `bootloader-ipod6g-uninstall.dfu` |
| Uninstaller size | 5,856 bytes |
| Uninstaller SHA-256 | `203e836412b71d3a16b3b57a5bf7ef034a0f5dcae1ca193685b8bfee33868a3a` |
| Marker | `HELIKON-OS WRITE DIAG` |
| `_moveend` | `0x2203b940` |
| `MOVE_AREA` | `0x22020800`–`0x22040000`; 18,112 bytes headroom |
| Build | `tools/build-write-diagnostic-bootloader.sh` |

It keeps the outer HFS+ volume read-only and writes only inside a root-level
`helikon-test.img` (16 MiB, journaled HFS+). The host core test creates an outer
HFS+ image, copies an inner HFS+ image into it, runs the same writer sequence,
and verifies both images with `fsck_hfs`; it passes, including a second run.

One owner-reported 6G run on 2026-11-03 reported every screen stage PASS. The
extracted inner image (SHA-256
`156060075eef60c463f421ce459c2138a00e1c43de44da802b77ce89c79ca0e8`) passed
`fsck_hfs -fn` exit 0, the `diag` folder was gone, and the outer volume passed
`diskutil verifyVolume` exit 0. This is not a library-write or release claim.

### Outer-volume write diagnostic artifact

| Field | Value |
|---|---|
| Image | `bootloader-ipod6g-outer-diag.ipod` |
| Image size | 110,408 bytes |
| Image SHA-256 | `32eeb541bcd64b45e4319a6ed16fbe4e22cf3123d2d82738b517877e53a3265e` |
| DFU installer | `bootloader-ipod6g-outer-diag.dfu` |
| DFU size | 118,544 bytes |
| DFU SHA-256 | `6e2e7d40377534b58eca4b6d34ef3525b8ae4f60221ce56103097ee3c8993dc7` |
| Marker | `HELIKON-OS OUTER DIAG` |
| `_moveend` | `0x2203b740` |
| `MOVE_AREA` | `0x22020800`–`0x22040000`; 18,624 bytes headroom |
| Build | `tools/build-outer-diagnostic-bootloader.sh` |

This image writes the real outer HFS+ journal, catalog, bitmap and volume
header, but only for the dedicated `/helikon-outer-test` folder after an opt-in
file and a clean-journal check. It is **not** installed on a library without the
full verified backup and restore rehearsal in
[`../OUTER_DIAG.md`](../OUTER_DIAG.md). No device run is recorded.

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

- The host suites pass (107 portable tests at the 2026-11-03 write-diagnostic
  commit), including ASan/UBSan.
- The splash packing test verifies the row-major source is converted to
  Rockbox's vertical mono-bitmap layout without pixel loss.
- The donor partition/HFS+ tests pass in the donor checkout; they are reference
  evidence, not Helikon-OS validation.

One owner-reported run of the first build exists: it booted and showed the
splash, with `HFS+ volume: PASS` and `HFS+ catalog: PASS`. The read probe failed
with `-11` and the report formatter corrupted the status lines. Both are fixed
in the current artifact, which has **not** been reflashed or rerun. Record the
rerun in [`../../docs/device-matrix.md`](../../docs/device-matrix.md).
