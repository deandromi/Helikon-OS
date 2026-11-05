# Prompt — start a Helikon-OS session

> Purpose: bootstrap any new Helikon-OS session so an agent knows the current
> phase, the active task, the evidence and the next safe step before editing.
> Status: living — full-procedure prompt
> Owner: docs lead; every agent
> Last updated: 2026-11-04
> Source: `agents.md`, `docs/state.md`, `docs/tasks.md`, `docs/handoff.md`,
> `docs/conventions.md`

**Use this prompt at the start of every session.** Paste it, optionally with the
task you want to work on. The agent must read the repository, not rely on chat
history, and must produce a short **Session Brief** before changing a file. If
the owner's first message already names a task or says "go", the agent may
proceed after the brief; otherwise it stops after the brief and waits.

---

## 0. The first rule: the repository is the memory

Assume you have no memory of previous sessions. A chat summary can be stale,
incomplete or wrong. The repository is the source of truth, in this order:

1. owner decisions and the root `README.md`;
2. `docs/design-paper.md`;
3. `docs/state.md`;
4. `docs/decisions.md`;
5. `agents.md`;
6. `docs/tasks.md`, `docs/handoff.md` and the latest log.

Do not start a task because a summary says so. Start it because `docs/state.md`,
`docs/tasks.md` and the owner's current instruction agree.

Uncommitted changes are probably the owner's or another agent's work. Inspect
them before you touch anything. Do not discard, overwrite or "clean up" a change
you did not make.

---

## 1. Read in this order

### 1.1 Contract and current truth

1. **`agents.md`** — non-negotiables, hard stops, handoff protocol. If anything
   conflicts with it, it wins.
2. **`docs/state.md`** — phase, what exists, blockers, evidence status.
3. **`docs/tasks.md`** — Now/Next/Later, priorities, dependencies, acceptance
   criteria.
4. **The newest entry in `docs/handoff.md`** — what the last session did, what it
   did not do, and what it recommended next.
5. **The latest `docs/log/YYYY-MM-DD.md`** — dated details and evidence.
6. **`docs/decisions.md`** — the decision index, plus the entries relevant to the
   task. Check status: `accepted`, `proposed`, `rejected`, `superseded`.

### 1.2 Task-specific docs

Read the files linked from the task row. Then:

| If the task is about… | Also read |
|---|---|
| Firmware F1–F5 | `docs/firmware-architecture.md`, `docs/prompts/build-firmware.md`, ADR-0003, ADR-0004, `bootloader/LOADER.md` |
| Bootloader / loader / DFU | `bootloader/README.md`, `bootloader/DFU.md`, `bootloader/LOADER.md`, the relevant `*_DIAG.md` |
| HFS+ reader/writer/journal | `docs/hfsplus-write-engine.md`, `docs/invariants.md`, `docs/failures.md`, `docs/testing.md` |
| Release | `docs/release.md`, `docs/validation.md`, `docs/security.md`, the release runbook |
| Device test | `docs/device-matrix.md`, `docs/verification.md`, `docs/prompts/run-device-matrix.md` |
| UI / typography | `docs/ui.md`, `docs/goals.md`, D-10/D-15/D-23/D-24 |
| Documentation | `docs/conventions.md`, `docs/prompts/audit-docs-for-contradictions.md` |
| 7G / other ports | `docs/device-matrix.md`, D-13; remember 7G is deferred until the owner provides hardware and a board audit |

### 1.3 Evidence rules

- A host test is not a device run.
- A successful link is not a boot.
- A screen photo is not `fsck_hfs`.
- A design target is not a measurement.
- One device is not a support matrix.
- `implemented` means the acceptance evidence is recorded, not "the code
  exists".

---

## 2. Check the repository state

Run these before forming an opinion:

```sh
git status -sb
git log -8 --oneline
git diff --stat
git tag --list --sort=-creatordate | head -5
```

If the worktree is dirty:

- read `git diff` and `git diff --cached`;
- identify which files are user/other-agent work;
- do not overwrite or revert them;
- if they conflict with your task, stop and ask.

If the branch is behind/diverged or detached, report it. Do not force-push,
rebase or rewrite history.

For release work, also check:

```sh
gh release list --limit 5
gh api repos/deandromi/Helikon-OS/releases/latest --jq '{tag:.tag_name,prerelease:.prerelease,draft:.draft}'
```

For device work, check `docs/device-matrix.md` for the latest recorded run and
its image hash. Do not reuse an old run as evidence for a new image.

---

## 3. Choose the active task

### 3.1 If the owner named a task

Use it. Confirm its ID, status, dependencies and acceptance criteria from
`docs/tasks.md`. If it is `implemented`, `rejected` or blocked, say so and
propose the nearest unblocked task instead of starting it anyway.

### 3.2 If the owner did not name a task

Pick the highest-priority unblocked task in this order:

1. **Now / P0** — the current critical line;
2. **Next / P1** — the first follow-up;
3. **Later / P2** — only if the P0/P1 lines are blocked or done.

Respect dependencies. Do not start a task whose dependency is `planned`,
`in progress` without evidence, or `proposed` and waiting for the owner. Do not
start a second phase because the first one is boring.

### 3.3 If there is no clear task

Stop and ask. A session without a task is a planning session, not a licence to
invent work.

### 3.4 Do not start

- a task blocked on an owner decision;
- a task that needs a real signed firmware image that does not exist;
- a device write without a verified backup and a disposable/backed-up volume;
- a 7G port without the owner's hardware and board audit;
- a release without the validation scorecard;
- a "quick fix" that touches the root README, the donor repo or a frozen format.

---

## 4. Produce the Session Brief before editing

Write this in the chat, not in a file (unless the owner asks for a file). Keep it
under ~30 lines. The owner uses it to confirm or redirect you.

```text
# Session Brief — YYYY-MM-DD

Phase: <pre-phase 0 / phase 0 / ...>
Repo: <branch, HEAD, clean/dirty>
Active task: <ID and title>
Why this task: <priority, dependency, owner instruction>
Dependencies: <met / missing>
Blocked by: <none / decision / evidence / hardware>
Last evidence: <last committed run or "none">
In scope: <3-6 bullets>
Out of scope: <3-6 bullets>
Hard stops that apply: <list>
Next smallest step: <one concrete action>
Docs I will update: <state/tasks/handoff/log/...>
```

Then state one of:

- **"Ready to start; confirm or redirect."** — if the owner did not say "go";
- **"Starting with <step>."** — if the owner already authorized the task.

If the brief reveals a contradiction between `state.md`, `tasks.md`, the
handoff and the owner's instruction, do not silently pick one. Record the
conflict in `docs/decisions.md` (or ask) and say which source you are following.

---

## 5. Work loop

For one logical change:

1. **Read the task's linked docs** and the relevant tests. Do not guess an
   interface.
2. **Check the current evidence.** If the task claims `implemented`, find the
   recorded test/device run; if it is missing, the task is not done.
3. **Make the smallest change** that moves the acceptance criterion.
4. **Test at the right layer:**
   - host unit/model tests for logic;
   - sanitizers for parsers and memory;
   - image/disposable-volume tests for filesystem changes;
   - device run only when the phase and backup gate allow it.
5. **Record evidence** in the linked file or `docs/device-matrix.md`.
6. **Update docs** before you forget: state, tasks, handoff, log, decisions,
   failures, file-map, architecture/design if the design changed.
7. **Commit only your files.** One logical change per commit. Do not commit
   generated images, backups, private keys or disk images.
8. **Report** what changed, what was tested, what was not, and the next step.

Do not skip step 6 because the code works. A change without evidence and docs is
not done.

---

## 6. Domain routing

| Domain | First action | Canonical docs | Evidence |
|---|---|---|---|
| Firmware F1–F5 | Use `docs/prompts/build-firmware.md`; start with F1/T-054 | firmware-architecture, ADR-0003/0004, LOADER | Host tests + one device run + `fsck_hfs` |
| Bootloader/loader | Check image hash, `MOVE_AREA`, DFU path | bootloader/README, DFU, LOADER, LOADER_TEST | Device run + screen + fsck |
| Diagnostic images | Check the specific runbook and opt-in gate | WRITE_DIAG, OUTER_DIAG, device-matrix | Device run + fsck + backup diff |
| HFS+ writer | Read the frozen interface and invariants | hfsplus-write-engine, invariants, failures | Model/crash tests + `fsck_hfs` |
| Release | Check the validation scorecard and tags | release, validation, security | Signed assets + hashes + device evidence |
| Device test | Confirm backup/recovery first | device-matrix, verification, run-device-matrix | Matrix row |
| Docs | Check canonical file and links | conventions, decisions | `git diff --check`, link check |
| UI | Check typography and fallback rules | ui, goals, D-10/D-15/D-23/D-24 | Host frames + panel photos |
| 7G/ports | Stop unless the owner provided hardware | device-matrix, D-13 | Board audit + matrix |

---

## 7. End of session

Before you stop:

- [ ] `docs/state.md` reflects the phase, blockers and evidence.
- [ ] `docs/tasks.md` reflects the task status and the next task.
- [ ] `docs/handoff.md` has a new top entry with what happened and what is next.
- [ ] `docs/log/YYYY-MM-DD.md` has a dated entry if the session produced work.
- [ ] `docs/device-matrix.md` has every device run.
- [ ] `docs/decisions.md` has new decisions/conflicts.
- [ ] `docs/failures.md` has new error codes/safe responses.
- [ ] `docs/file-map.md` has new files/modules.
- [ ] The design doc/ADR is updated if the design changed.
- [ ] `git diff --check` passes.
- [ ] Links resolve; tables have consistent columns.
- [ ] The commit contains only your files.
- [ ] The final report states what was **not** done.

Handoff entry template:

```text
# Handoff — YYYY-MM-DD (short title)

## What happened
- <changes, with file links>
- <tests/evidence>

## What is next
1. <smallest next step>
2. <blocked item, if any>

## Do not claim
- <what the evidence does not prove>
```

---

## 8. Hard-stop quick card

Stop and report if:

- a write is about to bypass `hfs_txn`;
- a journal is dirty/invalid and a write is requested;
- a fix is format, erase, restore, repartition, journal delete or metadata zero;
- a backup is unverified;
- the device is in an unknown mode;
- the only evidence is "it boots";
- a measurement or device result would be invented;
- a frozen format/ABI/map would change silently;
- the root README or donor repo would be edited;
- a legal/signing decision is needed that the owner has not made;
- the task requires 7G hardware that is not present.

Record the exact error, stage, device state and what you did **not** attempt.

---

## 9. Example Session Brief (current state, regenerate from the repo)

This example is a snapshot, not a hardcoded instruction. Re-read the repo and
replace it.

```text
# Session Brief — 2026-11-04

Phase: pre-phase 0 — F1 read-only firmware done (owner-reported 6G run); F2 next
Repo: main, clean, up to date
Active task: T-055 / F2 — bootstate promotion
Why this task: P0; first firmware write; follows the completed F1 gate
Dependencies: T-054 done; D-02; hfs_txn/hfs_journal host-tested
Blocked by: owner authorization for the first firmware write
Last evidence: F1 d7916411… owner-reported 6G run; F2 f74e3485… host-verified,
              artifact promotes TRYING→GOOD and loader sees GOOD; no F2 device run
In scope: F2 6G run, clean fsck_hfs, next loader boot GOOD, device power-cut
Out of scope: VFS, browser, UI, audio, USB, database, update
Hard stops: no write outside bootstate, no journal bypass, no unsigned image, no --single
Next smallest step: run the F2 image on a backed-up 6G and record the promotion
Docs to update: state, tasks, handoff, log, device-matrix after the run
```

If the repo no longer matches this example, the example is wrong and the repo is
right.

---

## 10. What not to do at session start

- Do not start coding before the Session Brief.
- Do not pick a task from a chat summary.
- Do not trust a handoff over `state.md`/`tasks.md` when they disagree.
- Do not edit uncommitted user work.
- Do not run `git reset --hard`, `git clean -fd`, `git push --force` or a
  destructive device command.
- Do not start a second phase because the first is slow.
- Do not claim a test passed if it was not run.
- Do not "fix" a docs inconsistency by silently choosing a side; record it.
- Do not add a new canonical file for a topic that already has one.

---

## 11. One-line invocation

```text
Use docs/prompts/session-start.md. Read the repository, produce the Session
Brief, and do not edit until I confirm. If I already named a task, use it.
```
