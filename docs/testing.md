# Testing — test layers, fixtures and how to add a test

> Purpose: define the test pyramid and the evidence each layer produces.
> Status: living document
> Owner: test lead
> Last updated: 2026-11-03
> Source: design paper §17, §24, §26, §28; `docs/verification.md`

**Helikon-OS has no test tree yet.** There are no Helikon-OS tests to run, no
test counts to quote and no device results. Donor tests are reference material
only. Do not describe a donor test pass as Helikon-OS validation.

## 1. Test layers

| Layer | What it proves | Runs on | Current status | Required for |
|---|---|---|---|---|
| Host unit | Parser, allocator, name, state-machine logic | Host | `planned` | Every module |
| Fuzzing | Malformed B-trees, extents, names, journal records do not crash/escape bounds | Host | `planned` | Reader, writer, journal |
| Model-based B-tree | Insert/update/delete, split/merge, key order, free-node map | Host | `planned` | Writer |
| Property tests | Allocation, ordering, commit atomicity, no lost/duplicate blocks | Host | `planned` | Writer/journal |
| Differential vs macOS | Same tree/names/metadata/`fsck_hfs` result on both sides | Host + macOS | `planned` | Writer |
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

**Current:** there is no Helikon-OS source or test command. The only commands in
the docs are donor reference commands:

```sh
# Donor reference only; not Helikon-OS tests.
make -C tools/hfsplus test
make -C tools/hfsplus sanitize
python3 tools/hfsplus/tests/check_integration.py
```

**Future:** when the test tree exists, add exact commands here, e.g.
`make -C tests unit`, `make -C tests sanitize`, `make -C tests fuzz`,
`make -C tests model`, plus the device/power-loss scripts. Do not invent these
before the files exist.

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
