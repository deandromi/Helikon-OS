# Firmware-loading bootloader — manifest and boot state

> Purpose: fixed on-disk formats and the read-only boot/fallback flow.
> Status: format frozen; host loader implemented and tested; bootloader image integration is T-049.
> Owner: boot lead
> Last updated: 2026-11-04
> Source: D-01, D-02; design paper §5.3, §16, §25.1–§25.2

The production bootloader is **read-only**. It reads native HFS+, verifies a
signed firmware manifest, selects a boot-state slot and loads the image to DRAM.
It never writes the filesystem and never loads an unsigned image.

## 1. Paths

The bootloader reads, in this order:

```text
/.helikon/firmware.manifest
/.helikon/firmware.img
/.helikon/bootstate
```

The update flow stages new files under `/.helikon/update/` through the HFS+
transaction engine and commits them to the paths above. A dirty or invalid
journal makes the volume read-only; the bootloader does not replay it.

## 2. Firmware manifest

A fixed 256-byte little-endian record:

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 8 | magic | `HELIKON1` |
| 8 | 4 | manifest version | `1` |
| 12 | 4 | board ID | `6` = iPod Classic 6G, `7` = 7G |
| 16 | 4 | image version | monotonically increasing, nonzero |
| 20 | 4 | load address | `0x08000000`–`0x087fffff` |
| 24 | 4 | image size | 1–8 MiB, fits load window |
| 28 | 4 | key ID | nonzero; selects the public key |
| 32 | 32 | image SHA-256 | over exactly `image size` bytes |
| 64 | 4 | flags | bit 0 = development-only |
| 68 | 4 | reserved | zero |
| 72 | 120 | reserved | zero |
| 192 | 64 | Ed25519 signature | over bytes 0–191 |

Rules:

- The image is loaded at `load address`; `load address + image size` must stay
  inside `0x08000000`–`0x08800000` (8 MiB donor DRAM window).
- The signature covers the manifest bytes `0..191`; the signature field is not
  part of the signed message.
- The bootloader verifies signature **before** copying the image to DRAM.
- A development-only manifest is accepted only by a development bootloader; it
  is never release-eligible.
- `key ID` is resolved through the public-key table; unknown IDs fail closed.
- A bad magic, version, board, image version, load address, size, hash,
  signature, key ID or flag is a hard failure. Do not fall through to an
  unsigned image.

## 3. Boot state

A fixed 512-byte little-endian file with two 256-byte slots:

| Slot offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `HBST` |
| 4 | 4 | format version `1` |
| 8 | 4 | sequence (higher is newer) |
| 12 | 4 | board ID |
| 16 | 4 | image version |
| 20 | 4 | load address |
| 24 | 4 | image size |
| 28 | 32 | image SHA-256 |
| 60 | 1 | state: `0` empty, `1` trying, `2` good, `3` failed |
| 61 | 3 | reserved, zero |
| 64 | 32 | slot SHA-256 over bytes 0–63 |
| 96 | 160 | reserved, zero |

Slot 0 starts at byte 0; slot 1 at byte 256.

Selection:

1. Validate magic/version/board/state/checksum and nonzero image fields.
2. Choose the highest valid `sequence` whose state is `GOOD`.
3. If the newest slot is `FAILED`, use the previous `GOOD` slot.
4. `TRYING` is not a bootable selection for this bootloader; the firmware promotes
   it to `GOOD` after its self-test, through the HFS+ transaction engine.
5. Missing/corrupt/board-mismatch state falls back to the default image and
   button-selected recovery. The bootloader never writes the state file.

## 4. Boot flow

```text
power-on
  -> splash
  -> storage init / partition scan
  -> HFS+ probe; dirty or invalid journal -> read-only recovery screen
  -> open catalog
  -> read manifest, verify magic/board/version/range/hash/signature
  -> read bootstate, select slot (optional; default image otherwise)
  -> read firmware image into DRAM
  -> flush/invalidate caches
  -> jump to load address
  -> on any failure: recovery screen, button-selected fallback, power off
```

The bootloader does not:

- write the journal, catalog, bitmap or bootstate;
- replay a dirty journal;
- load an unsigned image;
- use `--single`;
- auto-format, erase or repair.

## 5. Failure behaviour

| Failure | Result |
|---|---|
| Manifest missing/corrupt | recovery screen; no jump |
| Bad board/version/size/load address | recovery screen; no jump |
| Image hash mismatch | recovery screen; no jump |
| Signature/key failure | recovery screen; no jump |
| Bootstate missing/corrupt | use default image; recovery screen |
| Newest slot failed | previous good slot; recovery screen |
| Dirty/invalid journal | read-only; no replay; recovery |
| No valid image | recovery screen; DFU/Apple restore instructions |

Recovery must be visible and power off safely. Do not silently fall back to an
unsigned image.

## 6. Host tests

The host loader (`bootloader/firmware_loader.c`) reads the manifest/image/boot
state through the HFS+ read core and calls an explicit signature hook. The HFS+
adapter is `bootloader/hfs_loader.c`. `tests/bootloader/test_loader.c`,
`tests/bootloader/test_loader_hfs.c` and `tests/image/run-loader-macos.sh`
cover:

- known SHA-256 vectors;
- valid manifest and image;
- bad magic/version/board/image version/load address/size;
- truncated manifest/image;
- hash mismatch;
- signature hook rejection and unknown key ID;
- bootstate newest-good, newest-failed fallback, corrupt slot and missing file;
- no write calls in the loader path.

The signature hook is testable without a production key. No private key is
stored in this repository.
