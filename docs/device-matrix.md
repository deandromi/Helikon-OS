# Device matrix — hardware support status

> Purpose: record what actually ran on which hardware. No claim without a
> recorded run.
> Status: living document
> Owner: test lead; device owners
> Last updated: 2026-11-03
> Source: design paper §1, §24, §28.2.4, §28.4.5; `docs/verification.md`

**Helikon-OS has no recorded device run.** Every row below is `planned` or
`unproven`, not support. Do not add a device to a supported list because it has
the same SoC, because the donor ran on it, or because a build links.

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
| iPod Classic 6G (reference) | `open question` | `open question` | none (no build) | boot | `not run` | none | test lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (no build) | playback | `not run` | none | test lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (no build) | native HFS+ read | `not run` | none | test lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (no build) | native HFS+ write (disposable) | `not run` | none | HFS+ lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (no build) | journal replay | `not run` | none | journal lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (no build) | sleep/wake 100 cycles | `not run` | none | test lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (no build) | USB read-only | `not run` | none | USB lead |
| iPod Classic 6G (reference) | `open question` | `open question` | none (no build) | update/rollback | `not run` | none | release lead |
| iPod Classic 7G | board audit pending | `open question` | none | board audit | `not run` | none | hardware lead |

"Firmware: none (no build)" is literal: there is no Helikon-OS image. Do not
write a donor version into this column.

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
