# Firmware-loading bootloader — manifest and boot state

> Purpose: fixed on-disk formats and the read-only boot/fallback flow.
> Status: format frozen; host loader implemented and tested; entry ABI accepted (D-28) and the page fill implemented (T-059) with host coverage; loader test image builds and passes host gates, no device run.
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
  policy is an offline Ed25519 root plus a certified release key. The verifier
  for that chain is implemented and host-tested
  (`bootloader/certificate.c`); the loader test image still resolves the
  build-local key directly, because switching its trust path needs the owner's
  root key and a fresh device run. See §2.1.
- A bad magic, version, board, image version, load address, size, hash,
  signature, key ID or flag is a hard failure. Do not fall through to an
  unsigned image.

## 2.1 Root, release key, rotation and revocation (T-053)

The manifest names one `key_id`. In a release build that id is not a key in a
table but the subject of a certificate signed by the offline root (D-20). This
section is the wire format and the policy; `bootloader/certificate.c` implements
verification only — there is no signing path, no key generation and no clock in
the loader.

**Certificate, 120 bytes, big-endian.**

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | version, 1 |
| 4 | 4 | key ID the certificate certifies |
| 8 | 4 | issuer ID, which must be a root the build carries |
| 12 | 4 | first image version it may sign, inclusive |
| 16 | 4 | last image version it may sign, inclusive |
| 20 | 4 | flags: bit 0 = may sign development images; other bits must be zero |
| 24 | 32 | certified Ed25519 public key |
| 56 | 64 | Ed25519 signature by the issuer over bytes 0–55 |

**Revocation bundle, 140 bytes, big-endian.**

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | version, 1 |
| 4 | 4 | serial, for a host-side audit trail |
| 8 | 4 | count, 0–16 |
| 12 | 64 | revoked key IDs; unused slots must be zero |
| 76 | 64 | Ed25519 signature by a root over bytes 0–75 |

**Rotation is by image version, not by date.** A bootloader has no trusted
clock, so an expiry date could only be checked against a value an attacker
supplies. A certificate instead binds a release key to an inclusive range of
*signed* manifest image versions: the new key takes over from a version the
release process chooses, and the old one is refused from there. Two certificates
may carry the same key ID only when their ranges do not overlap; at boot the
store picks the certificate whose range covers the manifest's version. Revocation
is separate: a key ID in a valid bundle is refused even when its certificate is
current, which is the path a leaked release key needs.

**Fail-closed rules** (`helikon_key_store_verify`):

- No roots, no certificate for the id, or a certificate whose range does not
  cover the image version: reject.
- A certificate whose issuer is not a carried root, whose signature does not
  verify, whose version is not 1, whose range is empty, or whose flag bits are
  undefined: reject.
- A revocation bundle that is present but does not authenticate: reject
  everything, not just the listed ids.
- A revoked id: reject.
- Otherwise verify the manifest signature with the certified key. The store
  reports a single failure, never which check failed.

**Minting.** `tools/make-key-certificate.py` takes a PKCS8 Ed25519 key and writes
the bytes above (raw, hex or a C initialiser). The host keeps the root private
key offline; the repository carries no private key. The test vectors in
`tests/bootloader/test_certificate.c` are minted from throwaway seeds with the
same tool, which is why the tool and the tests agree byte for byte.

**What is not wired yet.** `bootloader/hardware/helikon_loader_hw.c` still passes
`helikon_key_table_verify` with the build-local table. Switching it to a
`struct helikon_key_store` needs the owner's root public key, the certificate
list that names the first release key, and a fresh loader device run; T-053 stays
`in progress` until then. No behaviour of the existing test image changes before
that swap.

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
  jump (`bootloader/boot_info_page.c`, host-tested in
  `tests/bootloader/test_boot_info_page.c`). It does not write the filesystem;
  the page lives in DRAM at `0x087ff000`.
- Boot-info is a cache of verified facts, **not a trust anchor**. The firmware
  must re-read and validate the bootstate file through HFS+ before writing it.
- The firmware accepts a known version/size range and ignores a newer trailing
  extension. Fields are never reordered.
- A missing or invalid page sends the firmware to `BOOT_INFO_INVALID`
  diagnostic mode; it must not lead to a bootstate write.
- The page writer refuses (`HELIKON_ERR_FORMAT`) when the manifest bytes it is
  asked to copy disagree with the facts it is asked to state — board, image
  version, load address, size or SHA-256 — and refuses
  (`HELIKON_ERR_ARGUMENT`) a short, unaligned or missing page. On a refusal the
  loader shows "Boot-info page refused" and jumps with `r0 = 0`, which the
  firmware already treats as "no page" and answers by re-reading the disk.
  Fail-closed on the metadata side, no write, no repair.
- `journal_state` is `0` (unknown) in the loader image: only the firmware
  mounts and opens the journal. A dirty or invalid journal already stops the
  boot before the page is filled.
- `loader_stage_ms` is measured from the start of the loader's own work (after
  the donor splash), in milliseconds on the tick counter, so it is a duration
  and not a wall-clock time.
- This section is the accepted contract with host coverage, not a device
  result: the current loader test image passes the host gates only (T-059).

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
- a certificate and revocation bundle minted with
  `tools/make-key-certificate.py`: parse, root signature, version range,
  rotation with two ranges on one key ID, revoked key, unauthenticated bundle,
  foreign issuer and tampered fields (`tests/bootloader/test_certificate.c`);
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
