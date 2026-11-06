# Helikon-OS Alpha 2.0.0 — Quick Start

This is a **development alpha**, not a support release. It contains the
firmware-loading bootloader, three firmware sets and a macOS companion shell.
The owner reports 6G testing is complete; the detailed per-image results were
not attached to the repository at cut time. Read the full
[`RELEASE_NOTES.md`](RELEASE_NOTES.md) before installing.

Installing a bootloader is DFU-only and can require an Apple restore, which can
erase your music library. Back up the whole iPod, read the backup back, and know
your restore path before you start.

## 1. What is in this release

| File | Purpose |
|---|---|
| `bootloader-ipod6g-loader-test.ipod` | Newest loader; verifies the signed manifest and image, then jumps. |
| `loader-test.dfu` | Prebuilt DFU installer for the same loader. |
| `bootloader-ipod6g-uninstall.dfu` | Recovery uninstaller. |
| `helikon-alpha2-launcher.zip` | Launcher image: six cells, Music opens the Music hub. No storage access. |
| `helikon-alpha2-music-hub.zip` | Launcher + Music hub + album/artist walls + 100-song demo. No storage access. |
| `helikon-alpha2-f2-bootstate.zip` | F2 bootstate promotion. Writes only `/.helikon/bootstate`. |
| `Helikon-Companion-0.1.0-alpha2.dmg` | Universal macOS companion shell preview. |
| `test-public-key.txt` | Public development test key, key ID `0x48454c01`. |
| `SHA256SUMS` | Hashes for every top-level asset. |
| `LICENSE`, `NOTICE` | GPL-2.0-or-later and provenance. |

Use each firmware zip as **one set**. Do not mix `firmware.img`,
`firmware.manifest` or `bootstate` from different sets: the manifest and
bootstate are bound to that image.

## 2. Verify the download

```sh
shasum -a 256 -c SHA256SUMS
```

Every line must say `OK`. The test key is a development key, not a release key.
No private key is included.

## 3. Back up and prepare the iPod

1. Back up the entire iPod library to a separate disk.
2. Read at least one file back from the backup and compare its hash.
3. Confirm you can restore the iPod (DFU/Apple restore or a rehearsed image).
4. Do not continue on the only copy of the library.

## 4. Choose one firmware set

| Set | Image version | Bootstate | What it does |
|---|---|---|---|
| `helikon-alpha2-launcher.zip` | 10 | GOOD | Launcher only; Music opens the hub. |
| `helikon-alpha2-music-hub.zip` | 12 | GOOD | Launcher + Music hub + walls + 100-song demo. |
| `helikon-alpha2-f2-bootstate.zip` | 2 | TRYING seq 1 | Promotes bootstate to GOOD; first firmware write. |

The launcher and Music hub images do **not** touch ATA or storage. The F2 image
writes only `/.helikon/bootstate`; it does not write the library.

The Music hub image embeds a 100-song demonstration catalog from the owner's
library, including 10 album covers. It is demonstration content only and does
not read or write your iPod library. Playback is not implemented.

## 5. Copy the firmware set

Unzip the chosen set. Mount the iPod's HFS+ volume and copy the three files:

```sh
IPOD=/Volumes/IPOD    # change to the actual mount point
SET=/path/to/unzipped-set

test -d "$IPOD"
mkdir -p "$IPOD/.helikon"
cp "$SET/firmware.img" "$SET/firmware.manifest" "$SET/bootstate" "$IPOD/.helikon/"
(cd "$SET" && shasum -a 256 -c SHA256SUMS)
cmp "$SET/firmware.img" "$IPOD/.helikon/firmware.img"
cmp "$SET/firmware.manifest" "$IPOD/.helikon/firmware.manifest"
cmp "$SET/bootstate" "$IPOD/.helikon/bootstate"
sync
diskutil eject "$IPOD"
```

Run `fsck_hfs -fn` on the volume from macOS before and after the copy. Do not
delete or modify library files. Do not add a second firmware set to the same
`.helikon` folder; replace the three files as one set.

## 6. Build or locate `mks5lboot`

`mks5lboot` comes from the pinned donor checkout. On macOS:

```sh
git clone https://github.com/deandromi/hfsplus-rockboxed-ipod.git
cd hfsplus-rockboxed-ipod
git checkout 55603bd4bc2524ded900fa17feae99a223b04349
make -C utils/mks5lboot -j"$(sysctl -n hw.ncpu)" CC=clang \
  LDOPTS="-framework IOKit -framework CoreFoundation"
```

The tool is `utils/mks5lboot/mks5lboot`. Never use `--single`; it destroys the
Apple NOR boot path.

## 7. DFU install the loader

Put the iPod in DFU: hold **Menu + Select** for about 12 seconds and release at
the black screen. Then:

```sh
MKS=/path/to/hfsplus-rockboxed-ipod/utils/mks5lboot/mks5lboot

"$MKS" --dfuscan
"$MKS" --bl-inst /path/to/bootloader-ipod6g-loader-test.ipod
```

Equivalent DFU path:

```sh
"$MKS" --dfuscan
"$MKS" --dfusend /path/to/loader-test.dfu
```

Expected install report:

```text
model name:   Classic 6G
platform:     ipod6g
RB name:      ip6g
RB num:       71
```

Do **not** add `--single`.

## 8. Expected screens

### Launcher

```text
HELIKON-OS splash
Loader progress
Launcher: Music, Podcasts, Audiobooks, Playlists, Games, Settings
```

The wheel or left/right moves the focus; SELECT or PLAY opens a cell; MENU
returns or exits. Music opens the Music hub. The other cells are labelled
placeholders. The image powers off after 60 seconds idle.

### Music hub

```text
Launcher -> Music -> Albums / Artists / Tracks
Albums or Artists -> 4x3 wall -> album/artist detail
Tracks -> title list -> track page
MENU returns
```

There is no playback, no storage access and no library write in this image.
The demo catalog is owner-supplied demonstration content.

### F2 bootstate

```text
Loader splash
F2 stages
PRESS ANY BUTTON
(write stage)
PASS  WRITE ... PROMOTED
BOOTSTATE GOOD SEQ 2
```

After the PASS screen powers off, run `fsck_hfs -fn` from macOS and boot again.
The second PASS screen should show `ALREADY GOOD`.

If the screen stops at an ATA stage, record the exact stage and power off. Do
not retry writes. Preserve the screen and report it.

## 9. Companion preview

1. Open `Helikon-Companion-0.1.0-alpha2.dmg`.
2. Drag `Helikon Companion.app` to Applications.
3. On first launch, right-click the app and choose Open, or use System Settings
   → Privacy & Security → Open Anyway. The app is ad-hoc signed and not
   notarized.
4. The app shows the owner welcome banner and an empty shell. It does not read
   or write a volume, use the network or install anything.

Requirements: macOS 14 or newer, universal arm64/x86_64 build.

## 10. Recovery

- Hold **Menu + Select** to return to DFU.
- Remove the test bootloader:

```sh
"$MKS" --dfusend /path/to/bootloader-ipod6g-uninstall.dfu
```

or:

```sh
"$MKS" --bl-uninst ipod6g
```

- To return to a previous firmware set, replace the three `/.helikon/` files
  with a matching set. Do not delete the journal or format the volume.
- OF/Disk Mode remains available through the native **Menu + Play** startup
  gesture; there is no in-bootloader recovery menu.
- Apple restore through Finder/iTunes is the last resort and can erase the
  library.

## 11. Limits

- No audio playback, VFS, storage-backed library, settings, USB, update client,
  installer or app/game runtime.
- No working companion: shell only.
- No release signature and no notarization.
- No A/B rollback; recovery is DFU/Apple restore.
- No 7G or other adapter support claim.
- The closest detailed device evidence is for older images; the new-image
  per-run details were not attached at cut time.

License: GPL-2.0-or-later. Source: this repository at tag `alpha-2.0.0`.
