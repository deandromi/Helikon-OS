# Roadmap — phases 0–12

> Purpose: sequence the work and make every phase's exit criterion explicit.
> Status: living document
> Owner: project owner; phase leads
> Last updated: 2026-11-05
> Source: design paper §23, §24, §27, §28; Rockbox iPod Classic manual
> `0d5c2b0604-261105`; [`competitive-roadmap.md`](competitive-roadmap.md)

**No phase is complete.** This is a design-stage roadmap, not a schedule
promise. Durations are intentionally omitted; the paper says months to years, not
a beta cycle. A phase starts only when its dependencies and exit criteria are
agreed.

**Canonical-file split:** this file owns phase sequencing and phase gates;
[`competitive-roadmap.md`](competitive-roadmap.md) owns the Rockbox comparison,
the parity policy, the differentiation bets and the decline list. The horizon
lanes below are a cross-phase view, not a second phase table.

## Phase table

| Phase | Deliverable | Exit criteria | Dependencies | Status |
|---:|---|---|---|---|
| 0 | Rescue bootloader + native HFS+ read-only firmware skeleton | Boots, loads a verified image, Apple NOR path intact, zero filesystem writes; volume hash unchanged. No in-bootloader recovery menu (D-26); native Menu+Play and DFU cover recovery | Toolchain/donor build; backup/recovery; D-01/D-02 manifest/boot-state design | `partial` |
| 1 | Read-only player: HAL, HFS+ read, audio, minimal UI | Plays music, browses the library, survives reboot, volume hash unchanged; unsupported files report a reason | Phase 0; HFS+ reader port; audio/UI bring-up | `planned` |
| 2 | Native HFS+ write prototype on a non-journaled disposable volume | Create/write/rename/delete/mkdir/truncate; `fsck_hfs` zero repairs; macOS mounts and sees the same tree; reboot preserves it | Phase 1; D-03/D-04 recorded; T-002/T-004/T-003; writer design | `planned` |
| 3 | Journaled HFS+ write engine | Journal append/replay; power-cut matrix; idempotent replay; `fsck_hfs` zero repairs | Phase 2; journal design; battery/charger gate; crash rig | `planned` |
| 4 | Metadata parity and user state | Timestamps, BSD/Finder info, settings, playlists, resume state; all transactional; macOS agrees | Phase 3; Unicode policy D-06; metadata design | `planned` |
| 5 | Anti-corruption foundations | Checksum index, verify-on-open for critical files, charging/idle scrub, quarantine; injected corruption detected without false OK | Phase 3/4; D-11; scrub scheduler | `planned` |
| 6 | Migration + host tools + Mac desktop MVP | Donor overlay exported to native files; hashes/names/sizes match; app can back up, install and verify | Phase 3/4; host exporter; D-09; desktop app | `planned` |
| 7 | Database: host builder + on-device incremental builder | 10k tracks; host/device parity; power-cut resume at 25/50/75%; playback unaffected | Phase 4; D-07; schema; storage budget | `planned` |
| 8 | UI goals: album/artist, skin subset, podcast, car Tier 1 | Rich local pages; 20-skin corpus; 500-episode podcast library; receiver matrix | Phase 4; D-10/D-14/D-15; UI framework | `planned` |
| 9 | USB read-only + updates + installer + desktop app v1 | Read-only LUN; signed update/rollback; non-coder install; tag/rate/scrobble tasks | Phase 6; D-01/D-16/D-20; release pipeline | `planned` |
| 10 | Apps/games sandbox | Runtime, SDK, quotas, signature/trust, crash isolation, adversarial tests | Phase 9; D-08; package format | `planned` |
| 11 | Battery, RAM, performance and thermal hardening | Measured battery/RAM/I/O matrix; no unmeasured defaults; budgets published | Phase 3–10; D-12; measurement rig | `planned` |
| 12 | Multi-device ports + release train/community | 7G and documented ports pass their matrix; public CI/docs/signed releases; support window | Phase 0–11; D-13; D-20 | `planned` |

Phase 0 is `partial`: the loader test and the F1 read-only firmware have
owner-reported 6G runs, and the F2 bootstate write path is host-verified, but
the F2 6G run, volume-hash comparison, production handoff and release-key path
remain.

## Competitive baseline (2026-11-05)

Rockbox is the reference competitor for the iPod Classic 6G. The comparison and
its evidence rules are in [`competitive-roadmap.md`](competitive-roadmap.md).
The short version:

- Rockbox is ahead today in codec breadth, native plugins and games, the full
  WPS theme language, voice/languages, device coverage and years of tuning.
- Rockbox on the Classic requires FAT32; HFS+ iPods are unsupported (manual
  §2.1). Helikon-OS keeps native HFS+ and refuses FAT.
- Rockbox updates by unzipping a build; no signature is documented. Helikon-OS
  has a signed manifest and a fail-closed bootstate.
- Rockbox has no HFS+ journal path and no documented data checksum/scrub.
  Helikon-OS owns the journal and plans a checksum sidecar and scrub.
- Rockbox builds its database on device and the Classic has a 1,800-byte
  metadata budget (manual Appendix B.2.3). Helikon-OS plans a host-built index
  plus a resumable device delta.
- Rockbox’s default UI is not the Helikon-OS typography; Helikon-OS targets
  anti-aliased IBM Plex Sans, 30 fps scroll and <200 ms screen open.

This is a direction, not a release claim. The 25 README goals remain the product
contract; the competitive matrix is the scope-control tool.

## Manual coverage audit (2026-11-05)

Every section of the Rockbox iPod Classic manual is now mapped in
[`competitive-roadmap.md`](competitive-roadmap.md) §12. The audit found no
critical untracked area after this pass. The remaining gaps are:

- **file browser, file operations, virtual keyboard, hotkeys, quick screen,
  shortcuts and main-menu configuration** — T-094;
- **playlist queue, playlist viewer, bookmarks and auto-resume breadth** —
  T-093;
- **display, startup/Shutdown and sleep-timer settings** — T-103;
- **first-party utilities** (clock, alarm, timer, calendar, calculator,
  reader, lyrics, metronome, pitch, stats, battery benchmark, disk tidy,
  search/sort) — T-104;
- **device health and care** — T-105;
- **library doctor and smart navigation** — T-106;
- **per-output profiles and hearing protection** — T-107.

Recording, FM and line-in are hardware `N/A` on the 6G. Native plugins, video
and game emulators are explicit declines; the sandbox is the extension path.
The new differentiators (alphabetical jump, read-only health scan, device
health, library doctor, smart playlists, per-output profiles, transactional tag
editing, settings rollback and others) are in
[`competitive-roadmap.md`](competitive-roadmap.md) §13.

## Horizon lanes (across the phase gates, not a second schedule)

These lanes run across phases 0–12. They do **not** renumber the phases, do not
authorize device work ahead of the writer gate and are not v1 commitments until
the owner accepts D-29 and the relevant decision. H4 is continuous; H1–H3 start
only after the dependency named in the row. Tasks are in
[`tasks.md`](tasks.md).

| Lane | Scope | Starts after | Tasks | Exit criterion |
|---|---|---|---|---|
| H1-A Install and recovery | Native-HFS+ install, backup/verify, dual boot, uninstall, signed update, recovery | Phase 4 + T-051 | T-062, T-063, T-064 | Non-coder installs, updates, rolls back and restores on a 6G with no data loss |
| H1-B Storage and safety | VFS semantics, journal path, adapter matrix, `fsck_hfs` gate | Phase 3 | T-065, T-081 | Crash and adapter matrices pass; zero `fsck_hfs` repairs |
| H1-C Library parity | File browser, playlists, bookmarks, resume, cuesheets, 10k index | Phase 4 | T-066, T-093, T-094 | 10k tracks browse/resume; host/device parity |
| H1-D Playback parity | Tier 1 codecs, gapless, ReplayGain, EQ, crossfade, volume limit | Phase 1 + audio HAL | T-068, T-070, T-097, T-099 | 20-track codec corpus; gapless/seek pass; no clipping |
| H1-E UI parity | Renderer, native theme, status bar, now playing, album art, display/startup settings | Phase 1 + T-042 | T-072, T-079, T-103 | 30 fps scroll, <200 ms open, panel photos, settings parity |
| H1-F Desktop and USB | Read-only LUN, HID, charge-only, macOS app MVP | Phase 9 | T-076, T-083 | Backup, import, tag, install and verify pass on macOS |
| H2-A Data leadership | Checksums, scrub, quarantine, corruption reporting, read-only health scan, device health | H1-B | T-078, T-105 | Injected corruption detected; zero false OK; health report recorded |
| H2-B Index leadership | On-device incremental builder, power-cut resume, library doctor, smart navigation | H1-C | T-067, T-106 | 10k build resumes at 25/50/75%; library doctor report clean |
| H2-C Audio leadership | All 37 audio families (including Opus), advanced DSP, DAC/output measurements, per-output profiles | H1-D | T-069, T-071, T-092, T-098, T-100, T-101, T-107 | Full audio corpus passes; published measurements; profile safety check |
| H2-D Interface leadership | WPS subset, 20-skin corpus, voice, large text, localization, first-party utilities | H1-E | T-073, T-074, T-075, T-104 | 20 skins render; voice/large-text/utility tasks pass |
| H2-E Ecosystem | Podcast/audiobook, car Tier 1/2, service mode, migration | H1-F | T-077, T-082, T-085, T-089 | 500 episodes; 100 ignition cycles; migration verifies |
| H2-F Release and support | Channels, LTS, support window, security response | H1-A | T-086 | Signed release train and published support window |
| H3-A Sandbox | App/game runtime, SDK, permissions, quotas, signing | H2-D + H2-F | T-084 | Escape, quota and crash-isolation tests pass |
| H3-B Ports | HAL contract, board description, 7G readiness | H2-F + owner hardware | T-087 | Board audit and per-target matrix; no support claim before it |
| H4 Continuous | Parity scorecard, differential harness, usability, privacy review | Now | T-060, T-061, T-090, T-091 | Matrix reviewed each phase; no stale claim |

A lane does not start because its row exists. It starts when its dependency
phase exits and the owner accepts the linked decision.

## Critical path

**Phases 2–4 are the critical path.** The native writer, journal and metadata
parity decide whether Helikon-OS is a real standalone player or a read-only
player with a macOS-assisted write mode.

- Phase 2 is a prototype on a volume you can throw away. It is **not** allowed on
  a music library.
- Phase 3 is the first phase that may touch a real library, and only after the
  power-cut matrix passes.
- Phase 4 makes the result usable for settings/playlists/resume without breaking
  macOS parity.

UI, skins, apps, podcast, car and desktop work must not consume the testing time
or memory budget the writer needs.

## Goal sequencing rule

**Goal phases 5–12 cannot begin device work before phase 3 passes.** A goal can
be designed earlier, but it cannot be declared supported or consume device
testing time ahead of the writer. This is the paper's explicit ordering; it
prevents a feature-rich read-only build from being mistaken for a safe writable
one.

## v1 competitive threshold

A v1 build does not need every Rockbox feature. It must not lose a basic job.
The gate in [`competitive-roadmap.md`](competitive-roadmap.md) §8.1 is:

1. install on an HFS+ 6G without reformatting, with a verified backup;
2. boot Helikon-OS and boot Apple firmware through the documented path;
3. play Tier 1 audio with gapless where the format allows;
4. browse by file, artist, album, genre and track; open an album in <200 ms;
5. create/save/load a playlist; bookmark and resume a track;
6. volume limit, balance, EQ, ReplayGain, crossfade and gapless work without
   clipping;
7. album art and now-playing render without tearing or clipped text;
8. USB read-only, charge-only, safe eject and 100 sleep/wake cycles pass;
9. a signed update rejects a bad board/hash/signature/truncated file; a cut
   during staging leaves the old image bootable;
10. a power cut during a write recovers to a `fsck_hfs`-clean volume;
11. uninstall restores Apple firmware and leaves no Helikon-owned file;
12. a non-coder completes install, play, update and restore with the checklist.

If any of these fails, v1 is not a Rockbox alternative regardless of the feature
count. The threshold is proposed under D-29 and is not a release gate until the
owner accepts it.

## Differentiation bets

The project does not win by matching Rockbox’s catalogue. It wins by being
measurably better at a small number of things:

| Bet | Metric | Horizon |
|---|---|---|
| Install without reformatting | Non-coder install on HFS+ 6G; no data loss; verified backup | H1-A |
| Transactional data safety | Power-cut matrix; zero `fsck_hfs` repairs; macOS mount | H1-B |
| Signed, staged updates | Bad image rejected; cut during staging leaves old image bootable | H1-A |
| Library index for 10k tracks | Host/device parity; resume at 25/50/75% | H2-B |
| Full Rockbox audio parity | All 37 audio families (36 in Appendix B.1 plus Opus) with metadata, seek/resume and a measured corpus | H2-C |
| Anti-corruption | Injected corruption detected; zero false OK | H2-A |
| Device health and care | Battery/storage/temperature/checksum status and a diagnostics bundle | H2-A |
| Library doctor | Duplicates, missing tags, corrupt files and checksum mismatches reported, not auto-deleted | H2-B |
| Alphabetical jump and saved searches | Type-ahead and smart navigation on 10k tracks | H2-B |
| First-party utilities | Clock/alarm/timer/calculator/reader/lyrics/metronome/stats without a plugin download | H2-D |
| Per-output profiles and hearing protection | Safe headphone/line-out/car profiles with measured limits | H2-C |
| Desktop app | macOS backup/tag/rate/describe/install/verify matrix | H1-F |
| Modern UI and accessibility | 30 fps, <200 ms open, voice/large text, panel photos | H1-E/H2-D |
| Sandboxed extensions | Escape, quota and crash-isolation tests | H3-A |
| Adapter safety | SD/CF/SSD/HDD boot/play/write/sleep/100-cycle matrix | H1-B |
| Evidence discipline | Every claim has a hash, device and recorded run | H4 |

A bet without a metric and a recorded run is a slogan. The competitive matrix
labels each row `match`, `exceed`, `differ` or `decline`.

## Phase gates

Each phase exits only when:

1. Its deliverable exists and is reviewable.
2. Its exit criteria are demonstrated, not asserted.
3. Device evidence is recorded in [`device-matrix.md`](device-matrix.md) if the
   phase touches hardware.
4. `fsck_hfs` zero repairs is recorded for any phase that writes metadata.
5. The next phase's dependencies are met.
6. No P0 goal is silently dropped; deferred goals are named.

A phase cannot be "mostly done". A prototype that mostly works is not a phase
exit, especially for the writer.

## Assembly order (paper §27)

The paper's module assembly order is a useful cross-check:

1. Toolchain/donor build.
2. Recovery and backup.
3. Bootloader native HFS+ read-only.
4. HAL and block layer.
5. HFS+ read-only namespace.
6. Minimal player/UI.
7. Non-journaled writer (disposable).
8. Journal/transaction engine.
9. Metadata parity/user state.
10. Database/browsing.
11. USB read-only/logs.
12. Update/migration.
13. Anti-corruption.
14. On-device database builder.
15. Skin compatibility.
16. Album/artist/podcast/car UI.
17. Desktop app/installer.
18. App/game sandbox.
19. Multi-device port.
20. Hardening.

A handoff package for each step needs an interface document, test fixture,
expected error codes, known limitations and a rollback method. A module without a
rollback method is not ready.

## What is not scheduled

- No release date.
- No promise that every README goal lands in v1.
- No 7G date; the board audit comes first.
- No Windows/Linux desktop date; D-09 is open.
- No app/game runtime date; D-08 is open.
- No battery percentage promise; D-12 defines the measurement.
- No full Rockbox plugin, theme or device parity; D-29 defines the finite
  parity set and the explicit declines. Audio formats are different: all 37
  Rockbox audio families plus Opus are a v1.x requirement (D-34). The
  first-party utility set covers the critical jobs (T-104); it is not a
  one-for-one port of every game, emulator or demo.
- No native 24/96, native DSD or “hi-res” output claim on the stock 6G; D-50
  defines the measured ceiling and resampling policy.
- No “better than Rockbox” claim until the differential evidence exists in
  [`validation.md`](validation.md) or [`device-matrix.md`](device-matrix.md).
- No v1.x date for the horizon lanes; D-30–D-51 are proposed.

## Related

- [`competitive-roadmap.md`](competitive-roadmap.md) — Rockbox parity matrix,
  differentiation bets, horizon lanes and declines
- [`audio-formats.md`](audio-formats.md) — 37-format inventory, 6G hardware
  ceiling, decoder architecture and corpus
- [`goals.md`](goals.md) — 25 goals and acceptance
- [`tasks.md`](tasks.md) — concrete current tasks and T-060–T-107
- [`validation.md`](validation.md) — release evidence
- [`decisions.md`](decisions.md) — open decisions and D-29–D-51
- Paper §23 (roadmap), §24 (validation), §27 (assembly), §28 (goals),
  Appendix I (competitive addendum)
