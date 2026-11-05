# F2 bootstate promotion — host and 6G runbook

> Purpose: build, verify and run the T-055/F2 image that promotes
> `/.helikon/bootstate` from `TRYING` to `GOOD`.
> Status: host-verified; no F2 device run.
> Owner: firmware lead; test lead for the device run.
> Last updated: 2026-11-04
> Source: T-055, D-02, `docs/firmware-architecture.md` §7.4,
> `bootloader/LOADER.md` §3, `docs/hfsplus-write-engine.md` §10.

This is the first firmware write. It writes **only** `/.helikon/bootstate`.
There is no library write, no journal replay, no settings write and no VFS in
this phase.

## 1. What the image does

```text
BOOT -> PMU -> LCD -> ATA -> PART -> HFS -> READ -> INPUT -> WRITE -> PASS
```

F1's read-only ladder runs unchanged. At `WRITE` the firmware:

1. reads `/.helikon/bootstate` through HFS+;
2. computes SHA-256 over the running image (`__image_start..__image_end`);
3. finds the slot matching board, image version, load address, size and hash;
4. refuses to write if the journal is dirty or the slot does not match;
5. promotes a `TRYING` slot to `GOOD` with a sequence higher than every valid
   slot, and recomputes the slot checksum;
6. overwrites the 512-byte file in place through `hfs_catalog_overwrite_file`
   and `hfs_txn` (journal append, replay, flush);
7. reads the file back, re-parses it and verifies `GOOD` and the sequence;
8. shows `WRITE` / `Bootstate OK` and then `PASS`.

If the slot is already `GOOD`, the firmware performs no write and reports
`ALREADY GOOD`.

## 2. Preconditions

- [ ] A verified backup of the iPod library and a known restore path.
- [ ] The 6G is on the reference iFlash SD or the documented test device.
- [ ] The volume journal is clean. A dirty volume stays read-only.
- [ ] The loader test image is installed by DFU only. Never use `--single`.
- [ ] The three device files are the matching F2 set; do not mix an F1 image
      with an F2 manifest or bootstate.

## 3. Build and host gates

From the repository root:

```sh
make -C firmware BOARD=6g VERSION=2 BOOTSTATE_STATE=trying image
./tools/check-f2-artifact.sh build/firmware/6g/out
make -C tests/firmware test
make -C tests/firmware sanitize
make -C tests/image f2-test
make -C tests/image crash-test
```

The host gates must show:

- the image and bootstate are version 2, board 6, load `0x08000000`;
- the bootstate starts `TRYING`, sequence 1;
- `check-f2-artifact.sh` reports `loader: OK` / `bootstate: DEFAULT` before
  promotion, then `promoted sequence 2, readback GOOD`, clean `fsck_hfs`, and
  `bootstate: GOOD` through the real loader host core;
- the overwrite crash matrix covers write, flush and partial-sector cuts and
  every recovered image is `fsck_hfs` clean.

Do not continue if any host gate fails. A host pass is not a device run.

## 4. Device files

Mount the iPod volume on macOS and replace the three files together:

```text
/.helikon/firmware.img
/.helikon/firmware.manifest
/.helikon/bootstate
```

Copy them from `build/firmware/6g/out/`; verify each SHA-256 against
`build/firmware/6g/out/SHA256SUMS`; eject cleanly and run `fsck_hfs -fn` before
booting. The bootstate must be `TRYING` at this point. The loader test image
will show `Boot state: DEFAULT` and still execute the signed image; that is the
expected pre-promotion state, not a failure.

## 5. Device run

1. Boot the loader test image.
2. At `PRESS ANY BUTTON`, press a button to run the self-test.
3. Record the screen at `WRITE` and `PASS`.
4. Confirm the screen says `PROMOTED`, `BOOTSTATE GOOD SEQ 2` and `NO WRITE` is
   absent.
5. Power off normally (the F2 image waits for USB to be removed).
6. From macOS, run `fsck_hfs -fn`; it must report the volume OK and must not
   repair anything.
7. Compare the volume with the pre-run listing. Only `/.helikon/bootstate`
   should differ.
8. Boot again. The loader must show `Boot state: GOOD`.
9. Copy `/.helikon/bootstate` off the device and verify slot 0 or slot 1 is
   `GOOD` with sequence 2 and a valid checksum.

Record the image hash, bootstate hash, device/adapter, stage timings, screen
photo, `fsck_hfs` output and the second loader boot.

## 6. Pass criteria

- `WRITE` passes and the screen shows `PROMOTED`.
- `PASS` appears and the device powers off.
- `fsck_hfs -fn` is clean; no repair is attempted.
- No file other than `/.helikon/bootstate` changes.
- The next loader boot selects `GOOD`.
- The bootstate checksum is valid and the sequence increased.

## 7. Power-cut test

After the clean run, repeat the promotion with a controlled power cut during
the `WRITE` stage. The acceptable recovered states are:

- the old `TRYING` slot with sequence 1, or
- the new `GOOD` slot with sequence 2.

Any partial slot, invalid checksum, dirty journal that is not replayed by the
write engine, or non-clean `fsck_hfs` is a stop condition. The host overwrite
matrix already injects write, flush and partial-sector cuts; the device cut is
still required for T-055.

## 8. Failure handling

- `BOOTSTATE`: stop. The three device files do not match; restore the matching
  set and do not retry the write.
- `DIRTY`: the volume did not pass the clean-journal gate. No write was
  attempted. Recover from the backup.
- `IO`: stop and preserve the image and screen. Do not retry with a different
  parameter.
- `READ-ONLY`: the board write gate stayed closed. No write was attempted.

Never format, delete the journal or "repair" the volume as a recovery step.

## 9. Loader scratch reservation

The loader test image keeps its own BSS, framebuffer and LCD DMA scratch at
`0x08800000`–`0x08884200`. F2 reserves `0x08800000`–`0x0888ffff` and starts the
firmware BSS at `0x08890000`. The kernel region is still
`0x08800000`–`0x09000000`; this is a sub-allocation.

Do not move the firmware BSS back to `0x08800000` without also changing the
loader test image to quiesce its display controller before the jump. F1's
229 KB BSS stopped below the active LCD DMA buffers; F2's 601 KB BSS reached
them. An F2 image built with BSS at `0x08800000` blanks the loader screen as
soon as `_start` zeroes BSS and can leave the display controller in a state
where the firmware never reaches its first frame. The linker asserts
`__bss_start >= 0x08890000`.

## 10. Early-display diagnostic build

If the loader reaches `Executing signed test payload...` and the screen goes
black before the F2 title, build a diagnostic image that initializes the LCD
and paints a marker **before** PMU/backlight init:

```sh
make -C firmware BOARD=6g VERSION=3 BOOTSTATE_STATE=good \
  BUILD=../build/firmware/6g-diag \
  EXTRA_CFLAGS=-DHELIKON_DIAG_EARLY_DISPLAY image
./tools/check-loader-artifact.sh build/firmware/6g-diag/out
```

Copy `build/firmware/6g-diag/out/{firmware.img,firmware.manifest,bootstate}` as
one set. The diagnostic screen shows:

```text
HELIKON-OS
EARLY DISPLAY OK
F2 CODE RUNNING
BEFORE PMU INIT
```

If that text appears, F2 entry, BSS and LCD init work; the failure is in PMU or
a later stage. If it succeeds it then paints `PMU OK / DIAGNOSTIC COMPLETE` and
powers off. If the loader text remains, the failure is before the LCD path.

The diagnostic image does not read or write bootstate. It is not F2 and must be
replaced by the matching F2 set after the test.

## 11. Current evidence

- F2 image: 75,472 bytes, SHA-256 `5e14793f…`, BSS `0x08890000`–`0x08922e60`,
  bootstate `TRYING` sequence 1.
- Host: ten firmware test groups, sanitizer-clean; `test_catalog_write`
  overwrite test; `run-f2-macos.sh`; `run-crash-macos.sh` overwrite matrix.
- `tools/check-f2-artifact.sh`: promoter + clean `fsck_hfs` + loader `GOOD`.
- No F2 device run and no device power-cut run yet.
