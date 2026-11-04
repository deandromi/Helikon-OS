# Prompts

> Purpose: reusable prompts for recurring documentation/design tasks.
> Status: living document
> Owner: docs lead
> Last updated: 2026-11-04

These are starting points. They are **not** a second specification. The prompt
must link to the real docs, and the agent must follow `agents.md` and the
invariants. Do not paste the design paper into a prompt.

## Rules

- Keep a routine prompt under ~40 lines.
- A **full-procedure prompt** is allowed when a fresh agent needs the complete
  execution contract in one file. Mark it `full-procedure` in the index and
  keep the canonical docs as the source of truth. There is currently one:
  [`build-firmware.md`](build-firmware.md).
- Name the exact files to read and the output required.
- Include the hard stops that apply.
- Do not ask an agent to invent a device result, measurement or decision.
- Do not include secrets, device data or proprietary assets.
- If a routine prompt becomes a full procedure, either promote it to a
  full-procedure prompt or move the procedure to a doc and link it.

## Prompts

- [`build-firmware.md`](build-firmware.md) — **full-procedure**: build the
  Helikon-OS firmware F1–F5, starting with F1/T-054.
- [`review-hfsplus-writer.md`](review-hfsplus-writer.md) — review a writer design
  or change against the journal/`fsck_hfs` gates.
- [`run-device-matrix.md`](run-device-matrix.md) — run and record one device
  matrix row with evidence.
- [`audit-docs-for-contradictions.md`](audit-docs-for-contradictions.md) — find
  stale claims, broken links and source conflicts.

## Output contract

Every prompt result should state: files read, changes made, checks run, evidence,
open questions and anything not done. Do not claim a test passed if it was not
run.
