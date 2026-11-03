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
| T-001 | Record journal strategy and ownership decisions (D-03, D-04). | P0 | Owner review | Both entries in `decisions.md` are `accepted` with owner, date, consequences and phase-2 gate. D-03: journaled writes required, non-journaled prototype disposable-only. D-04: Helikon-OS owns replay. | `implemented` | owner | [D-03](decisions.md#d-03--journal-strategy), [D-04](decisions.md#d-04--journal-ownership) |
| T-002 | Freeze the HFS+ writer interface and failure model. | P0 | T-001 | A reviewed design section lists begin/commit/abort, catalog/bitmap/extents operations, error codes, transaction boundaries and invariants I-03/I-05/I-15. §10 in `hfsplus-write-engine.md` freezes the interface and failure model; implementation not started. | `implemented` | architecture | [hfsplus-write-engine.md](hfsplus-write-engine.md#10-frozen-writer-interface-and-failure-model), [invariants.md](invariants.md) |
| T-003 | Define the backup, recovery and disposable-volume protocol. | P0 | Owner approval | Checklist covers full backup, SHA-256, read-back, `fsck_hfs -n`, Disk Mode, DFU, Apple restore, disposable-volume format and stop conditions. Dry run recorded. | `planned` | owner/test lead | [verification.md](verification.md), [device-matrix.md](device-matrix.md) |
| T-004 | Create the host fixture plan for HFS+ reader and writer tests. | P0 | None for reader fixtures; T-002 for writer fixtures | Fixture list covers clean/dirty journal, malformed B-trees, extents overflow, Unicode/case names, allocation edge cases; each fixture has a generation method, hash and expected result. Reader fixtures are started; allocation/writer fixtures remain. | `in progress` | test lead | [testing.md](testing.md), [validation.md](validation.md) |
| T-005 | Record the 7G board-audit checklist. | P1 | Owner access to hardware | Checklist covers SoC/board revision, panel, PMU, audio codec, storage, button map, `MOVE_AREA`, DFU/NOR; no 7G support claim until results are recorded. | `planned` | hardware lead | [device-matrix.md](device-matrix.md), [roadmap.md](roadmap.md) |
| T-006 | Record signing-key ownership and release-signing policy (D-20). | P0 | Owner/legal | D-20 is `accepted` with owner, key custody, rotation, revocation, recovery and a date; no release signing before then. | `planned` | owner/legal | [decisions.md](decisions.md#d-20--signing-key-ownership), [release.md](release.md), [security.md](security.md) |

## Done

| ID | Task | Priority | Dependencies | Acceptance criteria | Status | Owner | Links |
|---|---|---|---|---|---|---|---|
| T-023 | Implement the host HFS+ volume header/journal probe and tests. | P0 | None | `firmware/hfs/hfs_volume.c` validates primary/alternate headers, block size, attributes and journal flags; 12 host tests pass; sanitizer clean; no device writes. | `implemented` | firmware/test | [hfs_volume.c](../firmware/hfs/hfs_volume.c), [test_volume.c](../tests/hfsplus/test_volume.c) |
| T-025 | Implement generic HFS+ B-tree header/node validation and tests. | P0 | None | `firmware/hfs/hfs_btree.c` validates header node kind/height/counts, reverse offset table, depth/root/leaf bounds, node size and fork-size parity; 9 host tests pass; sanitizer clean; no device writes. | `implemented` | firmware/test | [hfs_btree.c](../firmware/hfs/hfs_btree.c), [test_btree.c](../tests/hfsplus/test_btree.c) |
| T-026 | Implement the host HFS+ catalog, Unicode and extents-overflow read core. | P0 | T-025 | Exact UTF-8 path lookup, folder/file parsing, inline and overflow file reads; 13 tests pass (8 Unicode, 3 catalog, 2 extents); sanitizer clean; no device writes. | `implemented` | firmware/test | [hfs_catalog.c](../firmware/hfs/hfs_catalog.c), [hfs_extents.c](../firmware/hfs/hfs_extents.c) |
| T-028 | Implement the read-only diagnostic bootloader core and splash. | P0 | T-026 | `bootloader/diagnostics.c` reports volume/catalog/read status, always reports write as `NOT IMPLEMENTED`, formats a report, and exposes the 320×240 1-bit splash; 7 host tests pass; sanitizer clean; no device writes. | `implemented` | boot/firmware | [bootloader/README.md](../bootloader/README.md), [test_diag.c](../tests/bootloader/test_diag.c) |
| T-031 | Implement the Apple HFS+ journal append/replay path and bounded transaction engine. | P0 | T-009 | `hfs_journal.c` validates header/block-list checksums, handles circular wrap, appends and replays transactions idempotently; `hfs_txn.c` stages bounded blocks and commits through the journal; 8 journal + 7 transaction tests pass; sanitizer clean; no device write. Catalog/bitmap/extents callers are not implemented. | `implemented` | HFS+/journal | [hfs_journal.c](../firmware/hfs/hfs_journal.c), [hfs_txn.c](../firmware/hfs/hfs_txn.c), [test_journal.c](../tests/hfsplus/test_journal.c) |
| T-033 | Implement the allocation bitmap alloc/free and free-block staging path. | P0 | T-031 | `hfs_bitmap.c` first-fit alloc/free, free-block counts, primary/alternate header staging and `NO_SPACE`; 5 host tests pass; sanitizer clean; allocation-file overflow extents return `UNSUPPORTED`; no device write. | `implemented` | HFS+/journal | [hfs_bitmap.c](../firmware/hfs/hfs_bitmap.c), [test_bitmap.c](../tests/hfsplus/test_bitmap.c) |
| T-035 | Implement B-tree node insert/delete/split and fork block staging. | P0 | T-031 | `hfs_btree_write.c` maintains the reverse offset table and free space and splits a node into left/right halves; `hfs_fork_io.c` stages full/partial fork blocks into `hfs_txn`; 7 + 4 host tests pass; sanitizer clean; tree-level parent/root updates and merge are not implemented. | `implemented` | HFS+/journal | [hfs_btree_write.c](../firmware/hfs/hfs_btree_write.c), [hfs_fork_io.c](../firmware/hfs/hfs_fork_io.c) |
| T-036 | Implement tree-level B-tree insertion with parent/root split. | P0 | T-035 | `hfs_btree_tree.c` traverses from root to leaf, allocates free nodes from the header map, splits leaves/index nodes, inserts parent records, creates a new root and stages all changes in `hfs_txn`; one 20-record model test passes; below-minimum insertion fails closed; delete/merge not implemented. | `implemented` | HFS+/journal | [hfs_btree_tree.c](../firmware/hfs/hfs_btree_tree.c), [test_btree_tree.c](../tests/hfsplus/test_btree_tree.c) |
| T-037 | Implement native empty-file catalog create/delete, valence and multi-extent data append/truncate. | P0 | T-031, T-033, T-036 | `hfs_catalog_write.c` inserts/deletes file and thread records, allocates a CNID, updates file count/parent valence, appends to empty and non-empty files through up to eight inline extents and truncates them; `hfs_volume_write.c` stages primary/alternate headers; one host test passes; extents-overflow, FastUnicodeCompare and fork growth are not implemented. | `implemented` | HFS+/journal | [hfs_catalog_write.c](../firmware/hfs/hfs_catalog_write.c), [hfs_volume_write.c](../firmware/hfs/hfs_volume_write.c), [test_catalog_write.c](../tests/hfsplus/test_catalog_write.c) |

## Next

These can start once the Now items they depend on are complete. They are design
and test work, not permission to write a real library.

| ID | Task | Priority | Dependencies | Acceptance criteria | Status | Owner | Links |
|---|---|---|---|---|---|---|---|
| T-027 | Define the bootloader manifest/boot-state flow and host image-loading core. | P0 | D-01, D-02, T-003, T-026 | A read-only host loader resolves the firmware path through the HFS+ read core, validates size/board/hash and calls an explicit signature-verification hook; no writes, no jump, tests recorded. | `planned` | boot lead | [architecture.md](architecture.md), [release.md](release.md), [decisions.md](decisions.md) |
| T-029 | Build and run a hardware-testable diagnostic bootloader image. | P0 | T-028, T-003 | Image built: 67,144 bytes, SHA-256 `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c`, `_moveend` `0x22030e40`, 61,888 bytes `MOVE_AREA` headroom. First run (`bd0ebc92…`) booted and splashed; volume/catalog PASS, read FAIL `-11`, report formatting corrupt. Fixed image (`280ed7ae…`) rerun: formatting correct, volume/catalog/write/journal/splash/partition correct, read `-8` because `/rockbox.ipod` is not a disk file; no write. Remaining: optional real-file probe, button/power-off record. | `in progress` | boot/release | [DFU.md](../bootloader/DFU.md), [artifact README](../bootloader/artifacts/README.md), [device-matrix.md](device-matrix.md) |
| T-007 | Write the catalog B-tree mutation design: insert/update/delete, split/merge, free-node map, leaf links and key ordering. | P0 | T-002, T-004 | Design includes node layouts, split/merge invariants, `FastUnicodeCompare` ordering and model-based test cases; review finds no direct-write path. | `planned` | HFS+ lead | [hfsplus-write-engine.md](hfsplus-write-engine.md) |
| T-032 | Implement a first native catalog mutation on a disposable image. | P0 | T-007, T-031, T-033, T-034 | Empty-file create/delete now stages through the B-tree and journal. Remaining: parent valence, data forks, extents/overflow, FastUnicodeCompare, fork growth and a disposable HFS+ image passing `fsck_hfs -n` with zero repairs; no library or device write. | `in progress` | HFS+/journal | [hfsplus-write-engine.md](hfsplus-write-engine.md), [hfs_catalog_write.c](../firmware/hfs/hfs_catalog_write.c) |
| T-034 | Implement the generic HFS+ B-tree mutation engine. | P0 | T-002, D-06 | Node insert/delete/split, tree-level insert with parent/root split, tree delete with first-record key propagation/empty-leaf unlink, next/previous-sibling merge/redistribution, one-child root collapse and empty-parent collapse are implemented. Remaining: fork growth, below-minimum insert, and catalog FastUnicodeCompare (D-06); extents uses binary keys. | `in progress` | HFS+ lead | [hfsplus-write-engine.md](hfsplus-write-engine.md), [hfs_btree_tree.c](../firmware/hfs/hfs_btree_tree.c) |
| T-008 | Write the allocation bitmap and extents/overflow design. | P0 | T-002, T-004 | Design covers alloc/free, free-block counts, inline/overflow extents, truncate and `NO_SPACE`; property tests are named. | `planned` | HFS+ lead | [hfsplus-write-engine.md](hfsplus-write-engine.md) |
| T-009 | Write the journal record, commit, barrier and replay design. | P0 | T-001, T-002 | Design covers sequence/checksum, commit header, apply, idempotent replay, clean marker and read-only failure; cut points enumerated. §6.5–§6.7 freeze the Apple HFS+ journal format, commit/replay algorithm and validation; implementation not started. | `implemented` | journal lead | [hfsplus-write-engine.md](hfsplus-write-engine.md#65-on-disk-format-apple-hfs-journal) |
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
| T-024 | Display the owner-supplied 320×240 startup image during boot. | P1 | T-015 (phase 0 bootloader), display HAL bring-up | The 6G shows `assets/startup/helikon-os-320x240.png` during startup, writes nothing to the filesystem, and can still enter recovery/DFU; recorded device evidence. | `planned` | boot/UI | [branding README](../assets/branding/README.md), [device-matrix.md](device-matrix.md) |
| T-030 | Add HFS+ write diagnostics on a disposable volume. | P0 | T-018 (journaled writer), D-03, D-04, T-003 | Write diagnostic only on a disposable or verified-backup volume; create/write/truncate/delete; `fsck_hfs` zero repairs; never on the library; not part of the first read-only image. | `planned` | HFS+ lead | [hfsplus-write-engine.md](hfsplus-write-engine.md), [DFU.md](../bootloader/DFU.md) |

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
