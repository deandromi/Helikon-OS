# Helikon-OS documentation

> Purpose: documentation index and reading order.
> Status: living document
> Owner: docs lead
> Last updated: 2026-11-03
> Source: `agents.md`, root `README.md`, design paper

Helikon-OS is a **design-stage** project to build a native-HFS+ bootloader and
firmware for the iPod Classic 6G (reference target) and 7G (next named port,
after a board audit). It is not a working firmware release: this repository currently
contains the owner-authored product README plus the documentation in this
directory. The separate V3.2 donor release is overlay-based and is not
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
5. [`tasks.md`](tasks.md)
6. [`verification.md`](verification.md)
7. [`decisions.md`](decisions.md)
8. [`goals.md`](goals.md) and [`roadmap.md`](roadmap.md)
9. [`design-paper.md`](design-paper.md)
10. [`failures.md`](failures.md), [`glossary.md`](glossary.md),
    [`device-matrix.md`](device-matrix.md)

Then read the specialist files you need:
[`hfsplus-write-engine.md`](hfsplus-write-engine.md),
[`ui.md`](ui.md), [`outer-volume-test.md`](outer-volume-test.md),
[`testing.md`](testing.md), [`validation.md`](validation.md),
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
| [`decisions.md`](decisions.md) | Decision log, conflict record and 23 decision entries. | living |
| [`tasks.md`](tasks.md) | Now/Next/Later task queue with acceptance criteria. | living |
| [`verification.md`](verification.md) | Per-change proof checklist and evidence template. | living |
| [`file-map.md`](file-map.md) | “I need to change X” → owning file/module. | living |
| [`architecture.md`](architecture.md) | Boot chain, layers, boundaries, memory, security. | living |
| [`ui.md`](ui.md) | Default first-party UI typography, anti-aliasing, font rules and open theme questions. | living |
| [`outer-volume-test.md`](outer-volume-test.md) | Real outer-volume write test protocol and backup gate. | living |
| [`failures.md`](failures.md) | Error taxonomy, decision trees, destructive-action warnings. | living |
| [`device-matrix.md`](device-matrix.md) | 6G/7G/ports, adapters, receivers, test evidence. | living |
| [`handoff.md`](handoff.md) | Latest session handoff and template. | living |
| [`conventions.md`](conventions.md) | Doc/code/commit/PR style and status rules. | living |
| [`glossary.md`](glossary.md) | Plain-language terms. | living |
| [`testing.md`](testing.md) | Test layers, fixtures, sanitizers, adding tests. | living |
| [`validation.md`](validation.md) | Release-level evidence matrix and scorecard. | living |
| [`goals.md`](goals.md) | All 25 README goals with priority and acceptance. | living |
| [`roadmap.md`](roadmap.md) | Phases 0–12, dependencies, exit criteria. | living |
| [`hfsplus-write-engine.md`](hfsplus-write-engine.md) | Native writer, journal and crash-consistency deep dive. | living |
| [`release.md`](release.md) | Channels, signing, staging, rollback, support window. | stub |
| [`security.md`](security.md) | Agent security rules and destructive-operation policy. | living |
| [`faq.md`](faq.md) | User-facing questions from Appendix G. | living |
| [`design-paper.md`](design-paper.md) | Full read-only design paper copy with provenance. | source |
| [`../assets/logo/README.md`](../assets/logo/README.md) | Logo source notes and the owner-supplied mountain reference image. | living |
| [`../assets/branding/README.md`](../assets/branding/README.md) | Owner-supplied poster and 320×240 startup crop. | living |
| [`../bootloader/README.md`](../bootloader/README.md) | Read-only diagnostic bootloader; image built, unproven on hardware. | living |
| [`../bootloader/DFU.md`](../bootloader/DFU.md) | Backup and DFU install runbook for the diagnostic image. | living |
| [`../bootloader/hardware/README.md`](../bootloader/hardware/README.md) | S5L8702/ATA/LCD glue and donor integration patch. | living |
| [`../bootloader/artifacts/README.md`](../bootloader/artifacts/README.md) | Diagnostic image hash, provenance and evidence status. | living |
| [`log/README.md`](log/README.md) | What the dated work log is for. | living |
| [`log/TEMPLATE.md`](log/TEMPLATE.md) | Dated log entry template. | template |
| [`log/2026-11-03.md`](log/2026-11-03.md) | Documentation setup and first diagnostic/write sessions. | living |
| [`log/2026-11-04.md`](log/2026-11-04.md) | Metadata timestamps and the current write-diagnostic image. | living |
| [`adr/README.md`](adr/README.md) | When to create a long-form ADR. | living |
| [`adr/0001-template.md`](adr/0001-template.md) | ADR template. | template |
| [`prompts/README.md`](prompts/README.md) | Scope and rules for reusable prompts. | living |
| [`prompts/review-hfsplus-writer.md`](prompts/review-hfsplus-writer.md) | Review the native writer design. | stub |
| [`prompts/run-device-matrix.md`](prompts/run-device-matrix.md) | Run and record a device matrix. | stub |
| [`prompts/audit-docs-for-contradictions.md`](prompts/audit-docs-for-contradictions.md) | Audit docs for contradictions and stale claims. | stub |

Key entry points: [`design-paper.md`](design-paper.md),
[`goals.md`](goals.md), [`roadmap.md`](roadmap.md),
[`validation.md`](validation.md), [`decisions.md`](decisions.md).

## What this documentation is not

- It is not firmware code, a firmware-loading bootloader, an installer, CI, a
  release, or a hardware validation report. A read-only diagnostic image and a
  separate inner-image write diagnostic exist; both have recorded 6G runs, but
  neither is a firmware loader or a support claim.
- It is not a signing policy or a release approval. The license is
  GPL-2.0-or-later (`LICENSE`); no code of conduct is currently adopted (D-19
  rejected); signing-key ownership is still open in
  [`decisions.md`](decisions.md) D-20.
- It is not a copy of Rockbox, Rockpod, donor themes, artwork, fonts or metadata.
  The donor is referenced for provenance and file locations only.
- It does not promise full Rockbox WPS compatibility, Rockbox plugin/game binary
  compatibility, FAT support, self-healing repair, or external USB DAC support
  on the 6G.

## Maintenance rule

Update `state.md`, `tasks.md` and `handoff.md` at the end of every session.
Record new contradictions in `decisions.md`. Do not create a second canonical
file for a topic already covered here.
