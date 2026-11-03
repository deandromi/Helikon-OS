# Handoff — Helikon-OS

> Purpose: the latest session handoff and the reusable template.
> Status: living document
> Owner: session author; next agent reads this first
> Last updated: 2026-11-03
> Source: `docs/state.md`, `docs/tasks.md`, repository history

Keep the newest handoff at the top. Move older entries to `docs/log/` when this
file grows. Do not rely on chat history for current state.

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
  it has no image or hash and must not be executed yet.
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
   recovery rehearsal; verify Thumb/`MOVE_AREA`, record the SHA-256, then use
   `bootloader/DFU.md`.
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
