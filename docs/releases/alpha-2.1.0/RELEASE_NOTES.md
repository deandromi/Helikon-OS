# Alpha 2.1.0 — Disk Mode

> **Engineering alpha for the iPod Classic 6G.**
> This is an alpha, not a support release. **No artifact in this release has
> been run on a device.** Every image and the loader were built and verified on
> the host. Back up the iPod before installing anything.

Alpha 2.1.0 adds the first Helikon-OS image whose job is the disk itself: a
Disk Mode firmware that hands the whole iPod disk to the host as one writable
USB drive, so the volume can be backed up, partitioned and formatted from a
laptop — including volumes larger than 128 GiB. It also rebuilds the loader and
the two UI images on top of the HFS+ writer work merged since Alpha 2.0.0.

## Release metadata

| Field | Value |
|---|---|
| Release | Alpha 2.1.0 — Disk Mode |
| Tag | `alpha-2.1.0` |
| Type | Normal GitHub release; development alpha (not a GitHub pre-release) |
| Update type | Firmware + bootloader + companion preview; manual copy and DFU |
| Channel | Development / Alpha |
| Base | `main` at the `alpha-2.1.0` tag |
| Build host | macOS 27.0, Xcode 27.2, Swift 6.4.2, arm64 |
| Target | iPod Classic 6G |
| Device evidence | **None for this release set**; older recorded runs are listed below |
| Firmware signing | Build-local Ed25519 test key `0x48454c01`; **not a release key** |
| Companion signing | Ad-hoc; **not notarized** |
| Validation | Host suites, sanitizers, fuzzers, static analysis and the loader artifact check pass; see the validation table |

## What's new

### Disk Mode (firmware image version 23, experimental)

- New image whose only job is storage: it initialises ATA writable, never mounts
  the volume, and exposes the whole disk as a single USB mass-storage LUN.
- One confirm screen (`SELECT` continues, `MENU` powers off), then
  `WAITING FOR THE HOST`, `HOST CONNECTED` and `EJECTED - SAFE`.
- The LUN path uses LBA48, so a volume above the 128 GiB boundary is addressed
  and formatted correctly. The host proof for that boundary is in
  `tests/hfsplus` (>128 GiB format) and `tests/usb` (LBA48 at and above the
  limit); the firmware half is host-verified only.
- A dirty-journal volume is not replayed and not mounted: Disk Mode is the
  restore path, not a repair tool. Eject from the host before unplugging.
- **This image writes whatever the host asks it to write.** It is the format
  and restore window. Back up, read the backup back, and never point it at your
  only copy.

### Launcher and Music hub refresh (image versions 21 and 22)

- Every Music surface is drawn from the IBM Plex atlas; the 3×5 diagnostic face
  is gone from the Music flow, and the atlas keeps its 1-bit fallback (D-24).
- The hub no longer blits the launcher masthead, so it stops faking a clock and
  a battery the image cannot read.
- Artist pages use the `assets/ui/artist-view` layout: portrait band, name,
  facts, PLAY plate, and the OVERVIEW / ALBUMS / TRACKS tabs.
- Cold-boot input initialisation no longer fails when no button is held.
- Both images are storage-free: no ATA, no filesystem, no library write. The
  Music hub still embeds a 100-song demonstration catalog (owner-supplied).

### Loader

- Rebuilt loader `79a1c02b…` (107,560 bytes):
  Ed25519 manifest verification before any image byte, SHA-256/board/version/
  load/size checks, direct DRAM read, ATA left active for the firmware handoff,
  immediate jump, no confirmation screens.
- New since Alpha 2.0.0: the loader reads the APM map and HFS+ geometry with the
  live ATA sector size, and fills the D-28 boot-info page it passes to the
  firmware.
- The release-key certificate path (root-signed release key, rotation and
  revocation, T-053) is implemented and host-tested. **This release still signs
  images with the build-local test key**; no release key exists yet.
- Host-verified with `tools/check-loader-artifact.sh` for all three firmware
  sets in this release.
- The last device-tested loader remains `6bc6927c…` from Alpha 1.0.1.

### HFS+ write engine (host-verified; not in these images)

- Overflow extents for fork growth, append past eight extents, truncation to any
  size, and delete down to an empty catalog that gives its nodes back.
- B-tree index-node merging, including the steal fallback when two siblings do
  not fit, and a node map that widens when a tight node fills.
- `hfs_check_volume` grew: free-node maps, leaf chains, key order, record
  bodies, catalog counts and thread records in both directions.
- Journal replay is idempotent and survives a fault during replay; the
  transaction overlay, the allocation bitmap and the staging rules have
  property tests; a fuzzer drives the writer with the fail-closed rules as its
  oracle.
- The firmware-owned write path (F2) is still blocked by an open defect and is
  **not** part of this release. No shipped image writes the filesystem.

### Codecs and player (host-verified; not in these images)

- WAV, FLAC and MP3 decode from a streaming pull source; MP3 via a vendored
  libmad port with a measured 30,976-byte workspace, seek, gapless trim from the
  LAME/FFmpeg tag and a bounce buffer for the last frame.
- ID3v2.2/2.3/2.4 text tags, FLAC Vorbis comments, M3U/M3U8/PLS import, cue
  sheet track start frames, read-ahead and a play queue — all host-tested.
- The shipped UI images have **no audio output**; they are catalog and browser
  builds. Audio device work remains.

### Tooling and CI

- `./tools/host-tests.sh clang` runs nine portable suites with one command, plus
  a `--sanitize` pass (ASan + UBSan) and the macOS disk-image suites.
- Eight fuzz harnesses with committed corpora and a replay mode that CI runs.
- Static analysis over the firmware tree, a coverage report, and CI on every
  push: host suites, sanitizers, fuzzers, static analysis, macOS disk images and
  firmware cross-builds.
- A nightly preview release keeps the newest `main` buildable and downloadable.

## What this release is not

- **No device run.** Nothing in this release has been on a 6G.
- **No firmware-owned filesystem write in the shipped UI images.** The writer
  exists in the tree and is host-tested; F2 remains blocked.
- **No audio playback** in the shipped images and no codec in them.
- **No storage-backed library.** The Music hub reads an embedded catalog.
- **No working companion.** The DMG is a shell preview with no device access.
- **No release signing and no notarization.** Test key and ad-hoc signature.
- **No A/B rollback.** Single-image fail-closed; recovery is DFU or Apple
  restore.
- **No in-bootloader recovery menu** (D-26).
- **No 7G or other adapter support claim.**
- **No performance, battery or "faster than Rockbox" claim.**

## Device evidence

This release is host-only. The repository's recorded device runs are older and
cover different images:

| Component | Image | Evidence | Status |
|---|---|---|---|
| Loader, last device-tested | `6bc6927c…` (Alpha 1.0.1) | one 6G/iFlash SD, 6.5 s startup, `fsck_hfs -fn` clean | pass (older) |
| Launcher | `b3218ccd…` (Alpha 2.0.0) | owner reports 6G testing complete; photo/timing not attached | owner-reported |
| Music hub / walls | `de6491a9…` (Alpha 2.0.0) | owner reports 6G testing complete; photo/timing not attached | owner-reported |
| Cleaned Music flow | `5292c8ed…` (2026-11-08) | owner reports a ~20 s stall when opening an artist | **fail** (older image) |
| WAV ring audio | `18f5fe39…` (2026-11-08) | owner reports clean audio, pause/resume works | pass (older image) |
| FLAC player | `6a2328ae…` (2026-11-08) | owner reports clean FLAC audio, pause/resume works | pass (older image) |
| Loader, this release | `79a1c02b…` | host artifact checks only | **not run** |
| Launcher, this release | `8a2a18c2…` | host artifact checks only | **not run** |
| Music hub, this release | `036911d2…` | host artifact checks only | **not run** |
| Disk Mode, this release | `e73f32f3…` | host suites only | **not run** |

## Install

A bootloader install is DFU-only and can require an Apple restore, which erases
the library. Do not use your only copy.

1. **Verify the download:** `shasum -a 256 -c SHA256SUMS`. Every line must say
   `OK`.
2. **Back up the iPod**, read at least one file back from the backup, and know
   your restore path.
3. **Pick one firmware set** from the table below and unzip it. Never mix
   `firmware.img`, `firmware.manifest` and `bootstate` between sets; each
   manifest is bound to its image and bootstate.
4. **Copy the set:** mount the iPod, `mkdir -p /.helikon`, copy those three
   files into it, run `shasum -a 256 -c SHA256SUMS` inside the set,
   `cmp` each file against the copy, `sync`, eject, and run
   `fsck_hfs -fn /dev/diskNsM` on the volume.
5. **DFU:** hold **Menu + Select** for ~12 s until the black screen, then
   `mks5lboot --dfuscan` and
   `mks5lboot --bl-inst bootloader-ipod6g-loader-test.ipod`
   (or `--dfusend loader-test.dfu`). **Never add `--single`.**
6. **Boot.** The loader shows its splash, verifies the manifest and image, and
   jumps.
7. **Disk Mode only:** with the Disk Mode set installed, boot, press `SELECT` at
   the confirm screen, connect USB, and use Disk Utility or `diskutil` on the
   host. Eject in the host before unplugging; `MENU` leaves Disk Mode.
8. **Recovery:** hold **Menu + Select** for DFU and
   `mks5lboot --dfusend bootloader-ipod6g-uninstall.dfu` (or
   `mks5lboot --bl-uninst ipod6g`). Apple restore is the last resort.

### Firmware sets

| Set | Image version | Bootstate | What it does | Storage |
|---|---|---|---|---|
| `helikon-alpha2.1-launcher.zip` | 21 | GOOD | Launcher; Music is a placeholder | none |
| `helikon-alpha2.1-music-hub.zip` | 22 | GOOD | Launcher + Music hub + walls + 100-song demo catalog | none |
| `helikon-alpha2.1-disk-mode.zip` | 23 | GOOD | Disk Mode: the whole disk as a writable USB LUN | whole disk, writable |

The Disk Mode set replaces the UI sets in `/.helikon`; it is the format and
restore window, not a daily-driver image. Keep a copy of the set you had before
so you can copy it back.

### Expected screens

- **Launcher:** loader splash and progress, then the launcher frame with six
  cells. Wheel or left/right moves, SELECT/PLAY opens, MENU exits, 60 s idle
  powers off.
- **Music hub:** launcher → Music opens the hub (Albums, Artists, Tracks);
  walls and detail pages with the OVERVIEW/ALBUMS/TRACKS tabs; MENU returns one
  level.
- **Disk Mode:** `DISK MODE` confirm → `WAITING FOR THE HOST` →
  `HOST CONNECTED` → `EJECTED - SAFE` → power off.

### Companion preview

1. Open `Helikon-Companion-0.1.0-alpha2.1.dmg` and drag `Helikon Companion.app`
   to Applications.
2. First launch: right-click → Open, or System Settings → Privacy & Security →
   Open Anyway. The build is ad-hoc signed and not notarized.
3. The app shows the welcome banner and an empty shell. It does not read or
   write a volume, use the network or install anything.

## Known issues

- No artifact here has been on a device; the loader and the Disk Mode image are
  the two most likely to need a fix after the first run.
- The ~20 s artist-open stall reported on the 2026-11-08 Music-flow image
  (`5292c8ed…`) has not been retested; the Music hub in this release contains
  that flow.
- Disk Mode leaves the volume dirty if the cable is pulled. Eject first.
- The Music hub demonstrates a catalog; it does not play audio.
- The companion is an empty shell with no tests.
- The DMG is not notarized and the firmware uses a build-local test key.
- Cross-host reproducibility is not claimed; see the addendum below.

## Validation

| Check | Result |
|---|---|
| Host suites (`./tools/host-tests.sh clang`) | pass: nine suites |
| Sanitizers (`--sanitize`, ASan + UBSan) | pass |
| Fuzzers (eight harnesses, replay) | clean: 8 harnesses, 47 corpus files |
| Static analysis | pass: 0 findings |
| macOS disk-image suites | pass: HFS+, write, image, bootloader image |
| HFS+ structural check suite | pass |
| B-tree model, catalog and allocation property tests | pass |
| Loader artifact check, all three sets | pass (image, manifest, bootstate verify through the HFS+ core) |
| Firmware cross-builds (launcher, Music hub, Disk Mode) | pass |
| Companion universal build and DMG verify | pass (ad-hoc signed) |
| Device run, any artifact | **not run** |
| Device power-cut matrix | **not run** |
| Release-key signing | **not run**; test key only |
| Companion notarization | **not run** |
| Device support matrix | **not run** |

## Assets

- `bootloader-ipod6g-loader-test.ipod` — loader,
  107,560 bytes.
- `loader-test.dfu` — DFU installer for the same loader.
- `bootloader-ipod6g-uninstall.dfu` — recovery uninstaller.
- `helikon-alpha2.1-launcher.zip` — launcher set (image version 21).
- `helikon-alpha2.1-music-hub.zip` — Music hub set (image version 22).
- `helikon-alpha2.1-disk-mode.zip` — Disk Mode set (image version 23).
- `Helikon-Companion-0.1.0-alpha2.1.dmg` — universal companion shell preview.
- `test-public-key.txt` — public test key, key ID `0x48454c01`.
- `RELEASE_NOTES.md`, `QUICKSTART.md`, `SHA256SUMS`, `LICENSE`, `NOTICE`.

### SHA-256

```text
79a1c02b7fd2d1e8c3bd685a7f3c676320dbc734f5878e7dfee67dc1c66d129f  bootloader-ipod6g-loader-test.ipod
d05c68ebb30f49c81bb9d8c36b38112b54d9927c7175c9f7a6ca93dd1e49f813  loader-test.dfu
203e836412b71d3a16b3b57a5bf7ef034a0f5dcae1ca193685b8bfee33868a3a  bootloader-ipod6g-uninstall.dfu
6779c78103fef11e2719cd1f873af694030b719002fbca64279b57dc56fa93e1  helikon-alpha2.1-launcher.zip
a408f20f13a437d1ef6f2d9d51b5eacc6299024e727b308f7cbd964fb58c62e1  helikon-alpha2.1-music-hub.zip
307cadb32e1fc81e7d5921c82a1968438e5df95720756d56ee1825cc9391d743  helikon-alpha2.1-disk-mode.zip
8e02f2b1d4606c11a00c4556ed9b629ace77d1d0a4fb76a53c832cc4581787bc  Helikon-Companion-0.1.0-alpha2.1.dmg
b7b1341bff7770159bb7f6987643fce37f59dcd2912ff806c390351791e0c053  test-public-key.txt
```

Inside each zip, `SHA256SUMS` covers the set:

| Set | Image | Manifest | Bootstate |
|---|---|---|---|
| Launcher | `8a2a18c2219cbbeb521ac07736e7ab23070481d13200a218b7c345c46b364f39` | `2a129d79da46f5a00a92d3234234dc423e80eb2d51a69ac7cde8c3718d1c2cb3` | `731510a02245783fc62f050c0c3ef5da123c5dab1c739a1cd3b38d6b60a49797` |
| Music hub | `036911d2b0daa3a38d4e123c3fd6b5bdbb0df3bfe1602fb764ff4e9ab489994d` | `431c1843a0bf60f8d6a9d7584d006bac3501776b325bfdf73ac7d7b41509e5a7` | `73e4431e11adea4be069531e0a23fadcd4be94856444346a1a2115fcabf3c0ac` |
| Disk Mode | `e73f32f3f4762c85e59ace3a7af6192c915e064c5f8c8de7101a22d866ad568a` | `3ccbc41f92d116a0fef9d7d6d63b3d10ec110c7c656dcfd27605fd41997a4b0e` | `fb7c6cc1afda45c2c6845282ff19d99bd62d09bc60161b1688f73f717c83c0f1` |

Test key: key ID `0x48454c01`, public key
`2e0753454e5f37e2cd0bba40c1aa964d77771adabb36be66a000b09349bc8635`. This is a
development test key, not a release key. No private key is included.

## Build provenance

- Firmware: `arm-elf-eabi-gcc 9.5.0` (Rockbox toolchain), load address
  `0x08000000`, board 6. Launcher and Music hub built with the diagnostic
  launcher entry (`-DHELIKON_DIAG_LAUNCHER`); Disk Mode built with
  `DISKMODE=1`.
- Loader: pinned donor commit `55603bd4bc2524ded900fa17feae99a223b04349` plus
  `bootloader/hardware/donor-loader.patch`.
- Companion: universal arm64 + x86_64, ad-hoc signed, DMG verified and launched
  on the build host.
- Host: macOS 27.0, Xcode 27.2, Swift 6.4.2, arm64.

## Source and license

GPL-2.0-or-later. Source: this repository at tag `alpha-2.1.0`. The firmware uses
a build-local Ed25519 test key; D-20/ADR-0002 defines the future offline root and
certified release-key policy. The companion is an ad-hoc signed, non-notarized
development build. The Music-hub image embeds an owner-supplied demonstration
catalog and artwork; the owner has approved its use for this alpha. No private
key, device backup or disk image is included.

## Addendum — 2026-11-23

`tools/build-release-sets.sh` rebuilt the three firmware images from this tree on
the same host with the same pinned toolchain and reproduced all three
`firmware.img` hashes byte-for-byte: launcher `8a2a18c2…`, Music hub
`036911d2…`, Disk Mode `e73f32f3…`. That is a same-host rebuild check, not a
reproducible-build claim: the loader and the companion DMG are not covered, and
a different host or toolchain is untested.
