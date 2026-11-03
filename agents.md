# agents.md — working contract for Helikon-OS

**Purpose:** working contract and documentation index.
**Status:** living document; design-stage project. No firmware implementation exists.
**Owner:** project owner; every agent
**Last updated:** 2026-11-03
**Source:** root `README.md`, `docs/design-paper.md`
**Entry point:** read this file first, then `docs/README.md`.

This is the contract and index for anyone working in this repository. It is not
the manual. The design source is `docs/design-paper.md`; current truth is
`docs/state.md`; settled and open choices are in `docs/decisions.md`.

## 1. Project identity and status
Helikon-OS is a planned native-HFS+ bootloader and firmware for the iPod Classic
6th generation, with the 7th generation as the next named port. It is a
greenfield design, not a working firmware release.

- **Design stage:** no bootloader/kernel/firmware tree, installer, release
  artifact or Helikon-OS device run exists. Host HFS+ and diagnostic bootloader cores exist.
- **Current repository content:** the owner-authored root `README.md`, this
  documentation set, `firmware/hfs/` and `tests/hfsplus/`.
- **Donor release:** the V3.2 release of `hfsplus-rockboxed-ipod` is an
  overlay-based Rockpod/Rockbox fork. It is separate from Helikon-OS and is not
  evidence for Helikon-OS.
- **Target:** iPod Classic 6G first. 7G is next after a board audit. Other iPods
  and other players are ports, not launch promises.
- **Defining promise:** native HFS+ is writable through a real transaction
  engine. There is no namespace overlay and no FAT mount.
- **Project policies:** license GPL-2.0-or-later. No code of conduct is
  currently adopted. Signing-key ownership remains open (D-20).

Never describe Helikon-OS as implemented, working, validated, or supported on a
device. Use `planned`, `in progress`, `unproven`, `implemented` or `rejected` as
defined in `docs/conventions.md`.

## 2. Source-of-truth order
1. **Owner decisions and the Helikon-OS root `README.md`.** The README goals are
   owner-authored product intent. Do not edit the root README.
2. **`docs/design-paper.md`** for technical design.
3. **`docs/state.md`** for what is true today.
4. **`docs/decisions.md`** for settled choices and open decisions.
5. **`agents.md`** for working rules.
6. **Other docs** for summaries and task aids.

If a summary conflicts with the paper, the paper wins. Record the conflict in
`docs/decisions.md`; do not silently pick one. If a conflict changes product
direction (overlay, FAT, journaling, 7G scope, licensing, app/plugin
compatibility), stop and report it to the owner.

The donor repository is read-only source material. It is not a source of truth
for Helikon-OS behaviour. Its V3.2 safety model (native metadata immutable,
writes in `/.rockpod-rw`) is the opposite of Helikon-OS by design.

## 3. Non-negotiables
1. **Native HFS+ writable.** The data partition is native HFS+ v4. There is no
   `/.rockpod-rw`, no namespace overlay, no shadow namespace, no hidden
   container and no FAT mount. A non-HFS+ volume returns `NOT_HFS`.
2. **Journal every metadata change.** Catalog, bitmap, extents, volume-header and
   journal changes go through the transaction engine. Release builds use
   journaled writes. A non-journaled writer is a disposable-volume prototype.
3. **`fsck_hfs` compatibility is a release gate.** After every crash and
   power-cut test, `fsck_hfs` must report zero repairs. One catalog or bitmap
   inconsistency blocks release.
4. **No silent journal bypass.** Never disable, delete, truncate, ignore or
   pretend a dirty journal is clean. A dirty or invalid journal mounts
   read-only; replay belongs to the write engine and must finish before the
   first write.
5. **No destructive repair.** Never format, erase, restore, repartition, delete
   the journal, or zero catalog/bitmap/metadata to "fix" a software error.
6. **Bootloader is read-only.** It reads native HFS+, verifies a firmware
   manifest, reads boot state, and never writes the filesystem.
7. **Never install a bootloader with `--single`.** It destroys the Apple NOR
   path. Bootloader changes are DFU-only.
8. **Never test writes on the only copy of the library.** A verified backup and
   a recovery plan are required first.
9. **No support or hardware claim without a recorded device run.** Host tests
   and a successful link are not device validation.
10. **No unsigned firmware.** The bootloader verifies magic, board ID, version,
    load address, size, SHA-256 and signature before jumping.

Full testable statements are in `docs/invariants.md`.

## 4. Target policy
- **6G:** reference target; first implementation and first device matrix.
- **7G:** next named port. Requires a board audit of panel, PMU, audio,
  storage, button map and `MOVE_AREA` before any support claim.
- **Other iPods and players:** ports. They require a board support package, a
  target description, hardware, a maintainer and a recorded test matrix.
- **No support claim** without a recorded run in `docs/device-matrix.md`.
- **No external USB DAC on the 6G.** The 6G is a USB device, not a USB host. The
  internal CS42L55 path is the audio target.

## 5. Repository map
```text
README.md                 owner-authored product goals; do not edit
agents.md                 this contract
docs/                     agent documentation (current)
  design-paper.md         full design paper copy with provenance
  state.md                current phase, blockers, evidence status
  invariants.md           numbered safety invariants
  decisions.md            decisions and open-decision index
  tasks.md                current task queue
  verification.md         per-change proof checklist
  architecture.md         layers, boundaries, memory, security
  file-map.md             where to change what
bootloader/               diagnostic core + splash; hardware entry planned
firmware/                 host HFS+ read core exists; HAL/VFS/player/UI planned
  hfs/                    HFS+ read core; volume header/journal probe implemented
tools/                    planned: host tools, inspectors, migration
tests/                    host HFS+ volume tests exist; device/writer tests planned
docs/adr/                 long-form decisions
docs/log/                 dated work logs
docs/prompts/             reusable task prompts
```

`docs/file-map.md` is the authoritative map, including donor files referenced by
the paper. A directory shown as planned does not exist yet.
## 6. Build and test commands
**No flashable firmware or bootloader can be built yet.** Host code:
```sh
make -C tests/hfsplus test
make -C tests/hfsplus sanitize
make -C tests/bootloader test
make -C tests/bootloader sanitize
```
Do not invent firmware commands, images, test counts or device results. The
donor repository's commands are reference material only and do not test
Helikon-OS:

```sh
# Donor reference only.
make -C tools/hfsplus test
make -C tools/hfsplus sanitize
python3 tools/hfsplus/tests/check_integration.py
```

They must run in a separate donor checkout, never here. They prove donor host
tests, not Helikon-OS correctness or device safety. Future firmware commands are
added only when that tree exists and `docs/testing.md` records them.

## 7. Change recipes

Every recipe starts with reading the linked docs and ends with evidence and a
rollback. If a change touches data safety, stop until the transaction and
recovery story is written.

### 7.1 Documentation change

**Read first:** `docs/conventions.md`, `docs/state.md`, `docs/tasks.md`.
**Do:** update the one canonical file, links and status. Do not duplicate a topic
or edit the root README. **Test:** `git diff --check`; link and table inspection;
contradiction check from `docs/prompts/audit-docs-for-contradictions.md`.
**Evidence:** changed paths, checks run, open questions. **Rollback:** revert the
documentation commit; do not rewrite history.

### 7.2 HFS+ reader change

**Read first:** `docs/architecture.md`, `docs/hfsplus-write-engine.md`,
`docs/testing.md`, `docs/failures.md`, paper §8. **Do:** keep the reader
read-only; preserve bounds checks, error taxonomy and unsupported-object
reporting. Do not add a write path to the reader. **Test:** host unit tests,
malformed-image fixtures, fuzzing, differential checks against macOS-created
images. **Evidence:** test output, fixture hashes, parser errors, before/after
volume hash on device. **Rollback:** revert; never "fix" a bad mount by
formatting or deleting the journal.

### 7.3 HFS+ writer/journal change

**Read first:** `docs/hfsplus-write-engine.md`, `docs/invariants.md`,
`docs/validation.md`, paper §9 and §21. **Do:** one writer; all metadata through
a transaction; data before metadata; flush/barrier before and after commit;
idempotent replay; fail closed. **Test:** model-based B-tree tests,
allocation/extents property tests, crash injection at every boundary,
`fsck_hfs -n` after every cut, macOS differential tree comparison. Device tests
only on a disposable volume until phase 3. **Evidence:** commit SHA, image hash,
operation, cut point, journal state, `fsck_hfs` output, recovered tree,
device/adapter. **Rollback:** restore the pre-test image; do not repair metadata
by hand.

### 7.4 UI/skin change

**Read first:** `docs/architecture.md`, paper §14 and §28.3.4,
`docs/decisions.md` D-10 and D-15. **Do:** keep the first-party UI usable if a
skin fails; cap parsed nodes, image cache and frame work; report unsupported tags
visibly. **Test:** 20-skin corpus (when available), malformed/hostile skins,
frame-time budget, memory cap, fallback to default UI. **Evidence:** skin
name/licence, tag report, frame/memory numbers, screenshots. **Rollback:**
disable the skin and return to the default theme.

### 7.5 App/game sandbox change

**Read first:** `docs/security.md`, paper §15 and §28.3.9-§28.3.10,
`docs/decisions.md` D-08. **Do:** capabilities, quotas, no raw
block/NOR/PMU/GPIO access, crash isolation, signed packages. Do not add Rockbox
plugin binary compatibility. **Test:** permission denial, escape attempts, quota
exhaustion, crash/restart, install/update/uninstall. **Evidence:** runtime
version, package hash, denial log, crash log, memory/CPU trace. **Rollback:**
uninstall the package and remove only its private data after confirmation.

### 7.6 Desktop app change

**Read first:** `docs/architecture.md`, `docs/security.md`,
`docs/decisions.md` D-09, paper §28.4.2. **Do:** back up before destructive
actions; never silently overwrite a newer file; use the device's transaction
engine or macOS HFS+ support; keep scrobbling host-side and opt-in. **Test:**
macOS task matrix, failed-transfer consistency, conflict handling,
backup/restore, no network dependency. **Evidence:** app version, device
firmware hash, task matrix, before/after hashes, logs. **Rollback:** restore from
the verified backup; do not reformat the device.

### 7.7 Release/update change

**Read first:** `docs/release.md`, `docs/validation.md`, `docs/security.md`,
paper §16 and §28.2.5. **Do:** signed manifest, staged image, explicit install,
boot-state record, rollback, bootloader DFU only. No release without the
validation scorecard. **Test:** bad board, bad version, truncated image, wrong
hash, power cut during staging, rollback, version/hash display. **Evidence:**
manifest, image hash, device run, update/rollback log, `fsck_hfs` output.
**Rollback:** bootloader keeps the previous verified image; bootloader changes
recover through DFU.

## 8. Release policy
- Channels: **stable**, **beta**, **development**.
- Firmware images and manifests are signed. Signing scheme and key ownership are
  not chosen; see `docs/decisions.md` D-01 and D-20.
- Firmware is staged under `/.helikon/update/` through the HFS+ transaction
  engine, flushed, read back and verified.
- The bootloader verifies before jumping and keeps the previous verified image
  on failure.
- Bootloader changes are DFU-only and have their own verification.
- Updates are explicit, never silent.
- No stable release without the `docs/validation.md` scorecard, a recorded
  device run, and `fsck_hfs` zero repairs after the power-cut matrix.
- No release notes may hide an unexecuted stress gate.

## 9. Hard stops
Stop and report; do not guess, format, erase, delete, or "try one more write":

- Unknown overlay/journal state, or a dirty/invalid journal.
- A backup cannot be verified by reading a file back.
- The device is in an unexpected mode (DFU, Disk Mode, recovery, unknown).
- A failure could indicate data loss or metadata corruption.
- A proposed fix is format, erase, restore, repartition, delete the journal, or
  zero metadata.
- The only evidence is "it boots."
- The change would require modifying the root README or the donor repository.
- A required step needs a legal decision (trademark clearance or signing policy)
  that the owner has not made. The license is settled; no code of conduct is
  currently adopted.

Record the exact error, stage, device state and what was not attempted.

## 10. Decisions
Full entries and status are in `docs/decisions.md`.

**Settled:** D-17 product name (clearance open), D-18 GPL-2.0-or-later.
**Rejected:** D-19 Contributor Covenant (owner deleted the file; no replacement).

**Open:** D-01 boot image verification and key storage · D-02 boot state and
fallback · D-03 journal strategy · D-04 journal ownership · D-05 B-tree cache
and transaction limits · D-06 Unicode policy · D-07 database location · D-08
app/game runtime · D-09 desktop app transport · D-10 skin compatibility scope ·
D-11 anti-corruption scope · D-12 battery target · D-13 7G and port scope ·
D-14 car protocol · D-15 theme format · D-16 update transport · D-17 name
clearance · D-20 signing-key ownership.

## 11. Handoff protocol
1. Read `docs/state.md` and `docs/tasks.md` before starting.
2. Do not rely on chat history for current state.
3. At the end of a session, update `docs/handoff.md` with the latest entry.
4. Record device tests in `docs/device-matrix.md`; no claim without a recorded
   run.
5. Record contradictions and decisions in `docs/decisions.md`.
6. Update `docs/state.md` when the phase, blockers or evidence status changes.
7. Move older handoffs to `docs/log/` when the handoff file grows.
8. Commit only the files you changed; do not rewrite history or force-push.

The handoff template is in `docs/handoff.md`. A dated log entry belongs in
`docs/log/YYYY-MM-DD.md`.

## 12. Documentation conventions
- Follow `docs/conventions.md`.
- Use the status labels `implemented`, `in progress`, `planned`, `unproven`,
  `rejected` only as defined there.
- Link to paper sections rather than copying long passages.
- No marketing claims, no unverified measurements, no invented hardware facts.
- Mark unknowns as `open question`.
- One canonical file per topic. Do not create `agent.md`, `decision-log.md` or
  `release-process.md` alongside the canonical files.
- Do not copy proprietary themes, artwork, fonts or metadata. Reference them.
- No secrets, device backups, `/.rockpod-rw`, disk images, serial numbers,
  Wi-Fi credentials or personal data.
- Update `docs/state.md`, `docs/tasks.md` and `docs/handoff.md` every session.

## 13. Full documentation index
[Index](docs/README.md) · [state](docs/state.md) · [invariants](docs/invariants.md) · [decisions](docs/decisions.md) · [tasks](docs/tasks.md) · [verification](docs/verification.md) · [file map](docs/file-map.md) · [architecture](docs/architecture.md).
[Failures](docs/failures.md) · [device matrix](docs/device-matrix.md) · [handoff](docs/handoff.md) · [conventions](docs/conventions.md) · [glossary](docs/glossary.md) · [testing](docs/testing.md) · [validation](docs/validation.md) · [goals](docs/goals.md).
[Roadmap](docs/roadmap.md) · [write engine](docs/hfsplus-write-engine.md) · [release](docs/release.md) · [security](docs/security.md) · [FAQ](docs/faq.md) · [design paper](docs/design-paper.md) · [logo assets](assets/logo/README.md) · [branding assets](assets/branding/README.md) · [bootloader](bootloader/README.md) · [ADRs](docs/adr/README.md) · [ADR template](docs/adr/0001-template.md).
[Log](docs/log/README.md) · [log template](docs/log/TEMPLATE.md) · [latest log](docs/log/2026-11-03.md) · [prompts](docs/prompts/README.md) · [writer review](docs/prompts/review-hfsplus-writer.md) · [device matrix prompt](docs/prompts/run-device-matrix.md) · [docs audit](docs/prompts/audit-docs-for-contradictions.md).
[Contributing](CONTRIBUTING.md) · [security policy](SECURITY.md) · [license](LICENSE) · [notice](NOTICE) · [issue templates](.github/ISSUE_TEMPLATE/).
