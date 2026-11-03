# State — Helikon-OS

> Purpose: what is true today. This file is not a plan and not a design paper.
> Status: living document
> Owner: project owner; maintained by the documentation agent
> Last updated: 2026-11-03
> Source: `README.md`, `docs/design-paper.md`, repository inspection

## Current phase

**Pre-phase 0 — design, host read core and a first diagnostic bootloader.** The
roadmap in [`roadmap.md`](roadmap.md) starts at phase 0 (rescue bootloader and
read-only firmware skeleton). The first read-only diagnostic bootloader image
exists, but it has no device run and does not load firmware.

Date of this snapshot: **2026-11-03**.

## What exists

| Item | Document status | Evidence |
|---|---|---|
| Root `README.md` with 25 owner-authored goals and poster | living | Current `README.md`; goals unchanged, poster added 2026-11-03 |
| Design paper, revision 2, 2026-11-03 | source | [`design-paper.md`](design-paper.md), source commit `3f220a73d0cceb083b3c057c6fe914d88468800a` |
| Agent contract and documentation set | living | `main`, merged 2026-11-03 |
| Product intent, safety rules, roadmap, goals, open decisions | living | `docs/` |
| Donor file references and provenance | living | [`file-map.md`](file-map.md) |
| License: GPL-2.0-or-later | living | `LICENSE`, `NOTICE`, D-18 |
| GitHub issue templates | living | `.github/ISSUE_TEMPLATE/` |
| Helikon-OS poster | living | `assets/branding/helikon-os-poster.png`; embedded in README |
| 320×240 startup crop | living (asset); `in progress` display | `assets/startup/helikon-os-320x240.png`; wired into the diagnostic image, unproven on hardware |
| Mountain reference image | living | `assets/logo/mount-helikon-sunset.jpg`; source/licence open question |

The "document status" column uses the document statuses from
[`conventions.md`](conventions.md); it does not mean firmware exists.

The README summary and 25-goal block are unchanged. The owner added the
Helikon-OS poster above the goals on 2026-11-03.

## Host code

| Item | Status | Evidence |
|---|---|---|
| HFS+ volume header/journal probe | `in progress` | `firmware/hfs/hfs_volume.c`; 12 host tests |
| Generic HFS+ B-tree header/node validation | `in progress` | `firmware/hfs/hfs_btree.c`; 9 host tests |
| HFS+ catalog iteration/lookup | `in progress` | `firmware/hfs/hfs_catalog.c`; 3 host tests |
| UTF-16BE name decode | `in progress` | `firmware/hfs/hfs_unicode.c`; 8 host tests |
| Extents-overflow file read | `in progress` | `firmware/hfs/hfs_extents.c`; 2 host tests |
| Diagnostic bootloader core | `in progress` | `bootloader/diagnostics.c`, `report.c`, `splash.c`; 7 host tests; read-only |
| Diagnostic hardware glue | `in progress` | `bootloader/hardware/helikon_diag_hw.c`; ATA read callback, donor partition scan, splash/report, button wait, power-off |
| Diagnostic bootloader image | `unproven` | `bootloader/artifacts/bootloader-ipod6g.ipod`; SHA-256 `280ed7ae…`; 67,144 bytes; `MOVE_AREA` headroom 61,888 bytes. Owner-reported: boot, splash, report, volume/catalog PASS; read `-8` because the probe file is absent; no write. |
| Diagnostic build script | `in progress` | `tools/build-diagnostic-bootloader.sh`; pinned donor commit, Thumb build, `MOVE_AREA` check, hash output |
| Writer interface/failure model | design | `docs/hfsplus-write-engine.md` §10; D-03/D-04 accepted; implementation not started |
| Journal format/commit/replay design | design | `docs/hfsplus-write-engine.md` §6.5–§6.7; Apple HFS+ journal format |
| Journal append/replay engine | `implemented` | `firmware/hfs/hfs_journal.c`; header/block-list checksum validation, wrap, append, idempotent replay; 8 host tests |
| Bounded transaction engine | `implemented` | `firmware/hfs/hfs_txn.c`; staged-block commit through the journal; 7 host tests |
| Allocation bitmap | `implemented` | `firmware/hfs/hfs_bitmap.c`; first-fit alloc/free, free-block counts, header/alternate staging, `NO_SPACE`; 5 host tests; allocation-file overflow extents unsupported |
| B-tree node mutation helpers | `implemented` | `firmware/hfs/hfs_btree_write.c`; node offset table insert/delete, free-space checks and node split; 7 host tests |
| B-tree tree insertion | `implemented` | `firmware/hfs/hfs_btree_tree.c`; root-to-leaf traversal, free-node map, leaf/index split, parent insertion, recursive split, root creation, leaf below-minimum insert and parent-key propagation; index-node below-minimum insert at depth >2 still fails closed; 1 model test with 20 records plus a below-minimum case |
| B-tree delete | `implemented` | `hfs_btree_delete` removes records including first records, propagates changed parent keys, unlinks/frees empty leaves, merges/redistributes with both siblings, collapses a one-child root to its nonempty leaf, and collapses an empty parent into its grandparent; fork growth is implemented separately; covered by the same model test |
| Volume header staging | `implemented` | `hfs_volume_write.c`; primary/alternate header file/folder counts, next CNID and write count; no `modifyDate` update |
| Private NUL names | `implemented` | Directory iteration escapes NUL as U+2400 and marks the entry `name_escaped`/`unsupported`; path lookup skips the escaped entry and continues instead of aborting the directory; 1 catalog test |
| Extents-overflow record | `implemented` | `hfs_extents_write.c`; data-fork records keyed by fork/file/startBlock, up to eight extents each; insert/get/delete; multiple records supported |
| Inner-image mapping | `implemented` | `bootloader/inner_image.c` maps a root-level outer file's data-fork extents into a block device, rejects out-of-range writes, and guards against overlap with outer metadata, journal, volume headers and forks; 4 host tests |
| Separate write diagnostic | `implemented` | `bootloader-ipod6g-write-diag.ipod` (110,920 bytes, `81350f…`, 18,112 bytes headroom) keeps the outer HFS+ volume read-only and runs the writer inside `helikon-test.img`; host outer/inner test passes twice, and one owner-reported 6G run reported every stage PASS; inner `fsck_hfs` and outer `diskutil verifyVolume` both exited 0 |
| Catalog metadata | `implemented` | Timestamps: create/access/content/attribute on create; content/attribute on data changes; attribute-only on rename; parent dates and volume modify date on mutation; backupDate 0. BSD owner/group 0, flags 0, file/folder mode, Finder info/textEncoding zero, special 0; preserved on append/truncate/rename. Host metadata tests; macOS outer-image test verifies modes, current-year timestamps, rename/truncate paths. Wider differential and Finder creator/type policy remain |
| Outer-volume device write | `blocked` | Host disposable-image path passes; the real outer-volume test needs a full verified backup, recovery rehearsal and the separate T-047 image. Do not run on the library |
| Controlled PMU cut | `pass` | Current image `81350f…`: targets 7/56 were pre-commit clean controls; target 96 cut after commit, recovered on the next boot, all stages PASS, and the extracted inner image passed `fsck_hfs` with `The volume HelikonInner appears to be OK`. Further replay targets are optional coverage |
| Write diagnostic 6G run | `pass` | One owner-reported run, 2026-11-03: all stages PASS, extracted inner image SHA-256 `156060…`, `fsck_hfs -fn` exit 0, no `diag` folder, outer `diskutil verifyVolume` exit 0; not a library-write or support claim |
| Journal crash injection | `implemented` | `tests/image/run-crash-macos.sh` injects a failed write at every cut point of create (25), append (43), truncate (34) and delete (25) transactions, reopens the image, replays the journal and runs `fsck_hfs -fn`; all 127 cuts recover to a consistent state and an fsck-clean volume; rename, partial-sector writes, fsync failures and power-loss timing remain |
| Disposable-image writer validation | `implemented` | `tests/image/run-macos.sh` creates a 16 MiB journaled HFS+ image, creates a folder/file, appends 4096 bytes, renames, truncates and deletes through the outer volume, mounts at each stage and checks content, modes, timestamps and paths; `fsck_hfs -fn` is clean after every stage. Host-only; no device or library write |
| B-tree fork growth | `implemented` | `hfs_btree_grow()` allocates a volume block, extends the catalog/extents fork in the primary/alternate headers, adds a free node to the header map and updates `totalNodes`; requires `node_size == block_size` and an inline fork extent slot; 1 host test |
| Catalog create/delete/rename | `implemented` | `hfs_catalog_write.c`; file and folder records, thread records, CNID allocation, file/folder counts, parent valence, rename/move with a folder-cycle check; names are normalized to HFS+ canonical form; 5 host tests |
| Unicode catalog comparison | `implemented` | `hfs_catalog_compare.c` uses Apple `FastUnicodeCompare`; `hfs_unicode_normalize_name()` decomposes, applies the HFS+ exception ranges, decomposes Hangul and canonically orders combining marks; `hfs_unicode_data.c` is generated from Unicode 2.1.9 by `tools/gen-hfs-unicode-table.py`; 23 Unicode and 6 key-comparison tests; a local macOS-created image matches the stored forms; broader corpus and `fsck_hfs` ordering checks remain |
| Data-fork append/truncate (eight inline + multiple overflow records) | `implemented` | `hfs_catalog_append_file` appends to empty or non-empty files, stages data before metadata, allocates individual free blocks, merges inline extents and writes multiple extents-overflow records; truncate frees all records; the transaction block budget caps very large fragmented writes |
| Fork block staging | `implemented` | `firmware/hfs/hfs_fork_io.c`; full/partial block write staging into `hfs_txn`; inline extents only; 4 host tests |
| DFU install runbook | living | [`bootloader/DFU.md`](../bootloader/DFU.md); backup, manual SHA-256, DFU install and recovery; image is unproven |
| Default UI typography | `planned` | D-23 and `docs/ui.md` record IBM Plex Sans 14/17/12/11 px and Manrope Bold logo; D-24 requires grayscale anti-aliased glyph masks/atlases with a 1-bit fallback. Font licences/subset and the 320×240 mockup are not done; no UI code exists |
| Bootloader splash redesign | `planned` | Current splash is the owner-supplied 320×240 1-bit image; owner will design a new splash (T-045). Rendering path and licence/source are open |
| Host test harness | `in progress` | 110 portable tests (98 HFS+, 12 bootloader), sanitizer clean, plus the macOS disposable-image, crash-injection and write-diagnostic tests; first diagnostic boot is owner-reported |

## What does not exist

| Item | Status | Note |
|---|---|---|
| Helikon-OS firmware source tree | `in progress` | Host HFS+ read core and a diagnostic bootloader exist; no kernel, VFS or player |
| Helikon-OS firmware-loading bootloader | `planned` | The diagnostic image does not load firmware; D-01/D-02 still open |
| Helikon-OS bootloader image | `unproven` | Read-only diagnostic image built; separate write diagnostic built; neither image loads firmware, and the write image has no device run |
| Bootloader manifest/boot state | `planned` | D-01/D-02 still open |
| VFS, player, UI, database | `planned` | No implementation |
| HFS+ write engine | `in progress` | Journal, txn, bitmap, B-tree insert/delete/growth/below-minimum, fork staging, file/folder create/delete/rename, multi-extent/overflow data append/truncate, FastUnicodeCompare, HFS+ normalization and disposable-image `fsck_hfs` validation exist; metadata parity, the broader macOS name corpus and any device write do not |
| Journal replay on a real dirty volume | `unproven` | Replay is host-tested on synthetic journals; no macOS-created dirty volume or device run |
| Installer or desktop app | `planned` | No host application |
| Build system, CI, release artifacts | `in progress` | Host test Makefiles and the diagnostic bootloader script; no CI or firmware release |
| Helikon-OS device run | `unproven` | Three owner-reported diagnostic runs: two read-only-image boots and one write-diagnostic run with every stage PASS; no firmware or library-write run |
| Supported device list | `unproven` | 6G is the reference; 7G physical audit is deferred from the active queue by owner direction (2026-11-04) |
| Code of conduct | `rejected` | Owner deleted Contributor Covenant 2.1 on 2026-11-03; no replacement |
| Signing policy and key ownership | `planned` | D-20 is still open |

## Last known-good commit and branch

- **Base branch:** `main`
- **Base commit:** `7bfe58b40ea7f0444368c4eb61d97cf57a0ec74b`
  (`Updated README.MD`, 2026-11-03)
- **Documentation branch:** `docs/agent-handbook`, fast-forwarded to `main` on
  2026-11-03.
- **Last known-good code commit:** `4898a6f` (`Code: add separate inner-image
  write diagnostic`, 2026-11-03); the write diagnostic from this commit passed
  one owner-reported 6G run on 2026-11-03. Check
  `git log -- firmware/hfs bootloader tests tools`. No firmware run exists.
- **Last known-good documentation commit:** `59db37e` (`Docs: record first
  write-diagnostic hardware pass`, 2026-11-03). Any later docs housekeeping
  commit is on `main`; check `git log -1`.

Do not treat the donor V3.2 release as a Helikon-OS baseline. It is a separate
overlay-based product.

## Last recorded device run

**Three owner-reported diagnostic runs, 2026-11-03.**

The first image (`bd0ebc92…`, 66,920 bytes) booted on the owner's 6G and showed
the splash. `HFS+ volume: PASS` and `HFS+ catalog: PASS`. The read probe failed
with `read error: -11` (`HFS_ERR_NAME_INVALID`), and the report displayed `llu`
instead of the byte count and shifted later status lines.

The fixed image (`280ed7ae…`, 67,144 bytes) was then run. The report formatting
was correct: volume, catalog, write (`NOT IMPLEMENTED`), journal, splash and
partition size displayed as expected. `HFS+ read` reported `-8`
(`HFS_ERR_NOT_FOUND`) because `/rockbox.ipod` is not a file on this iPod; the
lookup itself worked. No write was requested or reported.

The separate write diagnostic (`9e3881…`, 109,320 bytes) was then installed by
DFU with `--dfusend` and no `--single`. The owner reports every screen stage
PASS. The extracted inner image (16,777,216 bytes, SHA-256
`156060075eef60c463f421ce459c2138a00e1c43de44da802b77ce89c79ca0e8`) passed
`fsck_hfs -fn` with exit 0, the `diag` folder was gone, and
`diskutil verifyVolume` exited 0 on the outer volume.

The read-only runs are a partial bring-up; the write run covers only the
inner-image sequence. No library write, power-cut, adapter or release claim.
See [`device-matrix.md`](device-matrix.md) §2.1 and §2.2.

The donor project has owner-reported hardware use for its V3.2 release. That is
donor evidence, not Helikon-OS evidence, and it does not validate native HFS+
writes, the transaction engine, the bootloader, or any goal in
[`goals.md`](goals.md).

## Current blockers

1. **No firmware implementation.** There is a host HFS+ read/write core, a
   read-only diagnostic image and a separate inner-image write diagnostic. The
   firmware-loading bootloader, kernel, VFS, player, UI, database, installer and
   host app do not exist. The write diagnostic's library, power-cut, adapter and
   release paths are not tested.
2. **Journal decisions are settled; the writer is not implemented.** D-03
   requires journaled writes and D-04 makes Helikon-OS own replay. The
   transaction engine, journal record format, replay and crash tests do not
   exist yet.
3. **Writer internals are not all frozen.** The interface and journal design are
   frozen; the bitmap, B-tree insert/delete/growth, catalog/extents mutation,
   fork staging, Apple `FastUnicodeCompare`, HFS+ decomposition and canonical
   ordering, folder create/delete and rename are implemented, and a disposable
   macOS HFS+ image passes `fsck_hfs -fn`. Cache limits (D-05), metadata
   parity, the broader macOS name corpus and any device write remain. Do not
   substitute byte order.
4. **No signed-update or signing policy.** D-01, D-16 and D-20 are open.
5. **No device-test programme.** No backup/recovery rehearsal, adapter matrix or
   full `fsck_hfs` oracle run is recorded for Helikon-OS.
6. **No release infrastructure.** No reproducible build, manifest, signing key,
   staging flow, rollback test or support window. Signing policy is D-20.

## Next task

The owner accepted D-03 (journaled writes) and D-04 (Helikon-OS owns replay).
The writer interface and failure model are frozen in
[`hfsplus-write-engine.md`](hfsplus-write-engine.md) §10. The diagnostics read
probe is intentionally skipped; `-8` is correct because `/rockbox.ipod` is not a
disk file on this iPod.

The next writer tasks are:

- **T-034** — the generic B-tree mutation engine (insert/delete, split/merge,
  free-node map, leaf links, fork growth, FastUnicodeCompare and leaf
  below-minimum insert) is implemented; index-node below-minimum insert at
  depth >2 remains.
- **D-06** — accepted: Apple `FastUnicodeCompare` plus HFS+ normalization from
  generated Unicode 2.1.9 tables. Disposable-image `fsck_hfs` passes. Next is
  the broader macOS-created name corpus and the `:`/`/` mapping.
- **T-007** — catalog B-tree mutation design on top of T-034.
- **T-008** — extents/overflow mutation design on top of T-034.
- **T-004** — the disposable-image `fsck_hfs` path is implemented; the wider
  committed fixture corpus and power-loss matrix remain.
- **T-038** — the broader macOS name corpus and the `:`/`/` mapping.
- **T-039** — macOS NUL-prefixed private names are now escaped as U+2400 and
  marked unsupported; path lookup continues past them instead of aborting.
- **D-05** — cache/dirty-set limits.
- **T-010** — the create/append/truncate/delete write-failure matrices pass
  (127 cuts, fsck clean). Rename, partial-sector/fsync failures, power-loss
  timing and the evidence format remain.
- **D-22** — the separate write diagnostic is built and host-tested. Next: a
  recorded 6G run with `helikon-test.img`, then extract the inner image and run
  `fsck_hfs -fn` before any support claim.

The bootloader path resumes afterwards; D-01/D-02/D-20 remain open.

For the writer path, see [`tasks.md`](tasks.md):

- **T-001** — record D-03 and D-04 (journal strategy and ownership).
- **T-002** — freeze the HFS+ writer interface and failure model.
- **T-003** — build the model-based B-tree and allocation test plan.
- **T-004** — define the backup, recovery and disposable-volume protocol.

Do not start phase 2 code or any device write test before T-001 and T-004 are
complete.

## Unproven claims and open questions

| Claim or question | Status | What would prove it |
|---|---|---|
| Helikon-OS boots on a 6G | `unproven` | Recorded boot log and device run |
| Native HFS+ writes survive power loss | `unproven` | Power-cut matrix plus `fsck_hfs` zero repairs |
| macOS accepts the written volume | `unproven` | `fsck_hfs -n` and `diskutil verifyVolume` after device writes |
| Battery life improves over Rockbox | `unproven` | Three-run measured baseline; D-12 |
| RAM stays within budget on 10k tracks | `unproven` | Watermark and soak on device |
| 20 Rockbox skins render | `unproven` | Recorded corpus and frame/memory report; D-10 |
| 7G is a viable port | `unproven` | Board audit and per-target matrix; D-13 |
| Windows/Linux desktop support | `unproven` | Service-mode transport test; D-09 |
| Apps/games are safely sandboxed | `unproven` | Adversarial tests; D-08 |
| Anti-corruption detects injected corruption | `unproven` | Injection test and quarantine report; D-11 |
| Signed updates reject bad images | `unproven` | Manifest/signature/rollback tests; D-01, D-16, D-20 |

Open questions are tracked in [`decisions.md`](decisions.md). Unknown hardware
facts must be marked `open question`; do not fill them from memory.

## Update this file at the end of every session

Change the phase, date, blockers, last known-good commit, device evidence and
next task when they change. If you add a claim, add its evidence or mark it
`unproven`. Do not describe planned work as implemented.
