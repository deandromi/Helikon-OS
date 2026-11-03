# Tasks — current queue

> Purpose: what to do next, with dependencies and acceptance criteria.
> Status: living document
> Owner: project owner; task owners as listed
> Last updated: 2026-11-03
> Source: design paper §23–§28, `docs/state.md`

Priority: **P0** blocks the first safe write release; **P1** is the first
follow-up line; **P2** is later and must not delay P0. Status uses
`planned`, `in progress`, `unproven`, `implemented` or `rejected`.

## Now

License (GPL-2.0-or-later) is settled (D-18). No code of conduct is currently
adopted (D-19 rejected). Signing-key ownership remains open (D-20).

| ID | Task | Priority | Dependencies | Acceptance criteria | Status | Owner | Links |
|---|---|---|---|---|---|---|---|
| T-001 | Record journal strategy and ownership decisions (D-03, D-04). | P0 | Owner review | Both entries in `decisions.md` are `accepted` with owner, date, consequences and phase-2 gate; no recommendation is presented as settled before that. | `in progress` | owner | [D-03](decisions.md#d-03--journal-strategy), [D-04](decisions.md#d-04--journal-ownership) |
| T-002 | Freeze the HFS+ writer interface and failure model. | P0 | T-001 | A reviewed design section lists begin/commit/abort, catalog/bitmap/extents operations, error codes, transaction boundaries and invariants I-03/I-05/I-15. | `planned` | architecture | [hfsplus-write-engine.md](hfsplus-write-engine.md), [invariants.md](invariants.md) |
| T-003 | Define the backup, recovery and disposable-volume protocol. | P0 | Owner approval | Checklist covers full backup, SHA-256, read-back, `fsck_hfs -n`, Disk Mode, DFU, Apple restore, disposable-volume format and stop conditions. Dry run recorded. | `planned` | owner/test lead | [verification.md](verification.md), [device-matrix.md](device-matrix.md) |
| T-004 | Create the host fixture plan for HFS+ reader and writer tests. | P0 | T-002 | Fixture list covers clean/dirty journal, malformed B-trees, extents overflow, Unicode/case names, allocation edge cases; each fixture has a generation method, hash and expected result. | `planned` | test lead | [testing.md](testing.md), [validation.md](validation.md) |
| T-005 | Record the 7G board-audit checklist. | P1 | Owner access to hardware | Checklist covers SoC/board revision, panel, PMU, audio codec, storage, button map, `MOVE_AREA`, DFU/NOR; no 7G support claim until results are recorded. | `planned` | hardware lead | [device-matrix.md](device-matrix.md), [roadmap.md](roadmap.md) |
| T-006 | Record signing-key ownership and release-signing policy (D-20). | P0 | Owner/legal | D-20 is `accepted` with owner, key custody, rotation, revocation, recovery and a date; no release signing before then. | `planned` | owner/legal | [decisions.md](decisions.md#d-20--signing-key-ownership), [release.md](release.md), [security.md](security.md) |

## Next

These can start once the Now items they depend on are complete. They are design
and test work, not permission to write a real library.

| ID | Task | Priority | Dependencies | Acceptance criteria | Status | Owner | Links |
|---|---|---|---|---|---|---|---|
| T-007 | Write the catalog B-tree mutation design: insert/update/delete, split/merge, free-node map, leaf links and key ordering. | P0 | T-002, T-004 | Design includes node layouts, split/merge invariants, `FastUnicodeCompare` ordering and model-based test cases; review finds no direct-write path. | `planned` | HFS+ lead | [hfsplus-write-engine.md](hfsplus-write-engine.md) |
| T-008 | Write the allocation bitmap and extents/overflow design. | P0 | T-002, T-004 | Design covers alloc/free, free-block counts, inline/overflow extents, truncate and `NO_SPACE`; property tests are named. | `planned` | HFS+ lead | [hfsplus-write-engine.md](hfsplus-write-engine.md) |
| T-009 | Write the journal record, commit, barrier and replay design. | P0 | T-001, T-002 | Design covers sequence/checksum, commit header, apply, idempotent replay, clean marker and read-only failure; cut points enumerated. | `planned` | journal lead | [hfsplus-write-engine.md](hfsplus-write-engine.md) |
| T-010 | Define the power-loss matrix and evidence format. | P0 | T-003, T-009 | Matrix covers create, append, close, rename, delete, truncate, mkdir, settings, database and artwork; each row names cut points, oracle and evidence. | `planned` | test lead | [validation.md](validation.md) |
| T-011 | Design the bootloader manifest, boot-state and rollback flow. | P0 | D-01, D-02, D-16 | Design covers manifest fields, signature check, two-slot boot state, read-only bootloader and fallback; bad-image tests are listed. | `planned` | boot lead | [architecture.md](architecture.md), [release.md](release.md) |
| T-012 | Bootstrap the 6G device inventory and recovery rehearsal. | P0 | T-003 | Inventory records model, revision, capacity, adapter/card, battery, macOS, firmware/bootloader, journal state; Disk Mode/DFU/Apple restore confirmed; no serial numbers. | `planned` | test lead | [device-matrix.md](device-matrix.md) |
| T-013 | Define the first-party UI task tests and the 20-skin corpus licence review. | P1 | D-10, D-15 | Five tasks (play, browse, settings, podcast, install) have success criteria; corpus list records licence/source; unsupported-tag report format defined. | `planned` | UI/test lead | [goals.md](goals.md), [validation.md](validation.md) |
| T-014 | Publish and review the agent documentation set. | P0 | None | `agents.md` and all P0/P1/P2 docs exist; links and tables pass inspection; root README unchanged (requested block already present); PR #1 merged to `main`. | `implemented` | documentation agent | [README.md](README.md), [verification.md](verification.md) |

## Later

These are roadmap items, not current commitments. They require a source tree,
hardware access and recorded gates.

| ID | Task | Priority | Dependencies | Acceptance criteria | Status | Owner | Links |
|---|---|---|---|---|---|---|---|
| T-015 | Phase 0: rescue bootloader and native HFS+ read-only firmware skeleton. | P0 | T-011, source tree | Boots, loads a verified image, Apple path intact, recovery menu, zero writes; volume hash unchanged. | `planned` | boot/firmware | [roadmap.md](roadmap.md) |
| T-016 | Phase 1: read-only player with HAL, HFS+ read, audio and minimal UI. | P0 | T-015 | Plays, browses, survives reboot and leaves the volume hash unchanged. | `planned` | firmware | [roadmap.md](roadmap.md) |
| T-017 | Phase 2: non-journaled writer prototype on a disposable volume. | P0 | T-007, T-008, T-010 | Create/write/rename/delete/mkdir/truncate; `fsck_hfs` zero repairs; macOS sees the same tree. | `planned` | HFS+ lead | [hfsplus-write-engine.md](hfsplus-write-engine.md) |
| T-018 | Phase 3: journaled writer and replay engine. | P0 | T-009, T-017 | Power-cut matrix passes; replay idempotent; `fsck_hfs` zero repairs. | `planned` | journal lead | [hfsplus-write-engine.md](hfsplus-write-engine.md) |
| T-019 | Phase 4: metadata parity and transactional user state. | P0 | T-018 | Timestamps, BSD/Finder info, settings, playlists and resume survive reboot; macOS agrees. | `planned` | HFS+/firmware | [roadmap.md](roadmap.md) |
| T-020 | Phase 5: anti-corruption foundations. | P0 | T-018, D-11 | Injected corruption detected/quarantined; no false OK; scrub is charging/idle and rate-limited. | `planned` | firmware | [validation.md](validation.md) |
| T-021 | Phase 6–9: migration, database, UI, USB, updates, installer and desktop MVP. | P1 | T-018, T-019, D-07, D-09, D-16 | Each phase exits per `roadmap.md`; no release without validation scorecard. | `planned` | product/host | [roadmap.md](roadmap.md) |
| T-022 | Phase 10–12: sandbox, hardening, 7G and ports. | P2 | T-021, D-08, D-13 | Sandbox escape tests, measured battery/RAM/I/O, per-target matrices; no support claim without device run. | `planned` | product/hardware | [roadmap.md](roadmap.md) |

## Task rules

- Every task has an acceptance criterion that can be checked without reading a
  chat log.
- A task is not `implemented` because code exists; it is `implemented` when its
  acceptance evidence is recorded.
- A device task is not complete without the device, adapter, firmware hash,
  test, result and evidence in `device-matrix.md`.
- A decision task is not complete until the owner marks the decision
  `accepted`/`rejected` and records date and consequences.
- Do not add "improve UI", "make it faster" or "support more devices" without a
  measurable acceptance test.

## Related

- [`state.md`](state.md) — current blockers
- [`verification.md`](verification.md) — how to prove a task
- [`roadmap.md`](roadmap.md) — phase gates
- [`validation.md`](validation.md) — release evidence
