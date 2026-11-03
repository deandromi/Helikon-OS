# Outer-volume write test — real HFS+ volume

> Purpose: the first write test on the iPod's **real** outer HFS+ volume.
> Status: `planned`; not built and not run. Host disposable-image validation is done.
> Owner: project owner; HFS+/boot leads
> Last updated: 2026-11-04
> Source: D-03, D-04, D-22; `docs/device-matrix.md`; `docs/validation.md`

This is the bridge from the inner-image write diagnostic to a real HFS+ volume.
It writes the outer volume's journal, catalog, bitmap and a dedicated test
folder. It is **not** safe on the only copy of the library.

## 1. Hard preconditions

Do not build or run the outer-write image until every item is true:

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
- [ ] The outer-write diagnostic image is built and its SHA-256 recorded.
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
  test            created, appended 4096 bytes, renamed, truncated, deleted
```

The outer-write diagnostic must:

- require an opt-in root file, e.g. `helikon-outer-ok.txt` with a magic string;
- refuse if the outer journal is dirty or the volume is not HFS+ v4;
- create only `helikon-outer-test`, append/read/rename/truncate/delete its file,
  then delete the folder;
- report PASS/FAIL and write count;
- never touch another root entry;
- be a separate DFU-only image, not the read-only or inner-image diagnostic.

The image is not built yet. T-047 tracks it; the host outer-volume test already
covers the same writer sequence on a disposable image.

## 4. Run and pass criteria

1. Complete and verify the backup.
2. Install the separate outer-write image by DFU (never `--single`).
3. Run the sequence. It should create, append, read back, rename, truncate and
   delete `helikon-outer-test`.
4. `fsck_hfs -fn` on the outer volume: zero repairs.
5. Mount in macOS: no `helikon-outer-test` remains; the library tree matches the
   backup (`diff -qr`), and `diskutil verifyVolume` passes.
6. Record the image hash, report text, `fsck_hfs` output and backup comparison in
   `docs/device-matrix.md`.

## 5. Stop conditions

Stop and restore from the backup if any of these happen:

- `fsck_hfs` reports a repair, orphan, bad catalog/bitmap or dirty journal;
- a library file is missing, changed or unreadable;
- the test folder remains after a successful report;
- the diagnostic reports FAIL or writes outside `helikon-outer-test`;
- the device hangs, crashes or powers off unexpectedly.

Preserve the diagnostic report and logs. Do not retry writes on the same volume.
Do not format, erase, repartition or delete the journal.
