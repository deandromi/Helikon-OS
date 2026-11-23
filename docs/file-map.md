# File map — where to change what

> Purpose: map “I need to change X” to the owning file or module.
> Status: living document
> Owner: architecture; each module owner
> Last updated: 2026-11-11
> Source: repository inspection; design paper §3, §8–§19, Appendices A/B;
> [`competitive-roadmap.md`](competitive-roadmap.md); [`disk-mode.md`](disk-mode.md)

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
  assets/startup/           320×240 old startup crop; provenance only
  assets/bootloader/        owner Alpha splash source, PNG variants and README
  assets/fonts/             licensed Plex/Manrope subsets; external Benguiat (not committed)
  assets/ui/                host 320×240 UI mockups (typography, home concepts, album wall)
  bootloader/
    README.md               diagnostic bootloader scope, build and limits
    DFU.md                  backup, manual SHA-256, DFU install and recovery
    LOADER.md               manifest/boot-state/entry-ABI format, loader flow and limits
    LOADER_TEST.md          DFU-only loader test-image runbook
    diagnostics.c/.h        read-only volume/catalog/file diagnostic
    report.c/.h             on-screen report formatter
    splash.c/.h             320×240 1-bit splash for the write diagnostics
    splash_color.c/.h       generated final 4bpp Alpha splash and RGB565 renderer
    sha256.c/.h             SHA-256 for manifest/image verification
    manifest.c/.h           signed manifest parser and split verifier
    bootstate.c/.h          two-slot boot-state parser/selector
    boot_info_page.c/.h     D-28 boot-info page writer (ADR-0004 layout)
    ed25519.c/.h            verification-only TweetNaCl derivative
    keys.c/.h               public-key table and signature-hook adapter
    certificate.c/.h        root-signed release-key certificate, rotation and
                            revocation verification
    firmware_loader.c/.h    read-only manifest/image/bootstate loader
    hfs_loader.c/.h         HFS+ file-read adapter for the loader
    loader_error.c/.h       loader error taxonomy
    hardware/
      README.md             donor integration notes
      helikon_diag_hw.c     ATA read callback, partition scan, splash/report
      donor-bootloader.patch  removes the firmware-loading path, wires the diag
      helikon_loader_hw.c   loader test-image ATA/HFS/splash/jump glue
      helikon_build_keys.h  generated public-key table declaration
      donor-loader.patch    separate loader test-image donor integration
    artifacts/
      README.md             image hash, provenance and evidence status
      bootloader-ipod6g.ipod  built diagnostic image (unproven on hardware)
      SHA256SUMS            hash for the committed image
  firmware/
    Makefile              F1/F2 build: ELF, raw image, manifest, bootstate, dev signing
    boot/                 _start, vectors, linker script, boot-info, bootstate, panic, stack
    lib/                  freestanding mem/string/CRC/SHA-256/endian/ring/pool/fixed/format
    board/6g/             BSP constants, board description, runtime wiring, write gate
    hal/6g/               clock, PMU, backlight, LCD, ATA read/write/flush, buttons, CPU, CS42L55/I2S/DMA
    block/                block device with read-only gate, bounce, APM/MBR/raw scan
    storage/              mount the music volume: partition scan, HFS+ probe,
                          VFS mount and the catalog, read-only (mount.c/.h),
                          and the ATA driver as a block device (ata_block.c/.h)
    library/              HDB path index, playlist resolution into the queue, and
                          the storage-backed music source (volume + catalog +
                          index + queue + player, next/previous)
    diag/                 stage machine, RAM trace, crash record, RGB565 screen
    player/               bounded WAV parser, PCM playback state (buffer and
                          pull-source opens), the VFS media adapter
                          (vfs_media.c) that feeds a codec source from an open
                          file, the read-ahead window (read_ahead.c) that
                          turns small decoder pulls into few large reads, and
                          the play queue (queue.c) behind next/previous,
                          playlist import (playlist.c/.h): M3U/M3U8/PLS text,
                          streaming entries, path resolution, bounded,
                          and cue sheets (cuesheet.c/.h): FILE/TRACK/INDEX,
                          mm:ss:ff in 75ths, pregap and per-file tracks
    hfs/hfs_block_adapter.c/.h  hfs read/write/flush over the block device
    hfs/hfs_file.c/.h     one-file read helper (lookup + inline/overflow)
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
      hfs_unicode.c/.h      UTF-16BE/UTF-8 decode, UTF-8/UTF-16BE encode,
                            FastUnicodeCompare, HFS+ normalization
      hfs_unicode_data.c    generated fold, decomposition and CCC tables
      hfs_catalog.c/.h      catalog iteration, path lookup, inline file read
      hfs_catalog_compare.c/.h native catalog key ordering and comparison
      hfs_extents.c/.h      extents-overflow collection and file read
      hfs_journal.c/.h      Apple HFS+ journal append/replay and validation
      hfs_txn.c/.h          bounded staged-block transaction engine
      hfs_bitmap.c/.h       allocation bitmap alloc/free and header staging
      hfs_check.c/.h        read-only structural check (T-020)
      hfs_volume_write.c/.h primary/alternate header count staging
      hfs_catalog_write.c/.h catalog create/delete and in-place overwrite
      hfs_extents_write.c/.h one extents-overflow record
      hfs_btree_write.c/.h  node-level record insert/delete/split and offset tables
      hfs_btree_tree.c/.h   tree-level insert, parent/root split, rebalance, fork growth
      hfs_fork_io.c/.h      fork block staging into a transaction (inline or
                            a logical extent map)
    vfs/
      vfs.h               read-only VFS API; mount, entries, handles, kinds
      vfs.c               mount/path/lookup/iterate/read/seek implementation
  tests/
    firmware/
      Makefile              host tests for the F1/F2 freestanding modules
      test_lib.c            mem/string/endian/CRC/fixed/format
      test_boot_info.c      D-28 page validation and fallback
      test_bootstate.c      bootstate parse, checksum and TRYING->GOOD promotion
      test_block.c          block adapter, bounce reads/writes and partition scan
      test_bounce_props.c   4000-range property test over the byte-range layer:
                            read-back, refusals, and nothing outside a write
      test_check.h          CHECK/CHECK_EQ counting failures; the report turns
                            them into the exit status and an atexit guard fails a
                            test that forgets to report
      test_stages.c         stage order/timeout/failure including WRITE
      test_ring.c           wrap/overwrite
      test_pool.c           alloc/free/exhaustion/double free
      test_screen.c         text pixels, clipping and present
      test_trace.c          bounded trace ring
      test_hfs_file.c       one-path HFS+ read, bounds and missing file
    image/
      f2_host.c             F2 bootstate promotion through the HFS+ writer
      run-f2-macos.sh       disposable-image F2 promotion, fsck and loader GOOD
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
      test_fork_io.c        fork full/partial block staging tests, inline and
                            multi-extent map
      test_btree_tree.c     tree insert/delete/rebalance model test
      test_btree_grow.c     B-tree fork growth test: a tight node map widens, a
                            full map or a full extent list answers NO_SPACE, and
                            the attributes tree grows through its own fork
      test_catalog_compare.c catalog key/FastUnicodeCompare ordering tests
      test_catalog_index.c   two/three-level catalog lookup + mutation fuzz
      test_catalog_write.c  catalog create/delete/append/overwrite/truncate tests
      test_catalog_header.c a corrupt B-tree header node is refused, and a
                            header the tree cannot live with is not a mount
      test_image_mutation.c deterministic mutation sweep: a valid volume image
                            corrupted 1..8 fields at a time, driven through
                            probe, hfs_check, the catalog and the extents tree
      test_bitmap_props.c    allocation property test: 4,000 random alloc/free
                            runs against a model, every commit reopened from the
                            image, plus a two-bitmap-block volume for the
                            partial-free case
      test_journal_props.c   crash-atomicity property test: a write budget drops
                            every write past a chosen boundary, then the journal
                            is remounted from the medium and the volume must be
                            all-or-nothing, with a wrap variant
      test_txn_overlay_props.c transaction overlay property test: staging rules,
                            patch/merge behaviour, boundary slices and a full
                            32-block transaction checked against a byte map
      test_catalog_ops_props.c catalog operation property test: random creates,
                            deletes, renames, writes and truncates against a
                            directory model, with every folder's valence, every
                            file's contents and `hfs_check_volume` checked after
                            each operation
      test_btree_model.c    B-tree-level model test: random inserts, deletes and
                            in-place updates on the extents tree against a
                            sorted model of keys and bodies, with `find`, the
                            leaf chain and `hfs_check_volume` checked after
                            every operation
      test_writer_mutations.c write-path mutation sweep: one corrupted structural
                            field at a time, then a create, holding the writer to
                            "a refused call changes nothing" and "a write never
                            damages a clean volume"
    image/
      Makefile              disposable-image writer test build
      hfs_image_test.c      create/rename/truncate/delete/overflow/overwrite/
                            truncate-to-size/grow/Unicode on a HFS+ image, and
                            `order NAMES_FILE` (the corpus the macOS
                            differential has macOS read back)
      hfs_crash_test.c      journal write-failure injection and recovery
      check-overflow.py     per-block pattern check of the overflow file, plain or
                            overwritten (macOS mount)
      run-macos.sh          hdiutil + fsck_hfs + macOS mount driver
      run-crash-macos.sh    per-cut crash/replay/fsck driver
      write_diag_host.c     host outer/inner write-diagnostic runner
      run-write-diag-macos.sh  outer/inner image fsck integration test
      outer_diag_host.c     host outer-volume diagnostic runner and dirty-journal fixture
      run-outer-diag-macos.sh  outer-volume sequence, dirty replay and safety test
      loader_host.c          host HFS+ firmware loader runner
      run-loader-macos.sh    HFS+ manifest/image/bootstate loader fixture
      unicode_order_host.c   differential tool: `list` (catalog order), `tree`
                            (paths/kinds/sizes), `read`, `sort` (our comparator),
                            `canon` (our normalization)
      run-unicode-order-macos.sh  macOS creates the corpus and the tree, then
                            order, canonical form, tree read and file hash are
                            compared against what macOS produced
    bootloader/
      Makefile              bootloader host test target
      test_diag.c           diagnostic core/splash tests
      test_inner_image.c    inner-image extent mapping/metadata guard tests
      test_loader.c         manifest, SHA-256, bootstate and loader tests
      test_loader_hfs.c     HFS+ file-read adapter tests
      test_ed25519.c        RFC 8032, 192-byte and key-table tests
      test_certificate.c    certificate/rotation/revocation verification tests
      test_boot_info_page.c D-28 page writer round-trip through the firmware parser
      test_loader_error.c   every loader error code has a distinct, format-safe
                            message and there is no gap down to UNSUPPORTED
  tests/musicdb/
    Makefile                HelikonDB fixture, reader and library-glue tests
    test_hdb.c              fixture + optional real-library checks
    test_library_index.c    path index over the fixture and playlist resolution
                            into the play queue (order, missing, refused)
  tests/library/
    Makefile                HFS+ -> VFS -> HDB, audio storage path and playlist
                            resolution test (plus `macos-sanitize`)
    test_library.c          mounts an image, reads /.helikon/music.hdb, decodes
                            /Music/first.mp3 through the read-ahead window
                            against a resident decode, resolves
                            /Playlists/first.m3u into a queue and plays each
                            queued track from its catalog path, then drives
                            music_source.c through next/previous/repeat
    run-library-macos.sh    disposable hdiutil image + fsck_hfs runner; copies
                            the audio fixture to every path the catalog names,
                            /Music/first.mp3 and the playlist
  tests/ui/
    Makefile                launcher, Music, artist and audio host previews
    test_launcher.c         launcher render and checks
    test_music.c            hub/walls/browser render against the fixture HDB
    test_audio.c            Now Playing render and WAV player state
  tests/player/
    Makefile                WAV parser and player host tests (test + sanitize)
    test_wav.c              RIFF/WAVE validation and profile rejection
    test_player.c           buffer and media opens, bounded chunks, HAL mocks
    test_read_ahead.c       window hits, straddles, bypass, EOF latch, errors
    test_queue.c            natural and shuffled order, repeat, ends, capacity
    test_playlist.c         M3U/M3U8/PLS import and export: directives, path
                            resolution, the writer's exact bytes, refusals,
                            limits, capacity
    test_bookmark.c         the resume record: exact bytes, round trip, and
                            every writer and parser refusal
    test_cuesheet.c         cue sheets: FILE/TRACK/INDEX frames, pregap,
                            numbering, multiple files, refusals, caps
  tests/codec/
    Makefile                codec registry, MP3 (libmad) and FLAC host tests
    test_codec.c            probe/open/read/seek/malformed/bit-exact tests,
                            plus the streaming-source tests (bounded reads,
                            short reads, unknown size, head-only probe) and the
                            seek-to-the-end regression (#106/#107)
    test_codec_mutation.c   deterministic mutation sweep: a valid FLAC/MP3
                            fixture corrupted 1..8 fields at a time, driven
                            through probe/open/read/seek and the streaming
                            source path under ASan/UBSan
    make_fixtures.py        synthetic FLAC/PCM fixture generator
    make_mp3_fixtures.py    lame-based MP3 fixture generator (lame plus ffmpeg
                            for the one Layer II file)
    fixtures/               synthetic constant/verbatim/fixed/LPC FLAC + PCM
                            and committed MP3 fixtures (CBR/VBR/mono/ID3v2,
                            MPEG-2, MPEG-2.5, Layer II)
  tests/libmad.mk           shared libmad object rules for every suite that
                            links firmware/codec/codec.c; the sanitizer objects
                            are built with AddressSanitizer only, so a UBSan
                            report is always this project's own code
  tests/vfs/
    Makefile                VFS host test build/sanitizer targets
    test_vfs.c              mount, path rules, open/read/seek/EOF, handle
                            generation, directory semantics, an extents-overflow
                            read (inline + overflow splice, position, seek) and
                            the VFS media->codec source seam on an HFS+ fixture
                            (shared hfsplus fixture builders)
    test_vfs_write.c        the write half over the writer fixture plus a real
                            64 KiB in-FS journal: one transaction per call, a
                            clean journal and hfs_check_volume after each, and
                            every refusal proved byte-identical
  tests/update/
    Makefile                staged-update host test build/sanitizer targets
    make_update_set.py      mints the really signed fixture sets from a fixed
                            throwaway seed; --check guards drift
    update_test_key.h       the fixture public key (generated, never a real key)
    fixtures/               eight signed sets: good, next, bad-hash,
                            wrong-board, bad-signature, development,
                            short-image, same-version
    test_update.c           verify/park/commit over the journaled fixture, with
                            a byte-identical image after every refusal
  tools/hdb_build.py        HelikonDB reference builder and inspector
  tools/desktop/            macOS companion shell (SwiftPM, no feature modules)
    Sources/HelikonCompanion/App/       app entry, splash state and empty shell
    Sources/HelikonCompanion/Design/    paper/ink palette and button styles
    Sources/HelikonCompanion/Features/ welcome splash only
    Sources/HelikonCompanion/Resources/ owner poster crop for the welcome screen
    scripts/                 app bundle and icon generation
  docs/
    README.md               documentation index and reading order
    state.md                current phase and blockers
    invariants.md           safety invariants
    decisions.md            decisions and open decisions
    helikondb.md            HelikonDB format, builder contract and reader
    tasks.md                task queue
    verification.md         proof checklist
    file-map.md             this file
    architecture.md         boot chain, layers, boundaries
    firmware-architecture.md firmware-internal design paper (entry, memory, runtime, UI, ergonomics)
    ui.md                   default theme typography, anti-aliasing and font rules
    outer-volume-test.md    real outer-volume write protocol and backup gate
    failures.md             error taxonomy and decision trees
    device-matrix.md        hardware evidence
    handoff.md              latest handoff
    conventions.md          style and process
    glossary.md             terms
    testing.md              test layers
    fixtures.md             fixture plan, generation and hash policy
    validation.md           release matrix
    goals.md                README goals
    roadmap.md              phases 0–12, horizon lanes, v1 threshold
    competitive-roadmap.md  Rockbox parity matrix, differentiation, declines
    audio-formats.md        audio-format inventory, tiers, decoder plan, corpus
    hfsplus-write-engine.md writer deep dive
    release.md              release process, history and current release
    releases/               archived release notes and quick starts
    security.md             agent security rules
    faq.md                  user FAQ
    design-paper.md         full source copy with provenance; Appendix I addendum
    adr/                    long-form decision records
    log/                    dated work logs
    prompts/                reusable task prompts; session-start + build-firmware
```

## Planned tree

The F1 directories (`boot/`, `lib/`, `board/6g/`, `hal/6g/`, `block/`, `diag/`)
now exist in the current tree above. The list below is the remaining design
target, including the later VFS, player, UI and release modules.

```text
bootloader/                 rescue bootloader; native HFS+ read-only
firmware/
  boot/                     _start, vectors, linker script, boot-info, panic
  lib/                      freestanding mem/string/CRC/ring/pool helpers
  hal/6g/                   S5L8702 board primitives; no filesystem policy
  board/6g/                 BSP constants, init sequence, memory map
  block/                    block device, flush, power state
  hfs/                      read core + writer + journal + txn (current tree)
  vfs/                      lookup, handles, capabilities, object semantics
  db/                       immutable database reader and device delta log,
                            plus the VFS-backed hdb_io adapter
    doctor/                 duplicates, tags, corruption and storage report
  player/                   buffer/decode/output state machine
  audio/                    DSP chain, ReplayGain, EQ, crossfade, routing
    codecs/                 one bounded decoder module per format family
    metadata/               tag parsers, artwork, cuesheets, chapters
    profiles/               headphone/line-out/car/dock sound profiles
    resample/               documented polyphase rate conversion
  power/                    power states, spindown, thermal, calibration
  ui/                       retained-mode screens and renderer
    settings/               display/startup/system settings screens
    utilities/              clock, alarm, timer, calculator, reader, lyrics
  input/                    click wheel/button events, debounce, focus
  theme/                    native declarative themes and package validation
  skin/                     documented Rockbox WPS subset
  accessibility/            voice, talk clips, large text, contrast, motion
  sandbox/                  app/game runtime, quotas, host API
  usb/                      read-only LUN, HID, charge-only, service mode
  diag/                     events, counters, recovery screens
    health/                 battery/storage/temperature/checksum dashboard
  update/                   staging and bootstate promotion (later)
tools/
  build-diagnostic-bootloader.sh  pinned donor export, patch, Thumb build, checks
  build-loader-bootloader.sh      loader test image, test key/manifest/payload, checks
  make-bootloader-splash.py       pre-final owner splash variants and 4bpp C data
  make-final-bootloader-splash.py final green/red PNGs, mask and 4bpp C data
  make-firmware-image.py          F1 manifest/bootstate and development signing
  firmware-size.py                F1 map, headroom and undefined-symbol checks
  check-loader-artifact.sh        HFS+ host verification of the generated artifact
  build-write-diagnostic-bootloader.sh  separate inner-image write-diagnostic build
  build-outer-diagnostic-bootloader.sh  separate outer-volume write-diagnostic build
  make-write-diag-image.sh  create the disposable inner HFS+ image
  make-outer-opt-in.sh      create the outer-volume opt-in file
  install-write-diagnostic.sh  send the inner-image DFU with hash check
  install-outer-diagnostic.sh  send the outer-volume DFU with hash check
  cleanup-outer-test.sh     inspect/remove a marked outer test folder safely
  uninstall-bootloader.sh   send the 6G bootloader uninstaller DFU
  gen-hfs-unicode-table.py  generate the HFS+ fold/decomposition tables
  render-ui-mockup.py       render the 320×240 typography mockups
  hfsplus/                  host inspector, fixtures, tests
  parity/                   Rockbox differential harness and scorecard
  bench/                    boot, frame, seek, write and RAM benchmarks
  audio/                    codec corpus, decode benchmark, provenance matrix
  db/                       host database builder
  doctor/                    library doctor, duplicate/tag/corruption report
  health/                    diagnostics bundle and device-health report
  theme/                    theme package validator and WPS tag report
  voice/                    voice-file and talk-clip generation
  podcast/                  host feed fetch and episode sync
  migrate/                  donor overlay and Rockbox/Apple importer
  installer/                guided install, backup, verification
  desktop/                  macOS-first desktop app; shell now in the current tree
  service/                  framed USB service-mode host driver
tests/
  fixtures/                 deterministic HFS+ images and trees
  audio/                    per-format corpus, malformed files and decode bench
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
| `assets/fonts/` | Licensed IBM Plex Sans and Manrope subsets; owner-supplied Benguiat for the home/album concepts | UI lead | `assets/fonts/README.md` | OFL present, hashes match, host mockup renders | Do not commit the Benguiat `.otf` files; do not use the IBM Plex reserved name for a modified subset |
| `assets/ui/` | Host 320×240 UI mockups: typography, home concepts, three content walls and album/artist detail views | UI lead | `assets/ui/README.md`, `docs/ui.md`, D-52 | seven concept 1× frames and 2× previews, no clipping, regeneration needs `BENGUIAT_DIR` | Host artifacts only; the owner chose the launcher; walls and detail views are not home candidates |
| `docs/` | Agent documentation | docs lead | `docs/conventions.md` | `git diff --check`, link check | One canonical file per topic; no duplicate `agent.md`/`decision-log.md` |
| `docs/f2-bootstate-test.md` | F2 build, host gates and 6G bootstate runbook | firmware/test lead | T-055, D-02, `docs/hfsplus-write-engine.md` | host artifact check, clean `fsck_hfs`, next loader `GOOD`, device power-cut | Writes only `/.helikon/bootstate`; no other file; no device run yet |
| `bootloader/` | Diagnostic core and splash exist; hardware entry/manifest still planned | boot lead | `bootloader/README.md`, `docs/architecture.md`, paper §5, §16 | 7 host tests; device tests planned | No filesystem writes; no overlay; no `--single`; no write modules linked; not flashable yet |
| `firmware/boot/` | Entry, vectors, linker script, boot-info parser, bootstate parser/promoter, panic; early ATA probe ordering and the `HLO1` handoff marker | boot lead | `docs/firmware-architecture.md` §3, ADR-0004, `docs/f2-bootstate-test.md` | boot-info, bootstate and fault tests | No C before stacks/BSS; entry never returns; the bootstate write is the only F2 filesystem write |
| `firmware/lib/` | Freestanding mem/string/CRC/SHA-256/ring/pool helpers | firmware lead | `docs/firmware-architecture.md` §8.9 | host unit tests; no hosted symbols | No `malloc`, `stdio` or locale |
| `firmware/board/6g/` | BSP constants, init sequence, memory map | hardware lead | ADR-0003, paper §6 | map assertions, board audit | No target `#ifdef` in shared core |
| `firmware/hal/6g/` | S5L8702 clocks, GPIO, PMU, ATA read/write/flush, LCD, CS42L55 codec, I2S and DMA; VIC/IRQ (`vic.c`) and early-ATA probe ordering; planned USB devicecore | hardware lead | paper §6–§7, `docs/architecture.md`, `docs/disk-mode.md` | register-level bring-up, sleep/wake, PIO write/flush, first PCM output; LBA48/USB planned | No filesystem, UI or policy; no unmeasured overclock/undervolt |
| `firmware/block/` | Sector I/O, flush, power state, geometry and LBA48/APM discovery (`ata_geometry.c`) | storage lead | paper §7, `docs/architecture.md`, `docs/disk-mode.md` | read/write/flush, 512/4096, error injection | No paths/files/caches; a flush error must propagate |
| `firmware/hfs/` | HFS+ read core and writer: volume, B-tree, catalog, Unicode, extents, journal, txn, in-place overwrite | HFS+ lead | paper §8, §10.4, `docs/hfsplus-write-engine.md` | 104 host tests; overwrite and crash tests; fuzz/malformed/macOS differential planned | Writes only through `hfs_txn`; never hide unsupported entries |
| `firmware/hfs/hfs_catalog_write.c`, `hfs_btree_tree.c`, `hfs_bitmap.c`, `hfs_extents_write.c` | Catalog/bitmap/extents mutation | HFS+ lead | `docs/hfsplus-write-engine.md`, invariants I-03–I-15 | model/property/crash tests, `fsck_hfs` | No journal format, UI or boot policy; no direct writes outside a transaction |
| `firmware/hfs/hfs_journal.c`, `hfs_txn.c` | Journal append/commit/replay, bounded transaction engine | journal lead | paper §9.3–§9.4, `docs/hfsplus-write-engine.md` | crash injection, idempotent replay | No product policy/UI; never delete/disable/zero the journal |
| `firmware/hfs/hfs_check.c`, `tools/hfs-check.c` | Read-only structural check and its host CLI | HFS+ lead | D-11, `docs/testing.md` §5.3.2 | `tests/hfsplus/test_check`, real images | Never writes; skips what it cannot prove instead of guessing |
| `firmware/vfs/` | VFS over the HFS+ core: the read half (`vfs.c`/`vfs.h`) with mount, bounded path validation, generation-checked file/directory handles and stat/iterate/read/seek, and the write half (`vfs_rw.c`/`vfs_rw.h`) — create, mkdir, unlink, rmdir, rename/move, write and truncate, one journaled transaction per call, journal replay before the first write, fail-closed after a failed commit, and every open handle invalidated by a commit | VFS lead | paper §10, `docs/architecture.md`, T-056 | `tests/vfs` (9 read tests, sanitizer-clean, including an overflowed-fork read
through `test_fs_image_make_overflow()`) and `tests/vfs/test_vfs_write.c`
(the write half over the writer fixture plus a 64 KiB in-FS journal: after
every call the journal is clean and `hfs_check_volume` reports no problem, and
every refused call leaves the 1 MiB image byte-identical); no device run yet | Read-only; no hardware registers; no overlay branch; no errno guessing; empty extents file mounts, overflow forks fail closed if the extents B-tree never opens |
| `firmware/db/` | HelikonDB v1 read-only catalog reader: header validation, fixed track/album/artist records, strings, indices, artwork, CRC; `hdb_vfs.c` adapts an open VFS file to `struct hdb_io` so the same reader serves an embedded blob or a catalog inside the volume | database lead | paper §12, D-07, D-33, D-53, `docs/helikondb.md` | `tests/musicdb` fixture + 100-song corpus; `tests/library` reads the catalog from a disposable journaled HFS+ image through the VFS | No iTunesDB, tagcache or device scan; caller owns buffers; catalog is a derived cache; no device run yet |
| `firmware/player/` | Bounded PCM player (`wav.c`, `player.c`) feeding the four-buffer ring, with a codec-source bridge for compressed PCM; `wav.c` is one RIFF walker over a resident buffer or a pull source (short reads loop on absolute offsets); `vfs_media.c` binds an open VFS file to a codec source; `read_ahead.c` wraps a source in a 16 KiB window (hits, straddles, bypass, EOF latch); `player.c` opens either a resident buffer (`helikon_player_open`) or a pull source (`helikon_player_open_media`, which wraps it in the window) through one shared HAL bring-up and seeks with `helikon_player_seek_ms` (codec seek plus a stream restart at the target frame); `queue.c` is the next/previous order with repeat and seeded shuffle; `playlist.c` streams M3U/M3U8/PLS entries and writes M3U/PLS back through a sink; `vfs_playlist.c` is that sink over `vfs_rw`; `cuesheet.c` reads FILE/TRACK/INDEX into bounded track start frames (mm:ss:ff in 75ths), including pregaps and one file per track; `bookmark.c` is the resume record as strict text and `vfs_bookmark.c` is the only code that knows it lives at `/.helikon/resume.txt` (written to a temporary sibling, read back, then renamed through `vfs_rw`) | audio lead | paper §13, §28.3.8, `docs/audio-formats.md` §11, §14 | WAV/player host tests, FLAC bridge test, audio previews, 6G WAV and FLAC runs; WAV source-model tests | No filesystem internals; no unmeasured audiophile claims; only documented WAV/FLAC profiles |
| `firmware/codec/` | Static codec registry (`codec.c`, `codec.h`) plus the AIFF/AIFF-C decoder (`aiff/`), the native FLAC decoder (`flac/`), the MP3 wrapper (`mp3/`) over the vendored libmad (`libmad/`, see its README for the modification list), the bounded ID3v2 tag reader (`tag/id3v2.c`), the FLAC Vorbis-comment reader (`tag/vorbis.c`) and the WAV descriptor | audio lead | `docs/audio-formats.md` §11, §13, D-34, T-097 | `tests/codec`; 879,952 frames bit-exact FLAC; MP3 decode/seek tests green (test + sanitize), including the whole MPEG family (MPEG-2 22.05 kHz, MPEG-2.5 11.025 kHz, Layer II) with pinned frame counts and trim; FLAC workspace 65,608 B, MP3 workspace 30,976 B; a mutation sweep over mutated FLAC/MP3 fixtures is clean (see `docs/testing.md` §3.2) and found the two seek defects fixed in #106/#107; AIFF/AIFF-C decode is sample-exact
against committed fixtures whose reference PCM macOS `afconvert` reproduces
byte for byte (9 fixtures, 4 swept by the mutation harness); no device run for
MP3 or AIFF yet | No `malloc`; caller-owned fixed workspace; 16-bit ring output only; no storage/VFS, ALAC or AAC yet; libmad edits limited to the documented port header |
| `firmware/ui/` | Launcher renderer and the functional Music section (`music.c`: launcher-style hub, album/artist walls, lists and detail pages over an embedded HDB), the cleaned Music flow from `docs/ui.md` §6, the IBM Plex Sans atlas (`font_atlas.c`) and text renderer (`ui_text.c`), screens, input and focus | UI lead | paper §14, `docs/ui.md`, `docs/helikondb.md`, D-23/D-24 | `tests/ui` previews, frame trace, typography mockup; `5292c8ed…` device run found a ~20 s Artist-open stall | No storage policy; embedded catalog only in the pre-VFS test; default UI must survive skin failure |
| `firmware/input/` | Click wheel/button events, debounce, focus | UI lead | `docs/firmware-architecture.md` §9.7 | event/acceleration tests | No storage or playback policy |
| `firmware/skin/` | Documented WPS subset | UI lead | paper §28.3.4, D-10/D-15 | 20-skin corpus, hostile skin tests | No raw storage/playback policy; no full WPS engine; no unlicensed assets |
| `firmware/sandbox/` | App/game runtime, quotas, host API | runtime lead | paper §15, §28.3.9–10, D-08 | escape/quota/crash tests | No raw block/NOR/PMU/GPIO; no legacy plugin binary compatibility |
| `firmware/usb/` | USB device core: `usb_descriptors.c` (device/config/interface/endpoint/string), `usb_control.c` (chapter 9, endpoint halt, MSC class requests), `usb_msc.c` (bulk-only transport and SCSI over a block-device window: INQUIRY, CAPACITY(10/16), MODE SENSE, READ/WRITE/VERIFY(10/16), SYNCHRONIZE CACHE, eject flag, sense mapping, FORMAT UNIT refused), `disk_mode.c` (opt-in gate, LUN window, BOT/CSW phase machine, eject/release over a small transport ops table) and `usb_events.h` | USB lead | paper §16, D-09, D-54, `docs/disk-mode.md` §5–§9 | `tests/usb`: the core, the controller sequence and the mode machine, plain and under sanitizers — enumeration, CBW/data/CSW, a 4 TB whole-disk window, a write above 128 GiB, read-only DATA PROTECT, eject, bus reset | Host only: no IRQ 19, no device run, no caller in the boot path; normal profile never exposes a writable LUN; Disk Mode is exclusive and firmware does not mount HFS+ (I-16) |
| `firmware/hal/6g/usb.c/.h` | DesignWare OTG device driver over the `usb_hw.h` seam: PHY sequence, clock gates, core reset, FIFO budget, EP0/EP1/EP2 (PIO), the RX/TX pumps and the event queue; `usb_target.c` binds the seam to real registers, timer E and VIC IRQ 19 | USB lead | `docs/usb-device-stack.md` §2–§4, D-54 | `tests/usb/test_usb_hal` (mock registers): init values, EP0 enumeration, bulk in/out, bus reset, disconnect, spurious | Compile-only on target (cross-build passes, nothing calls it yet); no device run; PIO only, no DMA or cache assumptions |
| `firmware/hal/6g/usb_hw.h` | S5L8702 USB constants (bases, IRQ 19, gates, PHY and DWC registers, FIFO budget) and the seam the driver is tested through | USB lead | `docs/usb-device-stack.md` §2 | constants pinned by `test_usb_hal` | Donor facts, not measurements; every value needs the T-109/P2 trace |
| `firmware/diag/` | Events, counters, recovery | diagnostics lead | paper §11.2, `docs/failures.md` | log/export tests | Do not write logs on a failing filesystem; bounded RAM ring |
| `firmware/update/` | The signed staged-update client (T-064), plus the boot-state promotion path F2 owns: `update.c` verifies a set under `/.helikon/update/` through the loader's own manifest/Ed25519 verifiers, makes the `/.helikon/recovery/` copy, parks the new image beside the installed one, and swaps image + manifest + boot state in one transaction with a read-back of all three | release/update lead | D-31, D-01, D-02, paper §16, `docs/release.md`, [LOADER.md](../bootloader/LOADER.md) §1–§3 | `tests/update` over the journaled fixture with really signed sets: six refusal classes, park leaves the live pair alone, one-commit install, demotion of the replaced boot-state slot, downgrade refused | Host only; nothing calls it yet, so the linked image is unchanged; no silent install, no unsigned image, and no key material in the repository |
| `tools/disk-mode-format.sh` | macOS host script for the whole-disk LUN: guards (whole disk, not the boot disk, backup record, typed confirmation), then APM + one HFS+ volume filling the disk, then eject | USB/release lead | `docs/disk-mode.md` §11.1, D-54 | `bash -n`, `--help`, refusals and a dry run against a disk image; no real iPod | Refuses without a backup record; never touches the boot disk; `--dry-run` changes nothing |
| `tools/build-diagnostic-bootloader.sh` | Render the diagnostic image from a pinned donor checkout | boot/release lead | D-21, `bootloader/README.md` | Thumb build, `MOVE_AREA`, hash, `mks5lboot` dry-run | Uses a clean `git archive`; never modifies the donor checkout; image is unproven |
| `tools/build-loader-bootloader.sh` | Render the DFU-only loader test image and signed test files | boot lead | D-01, D-02, T-049, `bootloader/LOADER_TEST.md` | test key reuse, manifest signature, image/marker/`MOVE_AREA`, HFS+ artifact check, `mks5lboot` dry-run | Test key stays under `build/`; only the public half is linked; device runs recorded in `docs/device-matrix.md`; production handoff disabled |
| `tools/check-loader-artifact.sh` | Verify the exact loader test artifact through the host HFS+ core | boot lead | T-049 | disposable HFS+ image, `fsck_hfs`, `loader: OK`, `bootstate: GOOD` | Host-only; never touches a device |
| `tools/check-f2-artifact.sh` | Verify the exact F2 image/manifest/TRYING bootstate through the write path and host loader core | firmware/HFS+ lead | T-055, D-02, `docs/f2-bootstate-test.md` | `DEFAULT` before, promotion to `GOOD`, clean `fsck_hfs`, loader `GOOD`, unchanged manifest/image | Host-only; never touches a device |
| `tools/make-key-certificate.py` | Mint a root-signed release-key certificate or a revocation bundle offline | boot/release | D-20, T-053, `bootloader/LOADER.md` §2.1 | 120-byte certificate or 140-byte bundle, raw/hex/C output, deterministic for a given key and fields | Host tool only; the root private key stays offline and is never committed |
| `tools/make-firmware-image.py` | Build the F1/F2 manifest, bootstate and signed development image | firmware/release lead | D-01, D-02, ADR-0004, T-054, T-055 | Ed25519 signature, bootstate checksum, `VERSION`, `BOOTSTATE_STATE`, loader host-core verification | Development key only; never commit the private key; not a release image |
| `tools/coverage.sh` | Line coverage of `firmware/` and `bootloader/` across every host suite | test lead | `docs/testing.md` §5.3.3 | builds each suite with clang coverage, merges profiles, prints the files with the most unexecuted lines | Report only, no gate; leaves the suites instrumented; host-only |
| `tools/host-tests.sh` | Run the portable host suites with one compiler; `--sanitize`, `--macos`, `--keep-going`, `--fast`, `HOST_TESTS_IMAGE` | test lead | `docs/testing.md` §2, §12 | one log per suite under `build/host-tests/`, cleans each suite first, stops at the first red suite and prints its tail | Host-only; a green run is not a device claim |
| `tools/preview-build.sh` | Build both firmware payloads plus `README-PREVIEW.md`, `CHANGELOG.md` and `SHA256SUMS` for a preview | release lead | `docs/release.md` §1.1, `.github/workflows/preview.yml` | copies the two `out/` trees, per-run test key, no install path claimed | Preview only: no device run, no release key, no support claim |
| `tools/fuzz.sh` | Build and run the libFuzzer harnesses; `--replay` runs the committed corpus with no fuzzer runtime | test lead | `docs/testing.md` §3 | five harnesses, each with a stated invariant; the guided run needs `libclang_rt.fuzzer` | Host-only; a finding is committed as a corpus file, never a device image |
| `tools/static-analysis.sh` | `clang --analyze` over every firmware and bootloader translation unit; `--strict` exits 1 on a finding, an argument filters paths | test lead | `docs/testing.md` §3.1, `.github/workflows/tests.yml` | 74 units, firmware analysed freestanding and the bootloader hosted, libmad and donor glue skipped; findings come from the log because the analyzer exits 0 either way | Report only outside `--strict`; a clean run is not a correctness proof |
| `tools/fuzz-replay-main.c` | Standalone driver that feeds corpus files to a harness | test lead | `docs/testing.md` §3 | one process, ASan and UBSan, the same harness source | For hosts whose clang has no fuzzer runtime |
| `tests/*/fuzz/` | Harness sources, committed corpus and findings per suite | test lead | `docs/testing.md` §3 | `fuzz_<name>.c`, `corpus/<name>/`, `findings/` | Corpus files are small and deterministic; no device or proprietary data |
| `.github/workflows/tests.yml` | Host suites on Linux, the same under sanitizers, the fuzzers, static analysis, the macOS disk-image suites, and both firmware builds | test lead | `docs/testing.md` §12 | six jobs, `--keep-going`, strict static analysis, every push and pull request | CI is not a device run; no power-cut, battery or panel coverage |
| `.github/workflows/preview.yml` | Nightly prerelease from `main` | release lead | `docs/release.md` §1.1, T-117 | host suite gate, build, tag `preview-YYYY-MM-DD`, prerelease | Publishes nothing when the gate is red; images carry a test key |
| `tools/firmware-size.py` | Print the F1 map/size/headroom and check undefined symbols | firmware lead | ADR-0003, T-054 | `__image_end`, `__bss_end`, raw-size equality, banned hosted symbols | Host check only; not a device measurement |
| `tools/make-bootloader-splash.py` | Generate the pre-final owner splash PNG variants and 4bpp C data | boot/UI | T-045 | version, label mask, palette, packed base, preview | Requires the owner-provided ITC Benguiat file; font is not committed; historical design only |
| `tools/make-final-bootloader-splash.py` | Generate the final splash variants and 4bpp C data from the owner green/red PNGs | boot/UI | T-045, `assets/bootloader/README.md` | pair-difference mask, palette, packed base, preview | Uses the exact owner PNGs; no font file required; 6bc6927c refresh is not device-tested |
| `tools/render-ui-mockup.py` | Host 320×240 UI mockups, including launcher, index, content walls, the cleaned Music flow and Now Playing states | UI lead | `docs/ui.md`, `assets/fonts/README.md`, `assets/ui/README.md`, D-52 | images are 320×240, previews 640×480; text fits; Plex-only `--only music-flow` path needs no Benguiat | Host artifact only; no font file committed; not a device panel run or accepted theme |
| `tools/make-ui-font-atlas.py` | Generate the bounded IBM Plex Sans 4-bit/1-bit text atlas from the licensed subset | UI lead | `docs/ui.md` §6, D-23, `firmware/ui/FONT_ATLAS.md` | reproducible atlas, role sizes 11/12/14/17 px at 400/500/600, coverage check | Host generator only; atlas is data, not runtime rasterization; no unlicensed face committed |
| `tools/hfsplus/` | Host inspector, fixtures, tests | test lead | paper §8, §17, donor `tools/hfsplus` | host tests, sanitizer | Donor code is reference; do not modify donor; no device claims |
| `tools/db/` | Host database builder | database lead | paper §12, §28.4.1 | 10k-track parity | No on-device claim without device evidence |
| `tools/migrate/` | Donor overlay exporter | migration lead | paper §22 | fixture/hash/verify tests | Host-side only; never a runtime overlay; never overwrite silently |
| `tools/installer/` | Guided install, backup, model checks | release lead | paper §28.4.3, `docs/release.md` | clean-device install, recovery | No `--single`; no format as first response; backup first |
| `tools/desktop/` | macOS companion shell after the 2026-11-06 reset | host lead | [`companion-app.md`](companion-app.md), paper §28.4.2, D-09 | universal release build; `scripts/build-dmg.sh` creates and verifies the DMG; no test target yet | No feature code, device access, network or installer claim; ad-hoc signed, not notarized; no generated artwork |
| `tests/` | Host volume/B-tree, disposable-image, Unicode and crash fixtures now; fuzz/model/device planned | test lead | `docs/testing.md`, `docs/fixtures.md`, `docs/validation.md` | all layers | No proprietary themes/art/fonts; no personal data; no disk images in repo |
| `tests/image/vfs_write_host.c`, `tests/image/run-vfs-write-macos.sh` | The write VFS on a volume `hdiutil` formatted (`HFS+J`, Apple's journal layout): 20 KiB written in three short writes, truncate, mkdir, create, rename and unlink, each read back through the read VFS, then `hfs-check`, `fsck_hfs -fn` and a read-only macOS mount that has to see the same tree | test lead | `docs/testing.md` §3.8.1, T-065 | `make -C tests/image vfs-write-test`: structural check clean, `fsck_hfs` clean and no repair, macOS sees `/helikon-vfs.txt` (4096 B) and `/helikon-dir/moved.txt` | macOS only; creates and discards its own 16 MiB image; refuses a non-journaled or dirty volume; never repairs or formats anything |
| `docs/adr/` | Long-form decisions | docs lead | `docs/decisions.md` | review | Only for decisions too large for `decisions.md`; do not duplicate |
| `docs/disk-mode.md` | Canonical Helikon Disk Mode plan: writable-LUN exception, LUN layout, USB stack, LBA48 and capacity migration | firmware/USB lead | D-54, ADR-0005, paper §16.2/§28.4.2 | design review; no code/device claim | Normal USB stays read-only; Disk Mode is exclusive (I-16) |
| `tests/usb/` | Three host suites: `test_usb` (SCSI/BOT/descriptors over a sparse 4 TB fake disk), `test_usb_hal` (the controller sequence against a mock DesignWare register file) and `test_disk_mode` (the mode machine with a fake transport, including the opt-in gate, a whole-disk window, a write above 128 GiB and eject); each has a `_sanitize` twin | USB/test lead | `docs/disk-mode.md`, `docs/usb-device-stack.md`, T-110–T-112 | `test` + `sanitize` green: CBW validation, control requests, capacity saturation, high-LBA write/read-back, sense, refusals, PHY/FIFO/mask values, EP0 enumeration, bulk in/out, bus reset, mode transitions | The mock proves sequences, not silicon; no device writes; no vendor commands |
| `tools/collect-ipod-evidence.sh` | Read-only macOS evidence collector for Apple Disk Mode (planned) | boot/test lead | T-108, `docs/disk-mode.md` | dry-run and redaction check | Never writes `/dev/*`; redacts serial numbers |
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
