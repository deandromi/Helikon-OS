# State — Helikon-OS

> Purpose: what is true today. This file is not a plan and not a design paper.
> Status: living document
> Owner: project owner; maintained by the documentation agent
> Last updated: 2026-11-03
> Source: `README.md`, `docs/design-paper.md`, repository inspection

## Current phase

**Pre-phase 0 — design and documentation.** The roadmap in
[`roadmap.md`](roadmap.md) starts at phase 0 (rescue bootloader and read-only
firmware skeleton). Helikon-OS has not started phase 0.

Date of this snapshot: **2026-11-03**.

## What exists

| Item | Document status | Evidence |
|---|---|---|
| Root `README.md` with 25 owner-authored goals | living | Repo commit `7bfe58b40ea7f0444368c4eb61d97cf57a0ec74b`, 29 lines |
| Design paper, revision 2, 2026-11-03 | source | [`design-paper.md`](design-paper.md), source commit `3f220a73d0cceb083b3c057c6fe914d88468800a` |
| Agent contract and documentation set | living | This branch, `docs/agent-handbook` |
| Product intent, safety rules, roadmap, goals, open decisions | living | `docs/` |
| Donor file references and provenance | living | [`file-map.md`](file-map.md) |

The "document status" column uses the document statuses from
[`conventions.md`](conventions.md); it does not mean firmware exists.

## What does not exist

| Item | Status | Note |
|---|---|---|
| Helikon-OS firmware source tree | `planned` | No `firmware/` directory or source files |
| Helikon-OS bootloader source/image | `planned` | No `bootloader/` directory or image |
| HFS+ read/write engine | `planned` | Design requirements only |
| Journal/transaction engine | `planned` | Design requirements only |
| Installer or desktop app | `planned` | No host application |
| Build system, CI, release artifacts | `planned` | No build or release configuration |
| Helikon-OS device run | `unproven` | No device has run Helikon-OS |
| Supported device list | `unproven` | 6G is the reference; 7G needs a board audit |
| License, code of conduct, signing policy | `planned` | Owner decisions D-18, D-19, D-20 |

## Last known-good commit and branch

- **Base branch:** `main`
- **Base commit:** `7bfe58b40ea7f0444368c4eb61d97cf57a0ec74b`
  (`Updated README.MD`, 2026-11-03)
- **Documentation branch:** `docs/agent-handbook`
- **Last known-good code commit:** none; there is no Helikon-OS code.
- **Last known-good documentation commit:** the head of `docs/agent-handbook`
  after this documentation change. Check `git log -1` for the exact SHA.

Do not treat the donor V3.2 release as a Helikon-OS baseline. It is a separate
overlay-based product.

## Last recorded device run

**None for Helikon-OS.** No boot, playback, write, sleep, USB, update or recovery
test has been recorded against Helikon-OS hardware or firmware.

The donor project has owner-reported hardware use for its V3.2 release. That is
donor evidence, not Helikon-OS evidence, and it does not validate native HFS+
writes, the transaction engine, the bootloader, or any goal in
[`goals.md`](goals.md).

## Current blockers

1. **No implementation.** There is no source tree, bootloader, HFS+ writer,
   journal, VFS, player, UI, database, installer or host app.
2. **Journal decisions are unresolved.** D-03 (journal strategy) and D-04
   (journal ownership) must be recorded before phase 2 starts. The paper
   recommends journaled release writes and Helikon-OS-owned replay; neither is
   an owner-approved decision yet.
3. **Writer design is not frozen.** Catalog/B-tree mutation, bitmap, extents,
   transaction ordering, Unicode policy (D-06) and cache limits (D-05) need
   design review and test fixtures.
4. **No signed-update or signing policy.** D-01, D-16 and D-20 are open.
5. **No license or code of conduct.** D-18 and D-19 are open. Do not add
   `LICENSE` or `CODE_OF_CONDUCT.md`.
6. **No 7G board audit.** 7G support is unproven until a recorded audit covers
   panel, PMU, audio, storage, button map and `MOVE_AREA`.
7. **No device-test programme.** No backup/recovery rehearsal, adapter matrix,
   power-cut rig or `fsck_hfs` oracle run is recorded for Helikon-OS.
8. **No release infrastructure.** No reproducible build, manifest, signing key,
   staging flow, rollback test or support window.

## Next task

Start with the owner decisions that block phase 2, then write the writer design
and host-test plan. See [`tasks.md`](tasks.md):

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
