# Helikon-OS documentation

> Purpose: documentation index and reading order.
> Status: living document
> Owner: docs lead
> Last updated: 2026-11-06
> Source: `agents.md`, root `README.md`, design paper, Rockbox iPod Classic
> manual `0d5c2b0604-261105`

Helikon-OS is a **pre-phase 0** project to build a native-HFS+ bootloader and
firmware for the iPod Classic 6G (reference target) and 7G (next named port,
after a board audit). The repository contains the owner-authored product README,
this documentation, the host HFS+ read/write core, the loader-test bootloader,
the launcher and Music-hub firmware images, and the host tests. **Alpha 2.0.0
“Launchers and Interfaces”** is a development release, not a support release:
it ships engineering firmware images, a bootloader test image and a companion
shell preview. The separate V3.2 donor release is overlay-based and is not
Helikon-OS. The defining promise is a native, writable, journaled HFS+ volume
with no overlay and no FAT mount.

## Start here if you are an agent

1. Read `../agents.md` — the working contract and hard stops.
2. Read `state.md` — what is true today.
3. Read `invariants.md` — what must never break.
4. Read `tasks.md` — the current queue.
5. Do not rely on chat history. Update `state.md`, `tasks.md` and `handoff.md`
   at the end of a session.

If a source conflict changes product direction — overlay, FAT, journaling, 7G
scope, licensing, or app/plugin compatibility — stop and record it in
`decisions.md` instead of guessing.

## Reading order

1. [`../agents.md`](../agents.md)
2. [`state.md`](state.md)
3. [`invariants.md`](invariants.md)
4. [`architecture.md`](architecture.md) and [`file-map.md`](file-map.md)
5. [`firmware-architecture.md`](firmware-architecture.md) — firmware internals,
   memory, runtime, UI identity and developer ergonomics
6. [`tasks.md`](tasks.md)
7. [`verification.md`](verification.md)
8. [`decisions.md`](decisions.md)
9. [`goals.md`](goals.md), [`roadmap.md`](roadmap.md),
   [`competitive-roadmap.md`](competitive-roadmap.md) and
   [`audio-formats.md`](audio-formats.md)
10. [`design-paper.md`](design-paper.md)
11. [`failures.md`](failures.md), [`glossary.md`](glossary.md),
    [`device-matrix.md`](device-matrix.md)

Then read the specialist files you need:
[`firmware-architecture.md`](firmware-architecture.md),
[`hfsplus-write-engine.md`](hfsplus-write-engine.md),
[`ui.md`](ui.md), [`outer-volume-test.md`](outer-volume-test.md),
[`fixtures.md`](fixtures.md), [`testing.md`](testing.md),
[`validation.md`](validation.md),
[`release.md`](release.md), [`security.md`](security.md),
[`conventions.md`](conventions.md), [`handoff.md`](handoff.md),
[`faq.md`](faq.md).

## Status legend

Project and feature status uses exactly these labels:

| Label | Meaning |
|---|---|
| `implemented` | Exists and has recorded evidence for the claim. Do not use for Helikon-OS today. |
| `in progress` | Work has started and is not yet complete. |
| `planned` | Designed or required, but not started. |
| `unproven` | A claim or design exists without sufficient recorded evidence. |
| `rejected` | Considered and deliberately excluded. |

Document status in the table below is separate: `living` (maintained now),
`stub` (useful starting point, owner/TODO recorded), `template` (copy and fill),
`source` (read-only copy with provenance). Document status never implies firmware
status.

## Document index

| Document | Purpose | Document status |
|---|---|---|
| [`../agents.md`](../agents.md) | Entry contract: rules, sources, hard stops, handoff. | living |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | Contribution flow and device-report template. | living |
| [`../SECURITY.md`](../SECURITY.md) | Vulnerability reporting and key-handling policy. | living |
| [`../LICENSE`](../LICENSE) | GPL-2.0-or-later license text. | living |
| [`../NOTICE`](../NOTICE) | License notice, SPDX identifier and upstream attribution. | living |
| [`../.github/ISSUE_TEMPLATE/`](../.github/ISSUE_TEMPLATE/) | Bug, feature and device-test issue forms. | living |
| [`state.md`](state.md) | Current phase, what exists, blockers, evidence status. | living |
| [`invariants.md`](invariants.md) | Numbered, testable safety invariants. | living |
| [`decisions.md`](decisions.md) | Decision log, conflict record and 51 decision entries (D-29–D-51 proposed). | living |
| [`tasks.md`](tasks.md) | Now/Next/Later task queue plus the competitive/audio/manual horizon T-060–T-107. | living |
| [`verification.md`](verification.md) | Per-change proof checklist and evidence template. | living |
| [`file-map.md`](file-map.md) | “I need to change X” → owning file/module. | living |
| [`architecture.md`](architecture.md) | Boot chain, layers, boundaries, memory, security. | living |
| [`firmware-architecture.md`](firmware-architecture.md) | Firmware-internal architecture: entry, memory, runtime, modules, flows, aesthetics, ergonomics, testing, phases F1–F5. | living |
| [`adr/0002-signing-key-ownership.md`](adr/0002-signing-key-ownership.md) | Offline Ed25519 root and certified release key (accepted, D-20). | living |
| [`adr/0003-firmware-memory-map.md`](adr/0003-firmware-memory-map.md) | Accepted firmware memory map and image layout (D-27). | living |
| [`adr/0004-firmware-entry-abi.md`](adr/0004-firmware-entry-abi.md) | Accepted firmware entry ABI and boot-info page (D-28). | living |
| [`ui.md`](ui.md) | Default first-party UI typography, anti-aliasing, font rules and open theme questions. | living |
| [`outer-volume-test.md`](outer-volume-test.md) | Real outer-volume write test protocol and backup gate. | living |
| [`failures.md`](failures.md) | Error taxonomy, decision trees, destructive-action warnings. | living |
| [`device-matrix.md`](device-matrix.md) | 6G/7G/ports, adapters, receivers, test evidence. | living |
| [`handoff.md`](handoff.md) | Latest session handoff and template. | living |
| [`conventions.md`](conventions.md) | Doc/code/commit/PR style and status rules. | living |
| [`glossary.md`](glossary.md) | Plain-language terms. | living |
| [`testing.md`](testing.md) | Test layers, sanitizers and adding tests. | living |
| [`fixtures.md`](fixtures.md) | Host fixture plan, generation methods and hash/evidence policy. | living |
| [`validation.md`](validation.md) | Release-level evidence matrix and scorecard. | living |
| [`goals.md`](goals.md) | All 25 README goals with priority and acceptance. | living |
| [`roadmap.md`](roadmap.md) | Phases 0–12, horizon lanes, dependencies, exit criteria. | living |
| [`competitive-roadmap.md`](competitive-roadmap.md) | Rockbox parity matrix, manual section audit, differentiation bets, horizon lanes and explicit declines. | living |
| [`audio-formats.md`](audio-formats.md) | Rockbox audio-format inventory, 6G hardware ceiling, tiers, decoder architecture and test corpus. | living |
| [`companion-app.md`](companion-app.md) | Companion app reset record: what was removed, what shell remains and the boundary for rebuilding against the current firmware. | living |
| [`hfsplus-write-engine.md`](hfsplus-write-engine.md) | Native writer, journal and crash-consistency deep dive. | living |
| [`release.md`](release.md) | Channels, signing, staging, rollback, support window and release history. | living |
| [`releases/README.md`](releases/README.md) | Archive index for published release notes and quick starts. | living |
| [`releases/alpha-2.0.0/RELEASE_NOTES.md`](releases/alpha-2.0.0/RELEASE_NOTES.md) | Alpha 2.0.0 release notes: launcher, Music hub, loader, companion shell and limits. | living |
| [`security.md`](security.md) | Agent security rules and destructive-operation policy. | living |
| [`faq.md`](faq.md) | User-facing questions from Appendix G. | living |
| [`design-paper.md`](design-paper.md) | Full donor design paper copy with provenance; Appendix I is a marked Helikon-OS addendum. | source |
| [`../assets/logo/README.md`](../assets/logo/README.md) | Logo source notes and the owner-supplied mountain reference image. | living |
| [`../assets/branding/README.md`](../assets/branding/README.md) | Owner-supplied poster and 320×240 startup crop. | living |
| [`../bootloader/README.md`](../bootloader/README.md) | Read-only diagnostic bootloader; image built, unproven on hardware. | living |
| [`../bootloader/DFU.md`](../bootloader/DFU.md) | Backup and DFU install runbook for the diagnostic image. | living |
| [`../bootloader/WRITE_DIAG.md`](../bootloader/WRITE_DIAG.md) | Separate inner-image write diagnostic and PMU-cut runbook. | living |
| [`../bootloader/OUTER_DIAG.md`](../bootloader/OUTER_DIAG.md) | Separate outer-volume write diagnostic runbook and backup gate. | living |
| [`../bootloader/LOADER.md`](../bootloader/LOADER.md) | Firmware manifest, boot-state format, accepted entry ABI and read-only loader flow. | living |
| [`../bootloader/LOADER_TEST.md`](../bootloader/LOADER_TEST.md) | DFU-only loader test-image runbook, pass criteria and recovery. | living |
| [`../bootloader/hardware/README.md`](../bootloader/hardware/README.md) | S5L8702/ATA/LCD glue and donor integration patch. | living |
| [`../bootloader/artifacts/README.md`](../bootloader/artifacts/README.md) | Diagnostic image hash, provenance and evidence status. | living |
| [`log/README.md`](log/README.md) | What the dated work log is for. | living |
| [`log/TEMPLATE.md`](log/TEMPLATE.md) | Dated log entry template. | template |
| [`log/2026-11-03.md`](log/2026-11-03.md) | Documentation setup and first diagnostic/write sessions. | living |
| [`log/2026-11-04.md`](log/2026-11-04.md) | Metadata timestamps and the current write-diagnostic image. | living |
| [`log/2026-11-05.md`](log/2026-11-05.md) | Competitive-roadmap planning and the Rockbox manual review. | living |
| [`log/2026-11-06.md`](log/2026-11-06.md) | Launcher, HelikonDB, Music hub, companion reset and Alpha 2.0.0 release preparation. | living |
| [`adr/README.md`](adr/README.md) | When to create a long-form ADR. | living |
| [`adr/0001-template.md`](adr/0001-template.md) | ADR template. | template |
| [`prompts/README.md`](prompts/README.md) | Scope and rules for reusable prompts. | living |
| [`prompts/session-start.md`](prompts/session-start.md) | Full-procedure session bootstrap and Session Brief prompt. | living |
| [`prompts/build-firmware.md`](prompts/build-firmware.md) | Full-procedure prompt for the F1–F5 firmware build. | living |
| [`prompts/review-hfsplus-writer.md`](prompts/review-hfsplus-writer.md) | Review the native writer design. | stub |
| [`prompts/run-device-matrix.md`](prompts/run-device-matrix.md) | Run and record a device matrix. | stub |
| [`prompts/audit-docs-for-contradictions.md`](prompts/audit-docs-for-contradictions.md) | Audit docs for contradictions and stale claims. | stub |

Key entry points: [`design-paper.md`](design-paper.md),
[`goals.md`](goals.md), [`roadmap.md`](roadmap.md),
[`competitive-roadmap.md`](competitive-roadmap.md),
[`audio-formats.md`](audio-formats.md),
[`validation.md`](validation.md), [`decisions.md`](decisions.md).

## What this documentation is not

- It is not a production firmware-loading bootloader, an installer, CI or a
  hardware validation report. Alpha 2.0.0 is a development release with
  engineering firmware images and a companion shell; it is not a support claim.
  A read-only diagnostic image, a separate inner-image write diagnostic and a
  separate outer-volume write diagnostic also exist. The loader test image
  verifies a signed manifest and image before jumping.
- It is not a signing policy or a release approval. The license is
  GPL-2.0-or-later (`LICENSE`); no code of conduct is currently adopted (D-19
  rejected); signing-key ownership is accepted in
  [`decisions.md`](decisions.md) D-20/ADR-0002, but no release key has been
  generated and certificate verification is T-053.
- It is not a copy of Rockbox, Rockpod, donor themes, artwork, fonts or metadata.
  The donor is referenced for provenance and file locations only.
- It does not promise full Rockbox WPS compatibility, Rockbox plugin/game binary
  compatibility, FAT support, self-healing repair, or external USB DAC support
  on the 6G.
- It does not claim that Helikon-OS matches or beats Rockbox. The comparison in
  [`competitive-roadmap.md`](competitive-roadmap.md) is a plan; no row is
  evidence, and “better than Rockbox” requires the differential runs described
  there.
- It does not claim audio-format support today. [`audio-formats.md`](audio-formats.md)
  lists 37 audio families (36 in Appendix B.1 plus Opus) and the planned tiers; no
  decoder exists yet, and native 24/96 or DSD output is not claimed.

## Maintenance rule

Update `state.md`, `tasks.md` and `handoff.md` at the end of every session.
Record new contradictions in `decisions.md`. Do not create a second canonical
file for a topic already covered here.
