# Goals — the 25 README goals and acceptance

> Purpose: preserve every owner-authored README goal and tie it to evidence.
> Status: living document
> Owner: project owner; docs lead maintains the mapping
> Last updated: 2026-11-03
> Source: root `README.md`; design paper §28.1 and §28.6

The root `README.md` is the source of the goals and is not modified. The paper
is the technical design response. If a goal and the paper appear to conflict,
the conflict is recorded in [`decisions.md`](decisions.md); it is not silently
dropped.

## Scorecard summary

- **Goals met:** 0 of 25.
- **Implemented firmware goals:** 0.
- **Planned:** 25.
- **Unproven claims:** none should be published; any claim without a
  `validation.md` row is a wish.
- **Current stage:** design/documentation, pre-phase 0.

A goal is not met because a screen exists, a demo boots, or a host test passes.
It is met when the acceptance evidence in §28.6 is recorded on a device and the
owner accepts it.

## The 25 goals (verbatim)

| # | Goal (verbatim from README) | Priority | Target | Acceptance evidence | Status |
|---:|---|---|---|---|---|
| 1 | Improved Battery Life over Rockbox | P0 | v1 hardening | Playback hours + sleep drain vs recorded Rockbox baseline; 3 runs; same media/settings; ≥10% is a goal, not a promise | `planned` |
| 2 | Cleaner interfaces | P0 | v1 | Task-based usability test; no dead ends; consistent navigation/focus/error states | `planned` |
| 3 | The ability to use Rockbox skins on Helikon-OS | P1 | v1.x | Documented WPS subset; 20-skin corpus; tag report; frame/memory caps; fallback | `planned` |
| 4 | Improved RAM Usage | P0 | v1 | Peak watermark on 10k tracks within §28.3.2 budget; 72-hour soak; no OOM | `planned` |
| 5 | Cleaner Graphics | P0 | v1 | 30 fps list scroll; <200 ms screen open; no tearing/clipped text; visual review | `planned` |
| 6 | Improved support for iFlash/other adapters | P0 | v1 | SD/CF/SSD/HDD boot/play/write/sleep matrix; known-bad card reported | `planned` |
| 7 | Improved database building on iPods | P1 | v1.x | 10k tracks; power-cut resume at 25/50/75%; host/device parity; playback unaffected | `planned` |
| 8 | Keeping this opensource and community-friendly | P0 | continuous | Public docs/roadmap; GPL-2.0-or-later and Contributor Covenant 2.1 adopted; CI, signed builds and governance still open | `in progress` |
| 9 | Regular firmware/bootloader updates | P0 | v1 | Signed update/rollback; bad board/version/hash rejected; power-cut staging safe | `planned` |
| 10 | Clearer installation procedures | P0 | v1 | Non-coder completes install, first boot, playback and restore with checklist | `planned` |
| 11 | Make everything easier to use | P0 | v1 | Task completion and error-recovery test; safe defaults; explicit status | `planned` |
| 12 | HFS+ native without an overlay or FAT support | P0 | v1 | `fsck_hfs` clean; no FAT symbols; `NOT_HFS` path; no `/.rockpod-rw`; macOS sees native files | `planned` |
| 13 | Active anti-corruption protection for files | P0 | v1/v1.x | Injected corruption detected/quarantined; no false OK; scrub charging/idle; no self-healing claim | `planned` |
| 14 | Keeping the code lightweight (wherever possible) | P0 | continuous | Size/map/RAM reports; minimal vs full build; no legacy app tree | `planned` |
| 15 | (Custom) game support | P2 | v2 | Playable sandboxed demo; escape/crash tests; quotas; no Rockbox binary compatibility | `planned` |
| 16 | Improved DAC support | P1 | v1.x | Internal CS42L55 path; line-out measurements; correct rates; no external USB DAC on 6G | `planned` |
| 17 | Improved read/write speeds (if possible) | P1 | v1.x | Sequential/random benchmarks; no read regression; measured journal cost | `planned` |
| 18 | (Custom) Application support | P2 | v2 | Sandbox install/launch/suspend/resume/uninstall; permission denial; update preserves data | `planned` |
| 19 | Improved podcast support | P1 | v1.x | 500 episodes; resume/speed/chapters; desktop sync round-trip; no network on device | `planned` |
| 20 | A desktop app for: file tagging, album descriptions/ratings, last.fm scrobbling, easy install, improved read/write capabilities, app/game/skins installations etc. | P1 | v1.x | macOS task matrix: import/tag/rate/describe/scrobble/install/backup; failed transfer consistent; Windows/Linux later | `planned` |
| 21 | Improved album view (taking inspiration from Roonserver) | P1 | v1.x | 10k albums; <200 ms open; 30 fps scroll; local data, no network | `planned` |
| 22 | Improved artist view (taking inspiration from Roonserver) | P1 | v1.x | 10k artists; <200 ms open; 30 fps scroll; local data, no network | `planned` |
| 23 | Improved car/receiver playback | P1 | v1.x | 100 ignition cycles; resume/charge/no pops; receiver matrix; Tier 2 optional | `planned` |
| 24 | Support a wide range of iPod devices and maybe other MP3 players | P2 | v2 | 6G first; 7G after board audit; per-target boot/play/charge/sleep/write matrix | `planned` |
| 25 | A modern feasible alternative to Rockbox/Rockpod or others. | P0 | continuous | Release audit against paper; documented, maintained, smaller product; no feature-copy claim | `planned` |

## Goal details and conflicts

- **Goal 3 (skins):** the paper promises a documented subset, not full WPS
  compatibility. See C-02 and D-10. Do not advertise "all Rockbox skins work".
- **Goal 7 (database):** host builder first; on-device incremental builder is
  v1.x and D-07 is unresolved. Do not claim on-device v1.
- **Goal 12 (HFS+):** this is an accepted design decision (B-02), but there is no
  implementation. The goal is `planned`, not `implemented`.
- **Goal 13 (anti-corruption):** detection/quarantine, not self-healing. Repair
  comes from a backup or known-good copy.
- **Goal 15/18 (games/apps):** new sandboxed runtime, not Rockbox plugin/game
  binary compatibility. See C-05 and D-08.
- **Goal 16 (DAC):** internal CS42L55 path; external USB DAC is out of scope on
  the 6G. See C-04.
- **Goal 20 (desktop app):** macOS first; Windows/Linux depend on D-09 service
  mode and are not claimed.
- **Goal 24 (devices):** 6G reference, 7G next, other devices are ports; no v1
  claim for other players. See C-06.
- **Goal 8 (community):** license (GPL-2.0-or-later) and code of conduct
  (Contributor Covenant 2.1) are adopted. CI, signed builds, governance and
  signing-key ownership are still open (D-20).

## Not promised

The paper explicitly does not promise:

- 100% Rockbox WPS compatibility;
- Rockbox plugin or game binary compatibility;
- FAT32 support;
- self-healing data repair without a backup;
- external USB DAC support on the 6G;
- every iPod model or other MP3 player at launch;
- battery improvement without a recorded measurement.

These are scope boundaries, not missing features to be silently added. A change
to one is an owner decision, not an agent guess.

## How to change a goal status

1. Find the metric in [`validation.md`](validation.md).
2. Confirm a recorded device run with firmware hash, adapter and evidence.
3. Confirm `fsck_hfs` zero repairs where applicable.
4. Update the status to `implemented` only if the evidence is complete.
5. Update the scorecard and `state.md`.
6. If the goal is deferred, say so in `roadmap.md` and release notes; never drop
   it silently.

## Related

- [`design-paper.md`](design-paper.md) §28 (goal map, scorecard)
- [`roadmap.md`](roadmap.md) — phase sequencing
- [`validation.md`](validation.md) — release matrix
- [`decisions.md`](decisions.md) — open decisions and conflicts
