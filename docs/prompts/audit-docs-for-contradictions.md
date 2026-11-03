# Prompt — audit docs for contradictions

> Purpose: reusable documentation contradiction audit.
> Status: stub
> Owner: docs lead
> Last updated: 2026-11-03

Read `agents.md`, `docs/conventions.md`, `docs/state.md`, `docs/decisions.md`,
`docs/goals.md`, `docs/roadmap.md` and `docs/design-paper.md`.

Check for:

- claims that Helikon-OS is implemented, working, validated or supported;
- overlay, FAT, read-only native, "never writes native", plugin binary
  compatibility, "6G only" or full WPS claims;
- unverified battery/RAM/frame-time/device numbers;
- missing README goals or §25 decisions;
- roadmap phases without exit criteria; tasks without acceptance;
- stale state/handoff/task dates;
- broken relative links, inconsistent tables, duplicate canonical files;
- conflicts between README, paper and docs.

Output:

1. A table of finding, file/line, severity, and source conflict.
2. Proposed fix (do not silently change product direction).
3. New `C-xx` entries needed in `docs/decisions.md`.
4. Checks run.

Stop if a finding changes overlay/FAT/journaling/7G/licensing/app compatibility;
record it as an owner decision instead of choosing a side.
