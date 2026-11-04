# File map — where to change what

> Purpose: map “I need to change X” to the owning file or module.
> Status: living document
> Owner: architecture; each module owner
> Last updated: 2026-11-03
> Source: repository inspection; design paper §3, §8–§19, Appendices A/B

**Early host code exists.** The HFS+ read core and a read-only diagnostic
bootloader core are real host-side code. The flashable bootloader, device
firmware, kernel, writer, build system, CI and release artifacts are still
planned.

## Current tree

```text
Helikon-OS/
  README.md                 owner-authored product goals; never edit without owner
  agents.md                 agent entry contract (canonical)
  CONTRIBUTING.md           contribution flow and device-report template
  SECURITY.md               vulnerability reporting
  LICENSE                   GPL-2.0-or-later license text
  NOTICE                    license notice and upstream attribution
  .github/ISSUE_TEMPLATE/   bug, feature and device-test issue forms
  assets/logo/              mountain reference image; owner will design the logo
  assets/branding/          owner-supplied poster; README embeds it
  assets/startup/           320×240 startup crop; not wired into boot yet
  bootloader/
    README.md               diagnostic bootloader scope, build and limits
    DFU.md                  backup, manual SHA-256, DFU install and recovery
    diagnostics.c/.h        read-only volume/catalog/file diagnostic
    report.c/.h             on-screen report formatter
    splash.c/.h             320×240 1-bit splash data and Rockbox packing
    hardware/
      README.md             donor integration notes
      helikon_diag_hw.c     ATA read callback, partition scan, splash/report
      donor-bootloader.patch  removes the firmware-loading path, wires the diag
    artifacts/
      README.md             image hash, provenance and evidence status
      bootloader-ipod6g.ipod  built diagnostic image (unproven on hardware)
      SHA256SUMS            hash for the committed image
  firmware/
    inner_image.c/.h      outer-file extent mapping + metadata guard
    write_diag.c/.h       inner-image writer sequence and result
    write_report.c/.h     write-diagnostic screen formatter
    outer_diag.c/.h       outer-volume writer sequence and safety gates
    outer_report.c/.h     outer-volume screen formatter
    hardware/
      helikon_write_diag_hw.c  ATA read/write/flush, splash, report, power-off
      helikon_outer_diag_hw.c  outer-volume ATA glue, report and power-off
      donor-write-diag.patch   separate write-diagnostic donor integration
      donor-outer-diag.patch   separate outer-volume donor integration
    WRITE_DIAG.md         separate DFU-only inner-image write diagnostic runbook
    OUTER_DIAG.md         separate DFU-only outer-volume write diagnostic runbook
    hfs/
      hfs_volume.c          HFS+ volume header/journal probe
      hfs_volume.h          read-only volume API and error taxonomy
      hfs_btree.c           generic B-tree header/node validation
      hfs_btree.h           B-tree handle and leaf/node-record API
      hfs_unicode.c/.h      UTF-16BE/UTF-8 decode, FastUnicodeCompare, HFS+ normalization
      hfs_unicode_data.c    generated fold, decomposition and CCC tables
      hfs_catalog.c/.h      catalog iteration, path lookup, inline file read
      hfs_catalog_compare.c/.h native catalog key ordering and comparison
      hfs_extents.c/.h      extents-overflow collection and file read
      hfs_journal.c/.h      Apple HFS+ journal append/replay and validation
      hfs_txn.c/.h          bounded staged-block transaction engine
      hfs_bitmap.c/.h       allocation bitmap alloc/free and header staging
      hfs_volume_write.c/.h primary/alternate header count staging
      hfs_catalog_write.c/.h empty-file catalog create/delete
      hfs_extents_write.c/.h one extents-overflow record
      hfs_btree_write.c/.h  node-level record insert/delete/split and offset tables
      hfs_btree_tree.c/.h   tree-level insert, parent/root split, rebalance, fork growth
      hfs_fork_io.c/.h      fork block staging into a transaction
  tests/
    hfsplus/
      Makefile              host test build/sanitizer targets
      test_image.c/.h       shared in-memory image fixture
      test_check.h          shared test assertion macro
      test_volume.c         volume header/journal tests
      test_btree.c          B-tree header/node tests
      test_unicode.c        UTF-16BE/UTF-8 tests
      test_catalog.c        catalog iteration/lookup tests
      test_extents.c        extents-overflow tests
      test_journal.c        journal append/replay/checksum/wrap tests
      test_txn.c            transaction commit/abort/limit tests
      test_bitmap.c         allocation bitmap alloc/free/NO_SPACE tests
      test_btree_write.c    B-tree node insert/delete/split tests
      test_fork_io.c        fork full/partial block staging tests
      test_btree_tree.c     tree insert/delete/rebalance model test
      test_btree_grow.c     B-tree fork growth test
      test_catalog_compare.c catalog key/FastUnicodeCompare ordering tests
      test_catalog_write.c  empty-file catalog create/delete test
    image/
      Makefile              disposable-image writer test build
      hfs_image_test.c      create/append/delete on a journaled HFS+ image
      hfs_crash_test.c      journal write-failure injection and recovery
      run-macos.sh          hdiutil + fsck_hfs + macOS mount driver
      run-crash-macos.sh    per-cut crash/replay/fsck driver
      write_diag_host.c     host outer/inner write-diagnostic runner
      run-write-diag-macos.sh  outer/inner image fsck integration test
      outer_diag_host.c     host outer-volume diagnostic runner and dirty-journal fixture
      run-outer-diag-macos.sh  outer-volume sequence, dirty replay and safety test
    bootloader/
      Makefile              bootloader host test target
      test_diag.c           diagnostic core/splash tests
      test_inner_image.c    inner-image extent mapping/metadata guard tests
  docs/
    README.md               documentation index and reading order
    state.md                current phase and blockers
    invariants.md           safety invariants
    decisions.md            decisions and open decisions
    tasks.md                task queue
    verification.md         proof checklist
    file-map.md             this file
    architecture.md         boot chain, layers, boundaries
    ui.md                   default theme typography, anti-aliasing and font rules
    outer-volume-test.md    real outer-volume write protocol and backup gate
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
  build-diagnostic-bootloader.sh  pinned donor export, patch, Thumb build, checks
  build-write-diagnostic-bootloader.sh  separate inner-image write-diagnostic build
  build-outer-diagnostic-bootloader.sh  separate outer-volume write-diagnostic build
  make-write-diag-image.sh  create the disposable inner HFS+ image
  make-outer-opt-in.sh      create the outer-volume opt-in file
  install-write-diagnostic.sh  send the inner-image DFU with hash check
  install-outer-diagnostic.sh  send the outer-volume DFU with hash check
  cleanup-outer-test.sh     inspect/remove a marked outer test folder safely
  uninstall-bootloader.sh   send the 6G bootloader uninstaller DFU
  gen-hfs-unicode-table.py  generate the HFS+ fold/decomposition tables
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
| `.github/ISSUE_TEMPLATE/` | Bug/feature/device issue forms | docs lead | `CONTRIBUTING.md`, `docs/verification.md` | YAML parse + field check | No secrets, backups, disk images, serial numbers or personal data |
| `assets/logo/` | Owner-supplied mountain reference; owner will design the logo | owner/branding | `assets/logo/README.md` | image opens, no generated assets | Do not replace or redistribute the reference image; source/licence is an open question |
| `assets/branding/` | Owner-supplied Helikon-OS poster and branding notes | owner/branding | `assets/branding/README.md` | image opens, README embed resolves | Do not replace the poster without owner approval |
| `assets/startup/` | 320×240 startup crop for the iPod screen | boot/UI | `assets/branding/README.md` | exact 320×240, no writes in future display path | Not wired into firmware yet; do not claim it displays |
| `docs/` | Agent documentation | docs lead | `docs/conventions.md` | `git diff --check`, link check | One canonical file per topic; no duplicate `agent.md`/`decision-log.md` |
| `bootloader/` | Diagnostic core and splash exist; hardware entry/manifest still planned | boot lead | `bootloader/README.md`, `docs/architecture.md`, paper §5, §16 | 7 host tests; device tests planned | No filesystem writes; no overlay; no `--single`; no write modules linked; not flashable yet |
| `firmware/hal/` | S5L8702 clocks, GPIO, PMU, ATA, LCD, codec | hardware lead | paper §6–§7, `docs/architecture.md` | register-level bring-up, sleep/wake | No filesystem, UI or policy; no unmeasured overclock/undervolt |
| `firmware/block/` | Sector I/O, flush, power state | storage lead | paper §7, `docs/architecture.md` | read/write/flush, 512/4096, error injection | No paths/files/caches; a flush error must propagate |
| `firmware/hfs/` | HFS+ read core: volume, B-tree, catalog, Unicode and extents implemented | HFS+ lead | paper §8, §10.4, `docs/hfsplus-write-engine.md` | 34 host tests now; unit/fuzz/malformed/macOS differential planned | Read-only; no write path; never hide unsupported entries |
| `firmware/hfs/write/` | Catalog/bitmap/extents mutation | HFS+ lead | `docs/hfsplus-write-engine.md`, invariants I-03–I-15 | model/property/crash tests, `fsck_hfs` | No journal format, UI or boot policy; no direct writes outside a transaction |
| `firmware/journal/` | Journal append, commit, replay, barriers | journal lead | paper §9.3–§9.4, `docs/hfsplus-write-engine.md` | crash injection, idempotent replay | No product policy/UI; never delete/disable/zero the journal |
| `firmware/vfs/` | Lookup, handles, capabilities, path rules | VFS lead | paper §10, `docs/architecture.md` | path/name tests, capability tests | No hardware registers; no overlay branch; no errno guessing |
| `firmware/db/` | Immutable metadata/index reader | database lead | paper §12, D-07 | 10k-track browse, stale-entry fallback | No playback state; database is a cache, not truth |
| `firmware/player/` | Buffer/decode/output state machine | audio lead | paper §13, §28.3.8 | audio matrix, gapless, seek | No filesystem internals; no unmeasured audiophile claims |
| `firmware/ui/` | Screens, input, rendering | UI lead | paper §14, §28.3.3, `docs/ui.md` | task tests, frame trace, typography mockup | No storage policy; default UI must survive skin failure |
| `firmware/skin/` | Documented WPS subset | UI lead | paper §28.3.4, D-10/D-15 | 20-skin corpus, hostile skin tests | No raw storage/playback policy; no full WPS engine; no unlicensed assets |
| `firmware/sandbox/` | App/game runtime, quotas, host API | runtime lead | paper §15, §28.3.9–10, D-08 | escape/quota/crash tests | No raw block/NOR/PMU/GPIO; no legacy plugin binary compatibility |
| `firmware/usb/` | Read-only LUN, descriptors, optional service mode | USB lead | paper §16, D-09 | read-only mount, descriptor/error tests | No filesystem writes; no writable mass-storage LUN; RAM-only relabel |
| `firmware/diagnostics/` | Events, counters, recovery | diagnostics lead | paper §11.2, `docs/failures.md` | log/export tests | Do not write logs on a failing filesystem; bounded RAM ring |
| `tools/build-diagnostic-bootloader.sh` | Render the diagnostic image from a pinned donor checkout | boot/release lead | D-21, `bootloader/README.md` | Thumb build, `MOVE_AREA`, hash, `mks5lboot` dry-run | Uses a clean `git archive`; never modifies the donor checkout; image is unproven |
| `tools/hfsplus/` | Host inspector, fixtures, tests | test lead | paper §8, §17, donor `tools/hfsplus` | host tests, sanitizer | Donor code is reference; do not modify donor; no device claims |
| `tools/db/` | Host database builder | database lead | paper §12, §28.4.1 | 10k-track parity | No on-device claim without device evidence |
| `tools/migrate/` | Donor overlay exporter | migration lead | paper §22 | fixture/hash/verify tests | Host-side only; never a runtime overlay; never overwrite silently |
| `tools/installer/` | Guided install, backup, model checks | release lead | paper §28.4.3, `docs/release.md` | clean-device install, recovery | No `--single`; no format as first response; backup first |
| `tools/desktop/` | macOS-first desktop app | host lead | paper §28.4.2, D-09 | task matrix, failed-transfer consistency | No silent overwrite; host scrobbling opt-in; no secrets |
| `tests/` | Host volume/B-tree fixtures now; fuzz/model/power-loss/device planned | test lead | `docs/testing.md`, `docs/validation.md` | all layers | No proprietary themes/art/fonts; no personal data; no disk images in repo |
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
