# Firmware-loading bootloader — manifest and boot state

> Purpose: fixed on-disk formats and the read-only boot/fallback flow.
> Status: format frozen; host loader implemented and tested; entry ABI accepted (D-28), loader fill T-059 planned; loader test image builds and passes host gates, no device run.
> Owner: boot lead
> Last updated: 2026-11-04
> Source: D-01, D-02, D-28; design paper §5.3, §16, §25.1–§25.2

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
  The current test image uses a build-local test-key table. The accepted D-20
  policy is an offline Ed25519 root plus a certified release key; the
  certificate/revocation verifier is T-053 and is not in the test image.
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

**Owner decision (2026-11-04).** The frozen format has one image path, not
per-slot paths. The loader validates the selected `GOOD` slot against
`/.helikon/firmware.img` and the manifest. A failed or mismatched slot fails
closed: no jump and a recovery screen, with DFU/Apple restore as the recovery
path. There is **no A/B rollback** and no two-path implementation (T-050 is
closed as an explicit no-A/B decision).

## 4. Firmware entry ABI

Accepted 2026-11-04 (D-28; [`ADR-0004`](../docs/adr/0004-firmware-entry-abi.md)).
The loader passes a versioned boot-info page to the verified image:

| Item | Contract |
|---|---|
| `r0` | Physical pointer to the boot-info page at `0x087ff000` |
| `r1` | `0`; reserved |
| Mode | SVC, IRQ and FIQ disabled |
| Caches | Cleaned/invalidated by the loader |
| Stack | Undefined; firmware sets its own |
| `lr` | Return address into the loader; production firmware must not use it |
| Return | Forbidden; a return is a firmware panic |

The page is 4 KiB. The first 512 bytes are defined and little-endian; the rest
is reserved zero.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `HBI1` |
| 4 | 2 | version `1` |
| 6 | 2 | size `512` |
| 8 | 4 | loader version |
| 12 | 4 | board ID |
| 16 | 4 | image version |
| 20 | 4 | load address |
| 24 | 4 | image size |
| 28 | 32 | image SHA-256 |
| 60 | 4 | bootstate sequence |
| 64 | 1 | bootstate state (`0` empty, `1` trying, `2` good, `3` failed) |
| 65 | 1 | journal state (`0` unknown, `1` clean, `2` dirty, `3` absent) |
| 66 | 2 | flags (bit 0 development, bit 1 bootstate present, bit 2 manifest verified) |
| 68 | 4 | loader stage time in ms |
| 72 | 4 | CRC-32 over bytes 0–71 |
| 76 | 180 | reserved zero |
| 256 | 256 | verified manifest copy |
| 512 | 3584 | reserved zero |

Rules:

- The loader fills the page after manifest/image verification and before the
  jump. It does not write the filesystem.
- Boot-info is a cache of verified facts, **not a trust anchor**. The firmware
  must re-read and validate the bootstate file through HFS+ before writing it.
- The firmware accepts a known version/size range and ignores a newer trailing
  extension. Fields are never reordered.
- A missing or invalid page sends the firmware to `BOOT_INFO_INVALID`
  diagnostic mode; it must not lead to a bootstate write.
- The loader fill is **T-059** and is not implemented yet. Until it lands, the
  test payload path calls `entry(void)` and the firmware must handle `r0 = 0`.
  This section is the accepted contract, not a device result.

## 5. Boot flow

```text
power-on
  -> splash
  -> storage init / partition scan
  -> HFS+ probe; dirty or invalid journal -> read-only recovery screen
  -> open catalog
  -> read manifest, verify magic/version/board/range/policy/signature
  -> read firmware image directly to its signed load address
  -> verify SHA-256 and size
  -> read bootstate, select a valid GOOD slot (optional; default otherwise)
  -> fill boot-info page at 0x087ff000 and pass it in r0
  -> flush/invalidate caches
  -> jump to the verified load address; production entry does not return
  -> on any failure: recovery screen, no jump, power off
```

The bootloader does not:

- write the journal, catalog, bitmap or bootstate;
- replay a dirty journal;
- load an unsigned image;
- use `--single`;
- auto-format, erase or repair.

## 6. Failure behaviour

| Failure | Result |
|---|---|
| Manifest missing/corrupt | recovery screen; no jump |
| Bad board/version/size/load address | recovery screen; no jump |
| Image hash mismatch | recovery screen; no jump |
| Signature/key failure | recovery screen; no jump |
| Bootstate missing/corrupt/truncated | use the verified default image |
| Bootstate valid but differs from the manifest | recovery screen; no jump |
| Newest slot failed | previous good slot if it matches a verified image; otherwise recovery |
| Dirty/invalid journal | read-only; no replay; recovery |
| No valid image | recovery screen; DFU/Apple restore instructions |

Recovery must be visible and power off safely. Do not silently fall back to an
unsigned image.

## 7. Host tests

The host loader (`bootloader/firmware_loader.c`) reads the manifest/image/boot
state through the HFS+ read core and calls an explicit signature hook. The HFS+
adapter is `bootloader/hfs_loader.c`. `bootloader/ed25519.c` is a
verification-only TweetNaCl derivative; `bootloader/keys.c` resolves key IDs.
Tests cover:

- RFC 8032 Ed25519 vectors, a fixed 192-byte manifest-length vector, tampered
  signatures and unknown key IDs (`tests/bootloader/test_ed25519.c`);
- known SHA-256 vectors, valid manifest/image, real Ed25519 manifest
  verification, bad magic/version/board/address/size, hash mismatch, truncated
  manifest/image (`tests/bootloader/test_loader.c`);
- the HFS+ file-read adapter (`tests/bootloader/test_loader_hfs.c`);
- a disposable macOS HFS+ volume with a real generated key, a GOOD bootstate,
  a wrong key and an unknown key ID (`tests/image/run-loader-macos.sh`);
- the exact generated loader-test files through the HFS+ core
  (`tools/check-loader-artifact.sh`).

No production private key is stored in this repository. The loader-test build
uses a generated key under `build/loader-bootloader/`; only its public half is
linked into that image.

## 8. Loader test image

`tools/build-loader-bootloader.sh` builds a separate DFU-only
`bootloader-ipod6g-loader-test.ipod`. It accepts a development-only manifest,
reads the image directly to its signed load address, executes a 12-byte signed
ARM payload that returns `HELO`, and then powers off. Production handoff is
disabled. The install, expected screen, pass criteria and recovery are in
[`LOADER_TEST.md`](LOADER_TEST.md). No device run is recorded yet.
