# Alpha 2.0.0 — Launchers and Interfaces

> **Engineering preview for the iPod Classic 6G.**
> This is an alpha, not a support release. The owner reports 6G testing is
> complete; the exact per-image results were not attached to the repository at
> cut time. Back up the iPod before installing anything.

This is the first Helikon-OS alpha that ships the firmware-loading bootloader,
storage-free firmware images and the macOS companion together. It adds the
launcher home, the Music hub with album and artist walls, the native HelikonDB
catalog, and a universal macOS companion shell. It does **not** add a
firmware-owned filesystem write.

## Release metadata

| Field | Value |
|---|---|
| Release | Alpha 2.0.0 — Launchers and Interfaces |
| Tag | `alpha-2.0.0` |
| Type | Normal GitHub release; development alpha |
| Update type | Firmware + bootloader + companion preview; manual install |
| Channel | Development / Alpha |
| Base | `main` at the `alpha-2.0.0` tag |
| Build host | macOS 27.0, Xcode 27.2, Swift 6.4.2, arm64 |
| Target | iPod Classic 6G |
| Device evidence | Owner reports 6G testing complete; recorded older runs listed below |
| Firmware signing | Build-local Ed25519 test key `0x48454c01`; **not a release key** |
| Companion signing | Ad-hoc; **not notarized** |
| Validation | Host test suites pass; see the validation table |

## What's new

### Launcher home

- 320×240 RGB565 launcher with the Helikon-OS wordmark and six cells:
  Music, Podcasts, Audiobooks, Playlists, Games and Settings.
- The wheel or left/right moves row-major with wrap; SELECT or PLAY opens the
  selected cell; MENU returns or exits; 60 seconds idle powers off.
- Music opens the Music hub. The other five cells show labelled placeholders.
- The launcher image attaches to the loader's PMU, backlight and panel and does
  not touch ATA or storage.

### Music hub and walls

- The Music hub is a launcher-style surface with Albums, Artists and Tracks
  cells over the launcher background.
- Albums and Artists use the committed 4×3 wall geometry: 52 px frames, 46 px
  art, Benguiat 18 px headers with count and search mark, a selected caption and
  a position marker. Artist entries without a profile picture use a monogram.
- Tracks is the title-index list. Album, artist and track pages reuse the wall
  header strips. Album and artist detail pages show cover, year, ratings,
  biography and profile picture when the catalog carries them.
- This image embeds a **100-song demonstration catalog from the owner's
  library**, including 10 album covers. It is demonstration content only: it
  does not read or write your iPod library, and playback is not implemented.
- The image does not touch ATA or storage. It is a UI bring-up image, not a
  daily-driver music player.

### HelikonDB v1

- One immutable `/.helikon/music.hdb`: 128-byte header, 88-byte track records,
  48-byte album/artist records, a deduplicated UTF-8 string pool, four
  precomputed indices and 96×96 RGB565 artwork.
- `tools/hdb_build.py` is the reference builder and inspector;
  `firmware/db/hdb.c` is the read-only firmware reader. Every table, string and
  blob access is bounds-checked and the whole-file CRC can be verified.
- D-53: no iTunesDB, libgpod, Rockbox tagcache or on-device scan in the path.
- The production builder (T-066), on-device journal (T-067) and VFS-backed
  library (T-056) are not in this release.

### Bootloader

- Newest loader `7f13e7e5…` (106,184 bytes): Ed25519 verification before any
  image byte, SHA-256/board/version/load/size checks, direct DRAM read, ATA left
  active for the firmware handoff, no confirmation screens, immediate jump.
- Host-verified with `tools/check-loader-artifact.sh`.
- The last device-tested loader is `6bc6927c…` from Alpha 1.0.1: one 6G/iFlash
  SD, 6.5 seconds startup, clean `fsck_hfs -fn`. That image predates the current
  loader work and is not included here; use Alpha 1.0.1 if you need it.

### Companion preview

- macOS 14+ SwiftPM/SwiftUI shell with the owner-supplied welcome banner, a
  brand-only sidebar and one empty-state page.
- No volume read or write, no network, no library scan, no metadata, no
  transfer, no podcast, audiobook, scrobbler, installer or settings code. Those
  were deliberately removed on 2026-11-06 and the app is being rebuilt from
  capabilities the firmware actually exposes.
- No test target and no device run. The DMG is an ad-hoc signed universal
  development build, not notarized. It is included as a visual preview, not as
  a working companion.

## What this release is not

- **No firmware-owned filesystem write.** The firmware images are storage-free
  UI demos; the loader does not write the filesystem.
- **No audio playback.** No codec, audio HAL, I2S/DMA output or now-playing
  screen.
- **No VFS or storage-backed library.** The music image uses an embedded demo
  catalog.
- **No settings, USB, update client, installer or app/game runtime.**
- **No working companion.** Shell only.
- **No release signing or notarization.**
- **No A/B rollback.** Single-image fail-closed; recovery is DFU/Apple restore.
- **No in-bootloader OF/recovery menu.** Native Menu+Play and DFU cover recovery.
- **No 7G or other adapter support claim.** One 6G/iFlash SD is the only
  device evidence, and the detailed per-image result was not attached at cut
  time.
- **No battery, RAM, frame-time or "better than Rockbox" claim.**

## Device evidence

The owner reports the 6G test programme is complete. The repository records the
earlier diagnostic, loader, F1 and outer-volume runs; per-image timings, photos
and `fsck_hfs` output for the new launcher/Music-hub images were not attached at
cut time. This table separates recorded evidence from the owner report.

| Component | Image | Evidence | Status |
|---|---|---|---|
| F1 firmware | `d7916411…` | owner-reported 6G, 2026-11-04 | pass (older) |
| Loader, last device-tested | `6bc6927c…` | one 6G/iFlash SD, 6.5 s, fsck clean | pass (older, Alpha 1.0.1) |
| Loader, this release | `7f13e7e5…` | owner-reported 6G test; host artifact checks | owner-reported; detail pending |
| Launcher | `b3218ccd…` | owner-reported 6G test; host preview + loader check | owner-reported; detail pending |
| Music hub/walls | `de6491a9…` | owner-reported 6G test; host preview + loader check | owner-reported; detail pending |
| Companion shell | 0.1.0 | universal build, DMG mount, launch/quit | host only |

## Install

**Read this before you touch the iPod.** A bootloader install is DFU-only and
can require an Apple restore, which can erase the library. Do not use your only
copy.

1. Verify the download: `shasum -a 256 -c SHA256SUMS`.
2. Back up the whole iPod, read the backup back, and confirm a restore path.
3. Choose **one** firmware set below. Do not mix files from different sets:
   each manifest is bound to its image and bootstate.
4. Mount the iPod, create `/.helikon/` if needed, copy `firmware.img`,
   `firmware.manifest` and `bootstate` from the chosen set, verify the three
   hashes, `sync`, eject, and run `fsck_hfs -fn` on the volume.
5. Put the iPod in DFU and install the loader:
   `mks5lboot --bl-inst bootloader-ipod6g-loader-test.ipod`.
   **Never add `--single`.** It destroys the Apple NOR path.
6. Boot and follow the expected screen for your set.

### Firmware sets

| Set | Image version | Bootstate | What it does | Storage |
|---|---|---|---|---|
| `helikon-alpha2-launcher.zip` | 10 | GOOD | Launcher UI; Music opens the Music hub | none |
| `helikon-alpha2-music-hub.zip` | 12 | GOOD | Launcher + Music hub + album/artist walls + 100-song demo | none |

Both images are storage-free. They do not write the filesystem.

### Expected screens

- **Launcher / Music hub:** loader splash and progress, then the launcher frame.
  Music opens the Music hub; Albums or Artists open the walls; Tracks opens the
  title list; MENU returns; 60 seconds idle powers off.

### Companion preview

1. Open `Helikon-Companion-0.1.0-alpha2.dmg`.
2. Drag `Helikon Companion.app` to Applications.
3. First launch: right-click → Open, or use System Settings → Privacy &
   Security → Open Anyway. The build is ad-hoc signed and not notarized.
4. The app shows the welcome banner and an empty shell. It does not read or
   write a volume, use the network or install anything. Quit and drag it to the
   Trash to remove it.

## Known issues

- The detailed per-image 6G results for the new launcher and Music-hub images
  were not attached at cut time. Treat the owner report as the release evidence
  and add the logs when available.
- The music demo is a 100-song sample from the owner's library and does not play
  audio. It is demonstration content, not a library import.
- The companion is an empty shell with no tests.
- The DMG is not notarized.
- The firmware uses a build-local test key, not a release key.
- No device, adapter or support matrix is claimed.

## Validation

| Check | Result |
|---|---|
| HFS+ host suite | pass: 105 tests |
| Bootloader host suite | pass: 31 tests |
| Firmware host suite | pass: 10 groups |
| MusicDB reader suite | pass |
| UI previews | pass: launcher, Music hub, walls, tracks and detail pages |
| Disposable-image writer test | pass: create/append/rename/truncate/delete/Unicode, `fsck_hfs` clean |
| Loader artifact check | pass for the launcher and Music-hub sets |
| 346 host crash cuts | documented; not rerun in this cut |
| Owner-reported 6G run | reported complete; details not attached |
| Device power-cut matrix | **not run** |
| Release-key signing | **not run**; test key only |
| Companion notarization | **not run** |
| Device support matrix | **not run**; one older 6G/iFlash SD |

## Recovery

- Hold **Menu + Select** to return to DFU.
- Remove the test bootloader with
  `mks5lboot --dfusend bootloader-ipod6g-uninstall.dfu` or
  `mks5lboot --bl-uninst ipod6g`.
- To return to a previous firmware set, replace the three `/.helikon/` files
  with a matching set. Do not delete the journal or format the volume.
- Apple restore through Finder/iTunes is the last resort and can erase the
  library.

## Assets

- `bootloader-ipod6g-loader-test.ipod` — newest loader.
- `loader-test.dfu` — DFU installer for the same loader.
- `bootloader-ipod6g-uninstall.dfu` — recovery uninstaller.
- `helikon-alpha2-launcher.zip` — launcher firmware set.
- `helikon-alpha2-music-hub.zip` — Music hub/walls + 100-song demo set.
- `Helikon-Companion-0.1.0-alpha2.dmg` — universal companion shell preview.
- `test-public-key.txt` — public test key, key ID `0x48454c01`.
- `QUICKSTART.md` — full install, expected screens and recovery.
- `RELEASE_NOTES.md`, `SHA256SUMS`, `LICENSE`, `NOTICE`.

### SHA-256

The authoritative list is in `SHA256SUMS`. Key hashes:

```text
7f13e7e5320239e204722d4c3cad1bd3a377906002158e7cd66344291fcedb21  bootloader-ipod6g-loader-test.ipod
7188fe9b4d3be7352d3aaa5a320e3e6a4b3b9c654b990a09730e9cfccaccbbde  loader-test.dfu
203e836412b71d3a16b3b57a5bf7ef034a0f5dcae1ca193685b8bfee33868a3a  bootloader-ipod6g-uninstall.dfu
856a4b70ed40ee29d6667aa69390a3644c217e01fcd97bf12115aaa09268c547  helikon-alpha2-launcher.zip
817a37583b87635e0d39762bec4477f2ebf22d5fcf4782107b3cbad19b30c3a2  helikon-alpha2-music-hub.zip
d4b033b5a68bcedf66b8659decb3fd407cd7692abdf47fe4bb2c88d2d0dfeb6f  Helikon-Companion-0.1.0-alpha2.dmg
b7b1341bff7770159bb7f6987643fce37f59dcd2912ff806c390351791e0c053  test-public-key.txt
```

Firmware image, manifest and bootstate hashes are in each zip's `SHA256SUMS`:

| Set | Image SHA-256 | Manifest SHA-256 | Bootstate SHA-256 |
|---|---|---|---|
| Launcher | `b3218ccda3ce9a0e5ce273086ca204cb10259651bdabb1816b5c8a3041f30c6c` | `4ec21a4dd60acc6d454a7b2145b9671499cb6faa333fbd49b4f8807a4d7c781f` | `dc70b497adff80b9337bc867d890bbdde4362b9c4e5a1871c6d3b6e9a87e42a3` |
| Music hub | `de6491a9647ff26ff07da4c721307a2c38c641ce5070b2e62e1dc8cf88b8a420` | `5fb5676ffd28d33810a32d81e3b8f508645a355a85c715c8298ff0cec5933da2` | `2f1e3e7c71453012e8803a44d938089956e1e284b10e71c56657c076bcd00508` |

Test key: key ID `0x48454c01`, public key
`2e0753454e5f37e2cd0bba40c1aa964d77771adabb36be66a000b09349bc8635`.
This is a development test key, not a release key. No private key is included.

## Build provenance

- Firmware: `arm-elf-eabi-gcc 9.5.0` (Rockbox toolchain), load address
  `0x08000000`, board 6.
- Loader: pinned donor commit `55603bd4bc2524ded900fa17feae99a223b04349`
  plus `bootloader/hardware/donor-loader.patch`.
- Host: macOS 27.0, Xcode 27.2, Swift 6.4.2, arm64.
- Companion: universal arm64 + x86_64, ad-hoc signed, DMG verified and launched
  on the build host.
- The tree is not yet reproducible; the hashes above are the release set.

## Source and license

GPL-2.0-or-later. Source: this repository at tag `alpha-2.0.0`. The firmware
uses a build-local Ed25519 test key; it is not a release key and no release
signing is claimed. D-20/ADR-0002 defines the future offline root and certified
release-key policy. The companion is an ad-hoc signed, non-notarized development
build. The Music-hub image embeds an owner-supplied demonstration catalog and
artwork; the owner has approved its use for this alpha. No private key, device
backup or disk image is included; the demonstration catalog is the only
library-derived content in the release.
