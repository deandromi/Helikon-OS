# Prompt stubs

> Purpose: reusable prompts for recurring documentation/design tasks.
> Status: living document
> Owner: docs lead
> Last updated: 2026-11-03

These are short starting points. They are **not** a second specification. The
prompt must link to the real docs, and the agent must follow `agents.md` and the
invariants. Do not paste the design paper into a prompt.

## Rules

- Keep a prompt under ~40 lines.
- Name the exact files to read and the output required.
- Include the hard stops that apply.
- Do not ask an agent to invent a device result, measurement or decision.
- Do not include secrets, device data or proprietary assets.
- If a prompt becomes a full procedure, move the procedure to a doc and link it.

## Stubs

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
