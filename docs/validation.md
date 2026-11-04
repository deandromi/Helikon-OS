# Validation — release-level evidence matrix

> Purpose: define the evidence required before a release or a goal claim.
> Status: living document
> Owner: release lead; test lead
> Last updated: 2026-11-04
> Source: design paper §24, §26.12, §28.6; `docs/verification.md`

**No Helikon-OS release exists and no validation gate has been run.** Every
status below is `not run` or `unproven`. A green table is not evidence; a
recorded device run with logs and hashes is.

## 1. Release blocker gates

A stable release requires all P0 gates. A beta may omit a feature, but may not
hide an unexecuted safety gate.

| ID | Gate | Threshold | Evidence | Status |
|---|---|---|---|---|
| G-01 | Native HFS+ writer, journaled | Create/write/rename/delete/mkdir/truncate on a real library after phase 3 | Device run + `fsck_hfs` | `not run` |
| G-02 | `fsck_hfs` zero repairs | Zero repairs after every crash/power-cut test | Full `fsck_hfs` output | `not run` |
| G-03 | macOS parity | macOS mounts the volume and sees the same tree/metadata | `diskutil verifyVolume`, tree diff | `not run` |
| G-04 | Journal replay idempotent | Replay after cuts at every boundary; no lost committed state | Crash images + replay log | `not run` |
| G-05 | No overlay/FAT | No `/.rockpod-rw` path, no FAT mount/write symbols, `NOT_HFS` screen | Code inspection + FAT32 test | `not run` |
| G-06 | Bootloader read-only | No write symbols linked; volume hash unchanged after boot | Image/map + whole-volume hash | `not run` |
| G-07 | Apple/DFU recovery intact | Apple path bootable; DFU install/uninstall works; `--single` rejected | Device run | `not run` |
| G-08 | Signed update/rollback | Bad board/version/hash/size rejected; staging cut leaves old image bootable | Fault injection + device run | `not run` |
| G-09 | Backup/recovery rehearsal | Backup verified by read-back; restore path tested | Checklist + hash | `not run` |
| G-10 | No low-battery corruption | Write refused below threshold; no transaction starts | Battery injection + log | `not run` |
| G-11 | Memory budget | Peak within §28.3.2 budget on 10k tracks; no OOM in soak | Watermark + soak | `not run` |
| G-12 | Device support claim | Exact model/revision/adapter/firmware recorded | `device-matrix.md` row | `not run` |

## 2. Power-loss matrix

Cut power at every write boundary. Each cell needs a result; an empty cell is a
missing gate, not a pass.

| Operation | Before journal commit | During commit | After commit, before apply | During apply | After apply, before clean | Evidence |
|---|---|---|---|---|---|---|
| Create file | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Append/write | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Close/sync | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Rename | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Delete | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Truncate | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Mkdir | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Settings save | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Database install | `not run` | `not run` | `not run` | `not run` | `not run` | none |
| Artwork install | `not run` | `not run` | `not run` | `not run` | `not run` | none |

After every cut: boot, mount, capture journal state, run `fsck_hfs -n`, compare
the recovered tree with the committed state, and repeat at least 100 times per
operation. A single required repair blocks release.

### 2.1 Host crash-injection coverage

`tests/image/run-crash-macos.sh` injects a failed device write at every cut
point, reopens the image, replays the journal and runs `fsck_hfs -fn`. Current
coverage is **280 cuts**:

| Operation | Write cuts | Flush cuts | Partial-sector cuts | Status |
|---|---:|---:|---:|---|
| Create | 25 | 4 | 25 | `implemented`, fsck clean |
| Append | 34 | 4 | 34 | `implemented`, fsck clean |
| Truncate | 25 | 4 | 25 | `implemented`, fsck clean |
| Delete | 25 | 4 | 25 | `implemented`, fsck clean |
| Rename | 21 | 4 | 21 | `implemented`, fsck clean |

This is host evidence, not the device power-cut matrix above. Power-loss timing
and the remaining operations (close, mkdir, settings, database, artwork) are
still open; the device cells remain `not run`.

### 2.2 Evidence record format

Each power-cut result must record:

```text
operation: create | append | truncate | delete | rename | mkdir | ...
image: SHA-256 before the test
cut: write number and journal phase (before commit / commit / apply / clean)
journal: start, end, dirty/clean before replay
replay: result and number of applied blocks
fsck_hfs: full output, exit code and zero-repair confirmation
tree: recovered names, sizes, CNIDs and metadata compared with the expected state
device: 6G model/revision, adapter/card, firmware/bootloader image hash
result: pass | fail, plus the preserved image path
```

A missing field is not a pass.

## 3. Battery baseline

| Field | Requirement | Current |
|---|---|---|
| Baseline build | Rockbox commit + image hash recorded | `not run` |
| Device | Same model/revision, card, battery condition | `open question` |
| Media | Same album/track set, volume, backlight, output | `open question` |
| Runs | At least 3 per configuration; mean and range | `not run` |
| Metrics | Playback runtime, idle runtime, sleep drain/day, boot energy | `not run` |
| Target | Owner-defined (paper example: ≥10% playback, not a promise) | D-12 open |
| Claim rule | Withdraw if data does not support it | not applicable |

The donor's owner-reported ~2-hour gain is **not** Helikon-OS evidence.

## 4. RAM and performance

| Metric | Requirement | Current |
|---|---|---|
| Peak RAM, 10k tracks | Within §28.3.2 budget | `not run` |
| 72-hour soak | No OOM, no leak, no playback failure | `not run` |
| List scroll | 30 fps on reference device | `not run` |
| Screen open | <200 ms | `not run` |
| Sequential/random I/O | Published method + firmware hash | `not run` |
| 1 GiB copy / 1,000 files | Measured; no read regression claim without data | `not run` |

## 5. Adapter matrix

| Class | Boot | Playback | Write | Sleep/wake | Surprise removal | Status |
|---|---|---|---|---|---|---|
| HDD | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| iFlash SD | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| iFlash CF | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |
| SSD | `not run` | `not run` | `not run` | `not run` | `not run` | `planned` |

Record exact adapter/card model, firmware, sector sizes and negotiated mode. A
known-bad card must be reported, not silently used.

## 6. Skin, app and desktop validation

| Area | Minimum evidence | Status |
|---|---|---|
| Skin corpus | 20 skins load/render; supported/unsupported tag report; frame/memory caps; fallback; licences recorded | `not run` |
| App sandbox | Install/launch/suspend/resume/uninstall; permission denial; quota; crash isolation; escape attempts | `not run` |
| Desktop app | macOS task matrix: import/tag/rate/describe/scrobble/install/backup; failed transfer consistent | `not run` |
| Windows/Linux desktop | Service-mode transport tested; no claim before D-09 | `not run` |
| Car/receiver | 100 ignition cycles; resume/charge/no pops; receiver matrix; Tier 2 optional | `not run` |

## 7. Multi-device validation

| Target | Boot | Play | Charge | Sleep/wake | Write | Evidence | Status |
|---|---|---|---|---|---|---|---|
| 6G reference | `not run` | `not run` | `not run` | `not run` | `not run` | none | `planned` |
| 7G | `not run` | `not run` | `not run` | `not run` | `not run` | none; board audit first | `planned` |
| Other S5L8702 iPod | `not run` | `not run` | `not run` | `not run` | `not run` | none | `planned` |
| Other player | `not run` | `not run` | `not run` | `not run` | `not run` | none | `planned` |

A per-target matrix pass is required before a support claim. No target is
supported today.

## 8. Goal scorecard (paper §28.6)

| Goal | Metric | Target | Method | Gate | Status |
|---|---|---|---|---|---|
| Battery | Playback hours vs Rockbox baseline | ≥10% (goal, not promise) | 3 runs, same media/settings | v1 hardening | `not run` |
| RAM | Peak usage, 10k tracks | Within §28.3.2 budget | Watermark + soak | v1 | `not run` |
| UI | List scroll / screen open | 30 fps / <200 ms | Device trace | v1 | `not run` |
| Skins | Corpus compatibility | Documented subset, 20 skins | Visual + tag report | v1.x | `not run` |
| Adapters | Matrix pass | SD/CF/SSD/HDD | Boot/play/write/sleep | v1 | `not run` |
| Database | 10k build + resume | Completes, host parity | Power cut at 25/50/75% | v1.x | `not run` |
| Anti-corruption | Injected corruption detection | 100% detected, 0 false OK | Bit/block injection | v1.x | `not run` |
| Write speed | Sequential/random vs donor | No read regression; measured write cost | Benchmark suite | v1.x | `not run` |
| Updates | Update/rollback | Old image bootable after cut | Fault injection | v1 | `not run` |
| Install | Non-coder task | Completes with checklist | Moderated test | v1 | `not run` |
| DAC | Line-out measurements | Published, no regression | Audio rig | v1.x | `not run` |
| Read/write | 1 GiB copy, 1k files | Within measured budget | Device benchmark | v1.x | `not run` |
| Podcasts | 500 episodes | Resume/sync correct | Device + desktop | v1.x | `not run` |
| Desktop app | Task matrix | Import/tag/rate/scrobble/install | macOS first | v1.x | `not run` |
| Album/artist | 10k library | Open <200 ms, 30 fps | Device trace | v1.x | `not run` |
| Car | Receiver matrix | Resume/charge/no pops | 100 ignition cycles | v1.x | `not run` |
| Games/apps | Sandbox | No escape, crash isolation | Adversarial tests | v2 | `not run` |
| 7G/ports | Per-target matrix | Boot/play/charge/write | Device run | v2 | `not run` |
| Community | Release audit | Public docs, GPL-2.0-or-later, CI, signed builds | Checklist | continuous | `not run` |

## 9. Sign-off

A release sign-off requires:

- [ ] All P0 release gates have recorded evidence.
- [ ] `fsck_hfs` zero repairs after the power-cut matrix.
- [ ] macOS mounts the volume and sees the expected tree.
- [ ] A recorded device run with exact model/revision/adapter/firmware hash.
- [ ] Battery/RAM/performance claims have method and data, or are withdrawn.
- [ ] No unexecuted stress gate is hidden in release notes.
- [ ] No goal is marked met without a row here.
- [ ] Backup and recovery rehearsal recorded.
- [ ] Signed manifest/rollback tested (when D-01/D-16/D-20 are settled).

**No goal met without evidence.** A goal may be deferred, but it may not be
silently dropped. Release notes must say which goals are in, out and unproven.

## 10. Related

- [`verification.md`](verification.md) — per-change evidence template
- [`testing.md`](testing.md) — test layers
- [`device-matrix.md`](device-matrix.md) — hardware rows
- [`goals.md`](goals.md) — 25 goals
- [`roadmap.md`](roadmap.md) — phase gates
- Paper §24, §26.12, §28.6
