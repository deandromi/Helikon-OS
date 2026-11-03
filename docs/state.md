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
| DFU install runbook | living | [`bootloader/DFU.md`](../bootloader/DFU.md); backup, manual SHA-256, DFU install and recovery; image is unproven |
| Host test harness | `in progress` | 43 tests total (35 HFS+, 8 bootloader); sanitizer clean; first diagnostic boot is owner-reported |

## What does not exist

| Item | Status | Note |
|---|---|---|
| Helikon-OS firmware source tree | `in progress` | Host HFS+ read core and a diagnostic bootloader exist; no kernel, VFS or player |
| Helikon-OS firmware-loading bootloader | `planned` | The diagnostic image does not load firmware; D-01/D-02 still open |
| Helikon-OS bootloader image | `unproven` | Read-only diagnostic image built; no device run and no firmware loading |
| Bootloader manifest/boot state | `planned` | D-01/D-02 still open |
| VFS, player, UI, database | `planned` | No implementation |
| HFS+ write engine | `planned` | No writer |
| Journal/transaction engine | `planned` | Design requirements only |
| Installer or desktop app | `planned` | No host application |
| Build system, CI, release artifacts | `in progress` | Host test Makefiles and the diagnostic bootloader script; no CI or firmware release |
| Helikon-OS device run | `unproven` | One owner-reported diagnostic boot: splash shown, volume/catalog PASS, read FAIL `-11`; fixed image not rerun |
| Supported device list | `unproven` | 6G is the reference; 7G needs a board audit |
| Code of conduct | `rejected` | Owner deleted Contributor Covenant 2.1 on 2026-11-03; no replacement |
| Signing policy and key ownership | `planned` | D-20 is still open |

## Last known-good commit and branch

- **Base branch:** `main`
- **Base commit:** `7bfe58b40ea7f0444368c4eb61d97cf57a0ec74b`
  (`Updated README.MD`, 2026-11-03)
- **Documentation branch:** `docs/agent-handbook`, fast-forwarded to `main` on
  2026-11-03.
- **Last known-good code commit:** the diagnostic-bootloader build commit; check
  `git log -- firmware/hfs bootloader tests tools`. No firmware or device run
  exists.
- **Last known-good documentation commit:** `6df63ef63bacc93d253db40d1bb7f2efdd289a6c`
  on `main` (license/COC/issue-template follow-up, fast-forwarded). Any later
  docs housekeeping commit is on `main`; check `git log -1`.

Do not treat the donor V3.2 release as a Helikon-OS baseline. It is a separate
overlay-based product.

## Last recorded device run

**Two owner-reported diagnostic runs, 2026-11-03.**

The first image (`bd0ebc92…`, 66,920 bytes) booted on the owner's 6G and showed
the splash. `HFS+ volume: PASS` and `HFS+ catalog: PASS`. The read probe failed
with `read error: -11` (`HFS_ERR_NAME_INVALID`), and the report displayed `llu`
instead of the byte count and shifted later status lines.

The fixed image (`280ed7ae…`, 67,144 bytes) was then run. The report formatting
was correct: volume, catalog, write (`NOT IMPLEMENTED`), journal, splash and
partition size displayed as expected. `HFS+ read` reported `-8`
(`HFS_ERR_NOT_FOUND`) because `/rockbox.ipod` is not a file on this iPod; the
lookup itself worked. No write was requested or reported.

This is a partial bring-up run, not a support claim. The file-read path is not
hardware-exercised yet. See [`device-matrix.md`](device-matrix.md) §2.1.

The donor project has owner-reported hardware use for its V3.2 release. That is
donor evidence, not Helikon-OS evidence, and it does not validate native HFS+
writes, the transaction engine, the bootloader, or any goal in
[`goals.md`](goals.md).

## Current blockers

1. **No firmware implementation.** There is a host HFS+ read core and an
   unproven read-only diagnostic bootloader image; the firmware-loading
   bootloader, kernel, writer, journal, VFS, player, UI, database, installer and
   host app do not exist. The first diagnostic boot is owner-reported and the
   fixed image has not been rerun.
2. **Journal decisions are unresolved.** D-03 (journal strategy) and D-04
   (journal ownership) must be recorded before phase 2 starts. The paper
   recommends journaled release writes and Helikon-OS-owned replay; neither is
   an owner-approved decision yet.
3. **Writer design is not frozen.** Catalog/B-tree mutation, bitmap, extents,
   transaction ordering, Unicode policy (D-06) and cache limits (D-05) need
   design review and test fixtures.
4. **No signed-update or signing policy.** D-01, D-16 and D-20 are open.
5. **No 7G board audit.** 7G support is unproven until a recorded audit covers
   panel, PMU, audio, storage, button map and `MOVE_AREA`.
6. **No device-test programme.** No backup/recovery rehearsal, adapter matrix,
   power-cut rig or `fsck_hfs` oracle run is recorded for Helikon-OS.
7. **No release infrastructure.** No reproducible build, manifest, signing key,
   staging flow, rollback test or support window. Signing policy is D-20.

## Next task

The diagnostic has done its hardware job: boot, splash, report, HFS+ volume and
catalog reads are owner-reported. The `-8` read result is correct because
`/rockbox.ipod` is not a disk file on this iPod, not a bootloader failure.

Two paths remain:

1. **Close the read evidence (small).** Point the diagnostic probe at a real root
   file such as `/.rockpod-rw`, reflash once, confirm `HFS+ read: PASS`, and
   record it in [`device-matrix.md`](device-matrix.md).
2. **Start the writer (recommended).** Record D-03 and D-04, then freeze the
   writer interface and failure model (T-001/T-002). Native HFS+ writes are the
   defining promise; the read path is host-tested and the ATA read path is
   already exercised by the volume/catalog reads.

If the bootloader path is preferred instead, D-01/D-02/D-20 must be decided
before adding manifest verification, boot state or firmware loading.

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
