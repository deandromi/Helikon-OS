# Write diagnostic image — separate, DFU-only, unproven

> Status: **host-tested; first 6G run passed on 2026-11-03.**
> This is not the production bootloader, not a release and not a support claim.
> Owner: boot/release lead
> Last updated: 2026-11-03
> Source: D-22; `docs/design-paper.md` §5, §24; `docs/state.md`

## What this image is

This is a **separate** Helikon-OS write diagnostic. The production diagnostic
bootloader remains read-only. This image exists only to exercise the native
writer on a disposable target without writing the outer HFS+ filesystem.

It:

- shows the same 320×240 splash;
- reads the outer HFS+ volume, catalog and partition table read-only;
- finds a root-level file named `helikon-test.img`;
- requires that file to be a journaled HFS+ image of at least 16 MiB;
- maps only that file's data-fork extents to ATA reads and writes;
- runs create folder/file, append, readback, rename, truncate and delete
  through the real journaled writer inside the inner image;
- reports PASS/FAIL and leaves the inner image for macOS `fsck_hfs`.

It never writes the outer volume's volume header, journal, catalog, extents,
allocation bitmap or any other outer metadata. The outer file's **data blocks**
are changed; the outer filesystem metadata is not.

## Artifact

| Field | Value |
|---|---|
| Bootloader image | `bootloader/artifacts/bootloader-ipod6g-write-diag.ipod` |
| Image size | 109,320 bytes |
| Image SHA-256 | `9e3881297d8a793d6c0589e9779f1d624685f001b6e1cf12814c06c8b7b2aa99` |
| DFU installer | `bootloader/artifacts/bootloader-ipod6g-write-diag.dfu` |
| DFU size | 117,456 bytes |
| DFU SHA-256 | `b7385057e10d50a750afbb229543a1969862d89b4b82e174f7308efed00ab513` |
| Uninstaller DFU | `bootloader/artifacts/bootloader-ipod6g-uninstall.dfu` |
| Uninstaller SHA-256 | `203e836412b71d3a16b3b57a5bf7ef034a0f5dcae1ca193685b8bfee33868a3a` |
| Marker | `HELIKON-OS WRITE DIAG` |
| `_moveend` | `0x2203b300` |
| `MOVE_AREA` | `0x22020800`–`0x22040000`; 19,712 bytes headroom |
| Donor base | `hfsplus-rockboxed-ipod` `55603bd4bc2524ded900fa17feae99a223b04349` |
| Build | `tools/build-write-diagnostic-bootloader.sh` |

The hash above belongs to this artifact only. Do not compare it with the
read-only diagnostic hash.

## Install — exact steps

Run these from the Helikon-OS repository on macOS. Replace `<iPod>` with the
volume name from `ls /Volumes`.

1. Create the disposable inner image:

   ```sh
   tools/make-write-diag-image.sh ~/helikon-test.img
   ```

2. Copy it to the iPod root, verify the outer volume, and eject:

   ```sh
   cp ~/helikon-test.img "/Volumes/<iPod>/helikon-test.img"
   diskutil verifyVolume "/Volumes/<iPod>"
   diskutil eject "/Volumes/<iPod>"
   ```

3. Put the iPod into DFU: hold **Menu + Select** for about 12 seconds and
   release at the black screen.

4. With the iPod still in DFU, send the prebuilt installer:

   ```sh
   tools/install-write-diagnostic.sh
   ```

   The script verifies the DFU hash, scans for the DFU device and sends
   `bootloader-ipod6g-write-diag.dfu`. It never uses `--single`.

   If `mks5lboot` is missing, set `MKS5LBOOT=/path/to/mks5lboot`, or build it
   from the donor checkout:

   ```sh
   cd /path/to/hfsplus-rockboxed-ipod
   git checkout 55603bd4bc2524ded900fa17feae99a223b04349
   make -C utils/mks5lboot -j"$(sysctl -n hw.ncpu)" CC=clang \
     LDOPTS="-framework IOKit -framework CoreFoundation"
   cd /path/to/Helikon-OS
   MKS5LBOOT=/path/to/hfsplus-rockboxed-ipod/utils/mks5lboot/mks5lboot \
     tools/install-write-diagnostic.sh
   ```

5. The iPod reboots into the write diagnostic. Read the report on screen. It
   waits for a button and powers off.

6. Copy the inner image back and check it:

   ```sh
   cp "/Volumes/<iPod>/helikon-test.img" ~/helikon-test-after.img
   DEV=$(hdiutil attach -nomount -nobrowse ~/helikon-test-after.img |
         awk 'NR == 1 { print $1 }')
   fsck_hfs -fn "$DEV"
   hdiutil detach "$DEV"
   ```

   It must report the volume OK and no repair.

7. Recovery, if needed:

   ```sh
   ./build/write-diagnostic-bootloader/src/utils/mks5lboot/mks5lboot \
     --dfusend bootloader/artifacts/bootloader-ipod6g-uninstall.dfu
   ```

   Or `--bl-uninst ipod6g`.

   Or use the donor `mks5lboot` path. Do not format, erase or delete the
   journal.

## Prepare the inner image

On macOS:

```sh
tools/make-write-diag-image.sh helikon-test.img
```

Copy `helikon-test.img` to the root of the iPod volume, then eject cleanly. The
iPod volume must have at least 16 MiB free and its journal must be clean. Do not
compress the file and do not place it in a folder.

The diagnostic requires a root-level ASCII name exactly `helikon-test.img`. It
rejects a missing file, a non-journaled inner image, an inner image smaller than
16 MiB, an outer journal that is dirty, an extent map that overlaps outer
metadata, and an outer volume whose catalog/extents/allocation forks themselves
use extents overflow (it fails closed rather than risk an unmapped metadata
block).

## Install

Follow [`DFU.md`](DFU.md) for the backup, DFU install and recovery procedure.
The hard rules are unchanged:

- verify the SHA-256 above;
- DFU only; **never** use `mks5lboot --single`;
- never format, erase, repartition or delete the journal;
- keep a verified backup and a recovery path.

## Expected report

A healthy run shows:

```text
HELIKON-OS WRITE DIAG
Outer HFS+: PASS
Test image: PASS
Inner HFS+: PASS
Inner catalog: PASS
Inner journal: PASS
Folder: PASS
Create: PASS
Append: PASS
Readback: PASS
Rename: PASS
Truncate: PASS
Delete: PASS
Splash: PASS
Inner writes: <n>
Only the test image was written.
Outer metadata untouched.
Copy the test image to macOS for fsck.
```

A failure is reported as `FAIL` or `UNSUPPORTED` on the first failed stage, with
an error code. The image stops; it does not retry or repair.

## After the run

1. Power off when prompted.
2. Connect in Disk Mode and copy `helikon-test.img` back to macOS.
3. Run `fsck_hfs -fn` on it; it must report the volume OK and no repair.
4. Optionally mount it and confirm the `diag` folder is gone.
5. Run `diskutil verifyVolume` on the outer iPod volume as well.

If `fsck_hfs` reports a repair, any error, or a dirty journal, **stop**. Preserve
the image, record the report and do not run the write diagnostic again on that
image. Recreate `helikon-test.img` from macOS before another attempt.

## First hardware run — 2026-11-03

The owner installed the image on a 6G by DFU with `--dfusend` and no
`--single`. **Every screen stage passed.** The extracted inner image
`helikon-test.img` was 16,777,216 bytes, SHA-256
`156060075eef60c463f421ce459c2138a00e1c43de44da802b77ce89c79ca0e8`.
`fsck_hfs -fn` exited 0 and reported `The volume HelikonInner appears to be
OK`; the inner `diag` folder was gone, and `diskutil verifyVolume` exited 0 on
the outer `IPOD` volume.

This proves the inner-image write sequence and the outer-metadata guard on one
6G run. It does not prove library writes, power-cut recovery, adapter
compatibility or release readiness.

## What is not claimed

- Only one hardware run of this image is recorded.
- The writer still lacks metadata parity, a wider macOS name corpus and a
  device power-cut matrix.
- The production bootloader remains read-only; this image does not change that.
