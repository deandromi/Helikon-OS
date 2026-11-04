# Testing — test layers, fixtures and how to add a test

> Purpose: define the test pyramid and the evidence each layer produces.
> Status: living document
> Owner: test lead
> Last updated: 2026-11-04
> Source: design paper §17, §24, §26, §28; `docs/verification.md`

**Helikon-OS has a small host test tree.** It covers the HFS+ read core, the
read-only diagnostic bootloader core, and the writer foundation: 76 tests total
(68 HFS+, 8 bootloader), sanitizer-clean. The writer foundation covers the Apple
HFS+ journal append/replay path, a bounded transaction engine, the allocation
bitmap, node-level B-tree insert/delete and fork block staging. Tree-level
split/merge, catalog and extents mutation tests do not exist yet. There are no device or
power-loss tests. Donor tests are reference material only. Do not describe a
donor test pass as Helikon-OS validation.

## 1. Test layers

| Layer | What it proves | Runs on | Current status | Required for |
|---|---|---|---|---|
| Host unit | Parser, allocator, name, state-machine logic | Host | `in progress` — HFS+ read core, journal and txn | Every module |
| Fuzzing | Malformed B-trees, extents, names, journal records do not crash/escape bounds | Host | `planned` | Reader, writer, journal |
| Model-based B-tree | Insert/update/delete, split/merge, key order, free-node map | Host | `planned` | Writer |
| Property tests | Allocation, ordering, commit atomicity, no lost/duplicate blocks | Host | `planned` — journal append/replay has unit tests only | Writer/journal |
| Differential vs macOS | Same tree/names/metadata/`fsck_hfs` result on both sides | Host + macOS | `planned` | Writer |
| Bootloader diagnostics | Read-only volume/catalog/file report and splash | Host | `in progress` — 8 tests | Boot screen |
| Device | Boot, mount, playback, write, USB, sleep, recovery | 6G (then 7G/ports) | `not run` | Any hardware claim |
| Power-loss | Every metadata operation at every journal boundary | Device/rig | `not run` | Write release |
| Battery/RAM | Runtime, sleep drain, peak RAM, soak | Device/rig | `not run` | Goal 1/4, D-12 |
| Skin corpus | 20 skins, tags, frame/memory caps, fallback | Device | `not run` | Goal 3, D-10 |
| App sandbox | Permissions, quotas, escape, crash isolation | Device | `not run` | Goals 15/18, D-08 |
| Desktop app | Import/tag/rate/scrobble/install/backup, failed transfer | macOS first | `not run` | Goal 20, D-09 |
| Car/receiver | Line-out, charging, resume, ignition, Tier 2 controls | Device + receiver | `not run` | Goal 23, D-14 |
| Update/rollback | Bad image rejection, staging cut, rollback, version/hash | Device | `not run` | Goal 9, D-01/D-16/D-20 |

## 2. Host tests

Host tests are the fastest safety net. They must not require a device.

- One test binary per cohesive module (volume, B-tree, catalog, extents, bitmap,
  Unicode, journal, transaction, VFS).
- Deterministic fixtures shared by C and Python validators where useful.
- Every parser has malformed-input tests and a fuzz target.
- Every error code has a test that asserts the code, not just "fails".
- No test may depend on a private developer machine, absolute path or network.

## 3. Fuzzing and sanitizers

- Fuzz targets: volume header, B-tree node/offset table, catalog records,
  extents/overflow, names/Unicode, journal records, manifest.
- Run ASan/UBSan on every host target.
- Treat a crash, hang, out-of-bounds read/write or assertion as a defect, not a
  "known fuzz case".
- Keep corpus files small and deterministic; do not commit device images,
  proprietary data or personal data.

## 4. Model-based B-tree tests

The writer needs a model independent of the implementation:

- Generate a random sequence of insert/delete/update operations.
- Apply them to a reference model and to the on-disk tree.
- Compare key order, leaf links, free-node map, record counts and lookup results.
- Force node splits/merges at boundaries, including root changes.
- Compare names with Apple `FastUnicodeCompare` and composed/decomposed forms.
- After each operation, validate the tree and run the structural checker.

A model test that only checks "the file is there" is not enough; it must check
the catalog structure and `fsck_hfs` compatibility.

## 5. Differential tests against macOS

Create the same tree on macOS and on the device/test image:

- names (case, composed/decomposed, length boundaries, `:`/`/`);
- folders/files, rename, delete, mkdir, truncate, append;
- timestamps, BSD info, Finder info, flags;
- extents/overflow for large/fragmented files;
- allocation/free counts.

Compare names, sizes, types, metadata and `fsck_hfs` output. Compare CNID
uniqueness and monotonicity, not exact CNID values.

### 5.1 Disposable-image writer test

`tests/image/run-macos.sh` creates a fresh 16 MiB journaled HFS+ image under a
temporary directory and runs the host writer against it. It never accepts an
existing image or device path. The test:

- creates a folder and appends a 4096-byte file through `hfs_txn`;
- runs `fsck_hfs -fn` on the raw device and requires a clean report;
- mounts the image in macOS and verifies the content, file/folder modes and a
  current-year timestamp;
- renames the folder and file, remounts and verifies the new path, old-path
  absence, modes and timestamp;
- truncates, remounts and verifies size 0; deletes and verifies the folder is
  gone; runs `fsck_hfs -fn` after each stage;
- creates a scripted Unicode corpus (NFC/NFD, CJK, emoji, Hangul, combining
  marks, U+2126, U+F900, `:`/`/`, 255-unit and rejected 256-unit names) and
  verifies the macOS NFC tree and `fsck_hfs` clean.

`fsck_hfs` zero repairs and macOS content parity are the pass criteria. This is
host evidence; it is not a device run and does not authorize a library write.

### 5.2 Journal crash injection

`tests/image/run-crash-macos.sh` copies a clean journaled HFS+ image for every
cut point of a create transaction, fails the nth device write, then reopens the
copy, replays the journal and runs `fsck_hfs -fn`. The loop stops when the cut
is beyond the transaction's write count. It expects the file to be absent when
the cut precedes the journal commit header and present after replay when the
commit header was written; every case must be fsck-clean. The create/append/truncate/delete
matrices currently cover 280 cuts: 130 write cuts (create 25, append 34,
truncate 25, delete 25, rename 21), 20 flush cuts (4 per operation) and 130
partial-sector cuts. Power-loss timing and the remaining operations are T-010.

### 5.3 Write-diagnostic outer/inner image test

`tests/image/run-write-diag-macos.sh` creates a 32 MiB journaled outer HFS+
image and a 16 MiB journaled inner HFS+ image, copies the inner file into the
outer root as `helikon-test.img`, and runs the same core as the bootloader.
The core maps only the inner file's extents, runs folder/file create, append,
readback, rename, truncate and delete, and the test extracts the inner image and
runs `fsck_hfs -fn` on both images. It runs the sequence twice to check that the
inner image is reusable and leaves no `diag` folder. This is host evidence for
the separate write diagnostic; it is not a device run.

### 5.4 Firmware-loader host test

`tests/bootloader/test_ed25519.c` checks the RFC 8032 vectors, a fixed 192-byte
manifest-length vector, tampered signatures, bad arguments and the public-key
table. `tests/bootloader/test_loader.c` checks SHA-256 vectors, manifest
magic/board/address/size/hash/signature, a real Ed25519-signed manifest, missing/
corrupt/truncated boot state and newest-failed fallback.
`tests/bootloader/test_loader_hfs.c` checks the HFS+ file-read adapter.

`make -C tests/image loader-test` creates a disposable HFS+ image with
`/.helikon/firmware.manifest`, `firmware.img` and `bootstate`, signs the
manifest with a generated Ed25519 key, then runs `loader_host` through the HFS+
core and requires `loader: OK` and `bootstate: GOOD`. It also rejects a wrong
public key and an unknown key ID. No jump or write is performed.

The loader fixture and the test-image builder require Python `cryptography` for
Ed25519 key generation and signing. The private test key stays under
`build/loader-bootloader/`; it is never committed.

The loader test image has two additional gates:

```sh
./tools/build-loader-bootloader.sh
./tools/check-loader-artifact.sh
```

The build checks `MOVE_AREA` headroom, the image marker and the DFU dry run.
The artifact check installs the exact generated `firmware.img`,
`firmware.manifest` and `bootstate` into a disposable HFS+ image and verifies
them through the same HFS+ core. Neither gate is a device run.

The loader image also contains the owner Alpha splash generated by
`tools/make-bootloader-splash.py`: exact sRGB variants, a 16-colour 4bpp base
and a 4bpp HFS+ label mask. The generator self-checks the nibble packing. The
font file is owner-provided and is not committed; see
[`../assets/bootloader/README.md`](../assets/bootloader/README.md).

### 5.5 Outer-volume write-diagnostic test

`make -C tests/image outer-diag-test` (driver
`tests/image/run-outer-diag-macos.sh`) creates a disposable 32 MiB journaled
HFS+ image and runs the same core as the outer diagnostic. It checks:

- the full create/append/readback/rename/truncate/delete sequence and
  `Restored: PASS` (root file/folder counts and no test folder);
- `fsck_hfs -fn` clean after the sequence;
- a committed-but-unreplayed journal is refused with zero writes, and macOS
  `fsck_hfs` can replay it;
- a missing opt-in file is refused with zero writes;
- a pre-existing unmarked or marked `/helikon-outer-test` is left untouched;
- a sentinel file and the opt-in file are unchanged;
- a second run succeeds on the same volume;
- a disposable image with a patched 4 KiB journal header runs the same sequence
  and stays `fsck_hfs` clean, covering whole-sector volume/bitmap staging.

This is host evidence for T-047. It is not a device run and does not authorize
a library write.

### 5.6 Journal format compatibility

The multi-level B-tree model test and the catalog append/truncate tests exercise
same-key in-place record updates, so a record update does not force a delete and
reinsert. The journal unit test checks the Apple `block_list_header` layout: `max_blocks`
is `blhdr_size / 16 - 1`, the first header sets `BLHDR_FIRST_HEADER`, the
checksum covers the first 32 bytes, and the first `block_info` is a zero
continuation marker. A separate host integration check leaves a committed but
unreplayed journal and requires macOS `fsck_hfs -fy` to replay it to a clean
volume. This closes a compatibility gap where the writer's own replay worked
but macOS rejected the journal.

## 6. Device tests

Device tests require a recorded inventory and backup. Minimum matrix:

- cold/warm boot, battery-disconnect boot;
- HDD, iFlash SD, iFlash CF, SSD;
- 100-cycle sleep/wake and resume;
- playback, seek, next/previous, volume, line-out/headphone, charging;
- USB read-only mount and log export;
- DFU install/uninstall and Apple recovery;
- update/rollback and migration;
- per-target 7G/port matrix before a support claim.

Record the exact model/revision, adapter/card, firmware/bootloader hash, test,
result and evidence in [`device-matrix.md`](device-matrix.md).

## 7. Power-loss tests

Cut power at every journal and metadata boundary for:

- create, append, close, rename, delete, truncate, mkdir;
- settings save;
- database install;
- artwork install.

After every cut:

1. Boot/mount.
2. Capture the journal state.
3. Run `fsck_hfs -n`; zero repairs is required.
4. Compare the recovered tree with the committed state.
5. Repeat at least 100 times per operation.
6. Copy the image before/after replay for diagnosis.

A cut that leaves an unreadable directory, a non-idempotent replay, or an
ignored journal is a stop condition.

## 8. Battery, RAM and performance tests

- Battery: same model/card/album/volume/backlight/output; at least 3 runs; report
  mean and range; record the Rockbox baseline commit/hash.
- RAM: peak watermark on a 10k-track library after browse, play and database
  build; 72-hour soak.
- Performance: sequential/random read/write, 1 GiB copy, 1,000-file create/
  delete; publish method and firmware hash; no read regression claim without
  data.
- Do not run a benchmark on a volume containing the only copy of the data.

## 9. Skin, app and car tests

- **Skins:** 20-skin corpus, malformed/hostile skins, unsupported tags, frame/
  memory caps, fallback. Record licences.
- **Apps:** install/launch/suspend/resume/uninstall, permission denial, quota,
  crash isolation, escape attempts, update preserves private data.
- **Car:** 100 ignition cycles, resume, charging, no pops, receiver matrix;
  Tier 2 controls optional and legally reviewed.

## 10. How to add a test

1. Identify the layer and the invariant/goal it proves.
2. Add a deterministic fixture or generator; record its hash.
3. Write the smallest test that fails on the bug and passes on the fix.
4. Assert the specific error code/output, not just "non-zero".
5. Add it to the documented test command and `testing.md`.
6. Run sanitizers/fuzzers if the code parses input.
7. Record evidence in the PR/log; update `validation.md` if it is a release gate.

Do not add a test that requires a private device, network or absolute path. Do
not add a test that "passes" by skipping when a fixture is missing without
reporting the skip.

## 11. Current vs future commands

**Current:** the host HFS+ read core has a real test command:

```sh
make -C tests/hfsplus test
make -C tests/hfsplus sanitize
make -C tests/bootloader test
make -C tests/bootloader sanitize
```

Donor commands remain reference-only; they do not test Helikon-OS:

```sh
# Donor reference only.
make -C tools/hfsplus test
make -C tools/hfsplus sanitize
python3 tools/hfsplus/tests/check_integration.py
```

**Future:** add exact commands for B-tree/catalog/extents, fuzzing, model
property tests, device tests and power-loss runs when those trees exist. Do not
invent them before the files exist.

## 12. CI (future)

No CI is configured. When it is, it should run host unit, sanitizer, fuzz smoke,
model/property tests, size budgets and link checks. Device/power-loss/battery
gates cannot be replaced by CI; they need recorded hardware runs.

## 13. Related

- [`verification.md`](verification.md) — evidence template
- [`validation.md`](validation.md) — release matrix
- [`device-matrix.md`](device-matrix.md) — hardware rows
- [`hfsplus-write-engine.md`](hfsplus-write-engine.md) — writer invariants
- Paper §24 (validation strategy), §26 (build path), §27 (assembly)
