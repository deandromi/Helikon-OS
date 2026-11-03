# Verification — how to prove a change

> Purpose: per-change proof checklist and evidence template.
> Status: living document
> Owner: change author; reviewer verifies
> Last updated: 2026-11-03
> Source: design paper §24, §26.12, §27, §28.6

**No claim without evidence.** A change is not verified because it compiles,
boots, "looks right", or passed on another device. Verification is the recorded
command output, device run, image hash and oracle result that match the claim.

Helikon-OS has a small host code/test tree and no device build yet. The host
read-core checks are current; device and writer commands must be added when
those trees exist, not invented now. Donor commands are reference only and do
not verify Helikon-OS.

## 1. Choose the change class

| Class | Examples | Minimum proof |
|---|---|---|
| Docs only | README-adjacent docs, agent docs, ADRs, prompts | Diff check, link/table inspection, contradiction audit |
| Code | Reader, VFS, database, player, UI code | Build, unit tests, sanitizers, relevant host tests |
| Writer | Catalog/bitmap/extents/journal/transaction | Model/property tests, crash injection, `fsck_hfs`, macOS differential |
| UI/skin | Screens, theme, WPS subset | Frame/memory trace, corpus render, fallback test |
| Release/update | Manifest, staging, boot state, rollback | Signed-image tests, bad-image tests, power-cut staging, device rollback |

If a change spans classes, use the strictest checklist.

## 2. Docs-only checklist

**Read first:** `docs/conventions.md`, `docs/state.md`, `docs/tasks.md`,
`docs/decisions.md`.

**Commands:**

```sh
git diff --check
git status --short
# Inspect changed Markdown links and table columns manually.
```

**Expected:** no whitespace errors; only intended files changed; every relative
link resolves; tables have consistent column counts; no duplicate canonical file
(`agent.md`/`agents.md`, `decision-log.md`/`decisions.md`,
`release-process.md`/`release.md`).

**Evidence:** branch, changed paths, command output, list of links checked, any
contradiction found and where it was recorded.

**Rollback:** revert the documentation commit. Do not rewrite history or edit the
root README.

## 3. Code checklist (host)

**Read first:** the owning module in `docs/file-map.md`, the relevant invariant,
and the paper section.

**Commands:** use the repository's documented host commands:

```sh
make -C tests/hfsplus test
make -C tests/hfsplus sanitize
make -C tests/bootloader test
make -C tests/bootloader sanitize
```

Record the exact command, toolchain version and commit. Do not copy donor
commands and call them Helikon-OS tests.

**Expected:** build succeeds; unit tests pass; sanitizer run is clean; no new
warnings that indicate undefined behaviour; no direct hardware access outside the
HAL/block layer.

**Evidence:** commit SHA, branch, toolchain versions, command output, test count,
sanitizer result, map/size report.

**Rollback:** revert the commit. If the change touched a device, restore the
verified pre-test image.

## 4. Writer/journal checklist

**Read first:** `docs/hfsplus-write-engine.md`, `docs/invariants.md`
(I-03–I-07, I-11, I-13, I-15), paper §9 and §21.

**Commands/process (future):**

```sh
# Names are illustrative until the test tree exists.
make -C tools/hfsplus test          # donor reader reference only
make -C tools/hfsplus sanitize      # donor reader reference only
# Helikon-OS writer tests: model-based, property, crash injection, fsck oracle.
```

**Required checks:**

- Model-based B-tree insert/split/merge/delete tests pass.
- Allocation/extents property tests pass, including `NO_SPACE`.
- Crash injection at every transaction boundary leaves either the old or new
  consistent state.
- Replay is idempotent and bounded.
- `fsck_hfs -n` reports **zero repairs** after every cut.
- macOS mounts the volume and sees the same tree; compare names, sizes, CNIDs
  (uniqueness/monotonicity, not exact values), timestamps, BSD/Finder info and
  extents.
- A dirty/invalid journal mounts read-only and is not modified.
- No write occurs on a volume `fsck_hfs` reports dirty/damaged.

**Evidence:** commit SHA, image hash, operation, cut point, journal state before/
after, `fsck_hfs` full output, recovered tree, adapter/device, screenshots/logs.

**Rollback:** restore the pre-test image. Do not hand-edit metadata or delete the
journal. If the test was on a real library, stop and escalate.

## 5. UI/skin checklist

**Read first:** paper §14 and §28.3.4, D-10/D-15, `docs/device-matrix.md`.

**Checks:** default UI works with no skin; 20-skin corpus loads/renders; malformed
and hostile skins are rejected; unsupported tags are visible and logged; frame
time is within budget; memory is capped; fallback to default UI works.

**Evidence:** skin name and licence, tag report, frame-time trace, memory
watermark, screenshots, device/adapter/firmware hash.

**Rollback:** disable the skin and return to the default theme.

## 6. Release/update checklist

**Read first:** `docs/release.md`, `docs/validation.md`, `docs/security.md`,
paper §16 and §28.2.5.

**Checks:** manifest fields; signature verification; wrong board/version/hash/
size rejected; power cut during staging leaves the old image bootable; explicit
install; boot-state record; rollback; version/hash displayed; Apple/DFU recovery
intact; no writable USB LUN.

**Evidence:** manifest, image hash, device run, update/rollback log, `fsck_hfs`
output, screenshots.

**Rollback:** bootloader keeps the previous verified image; bootloader changes
recover through DFU. Do not use `--single`.

## 7. Evidence template

Copy this into the PR, issue or `docs/log/YYYY-MM-DD.md`. Replace every
placeholder; write `not run` rather than guessing.

```md
### Evidence — <short title>

- Date:
- Owner:
- Commit SHA:
- Branch:
- Change class: docs / code / writer / UI / release
- Device model/revision:
- Capacity:
- Storage adapter:
- Card/SSD/HDD model:
- Firmware image + SHA-256:
- Bootloader image + SHA-256:
- Host/toolchain:
- Test volume (disposable?):
- Backup verified? path/hash/read-back:
- Commands run:
- Expected result:
- Actual result:
- `fsck_hfs -n` output:
- `diskutil verifyVolume` output:
- Battery result (runs, mean/range):
- RAM watermark:
- Frame time:
- Screenshots/logs:
- Rollback performed:
- Open questions:
```

Do not include serial numbers, device backups, disk images, `/.rockpod-rw`
contents, Wi-Fi credentials or personal data.

## 8. Release claim review

Before a release note claims a goal is met:

1. Find the goal in [`goals.md`](goals.md) and its metric in
   [`validation.md`](validation.md).
2. Confirm the evidence row names the device, adapter, firmware hash and test.
3. Confirm the test was run on Helikon-OS, not the donor.
4. Confirm `fsck_hfs` zero repairs where applicable.
5. If evidence is missing, mark the goal `unproven` and remove the claim.

## 9. Related

- [`testing.md`](testing.md) — test layers and fixtures
- [`validation.md`](validation.md) — release-level matrix
- [`device-matrix.md`](device-matrix.md) — hardware evidence rows
- [`failures.md`](failures.md) — what an error means
- [`conventions.md`](conventions.md) — status labels and doc style
