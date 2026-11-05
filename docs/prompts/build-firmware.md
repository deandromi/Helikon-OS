# Prompt — build the Helikon-OS firmware (F1–F5)

> Purpose: a self-contained execution prompt for an agent that will create the
> first Helikon-OS firmware for the iPod Classic 6G.
> Status: living — full-procedure prompt; F1/T-054 complete with an owner-reported
> 6G run (2026-11-04); F2/T-055 is implemented and host-verified, with its 6G
> run next.
> Owner: firmware lead; docs lead
> Last updated: 2026-11-04
> Source: `agents.md`, `docs/firmware-architecture.md`, ADR-0003, ADR-0004,
> `bootloader/LOADER.md`, `docs/tasks.md`, `docs/invariants.md`

**This is a prompt, not a specification.** The canonical sources are
`agents.md`, the root `README.md`, `docs/design-paper.md`, `docs/state.md`,
`docs/decisions.md` and the ADRs. If this prompt conflicts with them, the
canonical source wins and you record the conflict in `docs/decisions.md`. Do not
invent a decision, a measurement or a device result to make the prompt fit.

**How to use this prompt.** Read it end to end before you touch a file. Then
read the required documents in §4. Your first authorized deliverable is **F1 /
T-054**; F2–F5 are specified so you understand the destination, not so you build
them in one run. Stop after F1, report, and wait for an explicit instruction to
continue. If the owner explicitly authorizes more, proceed one phase at a time
and do not skip a gate.

---

## 0. Role and mission

You are the **firmware implementation agent** for Helikon-OS. You write the
kernel, HAL, block layer, VFS, player, UI and diagnostics that run after the
verified bootloader jumps. You do not write the bootloader, the HFS+ writer or
the release tooling unless a task explicitly says so.

Your mission is to turn the accepted design into a **read-only, device-proven
firmware skeleton first**, then a **single safe firmware write**, then a
**read-only library browser**, then the **first-party UI**, then **audio**. You
do not start with the player. You do not start with the UI. You do not start
with the writer. You start with the boot chain, because every later layer
depends on it and because a player that cannot boot reliably is not a product.

The project is design-stage. F1 now exists and has one owner-reported 6G run;
F2–F5 do not. The HFS+ core is host-tested and real; the bootloader is
device-tested. Your job is to grow the firmware one phase at a time without
breaking the safety rules.

---

## 1. Authorized scope and phase gates

### 1.1 First authorization: F1 only

Your first authorized task is **T-054 / F1 — the read-only firmware skeleton**.
It must:

1. build as a signed image at `0x08000000`;
2. start from the bootloader jump without returning;
3. show a banner on the 6G panel;
4. initialize ATA and read a block;
5. probe the HFS+ volume and open the catalog;
6. read one known file through the existing `firmware/hfs/` core (for example
   `/.helikon/firmware.manifest`, which the loader already verified);
7. report `PASS` on screen;
8. power off cleanly;
9. write **nothing** to the filesystem;
10. record a device run with a screen photo and `fsck_hfs -fn` clean.

**Status 2026-11-04: F1/T-054 is complete.** The owner reports the 6G device run
accepted; the image is `d7916411…` (39,108 bytes). **F2/T-055 is implemented and
host-verified** (75,520 bytes, `f74e3485…`, `TRYING` sequence 1); its device run
and power-cut check are the remaining gate. See
[`../f2-bootstate-test.md`](../f2-bootstate-test.md).

Do not start F2 until F1 has a recorded device run and the owner (or the next
explicit session instruction) says continue.

### 1.2 Destination: F1–F5

| Phase | Task | Deliverable | Gate |
|---|---|---|---|
| F1 | T-054 | Boot skeleton + signed image | Banner, ATA read, HFS+ probe, one file read, PASS, power-off; no writes; `fsck_hfs` clean |
| F2 | T-055 | First firmware write | Bootstate `TRYING`→`GOOD` through `hfs_txn`, flush, readback, loader sees `GOOD`; `fsck_hfs` clean |
| F3 | T-056 | Read-only VFS + browser | Browse a real library; unsupported objects listed with reasons; no writes except bootstate |
| F4 | T-057 | UI renderer + screens | Typography/layout/now playing/settings/errors; panel check |
| F5 | T-058 | Audio | One codec, play/pause/seek/next/previous/volume; underruns handled |

T-059 is the bootloader-side fill of the boot-info page. It is not your code
unless the owner explicitly assigns it; F1 can run with the zero-`r0` fallback.

### 1.3 What you must not build

Until the phase gate says otherwise, do not build:

- USB mass storage or a USB service mode;
- a database or metadata index;
- skins, apps, games or a sandbox;
- podcast, car or desktop-app features;
- update installation or staging;
- 7G or any second board;
- battery optimization or thermal tuning;
- a writable music library;
- a "repair", format or journal-delete action;
- an in-firmware shell or scripting language.

Saying no is part of the job.

---

## 2. Hard stops — never do these

These are not suggestions. If a step requires one of them, stop and report.

1. **Never write a filesystem outside the transaction engine.** F1 writes
   nothing. F2 writes only `/.helikon/bootstate`, through `hfs_txn`.
2. **Never bypass, delete, truncate, ignore or zero the journal.** A dirty or
   invalid journal mounts read-only. Replay belongs to the write engine and must
   finish before the first write.
3. **Never format, erase, restore, repartition or "repair" metadata** as a
   software fix. Recovery is read-only diagnosis, DFU or Apple restore.
4. **Never write from the bootloader.** It is read-only. Do not add a write path
   to it, do not replay its journal, do not make it load an unsigned image.
5. **Never use `--single`.** It destroys the Apple NOR path.
6. **Never load or execute unsigned code.** No unsigned firmware, plugin,
   update, skin with code or test payload in a production path.
7. **Never test a write on the only copy of the library.** A verified backup and
   a recovery plan are required first.
8. **Never claim a device result you did not record.** A host test is not a
   device run. A link is not a boot. A screen photo is not `fsck_hfs`.
9. **Never invent a measurement, timing, battery number, RAM figure or
   temperature.** Mark it `unproven` or measure it.
10. **Never commit secrets, private keys, device backups, disk images, serial
    numbers or personal data.**
11. **Never edit the root `README.md` or the donor repository.** The root README
    is owner-authored; the donor is read-only source material.
12. **Never silently change a frozen format.** Manifest, bootstate, boot-info
    and error codes change only through a recorded decision and a version bump.
13. **Never let a skin, font or artwork failure produce a blank screen.** Fall
    back to the first-party UI and report the failure.
14. **Never let the render path block on storage.** If a screen needs data, load
    it before the frame or show a bounded loading state.
15. **Never return from the firmware entry.** A return is a panic, not a loader
    recovery path.
16. **Never add a target `#ifdef` to shared core code.** The BSP supplies
    constants and operations; the core stays target-neutral.
17. **Never allocate outside a fixed pool or the boot arena.** No `malloc`.
18. **Never use floating point** until the FPU question is answered by the board
    audit; use integer/fixed-point.
19. **Never write a log to a failing filesystem.** Diagnostics are RAM-only
    until the filesystem is healthy and the user exports.
20. **Never hide an unsupported file or object.** List it with a reason.

If you hit a hard stop, record the exact stage, error code, device state and what
you did **not** attempt. Do not "try one more write".

---

## 3. Current state you must assume

### 3.1 What exists

- `firmware/hfs/` — host HFS+ read core, writer, journal, transaction engine,
  bitmap, B-tree insert/delete, catalog create/delete/rename, extents overflow,
  Unicode normalization. **104 host tests**, sanitizer-clean. This is real code;
  reuse it, do not rewrite it.
- `bootloader/` — read-only diagnostic bootloader, inner-image and outer-volume
  write diagnostics, loader test image, manifest/bootstate verification,
  Ed25519, SHA-256, HFS adapter. **31 host tests**, sanitizer-clean.
- The loader test image reads `/.helikon/firmware.manifest`, `firmware.img` and
  `bootstate`, verifies Ed25519 before any image byte, verifies SHA-256/board/
  range, loads to the signed address and jumps. The final-splash `6bc6927c…`
  image is owner-reported on one 6G/iFlash SD: 6.5 s startup, `fsck_hfs -fn`
  clean. It executes a 12-byte test payload and disables production handoff.
- A bootloader-only Alpha release `alpha-1.0.1` exists. It is not a firmware
  release and uses a build-local test key.

### 3.2 What does not exist

- No `firmware/boot/`, `firmware/hal/`, `firmware/block/`, `firmware/vfs/`,
  `firmware/player/`, `firmware/ui/`, `firmware/input/`, `firmware/lib/` or
  `firmware/diag/` code.
- No kernel, startup file, linker script, boot-info parser, panic path, HAL,
  block driver, VFS, player, UI, database or USB.
- No firmware image, manifest, signed firmware or device run.
- No release key and no certificate verifier (T-053).

### 3.3 Hardware and product facts

- iPod Classic 6G, Samsung S5L8702, ARM926EJ-S, **64 MiB DRAM**.
- Display 320×240, RGB565.
- Click wheel, buttons, hold switch.
- CS42L55 codec, line-out and headphone.
- ATA/LBA48, 512-byte logical sectors, 4096-byte support.
- PMU, battery, charging, backlight, ATA sleep.
- Loader image window `0x08000000`–`0x08800000` (8 MiB).
- No network. No external USB DAC on the 6G. No FAT mount, no overlay.
- The 6G panel, PMU, audio routing and DMA constraints must be confirmed on the
  device; do not treat donor code as a measurement.

### 3.4 Accepted decisions you must follow

- **D-01** Ed25519-signed firmware manifest.
- **D-02** two-slot bootstate with **single-image fail-closed** recovery; no A/B
  rollback (T-050).
- **D-20 / ADR-0002** offline Ed25519 root, certified release key; no release
  key exists yet.
- **D-26** no in-bootloader OF/recovery menu; native Menu+Play and DFU cover
  recovery.
- **D-27 / ADR-0003** fixed 64 MiB memory map; image 8 MiB − 4 KiB; boot-info
  page at `0x087ff000`; audio 24 MiB; database 8 MiB; reserve 8 MiB.
- **D-28 / ADR-0004** boot-info page in `r0`; no-return entry; boot-info is a
  cache, not a trust authority; firmware re-reads bootstate before writing.

Open decisions that affect later phases: **D-05** cache/transaction limits,
**D-07** database location, **D-12** battery target, **D-16** update transport.
Do not silently choose them. If a task needs one, stop and ask, or implement the
smallest reversible behavior and mark it `proposed`.

---

## 4. Required reading and source of truth

Read in this order. Do not skip the first four.

1. **`agents.md`** — the working contract, non-negotiables, hard stops and
   handoff protocol. If anything in this prompt conflicts with it, `agents.md`
   wins.
2. **Root `README.md`** — owner product intent. Do not edit it.
3. **`docs/state.md`** — what is true today.
4. **`docs/firmware-architecture.md`** — the firmware-internal design paper.
   Read §3–§9 and §12–§13 before writing code. It defines the entry contract,
   memory map, runtime model, modules, flows, ergonomics, aesthetics and phases.
5. **`docs/architecture.md`** — system boundaries, boot chain, memory budget,
   security.
6. **`docs/adr/0003-firmware-memory-map.md`** and
   **`docs/adr/0004-firmware-entry-abi.md`** — accepted decisions. Implement them.
7. **`bootloader/LOADER.md`** — manifest, bootstate, accepted entry ABI and
   failure behaviour.
8. **`docs/invariants.md`** — I-01–I-15. Quote the relevant invariant in every
   safety-relevant commit.
9. **`docs/failures.md`** — error taxonomy, mount tree, safe responses.
10. **`docs/decisions.md`** — D-01–D-28; check the status before relying on one.
11. **`docs/tasks.md`** — T-054–T-059 and the current queue.
12. **`docs/ui.md`** — typography, anti-aliasing, fallback rules (F4).
13. **`docs/testing.md`** — test layers, sanitizers, evidence.
14. **`docs/file-map.md`** — where to change what.
15. **`docs/conventions.md`** — status labels, file naming, commit style.

Optional but useful before F1: `docs/design-paper.md` §5–§7, §19, §27;
`docs/hfsplus-write-engine.md` §6–§10; `bootloader/hardware/README.md`.

The donor repository is external and read-only. You may read its hardware
sequences as reference (paper §6, verdict K/M), but you must not modify it,
copy its overlay engine or treat its tests as Helikon-OS evidence. The pinned
commit and build scripts are in `tools/build-*.sh`.

---

## 5. Definition of done

### 5.1 F1 / T-054 — read-only skeleton

**Deliverables**

- `firmware/boot/`: `_start`, vectors, linker script, boot-info parser, panic.
- `firmware/lib/`: `memcpy`, `memset`, `memcmp`, endian helpers, a bounded ring
  buffer and a fixed pool allocator.
- `firmware/hal/6g/`: clock, PMU, backlight, LCD, ATA read, minimal buttons.
- `firmware/board/6g/`: BSP constants, memory map, board description.
- `firmware/block/`: read-only block device and partition scan.
- `firmware/hfs/` adapter: `hfs_read_fn` over the block device; no change to the
  read core's semantics.
- `firmware/diag/`: stage codes and a RAM trace ring.
- A build script that produces an ELF, a raw image, a manifest and a signed
  development image with the test key under `build/`.
- Host tests for boot-info parsing, the block adapter and the boot-stage state
  machine.

**Exit evidence**

- One recorded 6G run: banner with image version, `Storage OK`, `Volume found`,
  `HFS+ OK`, `Read OK`, `Input OK`, `PASS`, clean power-off.
- Screen photo and trace ring.
- `fsck_hfs -fn` clean (no writes expected).
- Image SHA-256, size, `__image_end`, `__bss_end`, headroom and map report.
- Boot-stage timings.
- A `docs/device-matrix.md` row with the result; no support claim beyond it.

**Must not**

- write the filesystem, replay the journal, load unsigned code, claim a firmware
  product, or treat a host test as a device run.

### 5.2 F2 / T-055 — first write

**Deliverables**

- Mount read-write only after a clean journal.
- Read `/.helikon/bootstate` through the VFS.
- Validate both 256-byte slots: magic, version, sequence, board, image version,
  load address, size, SHA-256, state, checksum.
- Find the slot that matches the running image. If it is `TRYING` and the
  self-test passed, stage a `GOOD` slot with a higher sequence.
- Commit through `hfs_txn`, flush, read back, verify the checksum.
- Show the result and power off.

**Exit evidence**

- Bootstate readback matches the expected slot and checksum.
- Next loader boot sees `GOOD`.
- `fsck_hfs -fn` clean.
- A power cut during promotion recovers to a consistent state (host first,
  device when the rig exists).
- No other file changed (volume diff).
- Device-matrix row.

**Must not**

- trust boot-info as authority, write before a clean mount, retry a failed
  promotion with a different slot, touch the journal directly, or write settings
  or the library.

### 5.3 F3 / T-056 — VFS and browser

**Deliverables**

- VFS lookup/iterate/open/read/seek/close with generation-checked handles.
- A browser over the real catalog (artists/albums/tracks or folder browser
  first).
- Unsupported objects listed with reasons.
- Error screens for `NOT_HFS`, `JOURNAL_DIRTY`, `CORRUPT`, `IO`.
- No writes except F2's bootstate promotion.

**Exit evidence**

- Browse a real library on the 6G.
- Open a file and read its first bytes through the VFS.
- A malformed/unsupported fixture shows the documented error.
- UI stays responsive on a large directory (measured).
- Device-matrix row.

### 5.4 F4 / T-057 — first-party UI

**Deliverables**

- RGB565 renderer with an IBM Plex Sans atlas, grayscale anti-aliasing and a
  1-bit fallback.
- Typography and layout from `docs/ui.md`.
- Home, music, now playing, settings, system info and error screens.
- Focus/selection, click-wheel acceleration, hold.
- Screen transitions and progress feedback.
- Large-text/high-contrast hooks.

**Exit evidence**

- Host frames for every screen.
- Panel photos at normal/high brightness.
- No clipping at 11 px; selection visible in monochrome.
- Frame time measured on the device.
- A failed font/artwork load falls back and reports.
- Device-matrix row.

### 5.5 F5 / T-058 — audio

**Deliverables**

- One codec (chosen by the audio lead; do not guess).
- Buffer/read-ahead, decode, I2S/DMA output.
- Play, pause, seek, next/previous, volume.
- Line-out and headphone.
- Underrun/error handling.
- Playback clock and now-playing updates.

**Exit evidence**

- Plays a known file on the 6G.
- Seek and track change without a click or hang.
- Line-out and headphone levels sane.
- Current/battery measured for the run.
- No metadata writes during playback; `fsck_hfs -fn` clean.
- Device-matrix row.

---

## 6. First-run execution plan

This is a concrete sequence for the first ten working sessions. It is not a
schedule promise; it is an order that keeps the risky parts early.

### Session 1 — reconnaissance and skeleton

1. Read the required docs.
2. Inspect `firmware/hfs/`, `bootloader/hfs_loader.c`, `bootloader/manifest.c`,
   `bootloader/hardware/helikon_loader_hw.c` and the loader test build script.
3. Create `firmware/boot/`, `firmware/lib/`, `firmware/board/6g/`,
   `firmware/hal/6g/`, `firmware/block/`, `firmware/diag/`.
4. Add a linker script with the accepted map and `ASSERT`s.
5. Add a `Makefile` with `image`, `size`, `test` and `clean` targets.
6. Do **not** add a HAL driver yet. Get an empty image to link at `0x08000000`
   and fail cleanly if the size exceeds the cap.
7. Report: files created, build output, image end, open questions.

### Session 2 — boot-info parser and host test

1. Implement the boot-info parser from ADR-0004: magic, version, size, CRC,
   board, range, manifest copy.
2. Implement the `BOOT_INFO_INVALID` path; it must not write anything.
3. Add a host test with a synthetic valid page and bad magic/version/size/CRC/
   board/range/manifest cases.
4. Add a test for `r0 = 0` (fallback).
5. Report: test count, failures, what a bad page does.

### Session 3 — startup, vectors, panic

1. Write `_start`: mask IRQs, set SVC/IRQ/FIQ/ABT/UND stacks, zero `.bss`,
   copy `.data` if needed, install vectors, call `firmware_main`.
2. Add a guard word to each stack and a panic path.
3. Add a `panic()` that records PC/LR/SP/mode/reason, paints a solid error
   screen if the display is up, and powers off or parks.
4. Test the panic path on the host with a forced fault, then on the device when
   the display path exists.
5. Report: stack sizes, guard behavior, panic output.

### Session 4 — clock, PMU, backlight, LCD

1. Port the minimum donor register sequences into `firmware/hal/6g/`.
2. Expose operations, not registers: `clock_init`, `pmu_init`,
   `backlight_set`, `lcd_init`, `lcd_blit`.
3. Paint a solid color and then a text banner.
4. Add a timeout to every wait.
5. Report: panel photo, init order, any donor assumptions that did not hold.

### Session 5 — ATA read and block layer

1. Implement ATA identify, read and flush (flush may be a no-op in F1 if read
   only, but the API must exist and return an error).
2. Add a bounce buffer for unaligned/non-DMA-safe callers.
3. Implement the partition scan over the read-only block device.
4. Add error injection on the host: timeout, short read, CRC/ID mismatch.
5. Report: `Storage OK`/`Volume found`, read throughput (measured), errors.

### Session 6 — HFS+ read integration

1. Adapt `firmware/hfs/` to the block device with `hfs_read_fn`.
2. Probe the volume, open the catalog, read one known file.
3. Print the volume name, block size, file size and first bytes.
4. Add a host test with a disposable HFS+ image and a missing-file case.
5. Report: `HFS+ OK`/`Read OK`, error codes, any parser assumption that failed.

### Session 7 — signed image and loader jump

1. Build a raw image and a manifest with the development test key.
2. Sign the manifest; put `firmware.img`, `firmware.manifest` and `bootstate`
   on a disposable HFS+ image.
3. Run the host loader fixture; require `loader: OK` and `bootstate: GOOD`.
4. Build the loader test image only if the loader fill (T-059) is in scope;
   otherwise use the existing loader test image and the zero-`r0` fallback.
5. Report: image hash, manifest hash, host loader output, jump contract.

### Session 8 — first device run

1. Confirm the backup/recovery gate and the device state.
2. Install the loader test image by DFU only; never `--single`.
3. Boot; photograph the banner, each stage code and `PASS`.
4. Power off; run `fsck_hfs -fn`; record the output.
5. Do not write anything. Do not "fix" a failure with a format or journal
   delete.
6. Report: image hash, device/adapter, stage timings, photo, fsck, what was not
   tested.

### Session 9 — fix F1 and close the evidence

1. Fix only the defects the device run exposed.
2. Re-run the host tests and sanitizers.
3. Rebuild; compare the image hash and size.
4. Re-run the device rung if the fix changes the boot path.
5. Update `docs/device-matrix.md`, `docs/state.md`, `docs/tasks.md` and
   `docs/handoff.md`.
6. Stop. Do not start F2 in the same session unless explicitly authorized.

### Session 10 — F2 bootstate promotion

1. Re-read `docs/hfsplus-write-engine.md` §10 and the bootstate format.
2. Mount read-write only after a clean journal.
3. Re-read bootstate through the VFS; do not trust boot-info.
4. Promote `TRYING`→`GOOD` through `hfs_txn`; flush; read back; verify.
5. Run the host test and the device run on a disposable/backed-up volume.
6. Record `fsck_hfs`, the next loader boot (`GOOD`) and the volume diff.
7. Stop and report.

### What to do when a session goes wrong

- Stop at the first safety-relevant failure.
- Preserve the image, the log ring and the screen photo.
- Record the exact stage and error code.
- Do not retry a write with a different parameter to "see if it works".
- Do not reformat, delete the journal or restore "just in case".
- Ask the owner if the next step is ambiguous.

---

## 7. Repository layout you must create

```text
firmware/
  Makefile
  boot/
    start.S              _start, stacks, BSS/data, vectors
    vectors.S            exception vector table
    boot_info.c/.h       boot-info parser and validation
    panic.c/.h           panic record, screen, power-off
    firmware.ld          6G linker script and ASSERTs
  lib/
    mem.c/.h             memcpy/memset/memcmp/memmove
    string.c/.h          bounded string helpers
    endian.c/.h          be16/be32/be64 helpers
    crc32.c/.h           boot-info CRC
    ring.c/.h            bounded trace ring
    pool.c/.h            fixed-size pool allocator
    fixed.c/.h           fixed-point helpers
  board/6g/
    board.h              board ID, capabilities, memory map
    board.c              board description and init order
    memory_map.h         HELIKON_* constants
  hal/6g/
    clock.c/.h
    pmu.c/.h
    backlight.c/.h
    lcd.c/.h
    ata.c/.h
    buttons.c/.h
    audio.c/.h           F5; stub until then
    adc.c/.h             later
    rtc.c/.h             later
  block/
    block.c/.h           block_device over the ATA HAL
    partition.c/.h       APM scan; read-only
    bounce.c/.h          aligned bounce buffers
  hfs/
    (existing core; do not duplicate)
    hfs_block_adapter.c/.h  hfs_read_fn over block_device
  vfs/
    vfs.c/.h             lookup/iterate/open/read/seek/close
    handle.c/.h          generation-checked handles
    path.c/.h            bounded path normalization
  diag/
    trace.c/.h           ring, levels, counters
    stages.c/.h          boot stage codes and timing
    crash.c/.h           panic record
  input/
    input.c/.h           wheel/button events, debounce, focus
  player/
    player.c/.h          state machine, buffer, clock
    decode.c/.h          one codec
  ui/
    screen.c/.h          screen stack, focus
    render.c/.h          RGB565, glyph atlas, blit
    theme.c/.h           first-party theme
    screens/             home, music, now playing, settings, error
  usb/                   later; do not create in F1
  update/                later; do not create in F1
```

Rules:

- Keep `firmware/hfs/` where it is. Do not move or rewrite it in F1.
- Do not create empty directories that imply a feature exists. A directory with
  only a README is not a module.
- One module, one public header. Private structs stay in the `.c`.
- The BSP is the only place that knows `0x6G`. The core sees `struct board`.
- The HAL is the only place that touches registers. The UI never includes a HAL
  register header.

---

## 8. Architecture contract you must implement

### 8.1 Boot-info and entry

The accepted ABI is in `bootloader/LOADER.md` §4 and ADR-0004. Summary:

```text
r0 = pointer to boot-info page at 0x087ff000
r1 = 0
SVC mode, IRQ/FIQ disabled
no valid stack, no zeroed BSS, no display/ATA state
entry at the first instruction of the image
no return
```

The boot-info page is 4 KiB; the first 512 bytes are defined:

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
| 64 | 1 | bootstate state |
| 65 | 1 | journal state |
| 66 | 2 | flags |
| 68 | 4 | loader stage time (ms) |
| 72 | 4 | CRC-32 over 0–71 |
| 76 | 180 | reserved zero |
| 256 | 256 | verified manifest copy |
| 512 | 3584 | reserved zero |

Validation order: `r0` range/alignment, magic/version/size/CRC, board, image
size/load/range against `__image_end - __image_start` and the map, then the
manifest copy's magic/board/hash.

On failure: `BOOT_INFO_INVALID`, read-only diagnostic mode, **no bootstate
write**. On `r0 = 0`: same diagnostic path. Boot-info is a cache; the firmware
re-reads bootstate before writing it.

### 8.2 Memory map

The accepted map is ADR-0003. Do not invent an address.

| Range | Size | Use |
|---|---:|---|
| `0x08000000`–`0x087fefff` | 8 MiB − 4 KiB | image `.text/.rodata/.data` |
| `0x087ff000`–`0x087fffff` | 4 KiB | boot-info page |
| `0x08800000`–`0x08ffffff` | 8 MiB | BSS, stacks, pools, log ring |
| `0x09000000`–`0x097fffff` | 8 MiB | UI/framebuffer/fonts/art |
| `0x09800000`–`0x0affffff` | 24 MiB | audio/DMA |
| `0x0b000000`–`0x0b7fffff` | 8 MiB | database cache |
| `0x0b800000`–`0x0bffffff` | 8 MiB | reserve |

Rules:

- Linker `ASSERT(__image_end <= 0x087ff000)`.
- Linker `ASSERT(__bss_start >= 0x08890000)`; the loader test image owns
  `0x08800000`–`0x0888ffff` for its BSS, framebuffer and LCD DMA scratch.
- Linker `ASSERT(__bss_end <= 0x09000000)`.
- `static_assert` every pool, stack and cache size.
- Print the map, `__image_end`, `__bss_end` and headroom in the build.
- The boot-info page is loader-owned; the firmware must not write it.
- A max-size image that would overlap `0x087ff000` is a build error.

### 8.3 Runtime model

- One core, one address space, no processes.
- Cooperative main loop: exception handlers → deferred work → main loop →
  boot/panic.
- IRQ handlers do not call the filesystem, UI, player or allocator.
- No `malloc`; fixed pools and a boot arena only.
- Every wait has a timeout; every flush error propagates.
- 64-bit time where wraparound matters; a 32-bit ms counter wraps in ~49 days.
- No FPU assumption. Integer/fixed-point only until the board audit says
  otherwise.
- Panic never writes the filesystem.
- Watchdog policy is open; do not arm one without measuring the longest
  operation.

### 8.4 Layer dependency rule

```text
UI -> player/VFS/diag
player -> VFS/audio HAL/diag
VFS -> HFS+/diag
HFS+ -> block/lib/diag
block -> HAL block ops
HAL -> BSP constants/registers
BSP -> HAL implementations
```

A lower layer never includes a higher layer's header. A violation is a review
blocker. The UI must not know what a B-tree is; the HFS+ core must not know what
a screen is.

### 8.5 HAL shape

Expose operations, not registers:

```c
struct block_device {
    int (*read)(void *ctx, uint64_t lba, unsigned int count, void *buf);
    int (*write)(void *ctx, uint64_t lba, unsigned int count, const void *buf);
    int (*flush)(void *ctx);
    int (*power)(void *ctx, enum power_state state);
    struct device_info info;
};
```

The same pattern for display, input, audio, PMU, RTC and backlight: opaque
context, init/deinit, explicit errors, no policy, no exposed register addresses.

### 8.6 Error model

Use the canonical codes: `NOT_HFS`, `JOURNAL_DIRTY`, `CORRUPT`, `NO_SPACE`,
`READ_ONLY`, `UNSUPPORTED`, `NAME_INVALID`, `DB_STALE`, `IO`, plus `NOT_FOUND`,
`NOT_DIR`, `IS_DIR`, `EXISTS`. `0` is success; negative is an error. No `errno`,
no hidden global error, no swallowing.

A flush error is always an error. A short read is an error. A timeout is an
error even if a retry succeeds later.

### 8.7 Diagnostics

- A bounded RAM ring: timestamp, level, module, event, two args.
- Levels `ERROR`, `WARN`, `INFO`, `TRACE`; `TRACE` off in release.
- Optional serial in development.
- On-screen stage code for bring-up.
- No file writes on a failing filesystem.
- Safe to call from panic and IRQ.

---

## 9. Build system and toolchain

### 9.1 Toolchain

Use the pinned Rockbox `arm-elf-eabi` toolchain already used by the bootloader
(`arm-elf-eabi-gcc` 9.5.0). Do not introduce a second compiler family in F1.
Record the compiler version and flags in the build output.

### 9.2 Commands

Create these targets (names are the contract; implementation is yours):

```sh
make -C firmware BOARD=6g            # ELF + raw image + manifest
make -C firmware BOARD=6g size       # map, section sizes, headroom, ASSERTs
make -C firmware BOARD=6g test       # host tests for freestanding modules
make -C firmware BOARD=6g image      # signed development image
make -C firmware BOARD=6g clean
```

The build must:

- compile with `-ffreestanding`, no hosted headers;
- fail on warnings in project code;
- print image end, BSS end, headroom, SHA-256;
- produce a raw binary and a manifest, not just an ELF;
- keep the dev private key under `build/` and never commit it;
- support a host build of the core modules with a fake HAL.

### 9.3 Image and manifest pipeline

1. Link the ELF at the signed load address.
2. `objcopy` to a raw binary.
3. Check `raw_size == __image_end - __image_start`.
4. Build the 256-byte manifest: magic `HELIKON1`, version 1, board 6, image
   version, load address `0x08000000`, size, key ID, SHA-256, flags
   development-only, reserved zero.
5. Sign bytes 0–191 with the development key.
6. Write `firmware.img`, `firmware.manifest`, `bootstate` into a disposable HFS+
   image for the host loader test.
7. Print both hashes.

### 9.4 Host harness

Build the same core modules for macOS with:

- a fake block device backed by a disk image or fixture;
- a deterministic clock;
- a fake display writing RGB565 frames to a buffer/PNG;
- a scripted input queue;
- a trace of block I/O, VFS calls, player state and UI frames.

The harness is a fast loop, not a device result. It must not hide a missing
timeout, a cache maximum or a hardware assumption.

---

## 10. Coding standards

### 10.1 Language and style

- C99. No C++, no exceptions, no RTTI, no global constructors.
- SPDX header on every file: `/* SPDX-License-Identifier: GPL-2.0-or-later */`.
- 4-space indentation, 80-column soft limit, braces on their own line for
  functions. Match the existing HFS+ style.
- `snake_case` functions/variables, `UPPER_CASE` macros, `struct lower_case`
  types.
- Fixed-width types from `<stdint.h>` for registers, on-disk values and sizes.
- `const` on inputs; `static` for file-local functions.
- No VLAs, no `alloca`, no recursion without a documented depth bound.
- No floating point in kernel/UI/audio paths. Use fixed-point with an explicit
  scale.
- No 64-bit division in hot paths unless measured; libgcc calls are not free.
- No `strcpy`, `sprintf`, unbounded `strlen` on untrusted data.
- Every public function comment states ownership, blocking behavior and errors.

### 10.2 Headers and interfaces

- A header includes what it uses; no reliance on include order.
- No HAL register header outside the HAL implementation.
- Opaque structs in public headers; private structs in the `.c`.
- One module, one public header. A second header means the module does two
  things.
- No circular includes. If A needs B and B needs A, the boundary is wrong.
- Pass an explicit context pointer; no hidden global.
- Return an error code; no sentinel pointers or negative sizes.
- Take a length with every buffer.
- Make blocking explicit: `_try`, `_poll`, `_wait`, or a "may block" comment.
- A callback gets `void *ctx` and a value, not the callee's private state.

### 10.3 Ownership and lifetime

- Caller owns its buffer; callee borrows it for the call.
- A handle is an index + generation, not a raw pointer. A stale handle returns
  `INVALID_HANDLE`.
- A module returning a pointer documents who frees it and when.
- No function stores a caller pointer beyond the call unless the API says so.
- Pools return an object exactly once; a double free is a panic in debug and a
  counter in release.
- A screen owns its widgets; a widget does not own the screen.
- The player owns its buffers; the VFS owns handles; the UI owns no file data
  beyond the current screen.

### 10.4 Error handling shape

```c
int rc = module_do(&ctx, args);
if (rc != 0)
    return rc;
```

For cleanup:

```c
int rc = 0;
resource = acquire();
if (!resource) {
    rc = ERR_NO_SPACE;
    goto out;
}
rc = use(resource);
out:
    release(resource);
    return rc;
```

Rules: forward `goto` cleanup only; log once where context is known; do not
convert an error to success because a retry succeeded; do not panic on a
recoverable user error; panic on an invariant violation.

### 10.5 Freestanding libc

Provide and test:

- `memcpy`, `memmove`, `memset`, `memcmp`;
- bounded `strlen`/`strcmp`/`strncmp`;
- endian helpers;
- CRC-32;
- fixed-point helpers;
- a ring buffer and a fixed pool.

No `printf`, `malloc`, `free`, `qsort`, `time`, `errno`, `stdio` or locale. A
bounded formatter may be added for diagnostics/UI, with truncation tests.

### 10.6 Static analysis

Minimum gate:

```sh
-Wall -Wextra -Werror -Wconversion -Wshadow -Wundef -Wpointer-arith
```

Also use, where available:

- `-fstack-usage` and a stack report;
- `cppcheck` or `clang --analyze` on the host build;
- ASan/UBSan on host tests;
- a symbol check that no hosted function (`malloc`, `printf`, `open`) is linked
  into the image.

### 10.7 Comments and documentation

- File header: purpose, owner, assumptions, error codes.
- Public function: what, ownership, blocking, errors.
- A non-obvious hardware sequence: register name and donor/paper reference.
- A workaround: task ID and removal condition.
- A magic number: named constant and source.
- Do not write a comment that restates the code.

### 10.8 What not to do

- No target `#ifdef` in shared code.
- No global mutable state that the host harness cannot reset.
- No blocking call in the render path.
- No direct block write outside `hfs_txn`.
- No retry loop without a timeout.
- No cache without a maximum.
- No "temporary" bypass without a task ID.
- No hidden allocation.
- No fake success.

---

## 11. Testing requirements

### 11.1 Test pyramid

```text
device runs (few, expensive)
perceptual/panel/audio checks
power-cut and crash injection
host integration with fake HAL
host unit + model + property tests
static analysis + fuzzing
```

Device runs are scarce. The lower layers must be strong.

### 11.2 Host unit tests

Minimum for F1:

- boot-info parser: valid, bad magic, bad version, bad size, bad CRC, bad board,
  bad range, bad manifest copy, `r0 = 0`;
- block adapter: read, short read, timeout, unaligned buffer;
- partition scan: APM, no HFS+, bad signature;
- stage state machine: order, timeout, failure;
- ring buffer: wrap, overwrite, panic write;
- pool: alloc/free, exhaustion, double free;
- mem/string/endian: edge cases.

F2 adds:

- bootstate slot validation: magic/version/sequence/board/size/hash/checksum;
- `TRYING`→`GOOD` staging;
- power-cut simulation at commit boundaries (host first);
- no write when the journal is dirty;
- no write when the image/state mismatches.

F3 adds:

- VFS path rules, traversal rejection, handle generation;
- catalog iteration and unsupported object reporting;
- large-directory responsiveness on the harness.

F4 adds:

- layout, truncation, focus, fallback font, frame capture;
- no clipping at 11 px;
- skin failure fallback (later).

F5 adds:

- player state machine, buffer accounting, seek, underrun;
- decode error handling;
- no metadata write during playback.

### 11.3 Model and property tests

- B-tree insert/delete at depth 3 with 60 records (existing).
- Catalog index descent with shared leaves (existing).
- Bitmap alloc/free invariants.
- Transaction dirty-set bounds and `NO_SPACE`.
- Bootstate selection and checksum.
- Ring/pool wrap.

A model test compares against a simple reference, not itself.

### 11.4 Fuzzing

Targets:

1. HFS+ catalog/B-tree/extents parsers (existing mutation fuzz; expand).
2. Manifest and bootstate parsers.
3. Boot-info parser.
4. VFS path/name handling.
5. Skin parser (later).
6. Database index (later).

Run on the host with ASan/UBSan. A crash is a bug; a hang is a bug; a wrong
error code is a bug.

### 11.5 Sanitizers

Run the HFS+ and bootloader suites with sanitizers before and after a change that
touches shared code:

```sh
make -C tests/hfsplus sanitize
make -C tests/bootloader sanitize
```

Add a firmware host test target with the same discipline. A sanitizer failure is
a blocker, even if the device boots.

### 11.6 Device bring-up ladder

Record each rung with a screen photo or trace:

1. banner and power-off;
2. ATA identify: `Storage OK`;
3. partition scan: `Volume found`;
4. HFS+ probe: `HFS+ OK`;
5. read one file: `Read OK`;
6. input events: `Input OK`;
7. bootstate write: loader sees `GOOD`;
8. browser list/selection;
9. first UI screen with real typography;
10. first audio playback;
11. seek/pause/resume/track change;
12. sleep/wake 100 cycles (later).

Do not skip a rung because the host test passes.

### 11.7 Evidence format

Every device result records:

```text
task: T-054 / F1
image: <sha256>, <bytes>, <commit>
device: 6G, adapter/card, battery state
boot: loader <sha>, bootstate <state>
stages: 0..8 pass/fail, screen photo
fsck: command, exit code, output
volume hash before/after (if no writes expected)
notes: what was not tested
```

### 11.8 What not to claim

- A host test is not a device run.
- A successful link is not a boot.
- A screen photo is not `fsck_hfs`.
- One device is not a support matrix.
- A design target is not a measurement.

---

## 12. Device run protocol

### 12.1 Before any device run

- Confirm the device is not the only copy of the library.
- Confirm a verified backup and read-back, or use a disposable volume.
- Confirm Disk Mode, DFU and Apple restore are available.
- Record the current image hash and bootloader state.
- Confirm the run is allowed by the phase: F1 is read-only; F2 writes only
  bootstate; library writes are out of scope until a later owner decision.
- Do not use `--single`.

### 12.2 Install

- Build the loader/test image and DFU installer with the pinned scripts.
- Check the image hash and `MOVE_AREA` headroom.
- Install by DFU only.
- Keep the uninstaller and restore path at hand.

### 12.3 Run

- Boot and photograph the splash and every stage.
- Record the time to each stage.
- If a stage fails, stop and record the code; do not press through a recovery
  screen to "see what happens".
- F1: no writes. Confirm the volume hash before/after if possible.
- F2: only bootstate. Confirm no other file changed.

### 12.4 After

- Power off cleanly.
- Run `fsck_hfs -fn` and record the full output.
- Run `diskutil verifyVolume` where appropriate.
- Compare the volume/tree with the expected state.
- Update `docs/device-matrix.md`, `docs/state.md`, `docs/tasks.md`,
  `docs/handoff.md`.
- If the run failed, preserve evidence and do not retry a destructive step.

---

## 13. Failure handling and stop conditions

### 13.1 Stop immediately

- The journal is dirty/invalid and a write is requested.
- A backup cannot be verified by reading a file back.
- The device is in an unexpected mode (DFU, Disk Mode, recovery, unknown).
- A failure could indicate data loss or metadata corruption.
- The proposed fix is format, erase, restore, repartition, delete the journal or
  zero metadata.
- The only evidence is "it boots".
- The change requires editing the root README or the donor repository.
- A legal/signing decision is needed that the owner has not made.

Record the exact error, stage, device state and what was not attempted.

### 13.2 Panic path

A panic:

1. disables interrupts;
2. records fault type, PC, LR, SP, mode and reason;
3. paints a panic screen if the display works;
4. writes one line to serial if available;
5. powers off or parks with interrupts disabled.

A panic never writes the filesystem, never replays the journal, never reboots in
a loop.

### 13.3 Dirty journal on boot

- Mount read-only.
- Show `JOURNAL_DIRTY` with a plain-language explanation.
- Do not replay from the read path.
- The write engine may replay before the first write, but only if F2/F3 authorizes
  it and the replay is bounded/idempotent.
- If replay fails, stay read-only and preserve the journal.

### 13.4 Bad image / bad bootstate

- The loader fails closed before the firmware runs.
- The firmware does not exist to handle it.
- Do not add a firmware recovery menu; D-26 keeps recovery on native Menu+Play
  and DFU/Apple restore.

### 13.5 When to ask the owner

Ask before:

- choosing a codec/container for F5;
- choosing a database schema (D-07);
- setting a battery/write threshold (D-12);
- choosing an update transport (D-16);
- changing the memory map, boot-info ABI or manifest/bootstate format;
- writing anything outside `/.helikon/bootstate`;
- adding a feature not in F1–F5;
- treating a host result as a device claim.

---

## 14. Reporting, commits and docs

### 14.1 Every session

End with a report that states:

1. files read;
2. files changed;
3. tests run and their output;
4. device evidence (or "not run");
5. open questions;
6. what is not done;
7. the next smallest step.

Do not rely on chat history. Update the repository.

### 14.2 Commit style

- One logical change per commit.
- Subject imperative, ≤72 characters, e.g.
  `Firmware: add boot-info parser and host test`.
- Body: what, why, checks, evidence, open decisions, what was not changed.
- Do not commit generated images, disk images, backups, private keys or
  proprietary assets.
- Do not rewrite history or force-push.
- Follow the repository's branch/PR convention. If the owner has authorized
  direct main commits for a docs-only change, that does not authorize direct
  main commits for firmware code; use a branch and PR unless told otherwise.

### 14.3 Docs to update

At the end of every session:

- `docs/state.md` — phase, blockers, evidence status.
- `docs/tasks.md` — task status, next task.
- `docs/handoff.md` — newest entry at the top.
- `docs/log/YYYY-MM-DD.md` — dated entry.
- `docs/device-matrix.md` — every device run.
- `docs/decisions.md` — new decisions/conflicts.
- `docs/failures.md` — new error codes or safe responses.
- `docs/file-map.md` — new files/modules.
- This prompt and `docs/firmware-architecture.md` — if the design changed.

### 14.4 Progress updates

At each checkpoint, report:

- what is proven now;
- what is still unproven;
- the next gate;
- any decision needed from the owner.

Do not report "done" for a phase without its exit evidence.

---

## 15. Anti-patterns and traps

These are the mistakes most likely to waste a week or break a device.

### 15.1 Starting with the fun part

Audio, UI and album art are the fun parts. They are also the parts that hide a
broken block layer. Do not start them before F1. A pretty screen that corrupts
the volume is worse than a text banner that does not.

### 15.2 Porting the donor firmware wholesale

The paper's verdict is **K/M/R/X**, not "copy the tree". Keep hardware sequences
(verdict K), replace policy layers (R), exclude the overlay and the plugin tree
(X). Do not bring in `storage.c` or `backlight.c` policy. Do not link the donor
filesystem or the overlay.

### 15.3 Treating boot-info as authority

Boot-info is a cache. The bootstate file is the durable record. The firmware
must re-read it through the VFS before writing it. A `TRYING` hint is not a
permission slip.

### 15.4 Writing "just this one" file directly

Every metadata write goes through `hfs_txn`. There is no "small" exception for
settings, logs, artwork or bootstate. If the transaction does not fit, return
`NO_SPACE` before changing anything.

### 15.5 A retry loop with no timeout

A driver that can wait forever is a hang. Every wait has a timeout, every flush
has an error return, every DMA has a completion or a timeout.

### 15.6 A cache with no maximum

Every cache has a maximum, a high-water counter and an eviction policy. "It will
fit in 64 MiB" is not a budget.

### 15.7 A blocking call in the render path

The render path owns the frame. If a screen needs data, load it before the frame
or show a bounded loading state. A `read()` in the middle of a frame is a
stutter, a dropped input and a battery cost.

### 15.8 Target `#ifdef`s

`#ifdef TARGET_6G` in the VFS is a portability bug. The BSP supplies constants
and operations. If a feature is optional, use a runtime capability.

### 15.9 Fake measurements

"This should boot in under a second" is a target. "It boots in 0.8 s" is a
measurement and needs a device, image hash and run count. Do not blur the two.

### 15.10 Skipping the device rung

A host test that passes is not a device run. A screen photo is not `fsck_hfs`.
A successful link is not a boot. Record the rung or say `not run`.

### 15.11 A "temporary" bypass

Every workaround gets a task ID, a removal condition and a test. If it bypasses a
safety rule, it is not a workaround; it is a defect.

### 15.12 Max-size image overlapping boot-info

The image cap is 8 MiB − 4 KiB. A linker `ASSERT` and a build check must fail
before a max-size image overwrites `0x087ff000`. Do not rely on the loader's
current 8 MiB check.

### 15.13 Returning from the entry

The test payload returns `HELO`; production firmware must not. `_start` ends in
`firmware_main`, and `firmware_main` never returns. A return is a panic.

### 15.14 Trusting the loader's display/ATA state

The loader may leave a splash on screen and ATA initialized. The firmware
repaints and re-inits. Do not assume a blank framebuffer or a usable ATA state.

### 15.15 Using a host `memcpy`

Link the firmware's own `firmware/lib/mem.c` into the host tests where possible.
A host libc `memcpy` can hide an overlap or alignment bug that the target
implementation would expose.

---

## 16. Open questions and how to resolve them

Do not invent an answer. For each, either measure it, ask the owner, or implement
the smallest reversible behavior and mark it `open question`.

**Boot and memory**

1. What are the exact MMU, cache and watchdog states at the loader jump?
2. Is the full 64 MiB DRAM contiguous and usable above `0x08800000`?
3. Where does the LCD controller expect its framebuffer, and what alignment?
4. Which DRAM region is safe for DMA?
5. What are the real stack high-water marks for boot, HFS+, UI and audio?
6. Is the 4 KiB image cap worth keeping, or should a later loader move boot-info
   above the signed window?

**Runtime**

7. Is a cooperative loop sufficient for audio, or is a worker/preemptive context
   needed?
8. What is the worst-case ATA read latency on the target adapter?
9. Is there a usable RTC alarm or wake source?
10. Does the device need a watchdog, and who owns it across the jump?
11. How are 64-bit time values represented at the boundaries?

**Filesystem and writes**

12. What is the cache budget for B-tree nodes, names and file data (D-05)?
13. Does the first player need a database, or is a catalog browser enough
    (D-07)?
14. What is the exact bootstate promotion sequence for a `TRYING` slot?
15. How does the firmware detect "self-test passed" without a user action?
16. What is the battery/charger threshold for writes (D-12)?

**Product and UI**

17. What is the exact ink colour and selected-row treatment?
18. Is there a dark theme, and does it fit the splash identity?
19. What are the final row pitch, title height and margins on the panel?
20. Which icon set and stroke weight?
21. What motion durations are worth the frame cost?
22. Is 11 px readable outdoors, or is 12 px the real floor?
23. How does the loader splash hand off to the first firmware frame?
24. What does the battery indicator show: glyph, percentage, or both?

**Audio**

25. Which codec/container is first?
26. What buffer size survives storage sleep and seek?
27. Is gapless a v1 requirement or a later goal?
28. What are the measured line-out and headphone levels?
29. Is the codec init sequence fully documented, or does it need donor code?

**Release and trust**

30. When is the release-key certificate verifier (T-053) required for firmware?
31. Does the firmware need to display the signing key ID/certificate state?
32. What is the update transport (D-16) and does it change the bootstate flow?

If a question blocks F1, ask. If it blocks F5, defer it. If it is a measurement,
instrument it. Never fill a gap with a guess and present it as a fact.

---

## 17. Handoff package for each phase

A phase is not ready to hand off without:

1. **Interface document** — the public headers, error codes, ownership, blocking
   behavior and limits.
2. **Test fixture** — host tests, a disposable image, a fake HAL or a recorded
   trace.
3. **Expected error codes** — the exact code for each failure path.
4. **Known limitations** — what is not implemented, what is unproven, what is
   target-specific.
5. **Rollback method** — how to remove the firmware, restore the loader, or
   recover the volume.
6. **Device evidence** — the matrix row and its photo/fsck/log.
7. **Docs** — state/tasks/handoff/log updated.

A module without a rollback method is not ready. A phase without evidence is not
done.

---

## 18. Exact F1 checklist

Use this as a literal checklist. Do not mark an item done without the evidence.

### 18.1 Repository and build

- [ ] `firmware/Makefile` with `all`, `size`, `test`, `image`, `clean`.
- [ ] `firmware/boot/firmware.ld` with the accepted map and `ASSERT`s.
- [ ] `_start` in assembly: mask IRQs, stacks, BSS, data, vectors, `main`.
- [ ] `firmware_main` never returns; a return calls `panic`.
- [ ] `make size` prints `__image_end`, `__bss_end`, headroom and map.
- [ ] Build fails if the image exceeds `0x087ff000` or BSS exceeds `0x09000000`.
- [ ] Raw image + manifest + dev signature build.
- [ ] Host test target builds without hosted headers.
- [ ] Symbol check: no `malloc`, `printf`, `open` in the image.

### 18.2 Boot-info

- [ ] Parser validates `r0` range/alignment, magic, version, size, CRC, board,
      image size/load/range, manifest copy.
- [ ] `r0 = 0` enters diagnostic mode, not a panic and not a write.
- [ ] Bad page enters `BOOT_INFO_INVALID`, not a write.
- [ ] Host tests cover every field and the fallback.
- [ ] The page is never written by the firmware.

### 18.3 HAL and board

- [ ] Clock/PMU/backlight init with timeouts.
- [ ] LCD init, solid color, text banner.
- [ ] ATA identify/read with timeout and error return.
- [ ] Partition scan finds the HFS+ volume.
- [ ] Button input minimal (a press can be observed) or explicitly deferred.
- [ ] No raw register access outside `firmware/hal/6g/`.
- [ ] No target `#ifdef` in `firmware/block`, `firmware/hfs`, `firmware/vfs` or
      `firmware/diag`.

### 18.4 HFS+ read

- [ ] `hfs_read_fn` adapter over `block_device`.
- [ ] Volume probe, catalog open, lookup of one known file.
- [ ] Read the first bytes and print size/name.
- [ ] Missing file returns `NOT_FOUND`, not a crash.
- [ ] Dirty journal is detected and reported read-only; no replay.
- [ ] Host test with a disposable HFS+ image.

### 18.5 Diagnostics and panic

- [ ] RAM trace ring with timestamps and stage codes.
- [ ] Boot stage codes: `BOOT`, `PMU`, `LCD`, `ATA`, `PART`, `HFS`, `READ`,
      `INPUT`, `PASS`.
- [ ] Panic screen with code and power-off.
- [ ] Panic never writes the filesystem.
- [ ] Serial output if available; otherwise a documented screen code.

### 18.6 Device run

- [ ] Backup/recovery gate confirmed.
- [ ] Loader image hash and `MOVE_AREA` headroom recorded.
- [ ] DFU install only; no `--single`.
- [ ] Stage-by-stage photo.
- [ ] `PASS` and clean power-off.
- [ ] `fsck_hfs -fn` clean.
- [ ] Volume hash/tree unchanged.
- [ ] `docs/device-matrix.md` row.
- [ ] `docs/state.md`, `docs/tasks.md`, `docs/handoff.md`, log updated.
- [ ] Stop and report; do not start F2.

---

## Appendix A — Boot-info quick reference

The full layout is in `bootloader/LOADER.md` §4 and ADR-0004. The fields you
must implement:

```text
magic HBI1 | version 1 | size 512 | loader version | board ID
image version | load address | image size | image SHA-256
bootstate sequence/state | journal state | flags | loader ms | CRC-32
manifest copy (256 bytes)
```

Rules: little-endian, CRC over bytes 0–71, additive versioning, invalid page →
`BOOT_INFO_INVALID`, no bootstate write, `r0 = 0` fallback.

---

## Appendix B — Memory constants

```c
#define HELIKON_IMAGE_BASE      0x08000000u
#define HELIKON_IMAGE_MAX       0x007ff000u  /* 8 MiB - 4 KiB */
#define HELIKON_BOOT_INFO_BASE  0x087ff000u
#define HELIKON_BOOT_INFO_SIZE  0x00001000u
#define HELIKON_KERNEL_BASE     0x08800000u
#define HELIKON_KERNEL_SIZE     0x00800000u
#define HELIKON_UI_BASE         0x09000000u
#define HELIKON_UI_SIZE         0x00800000u
#define HELIKON_AUDIO_BASE      0x09800000u
#define HELIKON_AUDIO_SIZE      0x01800000u  /* 24 MiB */
#define HELIKON_DB_BASE         0x0b000000u
#define HELIKON_DB_SIZE         0x00800000u  /* 8 MiB */
#define HELIKON_RESERVE_BASE    0x0b800000u
#define HELIKON_RESERVE_SIZE    0x00800000u
```

These are accepted 6G BSP constants, not measured facts. A different board
changes the header, not the core.

---

## Appendix C — Error codes

Use the canonical taxonomy:

| Code | Meaning |
|---|---|
| `NOT_HFS` | Not a native HFS+ v4 volume |
| `JOURNAL_DIRTY` | Journal present and not clean; read-only |
| `CORRUPT` | Structural validation failed; read-only |
| `NO_SPACE` | Transaction/allocation does not fit; abort before changing |
| `READ_ONLY` | Volume/mount is read-only |
| `UNSUPPORTED` | Object/feature exists but is not implemented; list it |
| `NAME_INVALID` | Name violates HFS+ policy; reject with reason |
| `DB_STALE` | Database entry does not match the catalog; fall back |
| `IO` | Block/transport failure; propagate, retry only per policy |
| `NOT_FOUND` | VFS lookup miss |
| `NOT_DIR` | Path component is not a directory |
| `IS_DIR` | Operation expected a file |
| `EXISTS` | Create/rename target exists |
| `BUFFER` | Buffer too small or missing |
| `INVALID_HANDLE` | Stale/generation-mismatch handle |
| `BOOT_INFO_INVALID` | Boot handoff failed validation; diagnostic mode |

`0` is success; negative is an error. Do not invent a numeric value that is not
in the canonical enum. `INVALID_HANDLE`, `BOOT_INFO_INVALID` and any new VFS
code are firmware additions: add them to the single error enum and
`docs/failures.md` before use; do not create a second taxonomy.

---

## Appendix D — F1 stage codes and screen text

Keep the codes stable; a photo must be enough to identify the stop.

| Code | Screen | Success |
|---|---|---|
| `BOOT` | `HELIKON-OS` + image version | Firmware started |
| `PMU` | `Power OK` | Clock/PMU/backlight initialized |
| `LCD` | `Display OK` | LCD initialized and first frame |
| `ATA` | `Storage OK` | ATA identify/read |
| `PART` | `Volume found` | HFS+ candidate found |
| `HFS` | `HFS+ OK` | Volume probe/catalog open |
| `READ` | `Read OK` + file name/size | Read one known file |
| `INPUT` | `Input OK` | Button/wheel event observed |
| `PASS` | `PASS` | All stages; power off |
| `FAIL` | `FAIL <code>` | Stop; no write |

A failure screen must include the code and a plain-language reason. Do not show
a raw negative number alone.

---

## Appendix E — Evidence templates

### E.1 Device-matrix row

```text
| iPod Classic 6G | 6G | iFlash SD | firmware <sha> | F1 read-only skeleton | pass/fail/blocked | Owner/agent report <date>: stages ..., fsck ..., photo ... | firmware lead |
```

### E.2 Session report

```text
Task: T-054 / F1
Read: <docs>
Changed: <files>
Build: <command>, image <sha>, size, headroom
Tests: <command>, <n> passed, sanitizer clean
Device: <model/adapter>, loader <sha>, bootstate <state>
Stages: <list pass/fail>, photo <path>
fsck: <command>, exit <n>, output <summary>
Not done: <list>
Open questions: <list>
Next: <smallest step>
```

### E.3 Commit body

```text
Firmware: add boot-info parser and host test

- Parse and validate the accepted D-28 page: magic/version/size/CRC/board/
  range/manifest copy.
- Invalid page enters BOOT_INFO_INVALID; r0=0 uses the same diagnostic path.
- No filesystem write.
- Host tests: <n> pass; sanitizer clean.
- Not a device run.
```

---

## Appendix F — Stop-condition quick card

If any of these is true, stop:

- you are about to write outside `hfs_txn`;
- you are about to replay/delete/zero a journal;
- you are about to format/erase/restore;
- the backup is unverified;
- the device is in an unknown mode;
- the only evidence is "it boots";
- the fix needs a root README or donor edit;
- the ABI/map/format would change silently;
- a measurement would be invented;
- a device claim would exceed one recorded run.

Record, preserve, report. Do not "try one more write".

---

## Appendix G — Required docs update checklist

- [ ] `docs/state.md` — phase/evidence.
- [ ] `docs/tasks.md` — task status, next.
- [ ] `docs/handoff.md` — new top entry.
- [ ] `docs/log/YYYY-MM-DD.md` — dated entry.
- [ ] `docs/device-matrix.md` — device run.
- [ ] `docs/decisions.md` — new decision/conflict.
- [ ] `docs/failures.md` — new error/response.
- [ ] `docs/file-map.md` — new module/file.
- [ ] `docs/firmware-architecture.md` — design change.
- [ ] `docs/prompts/build-firmware.md` — if the procedure changed.

---

## Appendix H — Sample first session report

```text
Task: T-054 / F1, session 1
Read: agents.md, state.md, firmware-architecture.md, ADR-0003/0004,
      LOADER.md, invariants.md, tasks.md
Changed: firmware/Makefile, firmware/boot/firmware.ld,
         firmware/boot/start.S, firmware/lib/mem.c, firmware/boot/panic.c
Build: make -C firmware BOARD=6g size
       image 1,234 bytes, __image_end 0x080004d2, __bss_end 0x08800120,
       headroom 8,380,846 bytes
Tests: make -C firmware BOARD=6g test -> 4 passed
Device: not run
Not done: HAL/LCD/ATA/HFS; no image signed yet; no boot-info test
Open: exact MMU/cache state at jump; framebuffer address
Next: boot-info parser + host test
```

This is a good report: it states what is proven, what is not, and the next step.

---

## Appendix I — F1 acceptance checklist

F1 is done only when all are true:

- [ ] A signed image boots from the loader and does not return.
- [ ] Banner shows the image version.
- [ ] `Storage OK`, `Volume found`, `HFS+ OK`, `Read OK`, `Input OK`, `PASS`
      observed.
- [ ] Clean power-off.
- [ ] `fsck_hfs -fn` clean.
- [ ] No filesystem write.
- [ ] Image hash, size, headroom, `__image_end`, `__bss_end` recorded.
- [ ] Stage timings recorded.
- [ ] Device-matrix row with photo.
- [ ] State/tasks/handoff/log updated.
- [ ] No support claim beyond one device.
- [ ] No F2 code started.

---

## Appendix J — Terms for the agent

- **Loader** — the read-only bootloader that verifies and jumps.
- **Firmware** — the code after the jump.
- **Kernel** — boot, memory, interrupts, time, panic, main loop. Not POSIX.
- **HAL** — hardware primitives, no policy.
- **BSP** — board constants and init for one target.
- **Boot-info** — the `r0` page; a cache, not a trust anchor.
- **Fixed pool** — static object pool, compile-time maximum.
- **Arena** — bump allocator over a reserved region.
- **Cooperative loop** — tasks run to completion; no preemption.
- **Host harness** — fake HAL/clock/display for tests.
- **`fsck_hfs`** — the external oracle; zero repairs is the gate.
- **DFU** — the only bootloader install path; never `--single`.

---

*End of prompt. This is an execution contract, not evidence. The firmware does
not exist until a device run says it does.*
