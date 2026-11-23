# State — Helikon-OS

> Purpose: what is true today. This file is not a plan and not a design paper.
> Status: living document
> Owner: project owner; maintained by the documentation agent
> Last updated: 2026-11-17
> Source: `README.md`, `docs/design-paper.md`, repository inspection;
> Rockbox iPod Classic manual `0d5c2b0604-261105`;
> [`audio-formats.md`](audio-formats.md), [`disk-mode.md`](disk-mode.md)

## Current phase

**Pre-phase 0 — Alpha 2.0.0 “Launchers and Interfaces” development release
published.** The release ships the loader test image, two storage-free firmware
images (launcher and Music hub/album-artist walls) and a universal macOS
companion shell. It does not include a firmware-owned filesystem write. The
owner reports 6G testing is complete for the release set; the detailed per-image
results (screen photos, timings and `fsck_hfs`) were not attached to the
repository at cut time. This is not a support release.

The roadmap in [`roadmap.md`](roadmap.md) starts at phase 0 (rescue bootloader
and read-only firmware skeleton). The read-only diagnostic image has
owner-reported 6G runs; the inner-image write diagnostic has one owner-reported
run, and the outer-volume diagnostic has one successful owner-reported 6G run.
The diagnostic images do not load firmware; the separate loader test image loads
the signed development images. The firmware-internal design is written down in
[`firmware-architecture.md`](firmware-architecture.md) and phases F1–F5
(T-054–T-058). F1 builds a signed read-only image (39,108 bytes, SHA-256
`d7916411…`) with an owner-reported 6G run. F2/T-055 implements a first
firmware-owned write path (ATA PIO write/flush, bootstate parser/promoter,
in-place HFS+ overwrite through `hfs_txn`, and the `WRITE` stage), but it is
unfinished and unconfirmed due to a currently unfixable bug; **it is not part of
Alpha 2.0.0**. F3–F5 are not implemented. The launcher, Music hub, album/artist
walls, HelikonDB reader and companion shell exist; playback, VFS,
storage-backed library and the working companion do not.

Date of this snapshot: **2026-11-17**. The repository retains the 2026-11-04
device runs; the owner reports the Alpha 2.0.0 release set (loader, launcher and
Music hub) was tested on a 6G, but the per-image evidence was not attached.
In the 2026-11-17 cleanup the branch backlog merged into `main` (PRs #27–#30:
LBA48/APM probe code, VIC/IRQ entry path, loader live-sector-size reads with a
dirty-journal read-only guard, the cleaned Music flow with a device-blocked
~20 s Artist-open stall, and the F2 ATA SRST/handoff chain), and the owner
accepted the D-29–D-52 decision batch (PR #31).

**Disk Mode line, 2026-11-11.** The owner accepted D-54: a separate, explicit,
exclusive Helikon Disk Mode may expose a writable USB LUN, while normal firmware
USB remains read-only. The plan is in [`disk-mode.md`](disk-mode.md) and
[ADR-0005](adr/0005-helikon-disk-mode.md). The first runtime step is T-109, a
read-only ATA/LBA48/APM probe on the 6G/iFlash SD; its host-side probe code and
the VIC/IRQ entry path have merged, but the recorded device run is still
pending. On the host, the LBA48 addressing path is now pinned at and above the
old 128 GiB limit: the geometry parser reports an exact 1 TB / 512 GB capacity
and selects LBA48 (`tests/firmware/test_ata_geometry.c`), an Apple partition map
may place an HFS+ partition above 2^28 sectors without losing a byte of it
(`tests/firmware/test_block.c`), and a volume whose base sits above 128 GiB
probes with the base folded into every read (`tests/hfsplus/test_volume.c`).
There is still no USB stack, no LUN, no LBA48 write, no capacity-migration tool,
and **no device has mounted a volume above 128 GiB**; this line does not unblock
F2/T-055. D-55 keeps the CE-ATA question open rather than claiming CE-ATA.

## What exists

| Item | Document status | Evidence |
|---|---|---|
| Root `README.md` with 25 owner-authored goals and poster | living | Current `README.md`; goals unchanged, poster added 2026-11-03 |
| Design paper, revision 2, 2026-11-03 | source | [`design-paper.md`](design-paper.md), source commit `3f220a73d0cceb083b3c057c6fe914d88468800a` |
| Firmware architecture research paper | living | [`firmware-architecture.md`](firmware-architecture.md); functional, aesthetic and developer-ergonomics design; F1 device run owner-reported 2026-11-04 |
| Helikon Disk Mode plan and ADR | design | [`disk-mode.md`](disk-mode.md), [ADR-0005](adr/0005-helikon-disk-mode.md), D-54; no USB stack, LUN or device run |
| Firmware memory map / entry ABI ADRs | living | [`adr/0003-firmware-memory-map.md`](adr/0003-firmware-memory-map.md), [`adr/0004-firmware-entry-abi.md`](adr/0004-firmware-entry-abi.md); D-27/D-28 accepted 2026-11-04 |
| Firmware F1/T-054 skeleton | `pass` | `firmware/boot`, `firmware/lib`, `firmware/board/6g`, `firmware/hal/6g`, `firmware/block`, `firmware/diag` and the HFS+ read adapter; signed development image 39,108 bytes, SHA-256 `d7916411…`; 9 host test groups sanitizer-clean; loader host core reports `loader: OK` / `bootstate: GOOD`; owner-reported 6G device run accepted 2026-11-04 (PASS, power-off, no write; photo/timings/`fsck_hfs` not attached) |
| Firmware F2/T-055 bootstate promotion | `in progress` — **blocked; not in Alpha 2.0.0** | ATA PIO write/flush, firmware bootstate parser/promoter, in-place `hfs_catalog_overwrite_file` through `hfs_txn`, and the `WRITE` stage. Current candidate image 76,080 bytes, SHA-256 `1d6d4c92…`, BSS `0x08890000`–`0x08922e60`, bootstate `TRYING` sequence 1. Host checks pass (`tools/check-f2-artifact.sh` promotes to `GOOD`, `fsck_hfs` clean, loader sees `GOOD`; 22 overwrite cuts plus flush/partial modes). The write gate is unfinished and unconfirmed due to a currently unfixable bug, so the image is **not** an Alpha 2.0.0 asset and no device success is claimed. Runbook: [`f2-bootstate-test.md`](f2-bootstate-test.md) |
| Firmware launcher image | `in progress` | `firmware/ui/launcher.c` plus generated RGB565 tiles: six cells, wheel/left-right row-major focus, SELECT/PLAY placeholders, MENU exit and 60 s idle power-off. Image 84,276 bytes, SHA-256 `b3218ccd…`, version 10, bootstate `GOOD`; no ATA/storage. `tests/ui` and the loader host check pass; the owner reports 6G testing complete with detail not attached. Included in Alpha 2.0.0 as `helikon-alpha2-launcher.zip` |
| Firmware build prompt | living | [`prompts/build-firmware.md`](prompts/build-firmware.md); full-procedure F1–F5 execution prompt |
| Session-start prompt | living | [`prompts/session-start.md`](prompts/session-start.md); repo-read bootstrap and Session Brief for every session |
| Agent contract and documentation set | living | `main`, merged 2026-11-03 |
| Product intent, safety rules, roadmap, goals, open decisions | living | `docs/` |
| Competitive roadmap (Rockbox comparison) | living | [`competitive-roadmap.md`](competitive-roadmap.md); Rockbox manual `0d5c2b0604-261105` reviewed 2026-11-05; D-29–D-52 accepted 2026-11-17 (D-08/D-13/D-14/D-47 excepted) and T-060–T-107 planned; manual section audit complete; no Helikon-OS feature claim |
| Audio-format plan | living | [`audio-formats.md`](audio-formats.md); 37 audio families (36 in Appendix B.1 plus Opus) inventoried; Tier 1 at v1 and full Rockbox audio parity at v1.x; the bounded codec framework, native FLAC and a host-implemented MP3 over vendored libmad exist (FLAC 2026-11-08 device run; MP3 host tests green 2026-11-18, no device run; the 2026-11-22 mutation sweep over mutated FLAC/MP3 fixtures is clean and found the two MP3 seek defects fixed as #106/#107, both with regression tests); ALAC and AAC-LC do not |
| Manual feature audit | living | [`competitive-roadmap.md`](competitive-roadmap.md) §12; every manual section mapped; remaining gaps T-093–T-107; recording/FM/line-in `N/A`; native plugins/video declined |
| Donor file references and provenance | living | [`file-map.md`](file-map.md) |
| License: GPL-2.0-or-later | living | `LICENSE`, `NOTICE`, D-18 |
| GitHub issue templates | living | `.github/ISSUE_TEMPLATE/` |
| Helikon-OS poster | living | `assets/branding/helikon-os-poster.png`; embedded in README |
| 320×240 startup crop | living (asset) | `assets/startup/helikon-os-320x240.png`; kept for provenance; superseded by the owner bootloader design in `assets/bootloader/` |
| Mountain reference image | living | `assets/logo/mount-helikon-sunset.jpg`; source/licence open question |

The "document status" column uses the document statuses from
[`conventions.md`](conventions.md); it does not mean a firmware device run
exists.

The README summary and 25-goal block are unchanged. The owner added the
Helikon-OS poster above the goals on 2026-11-03.

## Host code

| Item | Status | Evidence |
|---|---|---|
| HFS+ volume header/journal probe | `in progress` | `firmware/hfs/hfs_volume.c`; 12 host tests |
| Generic HFS+ B-tree header/node validation | `in progress` | `firmware/hfs/hfs_btree.c`; 9 host tests |
| HFS+ catalog iteration/lookup | `in progress` | `firmware/hfs/hfs_catalog.c`; 3 host tests |
| UTF-16BE name decode | `in progress` | `firmware/hfs/hfs_unicode.c`; 8 host tests |
| Extents-overflow file read | `in progress` | `firmware/hfs/hfs_extents.c`; 2 host tests |
| Diagnostic bootloader core | `in progress` | `bootloader/diagnostics.c`, `report.c`, `splash.c`; 7 host tests; read-only |
| Diagnostic hardware glue | `in progress` | `bootloader/hardware/helikon_diag_hw.c`; ATA read callback, donor partition scan, splash/report, button wait, power-off |
| Diagnostic bootloader image | `unproven` | `bootloader/artifacts/bootloader-ipod6g.ipod`; SHA-256 `280ed7ae…`; 67,144 bytes; `MOVE_AREA` headroom 61,888 bytes. Owner-reported: boot, splash, report, volume/catalog PASS; read `-8` because the probe file is absent; no write. |
| Diagnostic build script | `in progress` | `tools/build-diagnostic-bootloader.sh`; pinned donor commit, Thumb build, `MOVE_AREA` check, hash output |
| Writer interface/failure model | design | `docs/hfsplus-write-engine.md` §10; D-03/D-04 accepted; implementation not started |
| Journal format/commit/replay design | design | `docs/hfsplus-write-engine.md` §6.5–§6.7; Apple HFS+ journal format |
| Transaction engine | `implemented` | `firmware/hfs/hfs_txn.c`; bounded 32-block write set, stage/replace/abort, commit through the journal, and a staged-overlay read so two updaters of the header in one transaction merge. A stage patches the bytes it is given: an exact range is replaced, a range overlapping a staged run is merged into it, and a union larger than `HFS_TXN_BLOCK_MAX` is refused with `BUSY` (T-135). Fork staging works in runs of allocation blocks, so a B-tree node is one slot and a root split six rather than 34 (issue #115). Host property tests cover the overlay against a byte map (T-134) and the staging rules |
| Journal append/replay engine | `implemented` | `firmware/hfs/hfs_journal.c`; header/block-list checksum validation, wrap, append, idempotent replay; a host crash-atomicity property test over every write boundary plus wrap trials (T-133); Apple `max_blocks` and `BLHDR_FIRST_HEADER` fields now written, and a committed-unreplayed journal is replayed by macOS `fsck_hfs` in the host test; 9 host tests |
| Bounded transaction engine | `implemented` | `firmware/hfs/hfs_txn.c`; staged-block commit through the journal; 7 host tests |
| Allocation bitmap | `implemented` | `firmware/hfs/hfs_bitmap.c`; first-fit alloc/free that wraps past the hint, free-block counts, header/alternate staging, `NO_SPACE`; a refused free stages nothing; 6 host tests plus a 4,000-step allocation property test (T-132, which found #110 and #111); allocation-file overflow extents unsupported |
| B-tree node mutation helpers | `implemented` | `firmware/hfs/hfs_btree_write.c`; node offset table insert/delete, free-space checks and node split; 7 host tests |
| Write-path fail-closed gate | `implemented` | Since 2026-11-23 `tests/hfsplus/test_writer_mutations.c` corrupts one structural field of a valid volume (34 curated fields plus a seeded random tail) and drives a create through `hfs_volume_probe` → `hfs_check_volume` → `hfs_catalog_open` → `hfs_catalog_create_file`: a refused call must leave the image byte-identical, and a volume the check called clean must still be clean after a commit. It found issue #120: a `nextCatalogID` below 3 handed the writer a reserved CNID (the root's own ID when the field read 2) and the check never looked at the field; the writer now refuses it and the check reports `HFS_CHECK_CATALOG_ID`. Host-only  Since 2026-11-23 each iteration runs five calls - create, rename, write 1,500 bytes, truncate to zero, delete - so the allocation bitmap, the volume header and a fork record are exercised too, not just catalog nodes; 4,500 iterations over four seeds clean.
| B-tree growth | `implemented` | `firmware/hfs/hfs_btree_tree.c` allocates nodes from the header map and extends the fork when the map is empty; since 2026-11-23 a map record that covers exactly the nodes the tree has widens by two bytes into the header node's free space, a full map or a fork with no free extent answers `HFS_ERR_NO_SPACE` instead of `UNSUPPORTED`, and the attributes fork is addressable. `tests/hfsplus/test_btree_grow` pins 5 cases. Host-only; a macOS `newfs_hfs` volume sizes its map to fill the header node, so widening matters for trees another formatter made |
| B-tree tree insertion | `implemented` | `firmware/hfs/hfs_btree_tree.c`; root-to-leaf traversal, free-node map, leaf/index split, recursive parent split, root creation, leaf below-minimum insert, parent-key propagation and index-node below-minimum insert at depth >2; the model test now inserts and deletes 60 records at depth 3 |
| B-tree delete | `implemented` | `hfs_btree_delete` removes records including first records, propagates changed parent keys, unlinks/frees empty leaves, merges/redistributes with both siblings, collapses a one-child root to its nonempty leaf and collapses an empty parent into its grandparent. The 60-record depth-3 model now passes insert, update and delete; the key-loss alias in recursive parent-key propagation is fixed. Since 2026-11-23 an emptied leaf collapses through every ancestor left without a child, so a deep tree can be emptied from the top (issue #125); the model test's ordered cascade phase drains a depth-3 tree, checks the canonical empty state from memory and from the medium, refills it from depth 0 and reuses it through a reopened handle.
| Volume header staging | `implemented` | `hfs_volume_write.c`; primary/alternate header file/folder counts, next CNID, modify date and write count; stages a journal-sector-sized block so 512-byte and 4 KiB journal headers work; 4 KiB unit test |
| Private NUL names | `implemented` | Directory iteration escapes NUL as U+2400 and marks the entry `name_escaped`/`unsupported`; path lookup skips the escaped entry and continues instead of aborting the directory; 1 catalog test |
| Extents-overflow record | `implemented` | `hfs_extents_write.c`; data-fork records keyed by fork/file/startBlock, up to eight extents each; insert/get/delete; multiple records supported |
| Inner-image mapping | `implemented` | `bootloader/inner_image.c` maps a root-level outer file's data-fork extents into a block device, rejects out-of-range writes, and guards against overlap with outer metadata, journal, volume headers and forks; 4 host tests |
| Separate write diagnostic | `implemented` | `bootloader-ipod6g-write-diag.ipod` (110,920 bytes, `81350f…`, 18,112 bytes headroom) keeps the outer HFS+ volume read-only and runs the writer inside `helikon-test.img`; host outer/inner test passes twice, and one owner-reported 6G run reported every stage PASS; inner `fsck_hfs` and outer `diskutil verifyVolume` both exited 0. This artifact predates the journal `max_blocks` fix; rebuild before another inner power-cut run |
| Outer-volume write diagnostic | `pass` | Current tested image `bootloader-ipod6g-outer-diag.ipod` (110,408 bytes, `32eeb541…`) writes only `/helikon-outer-test` after an opt-in file and a clean-journal check. It adds in-place B-tree record updates for append/truncate and stage progress on screen. Owner report 2026-11-04: every stage PASS; `/helikon-outer-test` absent; post-run `fsck_hfs` and `diskutil verifyVolume` pass. Host test covers the full sequence, dirty-journal refusal, macOS replay, 512/4 KiB journal sectors, non-destructive cleanup and a second run. The artifact predates the T-034 insert/leaf fixes; rebuild before the next device run. One 6G run only |
| Catalog metadata | `implemented` | Timestamps: create/access/content/attribute on create; content/attribute on data changes; attribute-only on rename; parent dates and volume modify date on mutation; backupDate 0. BSD owner/group 0, flags 0, file/folder mode, Finder type/creator/flags/finderInfo zero, textEncoding zero, special 0; new-record Finder policy is zero without extension inference, existing bytes are preserved on append/truncate/rename. Host tests, `GetFileInfo -t/-c` macOS checks and the outer-image stage test pass. Wider Unicode corpus remains T-038 |
| Outer-volume device write | `pass` | T-048 closed 2026-11-04: one 6G run, every stage PASS, test folder removed, `fsck_hfs` zero repairs, `diskutil verifyVolume` pass; `.rockpod-rw` unchanged and `.rockbox` only `.DS_Store`. The owner confirmed the full backup/diff; the agent did not rerun it. Power-cut matrix and timing remain. Not a support claim |
| Controlled PMU cut | `pass` | Current image `81350f…`: targets 7/56 were pre-commit clean controls; target 96 cut after commit, recovered on the next boot, all stages PASS, and the extracted inner image passed `fsck_hfs` with `The volume HelikonInner appears to be OK`. Further replay targets are optional coverage |
| Write diagnostic 6G run | `pass` | One owner-reported run, 2026-11-03: all stages PASS, extracted inner image SHA-256 `156060…`, `fsck_hfs -fn` exit 0, no `diag` folder, outer `diskutil verifyVolume` exit 0; not a library-write or support claim |
| Journal crash injection | `implemented` | `tests/image/run-crash-macos.sh` injects write, flush and partial-sector failures at every cut point of create (25 write + 4 flush + 25 partial), append (34 + 4 + 34), truncate (25 + 4 + 25), delete (25 + 4 + 25), rename (21 + 4 + 21) and overwrite (21 + 4 + 21) truncate-to-size (21 + 4 + 21) and grow (34 + 4 + 34) transactions, reopens the image, replays the journal and runs `fsck_hfs -fn`; all 444 cuts recover to a consistent state and an fsck-clean volume; power-loss timing and the remaining operations remain |
| Disposable-image writer validation | `implemented` | `tests/image/run-macos.sh` creates a 16 MiB journaled HFS+ image, creates a folder/file, appends 4096 bytes, renames, truncates and deletes through the outer volume, mounts at each stage and checks content, modes, timestamps and paths; `fsck_hfs -fn` is clean after every stage. The overflow stage (2026-11-21) builds a file that needs four overflow extents by appending into twelve freed holes, so the volume's empty extents tree takes its first record, macOS verifies all 49,152 bytes, and a cleanup stage truncates and deletes the file back to an empty extents tree. The same file is then overwritten in place in two windows — one straddling the inline/overflow boundary, one inside the second overflow record — and macOS reads both windows back with `fsck_hfs` clean before the cleanup stage. A sized-truncate stage then shrinks the nine-block direct overflow file to four blocks plus 100 bytes, macOS checks the exact byte count and pattern; a grow stage then writes 500 bytes 400 bytes past that end, and macOS verifies the zero gap and the written range before the cleanup stage deletes what is left. Host-only; no device or library write |
| B-tree fork growth | `implemented` | `hfs_btree_grow()` allocates a volume block, extends the catalog/extents fork in the primary/alternate headers, adds a free node to the header map and updates `totalNodes`; requires `node_size == block_size` and an inline fork extent slot; 1 host test |
| Catalog create/delete/rename | `implemented` | `hfs_catalog_write.c`; file and folder records, thread records, CNID allocation, file/folder counts (which do not count the root folder), parent valence, rename/move with a folder-cycle check; names are normalized to HFS+ canonical form; 5 host tests plus a model-based property test that also runs `hfs_check_volume` after every operation (T-136) |
| Unicode catalog comparison | `implemented` | `hfs_catalog_compare.c` uses Apple `FastUnicodeCompare`; `hfs_unicode_normalize_name()` decomposes, applies the HFS+ exception ranges, decomposes Hangul and canonically orders combining marks; `hfs_unicode_data.c` is generated from Unicode 2.1.9. `tests/image/run-macos.sh` now creates a scripted macOS corpus with NFC/NFD, CJK, emoji, Hangul, combining marks, U+2126, U+F900, `:`/`/`, 255-unit and rejected 256-unit names, and macOS sees the same NFC tree with `fsck_hfs` clean. Since 2026-11-22 `tests/image/run-unicode-order-macos.sh` checks the comparator against the order Apple's catalog produced for 47 names, and our writer's order for the same names against Apple's, with `fsck_hfs` on both images. `hfs_utf8_to_utf16be()` encodes a UTF-8 name for the writer API and has six host tests. Broader fuzzing remains |
| Data-fork append/write/truncate/overwrite (eight inline + multiple overflow records) | `implemented` | `hfs_catalog_write_file` writes at an offset and grows the file, zero-filling the gap in bounded chunks (`hfs_catalog_append_file` appends, `hfs_catalog_overwrite_file` writes in place, `hfs_catalog_truncate_to` shrinks); an append fills the partial last block before allocating, and `hfs_bitmap_find` sees the transaction so two allocations in one transaction cannot pick the same block. `hfs_catalog_append_file` appends to empty or non-empty files, stages data before metadata, allocates individual free blocks, merges inline extents and writes multiple extents-overflow records; truncate frees all records; `hfs_catalog_overwrite_file` writes a range in place across the inline/overflow boundary through the fork's logical extent map (a hole fails before anything is staged; a fork needing more than `HFS_EXTENT_COUNT + HFS_OVERFLOW_EXTENT_MAX` entries returns `UNSUPPORTED`); `hfs_catalog_truncate_to` shrinks to any size: blocks past the new end return to the bitmap, the retained extents are rewritten inline and in the overflow tree, the bytes past the new end in the last kept block are zeroed, and a size above the current one is `ARGUMENT` (this call never grows a fork); the transaction block budget caps very large fragmented writes |
| Fork block staging | `implemented` | `firmware/hfs/hfs_fork_io.c`; full/partial block write staging into `hfs_txn`; `hfs_fork_stage()` keeps the inline-extents contract for a `struct hfs_fork`, `hfs_fork_stage_map()` walks a collected logical extent list and refuses a range its extents do not cover; 5 host tests |
| DFU install runbook | living | [`bootloader/DFU.md`](../bootloader/DFU.md); backup, manual SHA-256, DFU install and recovery; image is unproven |
| Default UI typography and home concepts | `in progress` | D-23/D-24 and `docs/ui.md` record the 14/17/12/11 px body scale and grayscale anti-aliasing. Licensed IBM Plex Sans and Manrope subsets, OFL texts, hashes and sixteen host mockups (two typography; launcher, neoclassical index, album/podcast/audiobook walls, album view and artist view, each at 1× and 2×) are in `assets/fonts/` and `assets/ui/`; `tools/render-ui-mockup.py` reproduces them with clipping checks. The launcher and index are home candidates; the launcher uses six muted icon accents in a frameless grid with short accent plinths, while the index is typographic. The album, podcast and audiobook walls are content browsers; the artist view is implemented on host in `firmware/ui/music.c` (portrait band, tabs, overview), while the album view remains proposed. None of the home candidates shows playback. Their Benguiat headers are an external, uncommitted owner-supplied face and the
accepted D-23 exception (D-52, 2026-11-17; firmware embedding still needs a
font licence decision before T-042/T-057). They are not an accepted theme or a renderer. The launcher firmware renderer is implemented on host: generated RGB565 tiles from the mockup toolchain, the owner-directed Benguiat wordmark raster with Plex labels, fixed clickwheel input, wheel/left-right row-major focus, a placeholder app screen on SELECT/PLAY, `tests/ui` preview and a storage-free `HELIKON_DIAG_LAUNCHER` image. The binding Music-flow and Now Playing specification is merged in `docs/ui.md` §6, and the firmware implementation (PR #21, image `5292c8ed…`, 4,195,680 bytes) adds the IBM Plex Sans text atlas (`firmware/ui/font_atlas.c`, `firmware/ui/ui_text.c`), the cleaned Artists/Artist/Album screens and the new Now Playing surface; it is device-blocked by a ~20 s Artist-open stall. Since T-128 the hub, the album wall and the Tracks list draw from that atlas too — no Music screen uses the 3×5 diagnostic face — and the hub masks the launcher tile's baked clock and battery, which it had inherited by blitting the tile as its masthead. The launcher's own masthead still carries them (T-129, owner decision). The 6G panel run and the remaining screens remain T-042/T-057 |
| Bootloader splash redesign | `implemented` | Final owner green/red PNGs are in `assets/bootloader/` with `VERSION ALPHA` and a green/red `HFS+ DETAILS` mask. The refreshed loader image uses a 16-colour 4bpp RGB565 base (38,400 bytes) plus a 324-byte label mask; the temporary bottom stage line is removed. The final-splash `6bc6927c…` image is device-tested: 6.5 s startup and `fsck_hfs -fn` clean |
| HelikonDB music catalog | `in progress` | Native HDB v1: one immutable `/.helikon/music.hdb` (128-byte header, 88/48/48-byte track/album/artist records, deduplicated string pool, four precomputed indices, 96×96 RGB565 artwork), `tools/hdb_build.py` reference builder with mtime/size and artwork caches, optional track/album ratings, artist biographies and profile pictures, and the read-only `firmware/db/hdb.c` reader. Tested on a 100-song sample from the owner's macOS library: 100 tracks, 78 artists, 100 albums, 98 album covers, 1.85 MB; cold build 2.1 s, warm 0.007 s; fixture/reader CRC tests pass. The Alpha 2.0.0 embedded-catalog image `de6491a9…` (474,252 bytes) uses a contiguous 100-song sample (6 artists, 10 albums, 10 covers) and `firmware/ui/music.c`: a launcher-style Music hub (Albums/Artists/Tracks), the 4×3 album wall, the artist wall with monogram fallback, 96×96 artwork, ratings, artist pages, wheel/left-right navigation and MENU back. The post-Alpha development image `b5425283…` (583,820 bytes, version 14) adds the `assets/ui/artist-view` layout with OVERVIEW/ALBUMS/TRACKS tabs and fixes cold-boot input init when no button is pressed. Host loader check and `tests/ui` previews pass. The owner reports 6G testing complete for Alpha 2.0.0; per-image detail was not attached, and the new image's device run is pending. The Alpha image is included in `helikon-alpha2-music-hub.zip`. Production builder is the companion (T-066); storage-backed HDB, journal and playback remain T-056/T-058/T-067 |
| Release-key certificate, rotation and revocation | `in progress` | `bootloader/certificate.c` verifies a root-signed release-key certificate (120 bytes: key ID, issuer ID, inclusive image-version range, flags, certified key, signature) and a root-signed revocation bundle (140 bytes), and exposes `helikon_key_store_verify` as the manifest signature hook. Rotation is by signed image version, not by date, because the bootloader has no trusted clock. 11 host tests cover parse, root signature, range, rotation of one key ID across two windows, revoked key, unauthenticated bundle, foreign issuer and tampering; `tools/make-key-certificate.py` mints the bytes offline and `LOADER.md` §2.1 records the format. **Not wired into the loader image yet:** the hardware loader still uses the build-local key table, so a release build needs the owner's root public key, the first certificate and a fresh device run | `in progress` | boot/release | [`LOADER.md`](../bootloader/LOADER.md), [`certificate.h`](../bootloader/certificate.h), [T-053](tasks.md) |
| Host test runner | `implemented` | `tools/host-tests.sh` runs the nine portable suites with one compiler, one log each, and stops at the first red one; `--sanitize` repeats them under ASan and UBSan, `--macos` runs the disk-image suites, `HOST_TESTS_IMAGE` adds `tests/library`. All three modes green on 2026-11-22 (9 + 8 + 10 suites). `tools/static-analysis.sh` adds `clang --analyze` over 74 firmware/bootloader units, clean on 2026-11-22 and run strict by CI. |
| CI and daily preview | `in progress` | `.github/workflows/tests.yml` (portable, sanitizers, macOS disk-image suites, both firmware builds) is green on PR #89, and its first four runs found real portability defects: `firmware/lib/error.h` shadowed by glibc's `<error.h>`, a Mach-O-only assembler blob in `tests/ui`, and a host-address DRAM check in the boot-info parser (now `image_ceiling`). `.github/workflows/preview.yml` published `preview-2026-11-22` on a manual dispatch (its first dispatch failed: the gate needed Pillow and mutagen, fixed in #103). The 04:17 UTC cron has never fired — GitHub's scheduler dropped `schedule` events for public repositories through November 2026 — so on 2026-11-23 the workflow gained a `push` to `main` top-up guarded by a `check` job that skips when `preview-<today>` exists (one prerelease per day, whichever trigger is first), and both workflows gained `timeout-minutes` and apt retries after a wedged toolchain install held a job for 65 minutes. |
| Host test harness | `in progress` | 208 portable HFS+/bootloader tests (164 HFS+, 44 bootloader), sanitizer-clean, plus eleven firmware host test groups in `tests/firmware/` (F1's boot-info, block/partition, stages, lib, ring, pool, screen, trace, hfs-file, the 4000-range byte-range property test and F2's bootstate), the `tests/musicdb` HDB fixture/reader test and the macOS disposable-image, Unicode, crash-injection (including overwrite and truncate-to-size), F2 artifact, inner-image, outer-volume and loader tests; the macOS differential (`run-unicode-order-macos.sh`) checks the comparator against the order Apple's own catalog produced for 47 names and reads back a macOS-written tree with a 70,000-byte file hash; fixture plan in `fixtures.md`; first diagnostic boot is owner-reported |
| Backup/recovery protocol | `implemented` | Full-volume backup, read-back, disposable restore rehearsal and Apple-restore/DFU path owner-confirmed 2026-11-04; protocol and stop conditions in `outer-volume-test.md` |
| Fixture plan | `implemented` | `docs/fixtures.md` lists synthetic reader/journal/B-tree/catalog/extents/bitmap/Unicode fixtures, disposable-image writer fixtures and write/flush/partial crash fixtures; no disk images committed |

## What does not exist

| Item | Status | Note |
|---|---|---|
| Helikon-OS firmware source tree | `in progress` | F1 source exists: boot entry/vectors/linker/boot-info/panic, freestanding lib, 6G BSP, clock/PMU/backlight/LCD/ATA/buttons HAL, read-only block/partition scan, HFS+ read adapter and diag stages/trace. F2 adds ATA write/flush, bootstate promotion, in-place HFS+ overwrite and the `WRITE` stage. F3 begins as `firmware/vfs/`: a read-only VFS over the HFS+ core with generation-checked handles, host-tested in `tests/vfs/` and built into the 6G image; a storage-backed library browser and a device run do not exist. F1 and the Alpha 2.0.0 loader/launcher/Music-hub set are owner-reported on 6G; F2 is blocked and unconfirmed |
| Host firmware loader | `implemented` | `bootloader/manifest.c`, `bootstate.c`, `sha256.c`, `ed25519.c`, `keys.c`, `firmware_loader.c` and `hfs_loader.c`; formats frozen in `bootloader/LOADER.md`. Signature is verified before any image byte; then SHA-256 and bootstate. 13 manifest/loader tests, 2 HFS adapter tests, 4 Ed25519/key-table tests and a macOS HFS+ fixture with a real generated key pass. No jump and no writes |
| Helikon-OS firmware-loading bootloader | `in progress` | The current tree builds `7c4edc96…` (107,560 bytes) after the 2026-11-21 boot-info page fill (T-059) and label-contrast change; the Alpha 2.0.0 build was `7f13e7e5…` (106,184 bytes). It builds with ATA DMA enabled for the fast loader path (LOADER_PIO=1 keeps the PIO variant), leaves the controller active before the jump, shortens the boot button-hold wait, and removes the firmware-confirmation and "Executing" screens plus their 3 s of delays; it jumps as soon as verification completes. It is host-verified; the owner reports the Alpha 2.0.0 release-set 6G test complete, but the per-image screen and timing evidence was not attached. The last detailed device-tested image is the final-splash `6bc6927c…` (106,536 bytes, 22,496 bytes headroom), owner-reported at 6.5 s on one 6G/iFlash SD with a clean `fsck_hfs -fn`. It verifies Ed25519 before any image byte, reads directly to the signed DRAM address, executes only a signed development payload and disables production handoff. Production handoff is T-051 and needs a real signed firmware image. |
| Helikon-OS bootloader image | `unproven` | Read-only diagnostic image built; separate inner-image and outer-volume write diagnostics built; the diagnostic images do not load firmware. The separate loader test image loads the signed F1 development image. The inner image has one owner-reported run; the outer image has one successful 6G run |
| Loader test image | `implemented` | `build/loader-bootloader/out/bootloader-ipod6g-loader-test.ipod` (107,560 bytes) is the current build `7c4edc96…`; it reuses the test key and its own 12-byte payload still verifies through the host loader core. It differs from the recorded `7f13e7e5…` by source merged since that build, the 2026-11-21 HFS+ label colours, which now clear 4.5:1 (deep green `#007A3D`, deep red `#C81E1E`) instead of 2.10:1 and 3.14:1 (`tests/bootloader/test_splash_color.c` guards the floor), and the T-059 D-28 boot-info page, which it fills at `0x087ff000` after verification and passes in `r0` (host round-trip through the real firmware parser; a refused page is passed as `r0 = 0`). **This build has no device run.** The last device-tested image is `6bc6927c…` (106,536 bytes, 6.5 s, `fsck_hfs -fn` clean). Production handoff is disabled |
| Bootloader manifest/boot state | `implemented` | Formats frozen in `bootloader/LOADER.md`; host parser/verifier/selector, real Ed25519 and HFS loader tested; loader test image owner-reported working. D-02 is single-image fail-closed by owner decision: no A/B fallback; recovery is DFU/Apple restore. D-28 entry ABI is accepted; the loader fill is T-059 |
| VFS and player | `in progress` | **Device-proven:** the four-buffer PL080 LLI ring, bounded 16-bit mono/stereo WAV at 44.1/48 kHz, pause/resume and Now Playing (PR #17; owner run 2026-11-08 on `18f5fe39…`, 4,111,124 B, clean audio and working pause/resume) and native FLAC, 16/24-bit mono/stereo at 44.1/48 kHz, in a 65,608-byte workspace (PR #19; owner run 2026-11-08 on `6a2328ae…`, 2,560,056 B, version 15, clean FLAC audio). **Host-only, merged:** the codec registry with FLAC and MP3 (vendored libmad, 30,976-byte measured workspace) (#35); a pull-source model where a decoder reads at an absolute offset, with a `streaming` flag and `HELIKON_ERR_NEED_MORE` for a head-only probe (#36); FLAC (8 KiB window) and MP3 (32 KiB window) as pull decoders, and `firmware/player/vfs_media.c` turning an open VFS file into a codec source (#37); bounded ID3v2.2/2.3/2.4 text tags (#38) and FLAC Vorbis comments (#43); the LAME/FFmpeg gapless trim for MP3 (#40, per file only — cross-track gapless is not implemented); `helikon_player_open_media`/`helikon_codec_probe_source` opening audio without a resident file (#41) and `helikon_player_seek_ms` restarting the PCM stream at a position for every codec (#42); the whole MPEG family executed by fixtures rather than claimed (#64); and a 16 KiB read-ahead window that turns small decoder pulls into few large reads (#66). `firmware/db/hdb_vfs.c` reads `/.helikon/music.hdb` through the VFS and the catalog CRC verifies over that path, proven on a disposable macOS-formatted journaled volume (#44, `helikondb.md` §6); a fork that continues in the extents tree also reads correctly there (#51). **Storage-backed decode, host only:** `tests/library` builds a disposable journaled HFS+ image, puts the MP3 fixture on it, and plays it through HFS+ → VFS → `vfs_media` → read-ahead → codec → the player ring with a silent host HAL, requiring the PCM to match a resident decode byte for byte; `macos-sanitize` repeats it under ASan/UBSan. The next/previous play order exists as `firmware/player/queue.c` (natural and seeded-shuffle permutations; `MOVED`/`WRAPPED`/`END`/`RESTART` answers; hard stops with repeat off) with host tests, but nothing calls it yet. Playlist import landed host-only on 2026-11-22: `firmware/player/playlist.c` streams M3U/M3U8/PLS entries with path resolution and counted refusals, ten host test groups and a fuzz harness, and no caller in the firmware. The library glue landed the same day: `firmware/library/library.c` indexes every HDB track path once per mount and resolves a playlist into the queue in playlist order, counting missing and refused entries separately, with `tests/musicdb/test_library_index.c` over the fixture, and `tests/library` now also resolves a playlist file that sits on the image against the catalog read through the VFS and plays each queued track from its catalog path (`PASS playlist: 4 entries -> 3 queued, 1 missing, 1 refused`; the CI macOS job runs it as `tests/image library-test`). Cue sheets landed the same day as a separate bounded reader: `firmware/player/cuesheet.c` turns `FILE`/`TRACK`/`INDEX mm:ss:ff` into at most 99 track start frames (75ths of a second), keeps `INDEX 00` as the pregap, handles one file per track, and counts rather than guesses the statements it refuses (nine host groups, a `cuesheet` fuzz harness with eight seeds). The ATA driver is presented as a block device by `firmware/storage/ata_block.c` (the driver's own read/write/flush/power are the ops table; the geometry refuses an impossible sector size or a capacity that overflows 64 bits, which is the 1 TB-read-as-128 GiB bug class), host-tested with counted stubs. Mounting landed too: `firmware/storage/mount.c` scans the partition map, keeps the first partition that probes as HFS+, mounts it read-only through the VFS and opens `/.helikon/music.hdb`, tested by `tests/library` over a read-only 512-byte block device built from the disposable image (79 static-analysis units, 0 findings; the 6G image still 80,020 B). The storage-backed music path is owned once: `firmware/library/music_source.c` binds a mounted VFS and an HDB, builds the index, plays a playlist that lives on the volume, and walks the queue with next/previous (repeat-one replays, the end stops), proven by `tests/library` over a real image on the host. Playlist export and a firmware call site do not exist. **Still missing:** the Music-hub call sites (the hub still plays its embedded blob and the firmware mounts no VFS), I2S/DMA output on the device, an underrun/read-latency measurement, and any device run of storage-backed playback. Selecting an artist on the merged Music flow (PR #21, image `5292c8ed…`, 4,195,680 B) stalled for almost 20 s on the device and is still unresolved. ALAC and AAC do not exist. The four-codec sequence is in [`audio-formats.md`](audio-formats.md) §11: audio bench, player/FLAC core, MP3/ALAC/AAC-LC, read-only storage, functional gate |
| HFS+ write engine | `in progress` | Journal, txn, bitmap, B-tree insert/delete/growth/deep delete/in-place update, fork staging, file/folder create/delete/rename, multi-extent/overflow data append/truncate, FastUnicodeCompare, HFS+ normalization, metadata policy and disposable-image `fsck_hfs` validation exist. A delete frees the fork's blocks before the catalog record goes away, an append to an already-overflowed fork edits only the fork and its last overflow record (so a fork grows to the HFS+ extents limit, not a writer-local 32-extent ceiling), freed B-tree nodes are erased, and two updaters of the volume header in one transaction merge through the staged overlay. One outer-volume 6G run passed; D-05 cache limits, broader fuzzing, timing and the device power-cut/adapter matrix remain. No writer run on a device since 2026-11-04 |
| F2 bootstate device run | `not run` — **blocked** | Host: the F2 candidate promotes `TRYING` to `GOOD`, `fsck_hfs` clean, loader sees `GOOD`; overwrite crash matrix passes. The firmware write gate is unfinished and unconfirmed due to a currently unfixable bug, so there is no confirmed 6G run. The image is not an Alpha 2.0.0 asset |
| Helikon Disk Mode | `in progress` | **Host-only, merged:** the USB device core (`firmware/usb`: descriptors, chapter 9 and MSC class control requests, bulk-only transport, the SCSI command set and a block-device LUN window) with `tests/usb` green plain and under sanitizers, over a sparse 4 TB disk: a host write at LBA `0x10000004` reads back, READ CAPACITY(10) saturates and CAPACITY(16) states 4 TB, sense survives to REQUEST SENSE, FORMAT UNIT and read-only/out-of-range writes are refused. The controller driver also exists (`firmware/hal/6g/usb.c` plus `usb_target.c`): the PHY sequence, clock gates, core reset, FIFO budget, EP0/EP1/EP2 over PIO and the event queue, with `tests/usb/test_usb_hal` pinning the register sequence against a mock core and the cross-build green at 80,004 bytes; it has never run on hardware. The mode module (`firmware/usb/disk_mode.c`) adds the opt-in gate, the LUN window (whole disk for a restore/format, or a read-only data volume), the BOT phase machine and eject/release over a transport ops table, with `tests/usb/test_disk_mode` covering enumeration, a write above 128 GiB, DATA PROTECT and eject on the host. A separate Disk Mode image now builds (`make -C firmware BOARD=6g DISKMODE=1`): 40,524 bytes, its own build directory, ATA plus the USB stack plus restore mode, and no `hfs_volume_probe` symbol anywhere in it, so that image provably never mounts the volume. It asks before it hands the disk over (SELECT yes, MENU or 30 s no) and shows the capacity and the addressing mode. A second, separate DFU-only bootloader image runs the same mode for a player whose firmware cannot boot: `tools/build-usb-disk-bootloader.sh`, 108,296 bytes, SHA-256 `de9b67e3…`, `_moveend` 20,736 bytes inside `MOVE_AREA`. `tests/usb/fuzz_msc` drives malformed CBWs, hostile CDB lengths, a data stage that stops halfway, a read-only device and an I/O-fault device through the transport: 162,500 commands per run from a fixed seed, clean under ASan/UBSan, checking that the window never moves, that no sector outside it is touched, that a read-only device takes no writes and that a device error is never reported as success. A refused CBW is answered with a 13-byte PHASE_ERROR CSW rather than silence. **Replay hardening, 2026-11-22:** the crash-injection harness now checks replay idempotency (`replay-twice`) and a second fault during replay (`replay-cut N`) at every cut point, with `fsck_hfs -fn` after each. **Still missing:** every device run — nothing has enumerated, mounted or been formatted. The LBA48/APM probe code has merged; the T-109 probe device run is pending. Normal USB stays read-only (D-54); the VID/PID in the source is a placeholder pending D-56 |
| Journal replay on a real dirty volume | `unproven` | Replay is host-tested on synthetic journals; no macOS-created dirty volume or device run |
| Installer or desktop app | `in progress` | Companion app reset under `tools/desktop/`: owner-banner welcome screen and a brand-only shell with no sidebar navigation. All previous device, library, metadata, transfer, podcast, audiobook, scrobbler, install, settings and test code was removed on 2026-11-06. The shell builds universal (arm64 + x86_64) and ships as `Helikon-Companion-0.1.0-alpha2.dmg`; it has no device, network or write functionality. See [`companion-app.md`](companion-app.md) |
| Build system, CI, release artifacts | `in progress` | `firmware/Makefile` and `tools/make-firmware-image.py` build the F1/F2 ELF, raw image, manifest, bootstate and signed development image. `VERSION` selects the image version and `BOOTSTATE_STATE=trying` or `good` selects the initial slot state. Host test Makefiles and bootloader build scripts exist. Alpha 2.0.0 (`alpha-2.0.0`) is published with the loader, two storage-free firmware sets and a universal companion DMG; it is an engineering preview with a test key and no firmware-owned write. `tools/host-tests.sh` runs the host suites and CI runs it on every push; the nightly preview is §1.1 of `release.md`. There is no reproducible release build, release key or staged update client |
| Helikon-OS device run | `partial` | Three earlier owner-reported diagnostic runs, the 2026-11-04 outer-volume run with every stage PASS, the loader-test passes on `3979de6f…` and `6bc6927c…`, and the F1 firmware run on `d7916411…` (owner-reported 2026-11-04, accepted as the T-054 gate). On 2026-11-08 the owner reported the WAV image `18f5fe39…` clean with working pause/resume, the FLAC image `6a2328ae…` clean with working pause/resume, and the cleaned Music-flow image `5292c8ed…` failing the Artist-open step with a ~20 s stall. The owner reports the Alpha 2.0.0 release-set 6G test is complete; per-image photos/timings/`fsck_hfs` are not attached. No support claim; one device/iFlash SD only |
| Supported device list | `unproven` | 6G is the reference; 7G physical audit is deferred from the active queue by owner direction (2026-11-04) |
| Code of conduct | `rejected` | Owner deleted Contributor Covenant 2.1 on 2026-11-03; no replacement |
| Signing policy and key ownership | `accepted` | D-20/ADR-0002 option A: offline Ed25519 root on two hardware tokens, certified release key, rotation/revocation/recovery; certificate verifier and a real signed release image are T-053/T-051 |

## Last known-good commit and branch

- **Base branch:** `main`
- **Base commit for Alpha 2.0.0:** `f5f8bfb` (`Revise feature list and add
  'Soon' labels`, 2026-11-06) plus the release-preparation PR. The release tag
  `alpha-2.0.0` records the exact commit. Check `git rev-parse alpha-2.0.0`.
- **F1 work:** merged to `main` from `firmware/f1-skeleton`; the source, host
  tests and docs carry the owner-reported 6G run. Check
  `git log -- firmware tests/firmware`.
- **F2 work:** merged to `main`; the current tree builds F2 `1d6d4c92…`
  (76,080 bytes) and the loader `7f13e7e5…` (106,184 bytes). Host checks pass,
  but the firmware write gate is unfinished and unconfirmed due to a currently
  unfixable bug. F2 is **not** part of Alpha 2.0.0 and has no confirmed 6G run.
  Check `git log -- firmware tests/firmware tests/image tools`.
- **Last known-good code commits:** `a86ed57` (`Firmware: harden F1
  display/panic paths and add HFS file test`, 2026-11-04) built the F1 image
  `d7916411…` (39,108 bytes) with one owner-reported 6G run; `4898a6f` (`Code:
  add separate inner-image write diagnostic`, 2026-11-03) passed one inner-image
  6G run; `8b528d1` (`Code: fix outer diagnostic truncate hang`, 2026-11-04)
  produced image `32eeb541…`, which passed one outer-volume 6G run on
  2026-11-04. Alpha 2.0.0 adds the launcher, Music hub/walls and companion DMG;
  those release images are owner-reported on 6G, not independently re-run in
  this cut. On 2026-11-08, `e9f6ad6` (codec framework + native FLAC) merged with
  the FLAC image `6a2328ae…` owner-reported clean and pause/resume working; the
  WAV image `18f5fe39…` is owner-reported clean; PR #21's Music-flow image
  `5292c8ed…` is merged (`f3b511d`), but its device report is a stall — selecting
  an artist took almost 20 s — and that is unresolved.
- **Last known-good documentation commit:** `f5f8bfb` and the Alpha 2.0.0
  release-preparation PR. Check `git log -1`.

Do not treat the donor V3.2 release as a Helikon-OS baseline. It is a separate
overlay-based product.

## Last recorded device run

**2026-11-08: WAV, FLAC and cleaned Music-flow images (owner-reported).** On one
6G/iFlash SD, the WAV image `18f5fe39…` (4,111,124 bytes) played cleanly with
working pause/resume; the FLAC image `6a2328ae…` (2,560,056 bytes, version 15)
played cleanly with working pause/resume; and the cleaned Music-flow image
`5292c8ed…` (4,195,680 bytes, version 15) stalled for almost 20 s when an artist
was selected, then started playback after repeated presses. The FLAC branch was
rebuilt after merging `main` `5fa0510` and reproduced the identical SHA-256, so
the tested FLAC image matches merged `main`. Screen photos and `fsck_hfs` output
were not attached. One device only.

**Alpha 2.0.0 release set, 2026-11-06 (owner-reported), plus F1 on 2026-11-04,
three earlier diagnostic runs on 2026-11-03 and the outer-volume run on
2026-11-04.** The owner reports the 6G test programme for the loader `7f13e7e5…`,
launcher `b3218ccd…` and Music hub `de6491a9…` is complete. Screen photos,
timings and post-run `fsck_hfs` output were not attached; the device-matrix
distinguishes the owner report from the older recorded runs. F2 is not part of
this release and has no confirmed device result.

The owner reports the F1/T-054 device run complete and accepts it as the F1
gate. The current development image is 39,108 bytes, SHA-256 `d7916411…`; it
writes nothing and does not replay the journal. The screen photo, stage timings
and post-run `fsck_hfs -fn` output were not attached to this repository.

The first image (`bd0ebc92…`, 66,920 bytes) booted on the owner's 6G and showed
the splash. `HFS+ volume: PASS` and `HFS+ catalog: PASS`. The read probe failed
with `read error: -11` (`HFS_ERR_NAME_INVALID`), and the report displayed `llu`
instead of the byte count and shifted later status lines.

The fixed image (`280ed7ae…`, 67,144 bytes) was then run. The report formatting
was correct: volume, catalog, write (`NOT IMPLEMENTED`), journal, splash and
partition size displayed as expected. `HFS+ read` reported `-8`
(`HFS_ERR_NOT_FOUND`) because `/rockbox.ipod` is not a file on this iPod; the
lookup itself worked. No write was requested or reported.

The separate write diagnostic (`9e3881…`, 109,320 bytes) was then installed by
DFU with `--dfusend` and no `--single`. The owner reports every screen stage
PASS. The extracted inner image (16,777,216 bytes, SHA-256
`156060075eef60c463f421ce459c2138a00e1c43de44da802b77ce89c79ca0e8`) passed
`fsck_hfs -fn` with exit 0, the `diag` folder was gone, and
`diskutil verifyVolume` exited 0 on the outer volume.

The read-only runs are a partial bring-up; the write run covers only the
inner-image sequence. On 2026-11-04 the separate outer-volume diagnostic
(`32eeb541…`) ran on the same 6G: every screen stage PASS, `/helikon-outer-test`
absent afterwards, `fsck_hfs -fn` zero repairs and `diskutil verifyVolume`
pass. `.rockpod-rw` was unchanged; `.rockbox` differed only in `.DS_Store`. This
is one outer-volume run, not a power-cut, adapter or support claim. See
[`device-matrix.md`](device-matrix.md) §2.1, §2.2 and §2.5.

F2/T-055 remains unfinished and unconfirmed due to a currently unfixable bug.
Its host evidence is the disposable-image promotion, artifact check and
overwrite crash matrix described in
[`f2-bootstate-test.md`](f2-bootstate-test.md), but there is no confirmed 6G
result and the image is not an Alpha 2.0.0 asset.

The donor project has owner-reported hardware use for its V3.2 release. That is
donor evidence, not Helikon-OS evidence, and it does not validate native HFS+
writes, the transaction engine, the bootloader, or any goal in
[`goals.md`](goals.md).

## Current blockers

1. **Alpha 2.0.0 per-image evidence and F3–F5 are outstanding.** The owner
   reports the release-set 6G run is complete, but the per-image photos,
   timings and post-run `fsck_hfs` for the loader, launcher and Music hub were
   not attached to the repository. The kernel beyond F2, VFS, player,
   storage-backed library, installer, working host app, production handoff and
   release key do not exist. The outer-volume writer has one successful 6G run;
   library-wide, power-cut and adapter paths are not tested.
2. **T-055/F2 is blocked and is not in Alpha 2.0.0.** The first firmware-owned
   write path is unfinished and unconfirmed due to a currently unfixable bug.
   Host evidence exists, but there is no confirmed 6G result and the image is
   not a release asset. T-051 depends on T-055, so production firmware handoff
   is blocked until the owner resolves or re-scopes that dependency.
3. **Journaled writer exists; device power-cut evidence is incomplete.** D-03
   requires journaled writes and D-04 makes Helikon-OS own replay. The
   transaction engine, Apple journal format, replay and 346 host crash cuts are
   implemented. The real device power-cut matrix and replay of a real dirty
   volume remain.
4. **Writer internals are mostly frozen.** The interface/journal design,
   bitmap, B-tree insert/delete/growth/deep delete, catalog/extents mutation,
   fork staging, FastUnicodeCompare, HFS+ decomposition, metadata policy and
   rename are implemented; the disposable image and one outer-volume run pass
   `fsck_hfs`. Cache limits (D-05), broader fuzzing and the device power-cut/
   adapter matrix remain.
5. **No signed-update release path yet.** D-01/D-02 and D-20 are accepted, but
   D-16 (update transport), the release-key certificate verifier (T-053) and a
   real signed firmware image (T-051) remain.
6. **Device-test programme is owner-closed for the current phase.** Backup,
   read-back, disposable restore rehearsal, the Apple-restore/DFU path and the
   6G/iFlash SD loader run are recorded. Other adapters, the full device
   power-cut matrix and library-wide differential remain future coverage.
7. **No production release infrastructure.** Alpha 2.0.0 (`alpha-2.0.0`) and
   the bootloader-only Alpha 1.0.1 (`alpha-1.0.1`) exist, but there is no
   reproducible firmware build, release manifest, signing-key ceremony record,
   staging flow, rollback test or support window. The D-20 policy is accepted;
   no release key has been generated and no release signing is claimed.

## Next task

The owner accepted D-03 (journaled writes) and D-04 (Helikon-OS owns replay).
The writer interface and failure model are frozen in
[`hfsplus-write-engine.md`](hfsplus-write-engine.md) §10. The diagnostics read
probe is intentionally skipped; `-8` is correct because `/rockbox.ipod` is not a
disk file on this iPod.

The P0 line is **T-055/F2** (first firmware write), but it is unfinished and
unconfirmed due to a currently unfixable bug. **F2 is not part of Alpha 2.0.0.**
T-051 (production firmware handoff) depends on T-055 and is therefore blocked
until the owner resolves or re-scopes that dependency; T-051 also needs a real
signed firmware image for end-to-end device validation. T-054/F1 is done.
T-006/D-20 and T-049 are implemented; T-050 is closed by owner decision as an
explicit no-A/B, single-image fail-closed model. D-26 records that the bootloader
has no OF/recovery menu; native Menu+Play and DFU cover recovery. The next
release follow-up is to attach the 6G photos, timings and `fsck_hfs` for the
loader, launcher and Music hub images to `device-matrix.md`. The P1 line is:

- **T-041** — UI font licences and the 320×240 typography mockup.
- **T-012** — full 6G model/adapter/battery inventory; backup/restore is done.
- **T-029** — finish the read-only diagnostic evidence.
- **T-045** — final owner splash is integrated and the stage line is removed;
  the refreshed image needs one clean device check.
- **T-053** — release-key certificate, rotation and revocation verifier.

T-041 and T-045 are implemented. T-042/T-013/T-052 are P2; T-052 tracks
reducing startup from 6.5 s toward 2–3 s.
The fixture plan is in [`fixtures.md`](fixtures.md); the writer crash matrix and
evidence format are in [`validation.md`](validation.md) §2.

The competitive horizon in [`competitive-roadmap.md`](competitive-roadmap.md)
and the audio-format plan in [`audio-formats.md`](audio-formats.md) are planning
only. D-29–D-52 were accepted by the owner on 2026-11-17 (D-08, D-13, D-14 and
D-47 excepted) and T-060–T-107 are `planned`; none changes the active P0 line.
No “matches Rockbox”, “better than Rockbox”, “24/96” or “hi-res” claim is
allowed until the differential and audio evidence exists. The manual audit
itself is complete; the remaining breadth gaps are tracked by T-093–T-107.

## Unproven claims and open questions

| Claim or question | Status | What would prove it |
|---|---|---|
| Helikon-OS boots on a 6G | `unproven` | Recorded boot log and device run |
| Alpha 2.0.0 launcher and Music hub images boot on a 6G | owner-reported | Attach screen photos, timings and post-run `fsck_hfs` to `device-matrix.md` |
| Native HFS+ writes survive power loss | `unproven` | Power-cut matrix plus `fsck_hfs` zero repairs |
| macOS accepts the written volume | `unproven` | `fsck_hfs -n` and `diskutil verifyVolume` after device writes |
| Battery life improves over Rockbox | `unproven` | Three-run measured baseline; D-12 |
| RAM stays within budget on 10k tracks | `unproven` | Watermark and soak on device |
| 20 Rockbox skins render | `unproven` | Recorded corpus and frame/memory report; D-10 |
| 7G is a viable port | `unproven` | Board audit and per-target matrix; D-13 |
| Windows/Linux desktop support | `unproven` | Service-mode transport test; D-09 |
| Apps/games are safely sandboxed | `unproven` | Adversarial tests; D-08 |
| Anti-corruption detects injected corruption | `in progress` | Injection test and quarantine report; D-11. `firmware/hfs/hfs_check.c` is a read-only structural check with 29 host cases and a CLI; it catches a bumped free-block count on a real macOS volume and does not false-alarm on a 140 GiB one (it marks the overflowed bitmap "not checked"). It also walks the catalog and compares its file/folder tally with the volume header: a real 32 MiB volume reads 2 files / 2 folders, the 140 GiB one 5 files / 3 folders across 14,208 nodes, both matching. That volume's bitmap lives in the extents overflow tree and is still counted: 36,594,065 free blocks both ways, through the resolved map. The same pass now walks each tree's leaf chain, checks catalog and extents key order across leaf boundaries, validates every catalog record body, checks each index node's child, level and key, and checks each tree's free-node map against the nodes reachable from its root (a leaked node or a reachable node marked free is a finding, and the header's free count must match the map); a 64 MB macOS volume with nested folders and non-ASCII names, 128 nodes and a real depth-2 catalog reports clean. Since 2026-11-23 it also pairs every file and folder record with its thread record in both directions through the tree itself (`HFS_CHECK_CATALOG_THREAD`: one missing, the wrong kind, or a different parent or name), which costs no table and no cap; a pristine 16 MiB macOS `HFS+J` volume, and the same volume after the writer's random create/append/truncate/rename/delete run, both report clean. No quarantine, no scrub policy yet |
| Signed updates reject bad images | `unproven` | Host Ed25519/manifest tests pass; release-key certificate/revocation, a real signed image and device update testing remain; D-16, T-051, T-053 |

Open questions are tracked in [`decisions.md`](decisions.md). Unknown hardware
facts must be marked `open question`; do not fill them from memory.

## Update this file at the end of every session

Change the phase, date, blockers, last known-good commit, device evidence and
next task when they change. If you add a claim, add its evidence or mark it
`unproven`. Do not describe planned work as implemented.
