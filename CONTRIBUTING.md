# Contributing to Helikon-OS

> Purpose: contribution flow, device-report template and review expectations.
> Status: living document
> Owner: project owner
> Last updated: 2026-11-03
> Source: `agents.md`, `docs/conventions.md`, design paper §18/§28

Helikon-OS is a design-stage project. There is no firmware source tree, bootloader
image, installer or release. The most useful contributions right now are design
review, documentation fixes, test plans, fixture ideas and owner decisions — not
firmware patches.

## Before you start

1. Read [`agents.md`](agents.md) and [`docs/README.md`](docs/README.md).
2. Read [`docs/state.md`](docs/state.md), [`docs/invariants.md`](docs/invariants.md)
   and [`docs/decisions.md`](docs/decisions.md).
3. Do not edit the root `README.md`; it is the owner-authored product goals.
4. Do not modify the donor repository `deandromi/hfsplus-rockboxed-ipod`. It is
   read-only source material.
5. Do not add a license, code of conduct, signing policy or release asset. Those
   are open owner decisions (D-18, D-19, D-20).

## What to contribute

| Type | Examples | Notes |
|---|---|---|
| Docs | Clarify a design section, fix links, add a test plan, record a contradiction | Follow `docs/conventions.md`; one canonical file per topic |
| Design review | Review the HFS+ writer/journal/Unicode/update design | Link invariants and paper sections; no invented hardware facts |
| Test design | Fixtures, model tests, power-loss matrix, device checklist | Say what evidence would prove the claim |
| Device reports | A recorded run with exact hardware and firmware hash | Use the template below; no serial numbers or personal data |
| Decisions | A proposal for an open decision | Do not mark it accepted; the owner decides |

Do not submit firmware, bootloader, build-system, CI or release changes as part of
a documentation PR. Open a separate proposal after the owner has agreed the
direction.

## Branch and PR flow

```sh
git switch main
git pull --ff-only
git switch -c <type>/<short-topic>   # docs/, design/, test/
# edit only the files you need
git diff --check
git add <files>
git commit -m "<type>: <summary>"
git push -u origin <branch>
```

Open a PR against `main`. In the body state:

- what changed and why;
- source docs/paper sections;
- checks run;
- contradictions/open decisions;
- what was not changed (firmware, donor, license, release assets).

Do not force-push, rewrite history, merge your own PR without review, or edit
`main` directly. The owner reviews documentation changes.

## Documentation expectations

- Use the status labels in `docs/conventions.md`.
- Add the document header (purpose, status, owner, last updated, source).
- No marketing claims, unverified measurements or invented device support.
- No secrets, backups, disk images, `/.rockpod-rw`, serial numbers, Wi-Fi
  credentials or personal data.
- No proprietary themes/art/fonts/metadata. Reference the corpus and record
  licences.
- If a source conflict changes product direction (overlay, FAT, journaling, 7G,
  licensing, app/plugin compatibility), stop and record it in
  `docs/decisions.md`; do not pick a side.

## Device-report template

```md
### Device report

- Date:
- Reporter:
- Device model/revision:
- Capacity:
- Storage adapter:
- Card/SSD/HDD model:
- Firmware image + SHA-256:
- Bootloader image + SHA-256:
- Host/toolchain:
- Test:
- Volume disposable/backed up?:
- Backup hash/read-back:
- Result: pass / fail / blocked / not run
- `fsck_hfs -n` output:
- Logs/screenshots:
- Recovery/rollback:
- Notes:
```

Do not include a serial number, UDID, full device backup or personal data.
Record the result in `docs/device-matrix.md`; a report without evidence is
`unproven`.

## Review expectations

- Be specific: link the file, invariant, paper section and requested change.
- Separate blocking safety issues from style suggestions.
- Do not ask for a feature that the paper explicitly excludes without an owner
  decision.
- Do not approve a device/support claim without a recorded run.
- If you cannot verify a claim, say so.

## Licensing

No license has been chosen. Do not assume a license for your contribution or copy
donor code/assets until the owner records D-18. Do not contribute code until the
owner records the contribution terms; documentation and design discussion are
welcome in the meantime.

## Code of conduct

No code of conduct has been adopted (D-19). Be civil and constructive; report
serious conduct issues through the private channel in [`SECURITY.md`](SECURITY.md).

## Questions

Open a documentation issue with the exact file and question. Do not use an issue
to publish a vulnerability or personal data.
