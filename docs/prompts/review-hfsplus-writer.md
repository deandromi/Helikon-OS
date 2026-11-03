# Prompt — review the HFS+ writer

> Purpose: reusable writer review prompt.
> Status: stub
> Owner: docs lead
> Last updated: 2026-11-03

Read `agents.md`, `docs/invariants.md`, `docs/hfsplus-write-engine.md`,
`docs/architecture.md`, `docs/failures.md`, `docs/decisions.md` (D-03–D-06,
D-11, D-15) and paper §9, §21 and §24.

Review the proposed writer or change for:

- any metadata write outside a transaction;
- catalog key ordering / Unicode policy gaps;
- bitmap/extents/volume-header inconsistency;
- journal ordering, barrier, commit and replay gaps;
- crash-consistency and `fsck_hfs` evidence;
- unsupported objects being hidden;
- overlay/FAT/write-path regressions;
- a destructive "repair" or journal bypass.

Output:

1. Blocking defects, each linked to an invariant and paper section.
2. Non-blocking concerns.
3. Tests/evidence required (model, crash, differential, device).
4. Open decisions that must be resolved before implementation.
5. Explicit "not reviewed" areas.

Stop if the change would touch a real library without a verified backup, or if it
requires deleting/zeroing the journal or formatting the volume. Do not run a
device write test from this prompt.
