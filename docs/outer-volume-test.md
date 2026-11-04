# Outer-volume write test — real HFS+ volume

> Purpose: the first write test on the iPod's **real** outer HFS+ volume.
> Status: `in progress`; image built and host-tested, no device run. Host disposable-image validation is done.
> Owner: project owner; HFS+/boot leads
> Last updated: 2026-11-04
> Source: D-03, D-04, D-22, D-25; `docs/device-matrix.md`; `docs/validation.md`

This is the bridge from the inner-image write diagnostic to a real HFS+ volume.
It writes the outer volume's journal, catalog, bitmap and a dedicated test
folder. It is **not** safe on the only copy of the library.

## 1. Hard preconditions

Do not install or run the outer-write image until every item is true:

- [ ] A **full** backup of the entire iPod volume exists: music, `.rockbox`,
      `/.rockpod-rw` and all hidden files. The `.rockbox`/`/.rockpod-rw` backup
      used for the read-only diagnostic is not sufficient.
- [ ] The backup destination has enough free space. The current 6G reports about
      71 GiB used; a file-level backup needs at least that, a block image needs
      the full 128 GB partition.
- [ ] The backup was read back and verified (`diff -qr`, or a block-image hash).
- [ ] A recovery path is written and tested: restore from the backup, or Apple
      restore as the last resort.
- [ ] `diskutil verifyVolume /Volumes/IPOD` passes.
- [ ] The outer journal is clean and the volume was ejected cleanly.
- [ ] The outer-write diagnostic image is built and its SHA-256 recorded
      (`32eeb541…`, see [`../bootloader/OUTER_DIAG.md`](../bootloader/OUTER_DIAG.md)).
- [ ] `helikon-outer-ok.txt` with the magic `HELIKON-OUTER-WRITE-OK` is at the
      iPod root and `/helikon-outer-test` does not exist.
- [ ] The owner explicitly confirms the backup and the test window.

If any box is unchecked, stop. Never format, erase, repartition or delete the
journal as a repair.

## 2. Backup

File-level backup on macOS:

```sh
VOL="/Volumes/IPOD"
DEST="/Volumes/Backup/Helikon-iPod-$(date +%F_%H%M%S)"
mkdir -p "$DEST"
ditto "$VOL" "$DEST"
diff -qr "$VOL" "$DEST" && echo "backup matches"
```

For a block-level image, unmount first and use a destination with at least the
partition size:

```sh
diskutil unmount "$VOL"
sudo dd if=/dev/rdisk4s1 of=/Volumes/Backup/ipod-classic-6g.img bs=4m
shasum -a 256 /Volumes/Backup/ipod-classic-6g.img
```

The block image is exact but large. A file-level backup is acceptable only if
`diff -qr` passes and a restore has been rehearsed.

## 3. Test target and image

The device test uses a dedicated root folder, never a library file:

```text
/helikon-outer-test/
  marker          contents: HELIKON-OUTER-TEST
  test            created, appended 4096 bytes, renamed, truncated, deleted
```

The outer-write diagnostic:

- requires the root file `helikon-outer-ok.txt` with the magic
  `HELIKON-OUTER-WRITE-OK`;
- refuses a dirty journal, a non-HFS+ v4 volume, an unsupported catalog or
  allocation fork, or an existing `/helikon-outer-test`;
- creates only `helikon-outer-test` and its `marker`/`test` files, appends 4096
  bytes, reads back, renames, truncates, deletes, and removes the folder;
- reports PASS/FAIL, a write count and `Restored: PASS` only when the root file
  and folder counts return to the pre-test values; the free-block delta is
  evidence and may reflect legitimate catalog-fork growth;
- never touches another root entry;
- is a separate DFU-only image, not the read-only or inner-image diagnostic.

The image is built (`32eeb541…`, `bootloader-ipod6g-outer-diag.ipod`) and the
host test passes. A previous image (`9eacde18…`) reached rename on the 6G and
stopped before truncate; its volume was fsck-clean and the next boot stopped at
`Cleanup: FAIL -14`. The current image adds in-place record updates and a
stage-progress screen but has no device run. See
[`../bootloader/OUTER_DIAG.md`](../bootloader/OUTER_DIAG.md) for the runbook.
T-047 tracks the build; T-048 tracks the device run.

## 4. Run and pass criteria

1. Complete and verify the backup.
2. Install the separate outer-write image by DFU (never `--single`) with
   `tools/install-outer-diagnostic.sh`.
3. Run the sequence. It should create, append, read back, rename, truncate and
   delete `helikon-outer-test`.
4. `fsck_hfs -fn` on the outer volume: zero repairs.
5. Mount in macOS: no `helikon-outer-test` remains; the library tree matches the
   backup, and `diskutil verifyVolume` passes. Compare the library paths and
   exclude macOS metadata and the opt-in file, because mounting can touch
   `.DS_Store`, Spotlight and `.fseventsd`:

   ```sh
   diff -qr \
     --exclude=.DS_Store --exclude=.Spotlight-V100 --exclude=.fseventsd \
     --exclude=.Trashes --exclude=.TemporaryItems \
     --exclude=.DocumentRevisions-V100 --exclude=helikon-outer-ok.txt \
     "$DEST" "$VOL"
   ```

   Record every remaining difference. A difference under `Music`,
   `iPod_Control`, `.rockbox` or `.rockpod-rw` is a stop condition.
6. Record the image hash, report text, `fsck_hfs` output and backup comparison in
   `docs/device-matrix.md`.
7. Remove `helikon-outer-ok.txt` after recording the evidence.

## 5. Stop conditions

Stop and restore from the backup if any of these happen:

- `fsck_hfs` reports a repair, orphan, bad catalog/bitmap or dirty journal;
- a library file is missing, changed or unreadable;
- the test folder remains after a successful report; if `Cleanup: FAIL` with
  `HFS_ERR_BUSY` appears before any write, inspect the existing folder with
  `tools/cleanup-outer-test.sh /Volumes/<iPod>` and do not delete unknown data;
- the diagnostic reports FAIL or writes outside `helikon-outer-test`;
- the device hangs, crashes or powers off unexpectedly.

Preserve the diagnostic report and logs. Do not retry writes on the same volume.
Do not format, erase, repartition or delete the journal.
