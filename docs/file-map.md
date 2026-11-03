# File map — where to change what

> Purpose: map “I need to change X” to the owning file or module.
> Status: living document
> Owner: architecture; each module owner
> Last updated: 2026-11-03
> Source: repository inspection; design paper §3, §8–§19, Appendices A/B

**Docs-only today.** This repository contains documentation. The planned
directories below do not exist yet. Do not create firmware files, build systems
or release assets as part of a documentation change.

## Current tree

```text
Helikon-OS/
  README.md                 owner-authored product goals; never edit without owner
  agents.md                 agent entry contract (canonical)
  CONTRIBUTING.md           contribution flow and device-report template
  SECURITY.md               vulnerability reporting
  LICENSE                   GPL-2.0-or-later license text
  NOTICE                    license notice and upstream attribution
  CODE_OF_CONDUCT.md        Contributor Covenant 2.1
  .github/ISSUE_TEMPLATE/   bug, feature and device-test issue forms
  assets/logo/              dot-matrix logo text assets
  docs/
    README.md               documentation index and reading order
    state.md                current phase and blockers
    invariants.md           safety invariants
    decisions.md            decisions and open decisions
    tasks.md                task queue
    verification.md         proof checklist
    file-map.md             this file
    architecture.md         boot chain, layers, boundaries
    failures.md             error taxonomy and decision trees
    device-matrix.md        hardware evidence
    handoff.md              latest handoff
    conventions.md          style and process
    glossary.md             terms
    testing.md              test layers
    validation.md           release matrix
    goals.md                README goals
    roadmap.md              phases 0–12
    hfsplus-write-engine.md writer deep dive
    release.md              release process stub
    security.md             agent security rules
    faq.md                  user FAQ
    design-paper.md         full source copy with provenance
    logo.md                 provisional dot-matrix logo design
    adr/                    long-form decision records
    log/                    dated work logs
    prompts/                reusable task prompts
```

## Planned tree

```text
bootloader/                 rescue bootloader; native HFS+ read-only
firmware/
  hal/                      S5L8702 board primitives; no filesystem policy
  block/                    block device, flush, power state
  hfs/                      reader: volume, btree, catalog, extents, unicode
  hfs/write/                catalog/bitmap/extents mutation
  journal/                  journal records, commit, replay, barriers
  vfs/                      fs_ops, lookup, handles, capabilities
  db/                       immutable database reader; host-built first
  player/                   buffer/decode/output state machine
  ui/                       retained-mode screens and renderer
  skin/                     documented Rockbox WPS subset
  sandbox/                  app/game runtime, quotas, host API
  usb/                      read-only LUN, optional service mode
  diagnostics/              events, counters, recovery screens
tools/
  hfsplus/                  host inspector, fixtures, tests
  db/                       host database builder
  migrate/                  donor overlay exporter (host-side, read-only source)
  installer/                guided install, backup, verification
  desktop/                  macOS-first desktop app (separate product)
tests/
  fixtures/                 deterministic HFS+ images and trees
  fuzz/                     parser and journal fuzz targets
  model/                    B-tree/allocation model tests
  powerloss/                crash injection harness
  device/                   device test scripts and evidence capture
```

Names are design targets, not existing files. Update this map when a real tree
is created; do not let it drift.

## Area ownership

| Area | Purpose | Owner | Read before edit | Tests | Never edit / notes |
|---|---|---|---|---|---|
| Root `README.md` | Owner product goals | owner | — | — | Never modify; propose changes in a PR description or issue |
| `agents.md` | Entry contract | docs lead | `docs/state.md`, `docs/tasks.md` | link/contradiction audit | Keep under ~300 lines; do not turn into a manual |
| `LICENSE`, `NOTICE` | Project license and attribution | owner/legal | `docs/decisions.md` D-18 | license text check | Do not edit the GPLv2 text; change the notice only with owner approval |
| `CODE_OF_CONDUCT.md` | Community conduct | owner | `docs/decisions.md` D-19 | link/contact check | Enforcement contact must stay current; do not invent an email |
| `.github/ISSUE_TEMPLATE/` | Bug/feature/device issue forms | docs lead | `CONTRIBUTING.md`, `docs/verification.md` | YAML parse + field check | No secrets, backups, disk images, serial numbers or personal data |
| `assets/logo/` | Mountain/iPod dot-matrix lockup and mark text assets | docs lead/branding | `docs/logo.md` | dot alignment, monochrome check | No generated images; do not add Apple trade dress or unlicensed assets |
| `docs/` | Agent documentation | docs lead | `docs/conventions.md` | `git diff --check`, link check | One canonical file per topic; no duplicate `agent.md`/`decision-log.md` |
| `bootloader/` | Rescue loader; native HFS+ read-only; manifest verify | boot lead | `docs/architecture.md`, paper §5, §16 | boot, manifest, recovery, no-write hash | No filesystem writes; no overlay; no `--single`; no write modules linked |
| `firmware/hal/` | S5L8702 clocks, GPIO, PMU, ATA, LCD, codec | hardware lead | paper §6–§7, `docs/architecture.md` | register-level bring-up, sleep/wake | No filesystem, UI or policy; no unmeasured overclock/undervolt |
| `firmware/block/` | Sector I/O, flush, power state | storage lead | paper §7, `docs/architecture.md` | read/write/flush, 512/4096, error injection | No paths/files/caches; a flush error must propagate |
| `firmware/hfs/` | HFS+ read core: volume, B-tree, catalog, extents, Unicode | HFS+ lead | paper §8, §10.4, `docs/hfsplus-write-engine.md` | unit, fuzz, malformed fixtures, macOS differential | Read-only; no write path; never hide unsupported entries |
| `firmware/hfs/write/` | Catalog/bitmap/extents mutation | HFS+ lead | `docs/hfsplus-write-engine.md`, invariants I-03–I-15 | model/property/crash tests, `fsck_hfs` | No journal format, UI or boot policy; no direct writes outside a transaction |
| `firmware/journal/` | Journal append, commit, replay, barriers | journal lead | paper §9.3–§9.4, `docs/hfsplus-write-engine.md` | crash injection, idempotent replay | No product policy/UI; never delete/disable/zero the journal |
| `firmware/vfs/` | Lookup, handles, capabilities, path rules | VFS lead | paper §10, `docs/architecture.md` | path/name tests, capability tests | No hardware registers; no overlay branch; no errno guessing |
| `firmware/db/` | Immutable metadata/index reader | database lead | paper §12, D-07 | 10k-track browse, stale-entry fallback | No playback state; database is a cache, not truth |
| `firmware/player/` | Buffer/decode/output state machine | audio lead | paper §13, §28.3.8 | audio matrix, gapless, seek | No filesystem internals; no unmeasured audiophile claims |
| `firmware/ui/` | Screens, input, rendering | UI lead | paper §14, §28.3.3 | task tests, frame trace | No storage policy; default UI must survive skin failure |
| `firmware/skin/` | Documented WPS subset | UI lead | paper §28.3.4, D-10/D-15 | 20-skin corpus, hostile skin tests | No raw storage/playback policy; no full WPS engine; no unlicensed assets |
| `firmware/sandbox/` | App/game runtime, quotas, host API | runtime lead | paper §15, §28.3.9–10, D-08 | escape/quota/crash tests | No raw block/NOR/PMU/GPIO; no legacy plugin binary compatibility |
| `firmware/usb/` | Read-only LUN, descriptors, optional service mode | USB lead | paper §16, D-09 | read-only mount, descriptor/error tests | No filesystem writes; no writable mass-storage LUN; RAM-only relabel |
| `firmware/diagnostics/` | Events, counters, recovery | diagnostics lead | paper §11.2, `docs/failures.md` | log/export tests | Do not write logs on a failing filesystem; bounded RAM ring |
| `tools/hfsplus/` | Host inspector, fixtures, tests | test lead | paper §8, §17, donor `tools/hfsplus` | host tests, sanitizer | Donor code is reference; do not modify donor; no device claims |
| `tools/db/` | Host database builder | database lead | paper §12, §28.4.1 | 10k-track parity | No on-device claim without device evidence |
| `tools/migrate/` | Donor overlay exporter | migration lead | paper §22 | fixture/hash/verify tests | Host-side only; never a runtime overlay; never overwrite silently |
| `tools/installer/` | Guided install, backup, model checks | release lead | paper §28.4.3, `docs/release.md` | clean-device install, recovery | No `--single`; no format as first response; backup first |
| `tools/desktop/` | macOS-first desktop app | host lead | paper §28.4.2, D-09 | task matrix, failed-transfer consistency | No silent overwrite; host scrobbling opt-in; no secrets |
| `tests/` | Fixtures, fuzz, model, power-loss, device | test lead | `docs/testing.md`, `docs/validation.md` | all layers | No proprietary themes/art/fonts; no personal data; no disk images in repo |
| `docs/adr/` | Long-form decisions | docs lead | `docs/decisions.md` | review | Only for decisions too large for `decisions.md`; do not duplicate |
| `docs/log/` | Dated work logs | docs lead | `docs/log/TEMPLATE.md` | review | One file per active day/session; no secrets |
| `docs/prompts/` | Reusable task prompts | docs lead | `docs/prompts/README.md` | review | Prompts link to docs; they are not a second spec |

## Donor files referenced by the paper

The donor repository is **read-only source material**. Do not modify it, copy
its overlay engine into Helikon-OS, or treat its host tests as Helikon-OS
validation. The paper's verdict tags: **K** keep, **M** modify, **R** replace,
**X** exclude, **D** defer.

| Donor path | Paper role | Verdict | Helikon-OS destination / note |
|---|---|---|---|
| `firmware/hfsplus/hfsplus_ro.c` (1,274) | HFS+ reader/validators | K/M | Split into `hfs_volume`, `hfs_btree`, `hfs_catalog`, `hfs_extents`, `hfs_unicode`, `hfs_read` |
| `firmware/hfsplus/hfsplus_ro.h` (163) | Reader contract | K/M | Narrow public surface |
| `firmware/hfsplus/hfsplus_partition.c` (296) | APM/MBR/GPT/raw scanner | K | `firmware/block` / partition scanner; never write partition table |
| `firmware/hfsplus/hfsplus_io.c` (220) | Sector I/O | K/M | New block API |
| `firmware/hfsplus/hfsplus_namespace.c` (686) | Native + overlay namespace | R | Native-only `hfs_lookup`/`hfs_iterate`; no overlay merge |
| `firmware/hfsplus/hfsplus_fat.c` (377) | FAT-shaped adapter | M | Thin `fs_ops` adapter; no FAT mount |
| `firmware/hfsplus/hfsplus_mount.c` (374) | Mount path | M | Native-only bootloader; transaction-aware firmware mount |
| `firmware/hfsplus/hfsplus_diag.c` (161) | Stage/reason diagnostics | K/M | Event log and counters; new stages for journal/txn |
| `firmware/hfsplus/hfsplus_time.c` (40) | Timestamp conversion | K | Keep |
| `firmware/hfsplus/hfsplus_overlay_format.c` (351) | Donor v3 overlay parser | K/M | Host exporter only; no runtime writer |
| `firmware/hfsplus/hfsplus_overlay_validate.c` (225) | Donor overlay validation | K/M | Host exporter fixtures only |
| `firmware/hfsplus/hfsplus_overlay_load.c` (256) | Runtime overlay loader | X | Do not carry into Helikon-OS |
| `firmware/hfsplus/hfsplus_overlay_alloc.c` (399) | Overlay allocator | X | Do not carry into Helikon-OS |
| `firmware/hfsplus/hfsplus_overlay_store.c` (367) | Overlay entry store | X | Do not carry into Helikon-OS |
| `firmware/target/arm/s5l8702/ipod6g/storage_ata-6g.c` (1,406) | ATA transport/power | K/M | Keep transport; new explicit power/flush policy |
| `firmware/target/arm/s5l8702/ipod6g/pmu-6g.c` (492) | PMU/charger/backlight | K | Keep register sequences; invalidate cached state |
| `firmware/target/arm/s5l8702/ipod6g/power-6g.c` (237) | Power/charger | K | Keep; add safe power-off path |
| `firmware/target/arm/s5l8702/ipod6g/lcd-6g.c` (266) | LCD init | K | Keep |
| `firmware/target/arm/s5l8702/ipod6g/audio-6g.c` (140) | I2S/audio routing | K | Keep |
| `firmware/target/arm/s5l8702/ipod6g/cscodec-6g.c` (66) | Codec control | K | Keep |
| `firmware/target/arm/s5l8702/ipod6g/button-target.h` (76) | Key map | K | Keep |
| `bootloader/ipod-s5l87xx.c` (1,016) | Boot bring-up and load | M | Keep bring-up/DFU; manifest verify; no write path |
| `utils/mks5lboot/` (3,382) | DFU/NOR installer | K/M | Keep DFU knowledge; reject `--single`; harden verification |
| `tools/hfsplus/` (~2,958) | Host inspector/tests | K | Keep and extend; donor reference only until ported |
| `tools/rockpod/` (~1,036) | Host DB/art/overlay tools | M/R | New schema/exporter; host-side only |
| `tools/database/` (137) | Legacy DB builder | R | New builder |
| `apps/plugins/` (946,251) | Rockbox plugin tree | X | No plugin tree; sandboxed apps later |
| `apps/tagcache.c` (5,756) / `tagtree.c` (2,975) | Legacy database/browser | R | New immutable DB and browser |
| `apps/playback.c` (4,363) | Legacy playback state machine | R | New player state machine |
| `apps/playlist.c` (4,154) | Legacy playlist storage | R | Versioned playlist file |
| `firmware/common/fat.c` (3,187) / `file.c` (1,433) | FAT-shaped VFS | R | New block/VFS |
| `apps/gui/` (20,127) | Rockbox GUI | M/R | New retained UI; WPS subset only |
| `wps/` (80,427) | WPS/skins | X/ref | Compatibility corpus only; no full engine |
| `themes/` (55,636) | Theme assets | X/select | One default theme; corpus reference; licences vary |
| `fonts/`, `icons/` | Assets | K/select | Two or three fonts; one icon set; check licences |
| `lib/rbcodec/` (350,947) | Codecs/DSP | K/M | Select codecs/DSP; drop unused |

## Donor-only files and rules

- `/.rockpod-rw` is donor user state. Never delete, truncate, reformat or copy it
  into this repository. It is migration material only.
- Donor `agents.md` and `shipping.md` describe the donor's overlay release.
  They are not Helikon-OS rules.
- Donor release notes, device reports and battery claims are not Helikon-OS
  evidence.
- Do not copy proprietary themes, artwork, fonts or metadata. Reference the
  corpus and record licences before any redistribution.

## Change rule

If you cannot name the owning area and the read-before-edit docs, do not change
the file. If the change crosses a module boundary, update
[`architecture.md`](architecture.md) or record a decision first.
