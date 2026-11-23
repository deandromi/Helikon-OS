# Helikon-OS Alpha 2.1.0 — Quick Start

This is a **development alpha**, not a support release. It contains the
firmware-loading bootloader, three firmware sets (launcher, Music hub and Disk
Mode) and a macOS companion shell. **No artifact in this release has been run on
a device.** Read the full [`RELEASE_NOTES.md`](RELEASE_NOTES.md) before
installing.

Installing a bootloader is DFU-only and can require an Apple restore, which
erases your library. Back up the whole iPod, read the backup back, and know your
restore path before you start.

## 1. The short version

```sh
# 1. verify the download
shasum -a 256 -c SHA256SUMS

# 2. back up the iPod, read a file back, and confirm you can restore it

# 3. copy ONE set to the iPod (never mix files between sets)
IPOD=/Volumes/IPOD
SET=/path/to/alpha-2.1/launcher        # or music-hub, or disk-mode
mkdir -p "$IPOD/.helikon"
cp "$SET/firmware.img" "$SET/firmware.manifest" "$SET/bootstate" "$IPOD/.helikon/"
cmp "$SET/firmware.img" "$IPOD/.helikon/firmware.img"
sync
diskutil eject "$IPOD"

# 4. DFU: hold Menu + Select ~12 s until the black screen, then install
"$MKS" --dfuscan
"$MKS" --bl-inst bootloader-ipod6g-loader-test.ipod     # never add --single
```

Recovery: hold **Menu + Select** for DFU, then
`"$MKS" --dfusend bootloader-ipod6g-uninstall.dfu`.

## 2. What is in this release

| File | Purpose |
|---|---|
| `bootloader-ipod6g-loader-test.ipod` | Loader: verifies the signed manifest and image, then jumps. |
| `loader-test.dfu` | Prebuilt DFU installer for the same loader. |
| `bootloader-ipod6g-uninstall.dfu` | Recovery uninstaller. |
| `helikon-alpha2.1-launcher.zip` | Launcher set (image 21): six cells, Music is a placeholder. Storage-free. |
| `helikon-alpha2.1-music-hub.zip` | Music hub set (image 22): launcher + hub + album/artist walls + 100-song demo catalog. Storage-free. |
| `helikon-alpha2.1-disk-mode.zip` | Disk Mode set (image 23): the whole disk as one writable USB drive. |
| `Helikon-Companion-0.1.0-alpha2.1.dmg` | Universal macOS companion shell preview. |
| `test-public-key.txt` | Public development test key, key ID `0x48454c01`. |
| `SHA256SUMS` | Hashes for every top-level asset. |
| `LICENSE`, `NOTICE` | GPL-2.0-or-later and provenance. |

Use each firmware zip as **one set**. The manifest is bound to its image and
bootstate; mixing files from two sets fails verification.

The launcher and Music hub never touch ATA or the filesystem. **Disk Mode does:
it is the format/restore window and it writes whatever the host asks.**

## 3. Verify the download

```sh
shasum -a 256 -c SHA256SUMS
```

Every line must say `OK`. The test key is a development key, not a release key;
no private key is included.

## 4. Back up and prepare the iPod

1. Back up the entire iPod library to a separate disk.
2. Read at least one file back from the backup and compare its hash.
3. Confirm you can restore the iPod (DFU/Apple restore or a rehearsed image).
4. Do not continue on the only copy of the library.

## 5. Choose one firmware set

| Set | Image | Bootstate | What it does | Storage |
|---|---|---|---|---|
| `helikon-alpha2.1-launcher.zip` | 21 | GOOD | Launcher; Music is a labelled placeholder | none |
| `helikon-alpha2.1-music-hub.zip` | 22 | GOOD | Launcher + Music hub + walls + 100-song demo | none |
| `helikon-alpha2.1-disk-mode.zip` | 23 | GOOD | Disk Mode: whole disk as a writable USB LUN | whole disk, writable |

The Music hub embeds a 100-song demonstration catalog from the owner's library,
including album covers. It is demonstration content only: it does not read or
write your iPod library and playback is not implemented.

## 6. Copy the firmware set

```sh
IPOD=/Volumes/IPOD    # change to the actual mount point
SET=/path/to/unzipped-set

test -d "$IPOD"
(cd "$SET" && shasum -a 256 -c SHA256SUMS)
mkdir -p "$IPOD/.helikon"
cp "$SET/firmware.img" "$SET/firmware.manifest" "$SET/bootstate" "$IPOD/.helikon/"
cmp "$SET/firmware.img"      "$IPOD/.helikon/firmware.img"
cmp "$SET/firmware.manifest" "$IPOD/.helikon/firmware.manifest"
cmp "$SET/bootstate"         "$IPOD/.helikon/bootstate"
sync
diskutil eject "$IPOD"
```

Run `fsck_hfs -fn /dev/diskNsM` on the volume before and after the copy. Do not
delete or modify library files. Do not keep two sets in `.helikon`; replace the
three files as one set.

## 7. Build or locate `mks5lboot`

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

## 8. DFU install the loader

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

## 9. Expected screens

### Launcher (image 21)

```text
HELIKON-OS splash
Loader progress
Launcher: Music, Podcasts, Audiobooks, Playlists, Games, Settings
```

Wheel or left/right moves the focus, SELECT or PLAY opens a cell, MENU exits,
60 seconds idle powers off. In this set Music shows a labelled placeholder.

### Music hub (image 22)

```text
Launcher -> Music -> Albums / Artists / Tracks
Albums or Artists -> 4x3 wall -> album or artist page
Artist page -> OVERVIEW / ALBUMS / TRACKS tabs
Tracks -> title list -> track page
MENU returns one level
```

No playback, no storage access, no library write.

### Disk Mode (image 23)

```text
Loader splash
DISK MODE confirm        SELECT continues, MENU powers off
WAITING FOR THE HOST     connect the USB cable now
HOST CONNECTED           the iPod is a USB drive on the laptop
EJECTED - SAFE           the host ejected it; powering off
```

On the host the iPod appears as an external USB drive. Back it up, or open Disk
Utility to partition and format it (HFS+ / Mac OS Extended, GUID Partition Map).
The LUN addresses the disk with LBA48, so volumes above 128 GiB are supported.
Eject on the host before unplugging; pulling the cable leaves the volume dirty.
MENU stops Disk Mode.

**Disk Mode writes whatever the host asks.** A format or partition erases the
library. Back up first and read the backup back.

## 10. Companion preview

1. Open `Helikon-Companion-0.1.0-alpha2.1.dmg`.
2. Drag `Helikon Companion.app` to Applications.
3. On first launch, right-click the app and choose Open, or use System Settings
   → Privacy & Security → Open Anyway. The app is ad-hoc signed and not
   notarized.
4. The app shows the owner welcome banner and an empty shell. It does not read
   or write a volume, use the network or install anything.

Requirements: macOS 14 or newer, universal arm64/x86_64 build.

## 11. Recovering a bricked or unwanted install

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
- If the volume will not mount, install the Disk Mode set and repair or restore
  it from the host — it does not mount the volume and does not replay a journal.
- Apple restore through Finder/iTunes is the last resort and erases the library.

## 12. Limits

- No device run for any artifact in this release.
- No firmware-owned filesystem write in the UI images; the Disk Mode image is a
  host-driven raw LUN, not a Helikon writer.
- No audio playback in the shipped images.
- No storage-backed library: the Music hub reads an embedded demo catalog.
- No working companion: shell only.
- No release signature and no notarization.
- No A/B rollback; recovery is DFU/Apple restore.
- No 7G or other adapter support claim.
