# Conventions — style and process

> Purpose: keep documentation, status claims and changes consistent.
> Status: living document
> Owner: docs lead; every contributor
> Last updated: 2026-11-03
> Source: `agents.md`, design paper §18, §26–§28

## 1. Status labels

Use exactly these labels for project, feature, task and goal status:

| Label | Meaning | Required evidence |
|---|---|---|
| `implemented` | Exists and works as claimed. | Recorded test/device evidence; not for Helikon-OS today. |
| `in progress` | Work has started and is not complete. | A task/PR/commit or design draft. |
| `planned` | Designed or required, not started. | A task, roadmap phase or decision entry. |
| `unproven` | A claim or design exists without sufficient evidence. | State what would prove it. |
| `rejected` | Considered and deliberately excluded. | Reason and paper/decision link. |

Do not use "done", "working", "stable", "supported", "validated" or "production"
for Helikon-OS unless the evidence exists and the owner has accepted the claim.
Do not use `implemented` for documentation; use a document status below.

Test results may use `pass`, `fail`, `blocked` or `not run`. Those are results,
not feature-status labels.

Document status is separate:

| Document status | Meaning |
|---|---|
| `living` | Maintained now; update when facts change. |
| `stub` | Useful starting point with owner and pending decision/TODO. Not empty. |
| `template` | Copy and fill; not a project claim. |
| `source` | Read-only copy with provenance; update deliberately. |

## 2. Document headers

Every document starts with a short block:

```md
> Purpose: one sentence.
> Status: living / stub / template / source
> Owner: role or person
> Last updated: YYYY-MM-DD
> Source: links or paper sections
```

`agents.md` and root `README.md` are exceptions only in format; they still state
status and date. Do not put a fake owner name or a personal email in a document.

## 3. Link style

- Use relative Markdown links inside the repository. Examples (not live links):

```md
[state.md](state.md)              # from docs/
[docs/state.md](docs/state.md)    # from root
[§9](../docs/design-paper.md#9-native-hfs-read-write)
```
- Do not link to a local absolute path, a chat message, a private drive or a
  temporary file.
- Do not link to proprietary theme/art/font downloads. Reference the corpus and
  record licences separately.
- Every link must resolve in the repository. Check before committing.

## 4. Table style

- Use one header row and a separator row.
- Keep column counts consistent; do not leave a cell ambiguous.
- Use `not run`, `open question` or `unproven` instead of an empty result when
  the meaning matters.
- Keep cells short; move explanation below the table.
- Do not put multi-paragraph prose in a table cell.
- Align numeric columns right where practical; alignment is cosmetic, not a
  correctness claim.

## 5. File naming and canonical files

- Lowercase, hyphen-separated: `hfsplus-write-engine.md`, `device-matrix.md`.
- Dates in logs: `YYYY-MM-DD.md`.
- ADRs: `NNNN-short-title.md` (`0001-template.md` is the template).
- One canonical file per topic. Do not create:
  - `agent.md` alongside `agents.md`;
  - `decision-log.md` alongside `decisions.md`;
  - `release-process.md` alongside `release.md`;
  - a second testing/validation/goals file.
- If a topic grows too large, split by a clear boundary and link both ways; do
  not duplicate the same content.
- Templates are marked `template`; they are not project claims.

## 6. Writing style

- Plain language, short sentences, active voice.
- No marketing claims, hype, emoji, or "revolutionary".
- No unverified measurements, battery numbers, frame rates, RAM figures or
  device support claims. Label design targets as targets.
- Mark unknowns as `open question`; do not fill them from memory.
- Prefer specific codes (`JOURNAL_DIRTY`) over vague "error".
- Say "design" or "planned" when there is no implementation.
- Do not call the donor V3.2 release "Helikon-OS" or use its battery claim as
  Helikon-OS evidence.
- No secrets, device backups, `/.rockpod-rw`, disk images, serial numbers,
  Wi-Fi credentials or personal data.

## 7. Commit style

- One logical change per commit.
- Subject: imperative, ≤72 characters, e.g.
  `Docs: add agent handbook and Helikon-OS design documentation`.
- Body: what changed, why, checks run, and any open decision/contradiction.
- Do not commit generated images, disk images, backups, `.rockpod-rw`, secrets or
  proprietary assets.
- Do not rewrite history, force-push or commit directly to `main` for a
  documentation change. Use a branch and PR.
- Do not modify the root `README.md` or the donor repository in a docs commit.

## 8. Pull request style

- Title matches the commit subject or summarizes the change.
- Body states: what changed, why, source URLs, base commit, checks run,
  contradictions and open decisions, and what was **not** changed (firmware,
  donor, release assets, or license/conduct unless owner-approved).
- Link the exact paper/README source commits.
- Do not merge without owner review. Do not enable auto-merge.
- If a check cannot run, say so; do not mark it passed.

## 9. Evidence and claims

- No claim without evidence. Link the evidence row, command output or device run.
- Host tests are not device validation. A successful link is not a boot.
- A goal is `implemented` only when its metric in `validation.md` is met on a
  recorded device run.
- A battery/RAM/frame-time number needs method, device, firmware hash and
  runs/range. Otherwise it is `unproven`.
- A device support claim needs a `device-matrix.md` row with evidence.

## 10. Session maintenance

At the end of every session:

1. Update `state.md` (phase, date, blockers, evidence).
2. Update `tasks.md` (status, next task).
3. Update `handoff.md` (newest entry at the top).
4. Record a dated entry in `docs/log/YYYY-MM-DD.md` if useful.
5. Record new decisions/conflicts in `docs/decisions.md`.
6. Record device runs in `docs/device-matrix.md`.
7. Run `git diff --check` and inspect links/tables.
8. Commit and push the branch; do not merge.

## 11. Review checklist

- [ ] Root README unchanged.
- [ ] Donor repository unchanged.
- [ ] No firmware/build/CI/release changes; license/code-of-conduct changes
      require owner approval.
- [ ] Every new doc has purpose/status/owner/date/source.
- [ ] Status labels are used correctly.
- [ ] No duplicate canonical file.
- [ ] Links resolve; tables have consistent columns.
- [ ] No claim of implementation, device validation or battery gain.
- [ ] Open decisions are still marked `proposed`.
- [ ] State/tasks/handoff are current.
