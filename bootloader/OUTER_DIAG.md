# Outer-volume write diagnostic — separate, DFU-only, unproven

> Status: **host-tested; no device run.** Not a release and not a support claim.
> Owner: boot/release lead
> Last updated: 2026-11-04
> Source: D-03, D-04, D-22, D-25; `docs/outer-volume-test.md`; `docs/state.md`

## What this image is

This is the **outer-volume** write diagnostic. Unlike
[`WRITE_DIAG.md`](WRITE_DIAG.md), it does not write inside a test image. It
writes the real outer HFS+ volume's journal, catalog, allocation bitmap and
volume header, and creates one dedicated folder:

```text
/helikon-outer-test/
  marker          contents: HELIKON-OUTER-TEST
  test            created, appended 4096 bytes, read back, renamed
  renamed         truncated, then deleted
```

The folder and marker are deleted at the end. No library file, no other root
entry and no `.rockbox`/`.rockpod-rw` file is touched.

The image refuses to run unless all of these are true:

- the volume is native HFS+ v4 and was probed through its primary header;
- the volume is journaled, the journal header is valid and `start == end`;
- a root-level `helikon-outer-ok.txt` exists with the exact magic
  `HELIKON-OUTER-WRITE-OK` (a trailing newline is accepted);
- `/helikon-outer-test` does not already exist;
- the catalog and allocation forks are inline (the current reader fails closed
  on extents-overflow metadata rather than risk an unmapped block);
- the volume and journal block sizes are supported by the writer.

It never replays a dirty journal and never repairs or formats anything. A dirty
journal or a failed probe is reported as `FAIL`/`UNSUPPORTED` with zero writes.

## Artifact

| Field | Value |
|---|---|
| Image | `bootloader/artifacts/bootloader-ipod6g-outer-diag.ipod` |
| Image size | 109,288 bytes |
| Image SHA-256 | `9eacde18ee0b6b8d2c583ea1786f1b079c8f76b78c0dddbcad80e201a23692b2` |
| DFU installer | `bootloader/artifacts/bootloader-ipod6g-outer-diag.dfu` |
| DFU size | 117,424 bytes |
| DFU SHA-256 | `dd027696b1710985fd9979904d2be8b29dcee48727819b95f26a5929b75ece0a` |
| Uninstaller DFU | `bootloader/artifacts/bootloader-ipod6g-uninstall.dfu` |
| Uninstaller SHA-256 | `203e836412b71d3a16b3b57a5bf7ef034a0f5dcae1ca193685b8bfee33868a3a` |
| Marker | `HELIKON-OS OUTER DIAG` |
| `_moveend` | `0x2203b2e0` |
| `MOVE_AREA` | `0x22020800`–`0x22040000`; 19,744 bytes headroom |
| Donor base | `hfsplus-rockboxed-ipod` `55603bd4bc2524ded900fa17feae99a223b04349` |
| Build | `tools/build-outer-diagnostic-bootloader.sh` |

The hash belongs to this artifact only. Do not compare it with the read-only or
inner-image diagnostic hashes.

## Hard preconditions

Do not install until every item is true:

- [ ] A **full** backup of the entire iPod volume exists: music, `.rockbox`,
      `/.rockpod-rw` and all hidden files.
- [ ] The backup was read back and verified (`diff -qr`, or a block-image hash).
- [ ] A restore has been rehearsed, or the owner explicitly accepts that the
      verified backup is the recovery path.
- [ ] `diskutil verifyVolume` passes and the volume was ejected cleanly.
- [ ] `helikon-outer-ok.txt` is at the iPod root with the exact magic.
- [ ] `/helikon-outer-test` does not exist.
- [ ] The owner confirms the test window.
- [ ] The DFU SHA-256 matches the value above.

If any box is unchecked, stop. Never format, erase, repartition or delete the
journal as a repair.

## Prepare the opt-in file

Connect the iPod in Disk Mode and use the helper:

```sh
tools/make-outer-opt-in.sh /Volumes/<iPod>
```

Or create the file manually:

```sh
printf 'HELIKON-OUTER-WRITE-OK\n' > "/Volumes/<iPod>/helikon-outer-ok.txt"
```

Then verify and eject:

```sh
diskutil verifyVolume "/Volumes/<iPod>"
diskutil eject "/Volumes/<iPod>"
```

The opt-in file is not deleted by the diagnostic. Remove it after the test if
you do not want to allow another run.

## Install

1. Build or verify the image:

   ```sh
   DONOR_DIR=/path/to/hfsplus-rockboxed-ipod \
     ./tools/build-outer-diagnostic-bootloader.sh
   shasum -a 256 bootloader/artifacts/bootloader-ipod6g-outer-diag.ipod
   ```

2. Put the iPod in DFU: hold **Menu + Select** for about 12 seconds and release
   at the black screen.

3. Send the installer:

   ```sh
   tools/install-outer-diagnostic.sh
   ```

   It verifies the DFU hash, scans for the device and sends the image. It never
   uses `--single`.

4. The iPod reboots into `HELIKON-OS OUTER DIAG`, shows the report, waits for a
   button and powers off.

## Expected report

A healthy run shows:

```text
HELIKON-OS OUTER DIAG
Opt-in: PASS
Outer HFS+: PASS
Journal: PASS
Catalog: PASS
Cleanup: PASS
Folder: PASS
Create: PASS
Append: PASS
Readback: PASS
Rename: PASS
Truncate: PASS
Delete file: PASS
Delete folder: PASS
Restored: PASS
Writes: <n>
Only /helikon-outer-test was touched.
Check fsck_hfs and the backup.
```

`Restored: PASS` means the root file and folder counts returned to their
pre-test values and `/helikon-outer-test` is gone. The write count and free-block
delta are evidence; a catalog-fork growth can leave the free-block count one or
more blocks lower, which is a legitimate allocation and not a leak. A failure is
`FAIL` or `UNSUPPORTED` on the first failed stage, with an error code. The image
stops; it does not retry or repair.

## After the run

1. Connect the iPod in Disk Mode.
2. Run `fsck_hfs -fn` on the outer volume; it must report the volume OK and no
   repairs.
3. Mount the volume in macOS. `/helikon-outer-test` must be absent. Compare the
   library tree with the backup, excluding macOS metadata (`.DS_Store`,
   `.Spotlight-V100`, `.fseventsd`, `.Trashes`, `.TemporaryItems`,
   `.DocumentRevisions-V100`) and `helikon-outer-ok.txt`; mounting itself can
   touch those. Run `diskutil verifyVolume`.
4. Record the image hash, report text, `fsck_hfs` output, `diff -qr` result and
   `diskutil` result in `docs/device-matrix.md`.
5. Remove `helikon-outer-ok.txt` after the evidence is recorded.

If the diagnostic reports `Cleanup: FAIL` because `/helikon-outer-test` already
exists, it did not write. Mount macOS, inspect that folder, and remove it only
if it is the Helikon test folder. The diagnostic deliberately does not delete a
pre-existing folder automatically.

## Stop conditions

Stop and restore from the backup if any of these happen:

- `fsck_hfs` reports a repair, orphan, bad catalog/bitmap or dirty journal;
- any library file is missing, changed or unreadable;
- the test folder remains after a successful report;
- the diagnostic reports `FAIL` or writes outside `/helikon-outer-test`;
- the device hangs, crashes or powers off unexpectedly.

If the journal is dirty after an interrupted run, boot macOS and let it replay
the journal, or use the recorded recovery path. Do not delete the journal and do
not run the diagnostic again until the volume is verified.

## Power-cut behaviour

This image has no cut target and no automatic cut. A real power loss during the
run leaves the outer journal dirty. On the next boot the read-only production
diagnostic and this outer diagnostic both refuse a dirty volume; macOS must
replay the journal before another attempt. This is deliberate.

## Host test

```sh
make -C tests/image outer-diag-test
```

The host test creates a disposable 32 MiB HFS+J image and checks:

- the full create/append/readback/rename/truncate/delete sequence;
- `fsck_hfs -fn` after the sequence and after a macOS journal replay;
- a dirty journal is refused with zero writes;
- a missing opt-in file is refused with zero writes;
- a pre-existing unmarked or marked `/helikon-outer-test` is left untouched;
- a sentinel file is unchanged;
- a second run succeeds on the same volume;
- a patched 4 KiB journal-header image runs the full sequence and stays
  `fsck_hfs` clean.

It does not prove device behaviour, timing, ATA errors, power loss or the real
volume's catalog layout.

## Journal compatibility note

The shared journal writer now sets the Apple `block_list_header.max_blocks`
field and the `BLHDR_FIRST_HEADER` flag. Without those fields macOS rejects the
journal on replay. The host test leaves a committed-but-unreplayed transaction
and requires macOS `fsck_hfs` to replay it successfully. The inner-image
artifact `81350f…` predates this fix; rebuild it before using the inner
diagnostic for a new power-cut run.
