# Device matrix — hardware support status

> Purpose: record what actually ran on which hardware. No claim without a
> recorded run.
> Status: living document
> Owner: test lead; device owners
> Last updated: 2026-11-03
> Source: design paper §1, §24, §28.2.4, §28.4.5; `docs/verification.md`

**Helikon-OS has three owner-reported diagnostic runs on one 6G:** two
read-only-image runs (one partial/failed, one corrected report) and one
write-diagnostic run that reported every stage PASS and whose inner image passed
`fsck_hfs`. This is recorded evidence for those tests, not a support claim.
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
| iPod Classic 6G (reference) | `open question` | `open question` | write diagnostic `81350f…` | controlled PMU cut inside `helikon-test.img` | `not run` | `helikon-cut.txt` target parser host-tested; no hardware run | boot/HFS+ |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | journal replay | `not run` | none | journal lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | sleep/wake 100 cycles | `not run` | none | test lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | USB read-only | `not run` | none | USB lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (firmware) | update/rollback | `not run` | none | release lead |
| iPod Classic 7G | board audit pending | `open question` | none (firmware) | board audit | `not run` | none | hardware lead |

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
