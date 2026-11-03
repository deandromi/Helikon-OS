# Handoff — Helikon-OS

> Purpose: the latest session handoff and the reusable template.
> Status: living document
> Owner: session author; next agent reads this first
> Last updated: 2026-11-03
> Source: `docs/state.md`, `docs/tasks.md`, repository history

Keep the newest handoff at the top. Move older entries to `docs/log/` when this
file grows. Do not rely on chat history for current state.

---

# Handoff — 2026-11-03 (journal and transaction engine code)

## Branch / commit

- Branch: `main`
- Current head: the journal/transaction implementation commit; check
  `git log -1`.

## What changed

- Added `firmware/hfs/hfs_journal.{c,h}`: Apple HFS+ journal header and
  block-list validation, checksum verification, circular wrap, append/commit of
  one transaction, idempotent replay and clean marking.
- Added `firmware/hfs/hfs_txn.{c,h}`: bounded staged-block transaction engine
  that commits through the journal and applies by replay.
- Added `HFS_ERR_NO_SPACE`, `HFS_ERR_READ_ONLY`, `HFS_ERR_BUSY`,
  `HFS_ERR_POWER` and `HFS_ERR_NOT_EMPTY`.
- Added 8 journal host tests and 7 transaction host tests.
- Updated test/state/handoff/task docs. 58 host tests total (50 HFS+, 8
  bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 50/50 pass.
- `make -C tests/hfsplus sanitize` — 50/50 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 8/8 pass.
- Journal tests cover clean open, append/replay, corrupt checksum, wrapped
  transaction, no-space, bad target, corrupt header and dirty replay failure.
- Transaction tests cover one/two-block commit, abort, duplicate sector, block
  limit, dirty-journal refusal and bad block size.
- No device write and no write-capable bootloader image.

## What failed

- The first journal implementation used a zero-based wrap and would have moved
  the cursor into the journal header; fixed with a sector-aware wrap helper.
- The initial test set did not cover a wrapped transaction; added.
- The writer is **not complete**: catalog/bitmap/extents mutation, file
  operations and the write diagnostic bootloader do not exist.

## What is next

1. Implement the allocation bitmap read/modify/stage path.
2. Implement catalog B-tree insert/update/delete, starting with the
   no-split/empty-file case and then splits/merges.
3. Implement extents/overflow mutation.
4. Add a disposable HFS+ image fixture and `fsck_hfs` gate.
5. Only then build a write diagnostic that targets a disposable image file on
   the iPod (outer HFS+ volume read-only), never the library.
6. Bootloader path resumes afterwards; D-01/D-02/D-20 remain open.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No `--single` on the bootloader path.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode normalization/FastUnicodeCompare policy.
- Catalog split/merge implementation order.
- Write diagnostic test-image placement and verification.

## Device/test evidence

- No new device run. The diagnostic runs remain owner-reported in
  [`device-matrix.md`](device-matrix.md) §2.1. The writer is host-only.

---

# Handoff — 2026-11-03 (journal decisions and writer interface)

## Branch / commit

- Branch: `main`
- Current head: the journal-decision/writer-interface commit; check
  `git log -1`.

## What changed

- D-03 accepted: journaled HFS+ writes are the required write path; a
  non-journaled writer is not the product path and may only be a
  disposable-volume prototype.
- D-04 accepted: Helikon-OS owns journal replay; a dirty/invalid journal mounts
  read-only, replay is idempotent and must finish before the first write.
- T-001 marked implemented.
- T-002 marked implemented: §10 of `docs/hfsplus-write-engine.md` freezes the
  transaction interface, commit protocol, replay contract, error taxonomy,
  transaction boundaries and invariant mapping.
- T-009 marked implemented: §6.5–§6.7 document the Apple HFS+ journal format,
  commit/clean/replay algorithm, validation rules and failure handling.
- `agents.md` settled/open decision lists updated.
- The diagnostic read probe is intentionally skipped; `-8` is correct because
  `/rockbox.ipod` is not a disk file on this iPod.

## What was verified

- Documentation only; no writer code or device write.
- The writer interface references I-03, I-05 and I-15 and keeps one writer,
  journaled commit, idempotent replay and `NO_SPACE` before media writes.
- The diagnostic hardware evidence and image hash are unchanged.

## What is next

1. **T-007** — catalog B-tree mutation design.
2. **T-008** — allocation bitmap and extents/overflow design.
3. **T-004** — writer fixture plan.
4. Decide **D-05** (cache/dirty-set limits) and **D-06** (Unicode policy) before
   implementation.
5. **T-010** — power-loss matrix and evidence format.
6. Resume the bootloader path afterwards; D-01/D-02/D-20 remain open.

## What must not be touched

- No writer implementation before the catalog/bitmap/journal designs and tests.
- No write test on the library or the only copy.
- No journal deletion, truncation or "clean" claim for an invalid journal.
- No `--single` on the bootloader path.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode normalization/FastUnicodeCompare policy.
- Journal record layout and replay bounds (T-009).
- Bootloader manifest/boot state/signing (D-01/D-02/D-20).

## Device/test evidence

- No new device run. The diagnostic runs remain owner-reported in
  [`device-matrix.md`](device-matrix.md) §2.1.

---

# Handoff — 2026-11-03 (first hardware report and report/lookup fix)

## Branch / commit

- Branch: `main`
- Current head: the formatter/lookup fix commit; check `git log -1`.
- Current artifact: `bootloader/artifacts/bootloader-ipod6g.ipod`, 67,144 bytes,
  SHA-256 `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c`.
- First image (superseded): `bd0ebc92…`, 66,920 bytes.

## What changed

- Replaced `%llu` with `%lu` in the report and partition lines. The Rockbox
  bootloader formatter supports `%c %s %l %d %u %x`, not long-long; a stray
  `%llu` shifted later varargs and corrupted the status lines.
- Added a build-script guard that rejects long-long conversion specifiers in the
  Helikon report sources.
- Added `hfs_catalog_lookup_ascii()`: a raw UTF-16BE key lookup for one ASCII
  root name. It does not decode unrelated names, so a single malformed root
  entry cannot hide `/rockbox.ipod`.
- `boot_diag_run()` uses the raw lookup for a single-component ASCII probe and
  keeps the strict decoded lookup for multi-component paths.
- Added tests: malformed-name root fixture, raw lookup, diagnostic read, report
  byte count. 35 HFS+ + 8 bootloader = 43 host tests.

## What was verified

- `make -C tests/hfsplus test` — 35/35 pass.
- `make -C tests/hfsplus sanitize` — 35/35 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 8/8 pass.
- `make -C tests/bootloader sanitize` — 8/8 pass with ASan/UBSan.
- Thumb image: `_movestart` `0x22020800`, `_moveend` `0x22030e40`,
  61,888 bytes below `MOVE_AREA`.
- `mks5lboot --mkdfu-inst` accepts the image as `Classic 6G` / `ipod6g` /
  `ip6g`, target 71.
- No `ata_write_sectors`, `storage_write_sectors`, `hfsplus_create_file` or
  `fat_write` symbol.

## What failed

- The first hardware run (image `bd0ebc92…`) booted and showed the splash.
  Volume and catalog reported `PASS`, but the report displayed `llu` instead of
  the byte count and shifted the write/journal/splash lines into dotted squares
  or `NOT IMPLEMENTED`. The read probe returned `-11`
  (`HFS_ERR_NAME_INVALID`).
- Cause of the display corruption: Rockbox `format()` does not implement
  long-long conversions. Cause of `-11`: the strict root lookup decoded an
  unrelated malformed name and aborted.
- Both are fixed in the current artifact. The fixed image has **not** been
  reflashed or rerun.

## What is next

The fixed image was rerun by the owner. The report formatting is correct:
volume, catalog, write (`NOT IMPLEMENTED`), journal, splash and partition size
displayed as expected. `read error: -8` (`NOT_FOUND`) is correct because
`/rockbox.ipod` is not a disk file on this iPod; the lookup itself worked. No
write was reported.

Choose one:

1. **Close the read evidence (small):** point the diagnostic probe at a real root
   file such as `/.rockpod-rw`, reflash once, confirm `HFS+ read: PASS`, and
   record it.
2. **Start the writer (recommended):** record D-03/D-04, then T-001/T-002.
   Native HFS+ writes are the defining promise.
3. **Start the firmware-loading bootloader:** decide D-01/D-02/D-20 first.

## What must not be touched

- No HFS+ or NOR write path in this image.
- No `--single`; no format, erase, repartition or journal deletion.
- Do not use the donor V3.0/V3.2 image or hash.
- Do not call the image validated or supported without a recorded rerun.

## Open questions

- Does the raw ASCII root lookup find `/rockbox.ipod` on the owner's volume?
- Should the diagnostic report the probe path or skipped malformed names?
- D-01/D-02/D-20 remain open for firmware loading.

## Device/test evidence

- Owner-reported first run (`bd0ebc92…`): splash shown; volume PASS; catalog
  PASS; read FAIL `-11`; report formatting corrupted.
- Owner-reported fixed-image run (`280ed7ae…`): report formatting correct;
  volume/catalog/write/journal/splash/partition as expected; read `-8` because
  `/rockbox.ipod` is not a disk file; no write.
- Recorded in [`device-matrix.md`](device-matrix.md) §2.1.

---

# Handoff — 2026-11-03 (diagnostic bootloader build)

## Branch / commit

- Branch: `main`
- Current head: the diagnostic-bootloader build commit; check `git log -1`.
- Artifact: `bootloader/artifacts/bootloader-ipod6g.ipod`, 67,144 bytes,
  SHA-256 `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c`.

## What changed

- Added `bootloader/hardware/helikon_diag_hw.c`: read-only ATA callback, donor
  partition scan, Rockbox mono-bitmap splash blit, report rendering, button wait
  and power-off.
- Added `bootloader/hardware/donor-bootloader.patch`: removes the donor
  firmware-loading/USB path, adds the Helikon sources and calls the diagnostic
  after `storage_init()`.
- Added `tools/build-diagnostic-bootloader.sh`: pinned donor export, patch,
  Thumb build, `MOVE_AREA` check, artifact copy and SHA-256 output.
- Added `bootloader/artifacts/` with the built image, `SHA256SUMS` and
  provenance/evidence notes.
- Fixed the splash packing: the source is row-major MSB-first; Rockbox mono
  bitmaps are vertical, eight pixels per byte. Added
  `helikon_splash_pack_rockbox()` and a round-trip host test.
- Rewrote `bootloader/DFU.md` for the real image: backup, manual hash, DFU scan,
  install, expected screen, power-off and recovery.

## What was verified

- 41/41 host tests pass; both suites are ASan/UBSan clean.
- The Thumb image links at `_movestart` `0x22020800`, `_moveend` `0x22030e40`,
  with 61,888 bytes of `MOVE_AREA` headroom.
- The image contains the Helikon diagnostic marker and no
  `ata_write_sectors`, `storage_write_sectors`, `hfsplus_create_file` or
  `fat_write` symbol.
- `mks5lboot --mkdfu-inst` accepts the image as `Classic 6G` / `ipod6g` /
  `ip6g` / target 71.
- The build script reproduces the exact image hash from a clean donor export.
- Donor partition/HFS+ tests pass in the donor checkout; this is reference
  evidence only.

## What failed

- The first splash blit used `lcd_mono_bitmap` on the row-major source. Rockbox
  expects vertical packing; the direct call would have read past the buffer.
  Fixed with an explicit packing step and test.
- The first image still linked the donor USB/FAT/HFS+ write stack (113 KB).
  Removing the unused USB mode and loader path reduced it to 67,144 bytes and
  removed write-capable symbols.
- The build script initially used GNU-only `patch --batch --forward`; macOS
  `patch` needed `-N`.

## What is next

1. Record the `.rockbox` + `/.rockpod-rw` backup and clean eject.
2. Run the first hardware diagnostic with [`../bootloader/DFU.md`](../bootloader/DFU.md):
   DFU scan, `--bl-inst`, confirm splash/report, press a button, unplug USB,
   confirm power-off.
3. Record the exact screen and result in [`device-matrix.md`](device-matrix.md).
4. If the run passes, decide D-01/D-02/D-20 for the firmware-loading bootloader.

## What must not be touched

- No HFS+ or NOR write path in this image.
- No `--single`; no format, erase, repartition or journal deletion.
- Do not use the donor V3.0/V3.2 image or hash.
- Do not call the image validated, supported or release-ready without a recorded
  device run.

## Open questions

- Does the image boot and show the splash/report on the owner's 6G?
- Does `/rockbox.ipod` exist and read through the Helikon HFS+ core?
- Does power-off behave correctly when USB is connected?
- D-01/D-02/D-20 remain open for firmware loading.

## Device/test evidence

- None. No Helikon-OS device run was performed.

---

# Handoff — 2026-11-03 (build)

## Branch / commit

- Branch: `main`
- Current head: the host read-core and diagnostic-bootloader commit on `main`;
  check `git log -1`.

## What changed

- Added `firmware/hfs/hfs_volume.{c,h}`: read-only HFS+ primary/alternate
  volume-header and journal probe with an explicit error taxonomy.
- Added `firmware/hfs/hfs_btree.{c,h}`: generic B-tree header-node, reverse
  offset-table and leaf-chain validation.
- Added `firmware/hfs/hfs_unicode.{c,h}`: exact UTF-16BE to UTF-8 decoding.
- Added `firmware/hfs/hfs_catalog.{c,h}`: catalog iteration, path lookup and
  inline file reads.
- Added `firmware/hfs/hfs_extents.{c,h}`: inline plus extents-overflow file
  reads.
- Added `tests/hfsplus/`: shared fixtures and 34 tests across volume, B-tree,
  Unicode, catalog and extents.
- Added `bootloader/diagnostics.{c,h}`, `report.{c,h}`, `splash.{c,h}` and
  `bootloader/README.md`: a read-only diagnostic core and 320×240 splash.
- Added `tests/bootloader/`: 7 diagnostic/splash host tests.
- Added `bootloader/DFU.md`: a blocked install runbook for the future image;
  it has no image or hash and must not be executed yet. It includes the
  `.rockbox` + `/.rockpod-rw` backup commands and the owner-selected manual
  SHA-256 procedure.
- Added the owner-supplied poster to the root README and a 320×240 startup crop
  under `assets/startup/`; the startup crop is not wired into firmware yet.
- Added `.gitignore` for the host test binaries and macOS metadata.
- Applied the thermo-nuclear quality pass: shared leaf iteration, removed dead
  code and named structural bounds.
- No flashable bootloader image, kernel or writer exists yet. The diagnostic
  core is host-tested only and does not touch a device.

## What was verified

- `make -C tests/hfsplus test` — 34/34 pass (12 volume, 9 B-tree, 8 Unicode,
  3 catalog, 2 extents).
- `make -C tests/hfsplus sanitize` — 34/34 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 7/7 pass.
- `make -C tests/bootloader sanitize` — 7/7 pass with ASan/UBSan.
- Test count and output are in `docs/log/2026-11-03.md`.
- No device write, bootloader install, DFU action or `--single` was attempted.
- The diagnostic core reports HFS+ write as `NOT IMPLEMENTED`; it never tests a
  write on a real library.
- Root `README.md` now includes the owner-supplied poster; the 25 goals are
  unchanged. The donor repository was not modified.

## What failed

- One journal fixture initially used flags `4` (need-init without in-FS); it was
  corrected to `1|4` to match the HFS+ journal-info semantics.
- The first B-tree fixture stored the reverse offset table in forward order; it
  was corrected to HFS+ reverse order.
- The first extents fixture used an 8-byte file with a second allocated block;
  it was corrected to a 516-byte file so the overflow boundary is meaningful.

## What is next

1. Define the bootloader manifest/boot-state flow and host image-loading core
   (T-027); D-01/D-02 remain owner decisions.
2. Build the hardware diagnostic bootloader image (T-029) after T-003 and the
   recovery rehearsal; D-21 accepts read-only diagnostic, manual SHA-256, no
   write test and wait-for-button/power-off. Verify Thumb/`MOVE_AREA`, record
   the SHA-256, then use `bootloader/DFU.md`.
3. Wire the 320×240 startup image into the phase 0 display path.
4. Keep the writer blocked until D-03/D-04 and T-002/T-004 are settled.

## What must not be touched

- No bootloader/NOR write path.
- No filesystem write path in this host slice.
- No `--single`.
- No format/erase/journal deletion.
- No claim that the host diagnostic core is a flashable bootloader or a device
  validation.

## Open questions

- D-03/D-04 (journal strategy/ownership) still block the writer.
- D-01/D-02 (manifest/boot state) still block the bootloader design.
- The next slice is the bootloader manifest/boot-state flow; D-01/D-02 must be
  recorded before any hardware boot path.

## Device/test evidence

- None. No Helikon-OS device run was performed.

---

# Handoff — 2026-11-03 (later)

## Branch / commit

- Branch: `main`
- Current head: the logo-removal/COC-cleanup commit on `main`; check `git log -1`.
- Remote note: the owner deleted `CODE_OF_CONDUCT.md` in `51f66a1`; that deletion
  is preserved, not reverted.

## What changed

- Removed `assets/logo/preview.html`, the dot-matrix overlay/mark text files and
  `docs/logo.md`.
- Kept `assets/logo/mount-helikon-sunset.jpg` as the owner-supplied mountain
  reference, with `assets/logo/README.md` noting that the owner will design the
  logo and that source/licence are open.
- Recorded the owner's deletion of `CODE_OF_CONDUCT.md`: D-19 is now `rejected`;
  all links to the deleted file were removed from the docs.
- No logo was generated and no mountain pixels were dotted.

## What was verified

- The mountain reference image is present and opens.
- No dot-matrix logo, preview HTML or `docs/logo.md` remains.
- No link points to the deleted `CODE_OF_CONDUCT.md`.
- Root `README.md` is unchanged.
- The remote deletion commit is preserved; the local branch was rebased, not
  force-pushed.

## What failed

- The first push was rejected because the remote already had the owner's
  `CODE_OF_CONDUCT.md` deletion. The branch was rebased onto `origin/main` and
  the deletion was preserved.

## What is next

1. Owner designs the logo using `assets/logo/mount-helikon-sunset.jpg`.
2. Resolve D-03/D-04 (journal strategy/ownership) before phase 2.
3. Record D-20 (signing-key ownership) before release work.
4. Do not recreate a code of conduct or logo preview without a new owner request.

## What must not be touched

- `assets/logo/mount-helikon-sunset.jpg` — owner reference; source/licence open.
- No `CODE_OF_CONDUCT.md` unless the owner explicitly asks for a new one.
- The native-HFS+ invariants, journal rules, `fsck_hfs` gate, bootloader
  read-only rule, or `--single` ban.

## Open questions

- D-20: signing-key ownership and release-signing policy.
- Logo direction and final asset: owner.
- D-03/D-04 and the other open decisions in `docs/decisions.md`.

## Device/test evidence

- None for Helikon-OS. No device run was performed.

---

# Handoff — 2026-11-03

## Branch / commit

- Branch: `main`
- Base: `main` at `9c558069b6e7b325451b3127b10adfbd41486315`
- Documentation commit: `6df63ef63bacc93d253db40d1bb7f2efdd289a6c`,
  fast-forwarded from `docs/license-coc-templates` and pushed to `origin/main`
- Merge: fast-forward on 2026-11-03; no PR for this follow-up. Later docs
  housekeeping commits are on `main`; check `git log -1`.

## What changed

- Read the Helikon-OS README (25 goals) and the full 3,178-line design paper
  (source commit `3f220a73d0cceb083b3c057c6fe914d88468800a`).
- Added `agents.md` as the canonical entry contract.
- Added the P0/P1/P2 documentation set under `docs/`: state, invariants,
  decisions, tasks, verification, file map, architecture, failures, device
  matrix, handoff, conventions, glossary, testing, validation, goals, roadmap,
  HFS+ writer deep dive, release, security, FAQ, design-paper copy, ADRs, logs
  and prompt stubs.
- Added root `CONTRIBUTING.md` and `SECURITY.md`.
- Fast-forwarded `docs/agent-handbook` to `main` and pushed `main`; PR #1 is
  merged.
- Added `LICENSE` (GPL-2.0-or-later), `NOTICE`, `CODE_OF_CONDUCT.md`
  (Contributor Covenant 2.1), and GitHub issue templates for bugs, features and
  device test reports; fast-forwarded the follow-up branch to `main` and pushed
  it.
- Did **not** modify the root `README.md`, the donor repository, any firmware,
  build system, CI or release asset. The requested README title, summary and 25
  goals were already present at the top, so no README change was needed.

## What was verified

- Repository cloned from `https://github.com/deandromi/Helikon-OS.git`; `origin`
  is correct; `main` exists; base commit recorded.
- Branched from `main` to `docs/agent-handbook` before writing, then
  fast-forwarded the branch to `main` and pushed it.
- README and paper fetched from raw GitHub URLs; full paper read in chunks. The
  paper copy preserves the body except for two trailing-space hard breaks
  normalized for whitespace checks (`diff -b` clean).
- Donor repository cloned to a temporary path for file references only; donor
  commit recorded; no donor files modified.
- Documentation set created and checked for:
  - `git diff --check`;
  - relative link and table inspection;
  - all 25 README goals present in `docs/goals.md`;
  - all 20 decisions present in `docs/decisions.md`;
  - roadmap phases 0–12 with exit criteria;
  - every task has an acceptance criterion;
  - no claim that Helikon-OS is implemented or hardware-validated;
  - no duplicate canonical files.
- No Helikon-OS build, test or device run was performed; there is no source tree.
- `main` and `origin/main` are at `6df63ef`; PR #1 state is MERGED. The requested
  README block is present verbatim (all 25 goal lines) and the README SHA-256 is
  unchanged from the base commit.
- License/COC/issue-template files parse/validate: YAML issue forms load; the
  license notice states GPL-2.0-or-later; the code of conduct has an enforcement
  contact and no placeholder remains.

## What failed

- Nothing failed in the documentation workflow.
- The initial docs pass needed a trim: `agents.md` was reduced from 363 to 290
  lines to keep it under the ~300-line contract limit.
- No device test was attempted; there is no Helikon-OS firmware.

## What is next

1. Owner reviews the merged docs on `main` and the conflict records C-01–C-09.
2. Resolve the phase-2 blockers first: D-03 (journal strategy) and D-04
   (journal ownership).
3. Freeze the writer interface and failure model (T-002), then the fixture plan
   (T-004) and backup/disposable-volume protocol (T-003).
4. Record the signing-key ownership policy (D-20/T-006) before any release work.
5. Start the 7G board-audit checklist (T-005) only when hardware access is
   available.

## What must not be touched

- Root `README.md` — owner-authored goals; do not edit.
- Donor repository `deandromi/hfsplus-rockboxed-ipod` — read-only reference.
- Any `/.rockpod-rw`, device backup, disk image, serial number or personal data.
- The native-HFS+ invariants, journal rules, `fsck_hfs` gate, bootloader
  read-only rule, or `--single` ban.
- `LICENSE` and `NOTICE` — the license choice is owner-approved; do not change
  it silently.
- Signing policy (D-20, not chosen).
- No code of conduct is currently adopted; `CODE_OF_CONDUCT.md` was later
  deleted by the owner (see latest handoff).

## Open questions

- D-03/D-04: journaled release writes and device-owned replay? Critical path.
- D-01/D-16: signature scheme and update transport.
- D-06: Unicode normalization/case-folding policy and fixture source.
- D-07: host-built database vs on-device incremental builder timing.
- D-10: exact Rockbox skin subset and corpus licences.
- D-13: 7G board audit and port scope.
- D-20: signing-key ownership and release-signing policy.
- Legal review of the GPL-2.0-or-later wording before a public release. No code
  of conduct is currently adopted.
- C-02–C-06: owner confirmation of README goal scope vs paper's narrower design
  response.

## Device/test evidence

- **None for Helikon-OS.** No boot, playback, write, journal replay, sleep, USB,
  update or recovery test was run.
- Donor V3.2 owner-reported hardware use exists in the donor repository but is
  not Helikon-OS evidence and is not recorded as a pass here.
- Do not add a device row as "pass" without the evidence template in
  `docs/verification.md`.

---

# Template — copy for the next session

```md
# Handoff — YYYY-MM-DD

## Branch / commit

## What changed

## What was verified

## What failed

## What is next

## What must not be touched

## Open questions

## Device/test evidence
```

## Template notes

- Put the newest handoff above this template; move older entries to
  `docs/log/YYYY-MM-DD.md`.
- Link the PR/commit; do not paste a diff.
- Say "not run" rather than implying a test happened.
- Record exact error codes and device states.
- If a hard stop was hit, say what was not attempted.
- Update `state.md` and `tasks.md` before writing the handoff.
