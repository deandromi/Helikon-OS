# Device matrix — hardware support status

> Purpose: record what actually ran on which hardware. No claim without a
> recorded run.
> Status: living document
> Owner: test lead; device owners
> Last updated: 2026-11-04
> Source: design paper §1, §24, §28.2.4, §28.4.5; `docs/verification.md`

**Helikon-OS has four completed owner-reported diagnostic runs on one 6G:**
two read-only-image runs (one partial/failed, one corrected report), one
inner-image write run, and one outer-volume write run. The outer diagnostic also
had two earlier boots on 2026-11-04: the first reached rename and the second
stopped at `Cleanup: FAIL` (`-14`); the third reported every stage PASS. This is
recorded evidence for those tests, not a support claim.
Every unrun row below is `planned`, `unproven` or `fail`. Do not add a device to
a supported list because it has the same SoC, because the donor ran on it, or
because a build links.

## 1. Target policy

| Target | Status | Rule |
|---|---|---|
| iPod Classic 6G | Reference target; `planned` | First implementation and first matrix. No support claim until a recorded run. |
| iPod Classic 7G | Next named port; `planned` | Requires a board audit (panel, PMU, audio, storage, button map, `MOVE_AREA`, DFU/NOR) before any port work or claim. |
| Other S5L8702 iPods | Ports; `planned` | Need a BSP, target description, hardware, maintainer and per-target matrix. |
| Other MP3 players | Exploratory; `planned` | Not a v1 promise. Need hardware, documentation and a long-term test plan. |

## 2. Helikon-OS test matrix

Columns are required for every row. Use `not run` rather than leaving a result
blank. Evidence links point to a log, image hash or report; do not attach disk
images, backups or serial numbers to the repository.

| Device | Revision | Adapter | Firmware | Test | Result | Evidence | Owner |
|---|---|---|---|---|---|---|---|
| iPod Classic 6G (reference) | `open question` | `open question` | diagnostic `bd0ebc92…` | boot + splash + report | `partial` | Owner report 2026-11-03: splashed; volume PASS, catalog PASS; report formatting corrupted | owner |
| iPod Classic 6G (reference) | `open question` | `open question` | diagnostic `bd0ebc92…` | native HFS+ read | `fail` | Owner report 2026-11-03: read FAIL, `read error: -11` (`NAME_INVALID`); fixed lookup pending reflash | owner |
| iPod Classic 6G (reference) | `open question` | `open question` | diagnostic `280ed7ae…` | boot + splash + report | `pass` | Owner report 2026-11-03: report formatting correct; volume PASS, catalog PASS, splash PASS, write NOT IMPLEMENTED, no write | owner |
| iPod Classic 6G (reference) | `open question` | `open question` | diagnostic `280ed7ae…` | native HFS+ file read | `blocked` | Owner report 2026-11-03: `read error: -8` (`NOT_FOUND`) because `/rockbox.ipod` is not a disk file; probe target needs a real root file | owner |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | playback | `not run` | none | test lead |
| iPod Classic 6G (reference) | `open question` | `open question` | write diagnostic `9e3881…` | native HFS+ write inside `helikon-test.img` | `pass` | Owner report 2026-11-03: every stage PASS; extracted inner image SHA-256 `156060…`, `fsck_hfs -fn` exit 0, no `diag`; outer `diskutil verifyVolume` exit 0 | boot/HFS+ |
| iPod Classic 6G (reference) | `open question` | `open question` | write diagnostic `81350f…` | controlled PMU cut, targets 7, 56, 96 | `pass` | Owner report 2026-11-04: 7/56 pre-commit cuts gave `Recovery: CLEAN`; target 96 post-commit cut recovered; all stages PASS; extracted inner image `fsck_hfs` reported `The volume HelikonInner appears to be OK` | boot/HFS+ |
| iPod Classic 6G (reference) | `open question` | `open question` | outer diagnostic `9eacde18…` | outer-volume `/helikon-outer-test` sequence | `partial` | Owner report 2026-11-04: reached rename, left `marker` + 4096-byte `renamed` (all `0xa5`); report not shown, splash remained; next boot `Cleanup: FAIL`, error `-14`; `fsck_hfs` reported the volume OK | owner/boot |
| iPod Classic 6G (reference) | `open question` | `open question` | outer diagnostic `32eeb541…` | outer-volume `/helikon-outer-test` create/append/rename/truncate/delete | `pass` | Owner report 2026-11-04: every screen stage PASS; `/helikon-outer-test` absent after the run; post-run `fsck_hfs -fn` reported `The volume IPOD appears to be OK`; `.rockpod-rw` SHA-256 `2ac065…` identical to the 2026-11-03 backup; `.rockbox` differs only in `.DS_Store`; owner confirmed the full backup/diff; timing not captured | owner/boot |
| iPod Classic 6G (reference) | `open question` | `open question` | loader test `c6125103…` | Alpha splash + loader startup | `partial` | Owner report 2026-11-04: splash displayed; startup completed after about 3 minutes. `HFS+ DETAILS` stayed neutral on the installed pre-`lcd_update` image. Exact `JUMP OK`/`JUMP FAILED` text and post-run `fsck_hfs` still to record | owner/boot |
| iPod Classic 6G (reference) | `open question` | `open question` | loader test `30f5bda2…` | Alpha splash + stage line + `lcd_update` fix | `not run` | Host gates pass; new build adds the HFS+ label update and bottom stage line; not installed | boot lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | journal replay | `not run` | none | journal lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | sleep/wake 100 cycles | `not run` | none | test lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | USB read-only | `not run` | none | USB lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | update/rollback | `not run` | none | release lead |
| iPod Classic 7G | board audit pending | `open question` | none (firmware) | board audit | `deferred` | Owner removed physical 7G from the active queue 2026-11-04; target policy remains future port | hardware lead |

"Firmware: none (firmware)" means there is no Helikon-OS firmware image. The
diagnostic rows name the bootloader image hash. Do not write a donor version into
this column.

### 2.1 Diagnostic runs — owner-reported, 2026-11-03

The first Helikon-OS diagnostic image (`bd0ebc92…`, 66,920 bytes) booted on the
owner's 6G and displayed the splash. The report showed:

```text
HFS+ volume: PASS
HFS+ catalog: PASS
HFS+ read: FAIL
HFS+ write: <corrupt>
Journal: <corrupt>
Splash: NOT IMPLEMENTED
read error: -11
```

Interpretation:

- The splash, LCD and HFS+ volume/catalog open paths worked on hardware.
- `%llu` is not supported by the Rockbox bootloader formatter; it printed
  `llu` and shifted later status arguments. The dotted squares and the
  `Splash: NOT IMPLEMENTED` line are display corruption, not the actual splash
  or write status.
- `read error: -11` is `HFS_ERR_NAME_INVALID` from the strict root path lookup.
  The raw ASCII probe lookup now skips undecodable unrelated names.
- No write was requested or reported. This row is not a support claim.

The fixed image (`280ed7ae…`, 67,144 bytes) was then run. The report formatting
was correct: volume, catalog, write (`NOT IMPLEMENTED`), journal, splash, and the
partition size displayed as expected. `HFS+ read` reported `-8`
(`HFS_ERR_NOT_FOUND`) because `/rockbox.ipod` is not a file on this iPod; the
lookup itself worked. No write was requested or reported.

The file-read path is therefore not hardware-exercised yet. The next diagnostic
probe should target a real root file such as `/.rockpod-rw`. This does not block
the writer design.

The separate write diagnostic (`9e3881…`, 109,320 bytes) now has one recorded
6G run; see §2.2. The inner image passed `fsck_hfs` and the outer volume passed
`diskutil verifyVolume`. This is not a library-write, power-cut, adapter or
release claim; those rows remain unrun.

### 2.2 Write diagnostic run — owner-reported, 2026-11-03

The separate write-diagnostic image (`9e3881…`, 109,320 bytes) was installed by
DFU with `--dfusend` (never `--single`) on the owner's 6G. The inner image
`helikon-test.img` was present at the root. The owner reports that **every stage
on screen passed**: outer HFS+, test image, inner HFS+, inner catalog, inner
journal, folder, create, append, readback, rename, truncate, delete and splash.

Post-run verification on macOS:

- Extracted inner image `helikon-test.img`, 16,777,216 bytes, SHA-256
  `156060075eef60c463f421ce459c2138a00e1c43de44da802b77ce89c79ca0e8`.
- `fsck_hfs -fn` (hfs-751) exited 0 and reported:
  `The volume HelikonInner appears to be OK.`
- Mounting the inner image showed no `diag` folder, so the diagnostic cleaned up
  its own create/delete sequence.
- `diskutil verifyVolume /Volumes/IPOD` exited 0; the outer volume was OK.

This proves the inner-image write path and the outer-metadata guard on one 6G
run. It is not a library-write, power-cut, adapter or release claim. The outer
volume's metadata was not written; only `helikon-test.img` data blocks changed.

### 2.3 Controlled PMU cut — target 7, owner-reported, 2026-11-04

With `helikon-cut.txt` containing `7`, then `56`, the diagnostic powered off
during the early boot/write sequence each time. After each cut the owner set the
target to `0`, rebooted and reported the normal screen with every stage PASS,
`Recovery: CLEAN` and `Cleanup: PASS`.

The cut count is 512-byte ATA writes. The measured one-extent sequence has
journal commits at about 35, 89, 159, 227, 287, 349 and 403; the corresponding
clean markers are at 54, 108, 194, 244, 314, 368 and 422. Targets 7 and 56 are
before the first and second commits, so a clean journal on the next boot is the
expected result, not a failed recovery. No `RECOVERED` line is expected from
those targets. Targets in the replay windows, such as `44`, `96`, `176`, `236`,
`300`, `356` or `410`, should produce a dirty journal and a `RECOVERED` line.
Target `96` did: the owner reports the post-commit cut recovered, every stage
passed, and the extracted inner image passed `fsck_hfs` with
`The volume HelikonInner appears to be OK`. Targets 7 and 56 remain the
pre-commit clean controls; further replay targets are optional coverage.

Inner-image `fsck_hfs` for target 96 reported the volume OK. Further replay
targets remain optional coverage.

### 2.4 Outer-volume diagnostic — partial, owner-reported, 2026-11-04

The first outer-volume image (`9eacde18…`) booted and reached the rename stage.
The owner saw the splash remain instead of the report. The next boot showed:

```text
Opt-in: PASS
Outer HFS+: PASS
Journal: PASS
Catalog: PASS
Cleanup: FAIL
... NOT TESTED
error: -14
```

`-14` is `HFS_ERR_BUSY`: `/helikon-outer-test` existed, so the second boot
stopped before any write. Inspecting the folder on macOS found only:

```text
marker   18 bytes, HELIKON-OUTER-TEST
renamed  4096 bytes, all 0xa5
```

That places the first-run stop after rename and before truncate. `diskutil
verifyVolume` / `fsck_hfs -fn` reported `The volume IPOD appears to be OK`; the
journal was clean. No library file was touched, and no report from the first
boot is available.

The next image (`32eeb541…`) replaces the append/truncate
delete-then-insert record update with an in-place B-tree record update, and
shows the current stage on screen. It is host-tested; no device run yet.

### 2.5 Outer-volume diagnostic — first full pass, owner-reported, 2026-11-04

The owner ran `32eeb541…` (the in-place-update/progress image) and reports every
screen stage PASS. Post-run checks on the mounted volume:

```text
/helikon-outer-test: absent
fsck_hfs -fn: The volume IPOD appears to be OK
.diskutil verifyVolume: exit 0
```

The partial `.rockpod-rw` was byte-identical to the 2026-11-03 backup (SHA-256
`2ac065…`); `.rockbox` differed only in `.DS_Store` metadata. The owner
confirmed the full backup and diff on 2026-11-04; the agent did not rerun it
because only the partial `.rockbox`/`.rockpod-rw` backup was visible on this
Mac. No timing was recorded; the owner described the run as slow.

This is one successful outer-volume run on one 6G. It is not a power-cut,
adapter, other-volume or support claim.

**Recovery rehearsal:** the owner confirmed the full backup read-back, a
disposable restore rehearsal and the Apple-restore/DFU fallback path on
2026-11-04. The protocol is in `outer-volume-test.md`; the inventory/adapter
matrix remains T-012.

## 3. Storage and adapter matrix

The paper requires at least one SD, one CF, one SSD and one HDD configuration
before an adapter claim. Record the exact bridge/card model and firmware; do not
generalize from "iFlash works".

| Storage class | Example configuration | Boot | Playback | Write | Sleep/wake | Surprise removal | Status |
|---|---|---|---|---|---|---|---|
| Original HDD | `open question` | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| iFlash SD | `open question` | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| iFlash CF | `open question` | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| SSD | `open question` | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |

Required per row: adapter model, card/SSD model, capacity, logical/physical
sector sizes, negotiated PIO/DMA mode, firmware, result, evidence. A known-bad
card must be reported, not silently used.

## 4. Receivers and car head units

Tier 1 is line-out, charging, resume and safe power behaviour. Tier 2 is an
optional 30-pin control subset; no Apple certification claim.

| Receiver/head unit | Cable/adapter | Charging | Resume | Controls | Audio | Ignition cycles | Status |
|---|---|---|---|---|---|---|---|
| `open question` | `open question` | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |

Do not name a receiver as supported without a recorded model, cable, test and
result. Unknown accessories fall back to line-out/charging, never a hang.

## 5. Skin corpus

The paper targets a documented subset and a 20-skin corpus, not full WPS
compatibility. Themes are installed by the user or desktop app; the project does
not redistribute themes without permission.

| Skin/theme | Source | Licence/permission | Tags supported | Tags unsupported | Frame budget | Memory cap | Status |
|---|---|---|---|---|---|---|---|
| `open question` | `open question` | `open question` | `not run` | `not run` | `not run` | `not run` | `planned` |

No proprietary theme, artwork, font or metadata may be committed. Record the
licence before adding a skin to the corpus. A malformed/hostile skin must be
rejected without a crash; the default UI remains available.

## 6. Donor baseline (not Helikon-OS evidence)

The donor V3.2 release has owner-reported hardware use. It is recorded here only
to prevent confusion:

| Item | Donor status | Use in Helikon-OS |
|---|---|---|
| V3.2 overlay firmware on a 6G | Owner-reported working; donor-specific | None. Do not cite as Helikon-OS boot, write, battery or adapter evidence. |
| Donor HFS+ host tests | Host-verified in donor | None. Different code and write model. |
| Donor battery report | Owner-reported, not a controlled Helikon-OS measurement | None. Helikon-OS battery claim needs D-12 evidence. |

Do not copy donor device reports into this matrix as Helikon-OS rows.

## 7. How to add a run

1. Complete the backup/recovery checklist in `verification.md`.
2. Record the exact model/revision, capacity, adapter/card, firmware image hash,
   bootloader hash, host/toolchain and test volume.
3. Run the test; capture logs, screenshots and `fsck_hfs` output.
4. Add one row with a result (`pass`, `fail`, `blocked`, `not run`) and evidence
   link.
5. If the test wrote to a volume, confirm it was disposable or backed up.
6. Update `state.md` if the run changes a blocker or a claim.

## 8. Claim rule

A support claim is valid only if:

- the row names the exact device/revision and adapter/card;
- the firmware hash and test method are recorded;
- the result is `pass` with evidence;
- the test ran Helikon-OS, not the donor;
- the claim is limited to what was tested.

Otherwise the claim is `unproven` and must be removed from release notes.
