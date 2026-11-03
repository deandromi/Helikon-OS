# Roadmap — phases 0–12

> Purpose: sequence the work and make every phase's exit criterion explicit.
> Status: living document
> Owner: project owner; phase leads
> Last updated: 2026-11-03
> Source: design paper §23, §24, §27, §28

**No phase is complete.** This is a design-stage roadmap, not a schedule
promise. Durations are intentionally omitted; the paper says months to years, not
a beta cycle. A phase starts only when its dependencies and exit criteria are
agreed.

## Phase table

| Phase | Deliverable | Exit criteria | Dependencies | Status |
|---:|---|---|---|---|
| 0 | Rescue bootloader + native HFS+ read-only firmware skeleton | Boots, loads a verified image, Apple NOR path intact, recovery menu, zero filesystem writes; volume hash unchanged | Toolchain/donor build; backup/recovery; D-01/D-02 manifest/boot-state design | `planned` |
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

## Related

- [`goals.md`](goals.md) — 25 goals and acceptance
- [`tasks.md`](tasks.md) — concrete current tasks
- [`validation.md`](validation.md) — release evidence
- [`decisions.md`](decisions.md) — open decisions
- Paper §23 (roadmap), §24 (validation), §27 (assembly)
