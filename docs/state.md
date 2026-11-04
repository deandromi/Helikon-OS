# State — Helikon-OS

> Purpose: what is true today. This file is not a plan and not a design paper.
> Status: living document
> Owner: project owner; maintained by the documentation agent
> Last updated: 2026-11-04
> Source: `README.md`, `docs/design-paper.md`, repository inspection

## Current phase

**Pre-phase 0 — design, host read/write core, diagnostics and host firmware
loader.** The roadmap in [`roadmap.md`](roadmap.md) starts at phase 0 (rescue
bootloader and read-only firmware skeleton). The read-only diagnostic image exists and has
owner-reported 6G runs; the inner-image write diagnostic has one owner-reported
run, and the outer-volume diagnostic has one successful owner-reported 6G run.
None loads firmware.

Date of this snapshot: **2026-11-04**.

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
| 320×240 startup crop | living (asset) | `assets/startup/helikon-os-320x240.png`; kept for provenance; superseded by the owner bootloader design in `assets/bootloader/` |
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
| Journal append/replay engine | `implemented` | `firmware/hfs/hfs_journal.c`; header/block-list checksum validation, wrap, append, idempotent replay; Apple `max_blocks` and `BLHDR_FIRST_HEADER` fields now written, and a committed-unreplayed journal is replayed by macOS `fsck_hfs` in the host test; 9 host tests |
| Bounded transaction engine | `implemented` | `firmware/hfs/hfs_txn.c`; staged-block commit through the journal; 7 host tests |
| Allocation bitmap | `implemented` | `firmware/hfs/hfs_bitmap.c`; first-fit alloc/free, free-block counts, header/alternate staging, `NO_SPACE`; 6 host tests; allocation-file overflow extents unsupported |
| B-tree node mutation helpers | `implemented` | `firmware/hfs/hfs_btree_write.c`; node offset table insert/delete, free-space checks and node split; 7 host tests |
| B-tree tree insertion | `implemented` | `firmware/hfs/hfs_btree_tree.c`; root-to-leaf traversal, free-node map, leaf/index split, recursive parent split, root creation, leaf below-minimum insert, parent-key propagation and index-node below-minimum insert at depth >2; the model test now inserts and deletes 60 records at depth 3 |
| B-tree delete | `implemented` | `hfs_btree_delete` removes records including first records, propagates changed parent keys, unlinks/frees empty leaves, merges/redistributes with both siblings, collapses a one-child root to its nonempty leaf and collapses an empty parent into its grandparent. The 60-record depth-3 model now passes insert, update and delete; the key-loss alias in recursive parent-key propagation is fixed |
| Volume header staging | `implemented` | `hfs_volume_write.c`; primary/alternate header file/folder counts, next CNID, modify date and write count; stages a journal-sector-sized block so 512-byte and 4 KiB journal headers work; 4 KiB unit test |
| Private NUL names | `implemented` | Directory iteration escapes NUL as U+2400 and marks the entry `name_escaped`/`unsupported`; path lookup skips the escaped entry and continues instead of aborting the directory; 1 catalog test |
| Extents-overflow record | `implemented` | `hfs_extents_write.c`; data-fork records keyed by fork/file/startBlock, up to eight extents each; insert/get/delete; multiple records supported |
| Inner-image mapping | `implemented` | `bootloader/inner_image.c` maps a root-level outer file's data-fork extents into a block device, rejects out-of-range writes, and guards against overlap with outer metadata, journal, volume headers and forks; 4 host tests |
| Separate write diagnostic | `implemented` | `bootloader-ipod6g-write-diag.ipod` (110,920 bytes, `81350f…`, 18,112 bytes headroom) keeps the outer HFS+ volume read-only and runs the writer inside `helikon-test.img`; host outer/inner test passes twice, and one owner-reported 6G run reported every stage PASS; inner `fsck_hfs` and outer `diskutil verifyVolume` both exited 0. This artifact predates the journal `max_blocks` fix; rebuild before another inner power-cut run |
| Outer-volume write diagnostic | `pass` | Current tested image `bootloader-ipod6g-outer-diag.ipod` (110,408 bytes, `32eeb541…`) writes only `/helikon-outer-test` after an opt-in file and a clean-journal check. It adds in-place B-tree record updates for append/truncate and stage progress on screen. Owner report 2026-11-04: every stage PASS; `/helikon-outer-test` absent; post-run `fsck_hfs` and `diskutil verifyVolume` pass. Host test covers the full sequence, dirty-journal refusal, macOS replay, 512/4 KiB journal sectors, non-destructive cleanup and a second run. The artifact predates the T-034 insert/leaf fixes; rebuild before the next device run. One 6G run only |
| Catalog metadata | `implemented` | Timestamps: create/access/content/attribute on create; content/attribute on data changes; attribute-only on rename; parent dates and volume modify date on mutation; backupDate 0. BSD owner/group 0, flags 0, file/folder mode, Finder type/creator/flags/finderInfo zero, textEncoding zero, special 0; new-record Finder policy is zero without extension inference, existing bytes are preserved on append/truncate/rename. Host tests, `GetFileInfo -t/-c` macOS checks and the outer-image stage test pass. Wider Unicode corpus remains T-038 |
| Outer-volume device write | `pass` | T-048 closed 2026-11-04: one 6G run, every stage PASS, test folder removed, `fsck_hfs` zero repairs, `diskutil verifyVolume` pass; `.rockpod-rw` unchanged and `.rockbox` only `.DS_Store`. The owner confirmed the full backup/diff; the agent did not rerun it. Power-cut matrix and timing remain. Not a support claim |
| Controlled PMU cut | `pass` | Current image `81350f…`: targets 7/56 were pre-commit clean controls; target 96 cut after commit, recovered on the next boot, all stages PASS, and the extracted inner image passed `fsck_hfs` with `The volume HelikonInner appears to be OK`. Further replay targets are optional coverage |
| Write diagnostic 6G run | `pass` | One owner-reported run, 2026-11-03: all stages PASS, extracted inner image SHA-256 `156060…`, `fsck_hfs -fn` exit 0, no `diag` folder, outer `diskutil verifyVolume` exit 0; not a library-write or support claim |
| Journal crash injection | `implemented` | `tests/image/run-crash-macos.sh` injects write, flush and partial-sector failures at every cut point of create (25 write + 4 flush + 25 partial), append (34 + 4 + 34), truncate (25 + 4 + 25), delete (25 + 4 + 25) and rename (21 + 4 + 21) transactions, reopens the image, replays the journal and runs `fsck_hfs -fn`; all 280 cuts recover to a consistent state and an fsck-clean volume; power-loss timing and the remaining operations remain |
| Disposable-image writer validation | `implemented` | `tests/image/run-macos.sh` creates a 16 MiB journaled HFS+ image, creates a folder/file, appends 4096 bytes, renames, truncates and deletes through the outer volume, mounts at each stage and checks content, modes, timestamps and paths; `fsck_hfs -fn` is clean after every stage. Host-only; no device or library write |
| B-tree fork growth | `implemented` | `hfs_btree_grow()` allocates a volume block, extends the catalog/extents fork in the primary/alternate headers, adds a free node to the header map and updates `totalNodes`; requires `node_size == block_size` and an inline fork extent slot; 1 host test |
| Catalog create/delete/rename | `implemented` | `hfs_catalog_write.c`; file and folder records, thread records, CNID allocation, file/folder counts, parent valence, rename/move with a folder-cycle check; names are normalized to HFS+ canonical form; 5 host tests |
| Unicode catalog comparison | `implemented` | `hfs_catalog_compare.c` uses Apple `FastUnicodeCompare`; `hfs_unicode_normalize_name()` decomposes, applies the HFS+ exception ranges, decomposes Hangul and canonically orders combining marks; `hfs_unicode_data.c` is generated from Unicode 2.1.9. `tests/image/run-macos.sh` now creates a scripted macOS corpus with NFC/NFD, CJK, emoji, Hangul, combining marks, U+2126, U+F900, `:`/`/`, 255-unit and rejected 256-unit names, and macOS sees the same NFC tree with `fsck_hfs` clean. Broader fuzzing and ordering checks remain |
| Data-fork append/truncate (eight inline + multiple overflow records) | `implemented` | `hfs_catalog_append_file` appends to empty or non-empty files, stages data before metadata, allocates individual free blocks, merges inline extents and writes multiple extents-overflow records; truncate frees all records; the transaction block budget caps very large fragmented writes |
| Fork block staging | `implemented` | `firmware/hfs/hfs_fork_io.c`; full/partial block write staging into `hfs_txn`; inline extents only; 4 host tests |
| DFU install runbook | living | [`bootloader/DFU.md`](../bootloader/DFU.md); backup, manual SHA-256, DFU install and recovery; image is unproven |
| Default UI typography | `in progress` | D-23/D-24 and `docs/ui.md` record the 14/17/12/11 px scale and grayscale anti-aliasing. Licensed IBM Plex Sans and Manrope subsets, OFL texts, hashes and two 320×240 host mockups are in `assets/fonts/` and `assets/ui/`; `tools/render-ui-mockup.py` reproduces them with clipping checks. Firmware renderer and 6G panel run remain T-042 |
| Bootloader splash redesign | `implemented` | Final owner green/red PNGs are in `assets/bootloader/` with `VERSION ALPHA` and a green/red `HFS+ DETAILS` mask. The refreshed loader image uses a 16-colour 4bpp RGB565 base (38,400 bytes) plus a 324-byte label mask; the temporary bottom stage line is removed. The final-splash `6bc6927c…` image is device-tested: 6.5 s startup and `fsck_hfs -fn` clean |
| Host test harness | `in progress` | 135 portable tests (104 HFS+, 31 bootloader), sanitizer clean, plus the macOS disposable-image, Unicode, crash-injection, inner-image, outer-volume and loader tests; fixture plan in `fixtures.md`; first diagnostic boot is owner-reported |
| Backup/recovery protocol | `implemented` | Full-volume backup, read-back, disposable restore rehearsal and Apple-restore/DFU path owner-confirmed 2026-11-04; protocol and stop conditions in `outer-volume-test.md` |
| Fixture plan | `implemented` | `docs/fixtures.md` lists synthetic reader/journal/B-tree/catalog/extents/bitmap/Unicode fixtures, disposable-image writer fixtures and write/flush/partial crash fixtures; no disk images committed |

## What does not exist

| Item | Status | Note |
|---|---|---|
| Helikon-OS firmware source tree | `in progress` | Host HFS+ read core, diagnostic bootloader, host write diagnostics and host firmware loader exist; no kernel, VFS or player |
| Host firmware loader | `implemented` | `bootloader/manifest.c`, `bootstate.c`, `sha256.c`, `ed25519.c`, `keys.c`, `firmware_loader.c` and `hfs_loader.c`; formats frozen in `bootloader/LOADER.md`. Signature is verified before any image byte; then SHA-256 and bootstate. 13 manifest/loader tests, 2 HFS adapter tests, 4 Ed25519/key-table tests and a macOS HFS+ fixture with a real generated key pass. No jump and no writes |
| Helikon-OS firmware-loading bootloader | `in progress` | The final-splash loader image `6bc6927c…` (106,536 bytes, 22,496 bytes headroom) is a visual-only refresh of the tested `3979de6f…` loader path. The earlier image was owner-reported working at 6.5 s on one 6G/iFlash SD; the final-splash device check passed with a clean `fsck_hfs -fn`. It verifies Ed25519 before any image byte, reads directly to the signed DRAM address, executes only a signed development payload and disables production handoff. Production handoff is T-051 and needs a real signed firmware image. |
| Helikon-OS bootloader image | `unproven` | Read-only diagnostic image built; separate inner-image and outer-volume write diagnostics built; none loads firmware. The inner image has one owner-reported run; the outer image has one successful 6G run |
| Loader test image | `implemented` | `build/loader-bootloader/out/bootloader-ipod6g-loader-test.ipod` (106,536 bytes, 22,496 bytes headroom) now carries the final Alpha splash; the loader path is the owner-reported `3979de6f…` design at 6.5 s. The final-splash `6bc6927c…` image is device-tested (6.5 s, `fsck_hfs -fn` clean). Production handoff is disabled |
| Bootloader manifest/boot state | `implemented` | Formats frozen in `bootloader/LOADER.md`; host parser/verifier/selector, real Ed25519 and HFS loader tested; loader test image owner-reported working. D-02 is single-image fail-closed by owner decision: no A/B fallback; recovery is DFU/Apple restore |
| VFS, player, UI, database | `planned` | No implementation |
| HFS+ write engine | `in progress` | Journal, txn, bitmap, B-tree insert/delete/growth/deep delete/in-place update, fork staging, file/folder create/delete/rename, multi-extent/overflow data append/truncate, FastUnicodeCompare, HFS+ normalization, metadata policy and disposable-image `fsck_hfs` validation exist. One outer-volume 6G run passed; D-05 cache limits, broader fuzzing, timing and the device power-cut/adapter matrix remain |
| Journal replay on a real dirty volume | `unproven` | Replay is host-tested on synthetic journals; no macOS-created dirty volume or device run |
| Installer or desktop app | `planned` | No host application |
| Build system, CI, release artifacts | `in progress` | Host test Makefiles and bootloader build scripts; bootloader-only Alpha release `alpha-1.0` published; no CI, reproducible firmware build or firmware release |
| Helikon-OS device run | `unproven` | Three earlier owner-reported diagnostic runs, the 2026-11-04 outer-volume run with every stage PASS, and the loader-test pass on `3979de6f…`. The final-splash `6bc6927c…` run passed (6.5 s, `fsck_hfs -fn` clean). No firmware run or support claim; one device/iFlash SD only |
| Supported device list | `unproven` | 6G is the reference; 7G physical audit is deferred from the active queue by owner direction (2026-11-04) |
| Code of conduct | `rejected` | Owner deleted Contributor Covenant 2.1 on 2026-11-03; no replacement |
| Signing policy and key ownership | `accepted` | D-20/ADR-0002 option A: offline Ed25519 root on two hardware tokens, certified release key, rotation/revocation/recovery; certificate verifier and a real signed release image are T-053/T-051 |

## Last known-good commit and branch

- **Base branch:** `main`
- **Base commit:** `7bfe58b40ea7f0444368c4eb61d97cf57a0ec74b`
  (`Updated README.MD`, 2026-11-03)
- **Documentation branch:** `docs/agent-handbook`, fast-forwarded to `main` on
  2026-11-03.
- **Last known-good code commits:** `4898a6f` (`Code: add separate inner-image
  write diagnostic`, 2026-11-03) passed one inner-image 6G run; `8b528d1`
  (`Code: fix outer diagnostic truncate hang`, 2026-11-04) produced image
  `32eeb541…`, which passed one outer-volume 6G run on 2026-11-04. Check
  `git log -- firmware/hfs bootloader tests tools`. No firmware run exists.
- **Last known-good documentation commit:** `59db37e` (`Docs: record first
  write-diagnostic hardware pass`, 2026-11-03). Any later docs housekeeping
  commit is on `main`; check `git log -1`.

Do not treat the donor V3.2 release as a Helikon-OS baseline. It is a separate
overlay-based product.

## Last recorded device run

**Three earlier owner-reported diagnostic runs, 2026-11-03, plus the
outer-volume run on 2026-11-04.**

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
inner-image sequence. On 2026-11-04 the separate outer-volume diagnostic
(`32eeb541…`) ran on the same 6G: every screen stage PASS, `/helikon-outer-test`
absent afterwards, `fsck_hfs -fn` zero repairs and `diskutil verifyVolume`
pass. `.rockpod-rw` was unchanged; `.rockbox` differed only in `.DS_Store`. This
is one outer-volume run, not a power-cut, adapter or support claim. See
[`device-matrix.md`](device-matrix.md) §2.1, §2.2 and §2.5.

The donor project has owner-reported hardware use for its V3.2 release. That is
donor evidence, not Helikon-OS evidence, and it does not validate native HFS+
writes, the transaction engine, the bootloader, or any goal in
[`goals.md`](goals.md).

## Current blockers

1. **No firmware implementation.** There is a host HFS+ read/write core, a
   read-only diagnostic image, an inner-image write diagnostic, an outer-volume
   write diagnostic and a host firmware loader. The firmware-loading bootloader
   image, kernel, VFS, player, UI, database, installer and host app do not exist.
   The outer-volume writer has one successful 6G run; library-wide, power-cut and
   adapter paths are not tested.
2. **Journaled writer exists; device power-cut evidence is incomplete.** D-03
   requires journaled writes and D-04 makes Helikon-OS own replay. The
   transaction engine, Apple journal format, replay and 280 host crash cuts are
   implemented. The real device power-cut matrix and replay of a real dirty
   volume remain.
3. **Writer internals are mostly frozen.** The interface/journal design,
   bitmap, B-tree insert/delete/growth/deep delete, catalog/extents mutation,
   fork staging, FastUnicodeCompare, HFS+ decomposition, metadata policy and
   rename are implemented; the disposable image and one outer-volume run pass
   `fsck_hfs`. Cache limits (D-05), broader fuzzing and the device power-cut/
   adapter matrix remain.
4. **No signed-update release path yet.** D-01/D-02 and D-20 are accepted, but
   D-16 (update transport), the release-key certificate verifier (T-053) and a
   real signed firmware image (T-051) remain.
5. **Device-test programme is owner-closed for the current phase.** Backup,
   read-back, disposable restore rehearsal, the Apple-restore/DFU path and the
   6G/iFlash SD loader run are recorded. Other adapters, the full device
   power-cut matrix and library-wide differential remain future coverage.
6. **No firmware release infrastructure.** A bootloader-only Alpha release
   (`alpha-1.0`) exists, but there is no reproducible firmware build, release
   manifest, signing-key ceremony record, staging flow, rollback test or support
   window. The D-20 policy is accepted; no release key has been generated and no
   release signing is claimed.

## Next task

The owner accepted D-03 (journaled writes) and D-04 (Helikon-OS owns replay).
The writer interface and failure model are frozen in
[`hfsplus-write-engine.md`](hfsplus-write-engine.md) §10. The diagnostics read
probe is intentionally skipped; `-8` is correct because `/rockbox.ipod` is not a
disk file on this iPod.

The active P0 line is **T-051** (production firmware handoff), which needs a real
signed firmware image for end-to-end device validation. T-006/D-20 and T-049 are
implemented; T-050 is closed by owner decision as an explicit no-A/B,
single-image fail-closed model. D-26 records that the bootloader has no
OF/recovery menu; native Menu+Play and DFU cover recovery. The P1 line is:

- **T-041** — UI font licences and the 320×240 typography mockup.
- **T-012** — full 6G model/adapter/battery inventory; backup/restore is done.
- **T-029** — finish the read-only diagnostic evidence.
- **T-045** — final owner splash is integrated and the stage line is removed;
  the refreshed image needs one clean device check.
- **T-053** — release-key certificate, rotation and revocation verifier.

T-042/T-013/T-052 are P2; T-052 tracks reducing startup from 6.5 s toward 2–3 s.
The fixture plan is in [`fixtures.md`](fixtures.md); the writer crash matrix and
evidence format are in [`validation.md`](validation.md) §2.

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
| Signed updates reject bad images | `unproven` | Host Ed25519/manifest tests pass; release-key certificate/revocation, a real signed image and device update testing remain; D-16, T-051, T-053 |

Open questions are tracked in [`decisions.md`](decisions.md). Unknown hardware
facts must be marked `open question`; do not fill them from memory.

## Update this file at the end of every session

Change the phase, date, blockers, last known-good commit, device evidence and
next task when they change. If you add a claim, add its evidence or mark it
`unproven`. Do not describe planned work as implemented.
