# Testing — test layers, fixtures and how to add a test

> Purpose: define the test pyramid and the evidence each layer produces.
> Status: living document
> Owner: test lead
> Last updated: 2026-11-11
> Source: design paper §17, §24, §26, §28; `docs/verification.md`;
> [`disk-mode.md`](disk-mode.md)

**Helikon-OS has a host test tree.** It covers the HFS+ read/write core, the
read-only diagnostic bootloader core, the host firmware loader and the F1
firmware modules. The portable HFS+/bootloader suites are 136 tests (105 HFS+,
31 bootloader), sanitizer-clean. The F1 firmware adds nine host test groups
(`tests/firmware/`: boot-info, block/partition, stages, lib, ring, pool, screen
and trace), also sanitizer-clean. F2 adds a tenth group for bootstate parsing
and promotion. The F3 read-only VFS adds a host suite (`tests/vfs/`) over a
generated HFS+ fixture, covering path validation, open/read/seek/EOF, bounded
seek, generation-checked handles and directory semantics; it is sanitizer-clean.
The writer foundation covers the Apple HFS+
journal append/replay path, a bounded transaction engine, the allocation
bitmap, node-level B-tree insert/delete and fork block staging; the disposable
image and crash matrices add the macOS-visible behaviour. The F1 read-only image
has one owner-reported 6G device run; there are no firmware power-loss tests yet.
Donor tests are reference material only. Do not describe a donor test pass as
Helikon-OS validation.

## 1. Test layers

| Layer | What it proves | Runs on | Current status | Required for |
|---|---|---|---|---|
| Host unit | Parser, allocator, name, state-machine logic | Host | `in progress` — HFS+ read core, journal and txn | Every module |
| Fuzzing | Malformed B-trees, extents, names, journal records do not crash/escape bounds | Host | `planned` | Reader, writer, journal |
| Model-based B-tree | Insert/update/delete, split/merge, key order, free-node map | Host | `planned` | Writer |
| Property tests | Allocation, ordering, commit atomicity, no lost/duplicate blocks | Host | `in progress` — the byte-range block layer has a 4000-range property test; writer/journal properties `planned` | Writer/journal |
| Differential vs macOS | Same tree/names/metadata/`fsck_hfs` result on both sides; catalog order matches Apple's | Host + macOS | `in progress` — ordering and read differential run; writer-side tree comparisons partly covered | Writer |
| Bootloader diagnostics | Read-only volume/catalog/file report and splash | Host | `in progress` — 8 tests | Boot screen |
| Device | Boot, mount, playback, write, USB, sleep, recovery | 6G (then 7G/ports) | `not run` | Any hardware claim |
| Power-loss | Every metadata operation at every journal boundary | Device/rig | `not run` | Write release |
| Battery/RAM | Runtime, sleep drain, peak RAM, soak | Device/rig | `not run` | Goal 1/4, D-12 |
| Skin corpus | 20 skins, tags, frame/memory caps, fallback | Device | `not run` | Goal 3, D-10 |
| App sandbox | Permissions, quotas, escape, crash isolation | Device | `not run` | Goals 15/18, D-08 |
| Desktop app | Import/tag/rate/scrobble/install/backup, failed transfer | macOS first | `not run` — app reset to a shell on 2026-11-06; no test target or device matrix | Goal 20, D-09 |
| Car/receiver | Line-out, charging, resume, ignition, Tier 2 controls | Device + receiver | `not run` | Goal 23, D-14 |
| Update/rollback | Bad image rejection, staging cut, rollback, version/hash | Device | `not run` | Goal 9, D-01/D-16/D-20 |

## 2. Host tests

Host tests are the fastest safety net. They must not require a device.

- One test binary per cohesive module (volume, B-tree, catalog, extents, bitmap,
  Unicode, journal, transaction, VFS).
- Deterministic fixtures shared by C and Python validators where useful.
- Every parser has malformed-input tests and a fuzz target.
- Every error code has a test that asserts the code, not just "fails".
- No test may depend on a private developer machine, absolute path or network.

**One command runs the portable suites.** `tools/host-tests.sh` builds and runs
`tests/{hfsplus,firmware,bootloader,vfs,codec,musicdb,player,ui,usb}` with one
compiler, keeps a log per suite under `build/host-tests/`, and stops at the first
red suite with its last twenty lines:

```sh
./tools/host-tests.sh clang                     # the nine portable suites
./tools/host-tests.sh clang --sanitize          # ASan + UBSan over the same code
./tools/host-tests.sh --macos                   # tests/image: hdiutil and fsck_hfs
./tools/host-tests.sh clang --keep-going        # every suite, every failure
./tools/host-tests.sh clang --fast              # keep the last run's objects
HOST_TESTS_IMAGE=/tmp/vol.img ./tools/host-tests.sh clang   # adds tests/library
```

Each suite is cleaned before it is built, unless `--fast` is given. That is not
fussiness: a `sanitize` target leaves instrumented objects behind, and the next
plain build then fails to link them — `tests/firmware` did exactly that on the
first CI run.

The suites stay separate Makefiles, so a single red test is still
`make -C tests/hfsplus test`; the runner exists so a change cannot pass review by
being tested the one way that happens to be green. `--macos` is Darwin-only and
the disk-image suites create their own images; neither mode touches a device.

**Every check must be able to fail.** `tests/firmware/test_check.h` counts
failures and `helikon_test_report()` is what turns them into an exit status; the
Makefile and `tools/host-tests.sh` read nothing else. A firmware test that
recorded a `CHECK` failure and then returned 0 would leave the suite green, and
exactly that happened while the block property test was being written — the
failure lines printed and `make` still said `PASS`. The header now installs an
`atexit` guard: a process that ends with failures counted and no report prints
them and calls `_Exit(1)`. Keep the report call at the end of every test; the
guard is the backstop, not the contract.

## 3. Fuzzing and sanitizers

`tools/fuzz.sh` builds and runs the libFuzzer harnesses. Six exist, each with
a stated invariant, because a fuzz target that only proves "does not crash" is
worth much less than one that knows what would be wrong:

| Harness | Target | Invariant |
|---|---|---|
| `id3v2` | `firmware/codec/tag/id3v2.c` | a non-zero tag size only comes from a buffer that starts with the header; every text field is terminated inside its array |
| `vorbis` | `firmware/codec/tag/vorbis.c` | comments stay terminated; the FLAC walker only accepts a buffer that starts with `fLaC` |
| `hdb` | `firmware/db/hdb.c` | a database whose header claims more bytes than the file holds is refused, and every offset the reader reports is inside the file it was given |
| `manifest` | `bootloader/manifest.c`, `bootstate.c` | a short or corrupt page is refused and leaves the output untouched; an accepted image size is inside DRAM |
| `journal` | `firmware/hfs/hfs_journal.c` | a journal whose checksum does not match is never reported clean (I-04); a hostile but checksum-consistent journal may fail to replay but never writes outside the device |
| `playlist` | `firmware/player/playlist.c` | every entry path is absolute, terminated and free of control characters; `open` refuses a buffer with a NUL instead of stopping at it; the entry count never exceeds the cap and `next` never fails after `open` succeeded |

```sh
./tools/fuzz.sh                    # every harness, 60 seconds each
./tools/fuzz.sh id3v2 300          # one harness, five minutes
./tools/fuzz.sh --replay           # run the committed corpus, no libFuzzer
```

The corpus lives in `tests/<suite>/fuzz/corpus/<harness>/` and is committed, so a
finding becomes a test the moment it is understood: copy the reproducer from
`tests/<suite>/fuzz/findings/`, add the fix, commit both. `--replay` builds each
harness against `tools/fuzz-replay-main.c` with ASan and UBSan and runs every
committed file through it — that works on a Mac whose clang has no
`libclang_rt.fuzzer`, which is every Mac with only the Command Line Tools. The
guided run needs the runtime and is what CI does for 20 seconds per harness. The
corpus alone already reaches 64 % of `id3v2.c`'s regions, so replay is a real
test, not a formality.

Anyone adding a parser should add a harness and a handful of seeds in the same
pull request. A crash, a hang or a violated invariant is a defect, not a "known
fuzz case", and no corpus file may be a device image, a proprietary sample or
personal data.

### 3.1 Static analysis

`tools/static-analysis.sh` runs `clang --analyze` — the path-sensitive checker
that follows a value through branches, not a linter — over every firmware and
bootloader translation unit:

```sh
./tools/static-analysis.sh              # report only, exit 0
./tools/static-analysis.sh --strict     # exit 1 on the first finding
./tools/static-analysis.sh hfs_journal  # only paths matching the argument
```

Firmware sources are analysed the way the device builds them (`-ffreestanding
-nostdinc` and the compiler's own `stddef.h`/`stdint.h`); `bootloader/*` is
analysed hosted, which is how its tests compile it. Vendored libmad and the
donor-facing hardware glue are skipped: the first is not ours to change, the
second pokes registers whose values only a device can confirm. **74 translation
units currently analyse clean.**

The exit status is not the signal. `clang --analyze` exits 0 whether or not it
found something, so the script greps each unit's log for ` warning: ` or
` error: ` and `--strict` sets the status from that count. A compile error counts
as a finding too: a file the analyzer could not parse is a file nothing checked.
The `analyze` job in `.github/workflows/tests.yml` runs the strict mode on every
push and pull request.

It paid for itself on the first run, with three findings in code that already had
tests: a dead store in `vfs.c` (a local initialised from the file position and
overwritten before any read), a signed-shift guard in the FLAC predictor that
only rejected `shift >= 31` for a 5-bit signed field, and a division by
`msc->sector_size` in `usb_msc.c` that the analyzer could not refine through the
struct field. The first two are fixed; the third reads the field into a local
once, which is also the clearer code.

## 4. Model-based B-tree tests

The writer needs a model independent of the implementation:

- Generate a random sequence of insert/delete/update operations.
- Apply them to a reference model and to the on-disk tree.
- Compare key order, leaf links, free-node map, record counts and lookup results.
- Force node splits/merges at boundaries, including root changes.
- Compare names with Apple `FastUnicodeCompare` and composed/decomposed forms.
- After each operation, validate the tree and run the structural checker.

A model test that only checks "the file is there" is not enough; it must check
the catalog structure and `fsck_hfs` compatibility.

## 5. Differential tests against macOS

Create the same tree on macOS and on the device/test image:

- names (case, composed/decomposed, length boundaries, `:`/`/`);
- folders/files, rename, delete, mkdir, truncate, append;
- timestamps, BSD info, Finder info, flags;
- extents/overflow for large/fragmented files;
- allocation/free counts.

Compare names, sizes, types, metadata and `fsck_hfs` output. Compare CNID
uniqueness and monotonicity, not exact CNID values.

### 5.1 Disposable-image writer test

`tests/image/run-macos.sh` creates a fresh 16 MiB journaled HFS+ image under a
temporary directory and runs the host writer against it. It never accepts an
existing image or device path. The test:

- creates a folder and appends a 4096-byte file through `hfs_txn`;
- runs `fsck_hfs -fn` on the raw device and requires a clean report;
- mounts the image in macOS and verifies the content, file/folder modes and a
  current-year timestamp;
- renames the folder and file, remounts and verifies the new path, old-path
  absence, modes and timestamp;
- truncates, remounts and verifies size 0; deletes and verifies the folder is
  gone; runs `fsck_hfs -fn` after each stage;
- creates a scripted Unicode corpus (NFC/NFD, CJK, emoji, Hangul, combining
  marks, U+2126, U+F900, `:`/`/`, 255-unit and rejected 256-unit names) and
  verifies the macOS NFC tree and `fsck_hfs` clean;
- creates 192 one-block files, deletes every second one and appends eighty
  blocks to a new file one call at a time, so the fork needs eight inline
  extents plus seventy-two single-block overflow extents spread over nine
  overflow records, and the volume's empty extents tree takes its first record;
  macOS reads the 327,680 bytes back with a per-block pattern, and `fsck_hfs
  -fn` stays clean;
- deletes a second overflowed file directly, with no truncate in front of it, so
  the delete path itself frees its inline and overflow blocks;
- truncates and deletes the first overflow file, requires the extents tree to be
  empty and the blocks to be free again, remounts and checks the paths are gone.

`fsck_hfs` zero repairs and macOS content parity are the pass criteria. This is
host evidence; it is not a device run and does not authorize a library write.
The free-block arithmetic is asserted too: creating the holes must lower
`free_blocks` by 24, deleting the twelve holes must raise it by 12, and the
overflow append must spend exactly twelve more. A delete that only removed
catalog records used to leave the count unchanged and the blocks allocated.

The host unit suites carry the same rules without macOS: `test_catalog_write.c`
(`append past overflow`) builds a fork past eight extents and reads it back,
`test_bitmap.c` (`header updates compose`) pins the one-transaction volume-header
merge, and `test_btree_tree.c` asserts a freed node is erased.

### 5.2 Journal crash injection

`tests/image/run-crash-macos.sh` copies a clean journaled HFS+ image for every
cut point of a create transaction, fails the nth device write, then reopens the
copy, replays the journal and runs `fsck_hfs -fn`. The loop stops when the cut
is beyond the transaction's write count. It expects the file to be absent when
the cut precedes the journal commit header and present after replay when the
commit header was written; every case must be fsck-clean. The
create/append/truncate/delete/rename/overwrite matrices currently cover 346
cuts: 152 write cuts (create 25, append 34, truncate 25, delete 25, rename 21,
overwrite 22), 42 flush cuts and 152 partial-sector cuts.

Two further properties are checked at every cut point. **Replay is idempotent:**
`replay-twice` recovers the image, opens it again and recovers again, and the
second pass must find a clean journal and the same tree. **A fault during replay
still converges:** `replay-cut N` fails the nth write of the replay itself,
leaving the image half-replayed on purpose; a later replay must land on one of
the two legal states, not a mixture, and `fsck_hfs -fn` must agree. The
double-fault pass is sampled (`DOUBLE_CUTS`, `DOUBLE_SCENARIOS`) because each
point costs an attach, a replay and an fsck. `SCENARIOS="create grow"` narrows a
run while iterating. Power-loss timing and the remaining operations are T-010.

### 5.3 Write-diagnostic outer/inner image test

`tests/image/run-write-diag-macos.sh` creates a 32 MiB journaled outer HFS+
image and a 16 MiB journaled inner HFS+ image, copies the inner file into the
outer root as `helikon-test.img`, and runs the same core as the bootloader.
The core maps only the inner file's extents, runs folder/file create, append,
readback, rename, truncate and delete, and the test extracts the inner image and
runs `fsck_hfs -fn` on both images. It runs the sequence twice to check that the
inner image is reusable and leaves no `diag` folder. This is host evidence for
the separate write diagnostic; it is not a device run.

### 5.3.1 The >128 GiB format path

`tests/image/run-format-macos.sh` (`make -C tests/image format-test`) creates a
140 GiB sparse image, attaches it, and runs the real
`tools/disk-mode-format.sh` against it. It checks the guards first — a slice is
refused as `--device`, a missing or empty backup record is refused, a path that
is not a block device is refused — then that `--dry-run` prints commands and
changes nothing. The real run must produce an Apple Partition Map with one
`Apple_HFS` volume, and the volume must be larger than 137,438,953,472 bytes;
`fsck_hfs -fn` must call it clean. Measured here: 150,189,596,672 bytes, fsck
clean.

This is the host half of "format the iPod above 128 GiB". The volume is not
mounted afterwards on purpose: macOS writes `.fseventsd` and `.Trashes` on a
first mount, and the test would then be describing a volume it changed.

### 5.3.2 The read-only structural check

`firmware/hfs/hfs_check.c` answers "does this volume's own metadata agree with
itself?" without writing anything: block size and count, every fork's extents
against the volume, fork sizes against their blocks, the allocation file against
the block count, the header's free-block count against the bitmap, the three
metadata B-tree headers through the reader's own validation, each tree's leaf
chain (it ends at the header's last leaf and holds the number of records the
header claims), the key order of the catalog and the extents tree across leaf
boundaries, every catalog record body against the structure its type promises,
each index node's child pointer, level and key against the subtree it points at,
and the file and folder counts the volume header claims against the records
actually present. The root
folder is excluded from the folder count because the format excludes it, and the
padding bits of the bitmap's last byte are ignored because macOS leaves them
zero — both rules came from running the check against real volumes, where the
first drafts produced false alarms.

The allocation file is read through its resolved extent map — the inline extents
plus whatever the extents overflow tree supplies — so a large volume's bitmap is
counted rather than skipped: the 140 GiB volume reads 36,594,065 free blocks both
ways in about 0.2 s. The pass is I/O-bound and belongs off the boot path.

```sh
make -C tests/hfsplus test_check            # 26 cases, one corruption each
make -C tests/image hfs-check
./tests/image/hfs-check disk.img            # 0 clean, 1 findings, 2 unusable
```

Its tests build a 125-block volume by hand and put exactly one thing wrong in
each case: two sibling folders in the wrong key order, a thread key that carries
a name, a file body cut from 248 bytes to 100, a header claiming one record more
than its leaf holds, two extents records swapped, an index key that sorts after
the subtree it points at, a child pointer past the last node, a leaf that claims
to be an index node, and three clean catalogs — one folder, one file, and a real
depth-2 tree with an index node over two leaves — that must report nothing at
all. The clean cases matter most: a checker that cries wolf is worse than none.

Run against real volumes: a 64 MB macOS `HFS+J` volume holding nested folders,
16 files and names such as `£ euro.txt` and `日本.txt` across a 128-node catalog
reports clean, which is the check that the key order really does match Apple's
FastUnicodeCompare; a 32 MiB volume reports clean and the same volume with
`free_blocks` bumped by five reports the mismatch; a 140 GiB volume formatted by
`tools/disk-mode-format.sh` reports clean with 36,594,065 free blocks counted
through its overflow extents. "Not checked" stays a separate field from
"problems", because a large volume must never look corrupt.

### 5.3.3 Coverage of the host suites

`tools/coverage.sh` answers a different question from any single test: which
lines of `firmware/` and `bootloader/` does any host suite ever execute? It
builds every suite with clang's source-based coverage, runs them, merges the
profiles and prints the files with the most unexecuted lines. It is a map, not a
gate: a file at 70 % is not a failure, it is a place to look.

The number to read is the last line. At the time of writing the suites reach
**82 %** of 16,126 lines, and the ten weakest files are:

| file | lines | covered |
|---|---|---|
| `firmware/hfs/hfs_btree_tree.c` | 1549 | 76.6 % |
| `firmware/codec/libmad/layer3.c` | 1281 | 81.7 % |
| `firmware/codec/flac/flac_decoder.c` | 699 | 81.0 % |
| `firmware/usb/usb_msc.c` | 573 | 77.0 % |
| `firmware/hal/6g/usb.c` | 563 | 76.9 % |
| `firmware/hfs/hfs_catalog_write.c` | 988 | 87.0 % |
| `firmware/codec/libmad/layer12.c` | 241 | 46.9 % |
| `bootloader/inner_image.c` | 234 | 59.4 % |
| `firmware/usb/disk_mode.c` | 306 | 69.9 % |
| `firmware/block/partition.c` | 179 | 60.9 % |

Two lessons came out of building it. `tests/firmware` compiles its objects with
`HOSTCC`, not `CC`, so a coverage run that sets only `CC` links uninstrumented
code and reports whole modules as untouched — the script sets both. And
`bootloader/loader_error.c` is the only file no test executes at all: the
`strerror` table for the loader's diagnostics. Everything else is reached by
something.

```sh
./tools/coverage.sh                       # writes build/coverage/coverage.txt
COVERAGE_IMAGE=/tmp/vol.img ./tools/coverage.sh   # also runs the library suite
```

The suites are left instrumented, so run `make -C tests/<suite> clean` before a
normal build.

### 5.3.4 Random write round-trip

`tests/image/hfs_ops_test.c` drives the catalog writer through a deterministic
pseudo-random sequence of create, append, truncate, rename and delete operations
on a disposable image, keeps a model of what the volume should hold, and asks the
read core after every single operation whether it agrees. On top of that it
checks that every block a live file claims is marked allocated in the bitmap and
that no block belongs to two files — the two failures that end in silent data
loss rather than an error. `tests/image/run-ops-macos.sh` adds the outside
oracles: `hfs-check` over the whole volume and `fsck_hfs -fn`, after each seed.

```sh
make -C tests/image ops-test          # 4 seeds x 200 operations, ~10 s
tests/image/run-ops-macos.sh 8 300    # the soak, green on 2026-11-22
```

The seed and operation index are the whole reproduction: every random draw comes
from one LCG, so a failure prints the two numbers that rerun it, and
`hfs_ops_test IMAGE dump NAME` prints a file's inline extents and overflow
records so the state can be read instead of guessed at.

It found three real bugs, all three now fixed with unit regressions:

1. **Shrinking a file inside its last block failed.** `hfs_catalog_truncate_to`
   asked the fork writer to zero the whole tail of the last kept block, which
   reached past the old EOF and came back `ARGUMENT` — so `truncate(2)` of a
   file whose size was not a block multiple did nothing. The zero range now stops
   at the old size (`test_truncate_inside_last_block`).
2. **Clearing and rewriting the same overflow record failed.** When a truncate
   removed a fork's last overflow record and then wrote the smaller one back in
   the same transaction, the second delete walked a root of zero into the header
   node and returned `FORMAT`. `hfs_btree_delete` now reads the staged header and
   answers `NOT_FOUND` for a tree the transaction already emptied.
3. **Appending three blocks in one call lost the third.** A fork with seven
   inline extents (nine blocks) grew by three blocks at 219, 220 and X: the
   first started a new extent, the second extended it, and the third had to go
   into an overflow record keyed by the fork's covered block count.
   `append_tail_extent` extended the extent without moving that count, so the
   record was written one logical block early — the fork claimed twelve blocks,
   eleven were covered, and the twelfth was a hole no reader could reach and a
   block the bitmap kept allocated. Found at seed 3, operation 157, file `n22`;
   `test_append_extends_new_extent` reproduces exactly that allocation order and
   fails without the fix. The soak `run-ops-macos.sh 8 300` is green.

### 5.4 Firmware-loader host test

`tests/bootloader/test_ed25519.c` checks the RFC 8032 vectors, a fixed 192-byte
manifest-length vector, tampered signatures, bad arguments and the public-key
table. `tests/bootloader/test_loader.c` checks SHA-256 vectors, manifest
magic/board/address/size/hash/signature, a real Ed25519-signed manifest, missing/
corrupt/truncated boot state and newest-failed fallback.
`tests/bootloader/test_loader_hfs.c` checks the HFS+ file-read adapter.
`tests/bootloader/test_certificate.c` checks a root-signed release-key
certificate and a revocation bundle minted with
`tools/make-key-certificate.py`: parsing, root signature, image-version range,
rotation with two ranges on one key ID, a revoked key, an unauthenticated bundle
that fails closed for every key, a foreign issuer, and tampered payload, key,
range or signature bytes.
`tests/hfsplus/test_catalog_index.c` builds two- and three-level catalogs and
checks nested lookup, shared-leaf descent and cross-subtree leaf scanning. It
also runs 1,000 deterministic mutations of the catalog header/index/leaf bytes
and calls lookup under sanitizers to catch bounds and malformed-index bugs.

`make -C tests/image loader-test` creates a disposable HFS+ image with
`/.helikon/firmware.manifest`, `firmware.img` and `bootstate`, signs the
manifest with a generated Ed25519 key, then runs `loader_host` through the HFS+
core and requires `loader: OK` and `bootstate: GOOD`. It also rejects a wrong
public key and an unknown key ID. No jump or write is performed.

The loader fixture and the test-image builder require Python `cryptography` for
Ed25519 key generation and signing. The private test key stays under
`build/loader-bootloader/`; it is never committed.

The loader test image has two additional gates:

```sh
./tools/build-loader-bootloader.sh
./tools/check-loader-artifact.sh
```

The build checks `MOVE_AREA` headroom, the image marker and the DFU dry run.
The artifact check installs the exact generated `firmware.img`,
`firmware.manifest` and `bootstate` into a disposable HFS+ image and verifies
them through the same HFS+ core. Neither gate is a device run.

The loader image also contains the owner Alpha splash generated by
`tools/make-bootloader-splash.py`: exact sRGB variants, a 16-colour 4bpp base
and a 4bpp HFS+ label mask. The generator self-checks the nibble packing. The
font file is owner-provided and is not committed; see
[`../assets/bootloader/README.md`](../assets/bootloader/README.md).

### 5.5 Outer-volume write-diagnostic test

`make -C tests/image outer-diag-test` (driver
`tests/image/run-outer-diag-macos.sh`) creates a disposable 32 MiB journaled
HFS+ image and runs the same core as the outer diagnostic. It checks:

- the full create/append/readback/rename/truncate/delete sequence and
  `Restored: PASS` (root file/folder counts and no test folder);
- `fsck_hfs -fn` clean after the sequence;
- a committed-but-unreplayed journal is refused with zero writes, and macOS
  `fsck_hfs` can replay it;
- a missing opt-in file is refused with zero writes;
- a pre-existing unmarked or marked `/helikon-outer-test` is left untouched;
- a sentinel file and the opt-in file are unchanged;
- a second run succeeds on the same volume;
- a disposable image with a patched 4 KiB journal header runs the same sequence
  and stays `fsck_hfs` clean, covering whole-sector volume/bitmap staging.

This is host evidence for T-047. It is not a device run and does not authorize
a library write.

### 5.6 Journal format compatibility

The multi-level B-tree model test and the catalog append/truncate tests exercise
same-key in-place record updates, so a record update does not force a delete and
reinsert. The journal unit test checks the Apple `block_list_header` layout: `max_blocks`
is `blhdr_size / 16 - 1`, the first header sets `BLHDR_FIRST_HEADER`, the
checksum covers the first 32 bytes, and the first `block_info` is a zero
continuation marker. A separate host integration check leaves a committed but
unreplayed journal and requires macOS `fsck_hfs -fy` to replay it to a clean
volume. This closes a compatibility gap where the writer's own replay worked
but macOS rejected the journal.

### 5.7 Firmware host harness and bring-up ladder

Firmware tests are specified in
[`firmware-architecture.md`](firmware-architecture.md) §12. The core modules are
built for the host with a fake block device, deterministic clock, scripted input
and frame capture; the same `firmware/lib/` code is linked where possible. The
host harness is a fast loop, not a device result.

The F1 tree has an initial host suite. It compiles the freestanding modules with
`-DHELIKON_HOST_TEST` and the target-style memory names, then links the test
driver against the host toolchain:

```sh
make -C tests/firmware test
make -C tests/firmware sanitize
make -C tests/vfs test
make -C tests/vfs sanitize
```

The launcher and music browser have host previews and checks:

```sh
make -C tests/ui preview
```

It renders the real firmware framebuffer to `build/ui/launcher.png`,
`build/ui/music-*.png` (hub, albums, artists, tracks, artist overview, artist
albums, artist tracks, album, track) and `build/ui/audio-*.png`
(now-playing, paused), and checks the launcher focus marks, the hub focus mark,
both wall headers and frames, the tracks header, the artist tab
underline/covers/track row, the monogram fallback, the playback progress bar
and the player chunk/position logic.

The WAV container and player state are host-tested separately, including the
pull-source open (`helikon_player_open_media`) and the short-read WAV path:

```sh
make -C tests/player test
make -C tests/player sanitize
```

The USB stack has two host suites. `tests/usb` drives the device core the way a
laptop does — descriptors, every control request, CBW/CSW and the SCSI commands
over a sparse 4 TB fake disk — and `test_usb_hal` runs the controller driver's
register sequence against a mock DesignWare register file, a mock FIFO pair and
a mock VIC/clock, so an init mistake shows up on the host instead of as a silent
device that never enumerates:

```sh
make -C tests/usb test
make -C tests/usb sanitize
```

`tests/usb/fuzz_msc` sends malformed CBWs, hostile CDB lengths, a data stage that
stops halfway, a read-only device and an I/O-fault device through the same
transport, 50,000 commands per case from a fixed seed:

```sh
make -C tests/usb fuzz
make -C tests/usb fuzz-sanitize
```

It asserts that the window never moves, that no sector outside it is touched,
that a read-only device takes no writes, that the CSW is always well formed and
that a device error is never reported as success. The seed is printed, so a
failure reproduces.

`tests/usb/fuzz_mode` covers the layer above: the phase machine and the
enumeration state. Random control requests, CBWs of the wrong length, data
arriving at the wrong moment, completions nobody asked for, resets and
disconnects, then a liveness check — after any walk, a bus reset and a fresh
`SET_CONFIGURATION`, a valid INQUIRY must still be answered within a bounded
number of completions. It also runs T-115's 100 connect/disconnect cycles on the
mock, which is where a leak between cycles shows up.

The mode machine also builds into a separate firmware image:

```sh
PATH="$HOME/.rockbox-toolchain/bin:$PATH" make -C firmware BOARD=6g DISKMODE=1
```

It lands in `build/firmware/6g-diskmode/` — a variant directory, because the
flag changes `boot/firmware_main.c` and a stale object would produce an image
that claims to be one thing and behaves as the other. The image is 40,524 bytes
and contains no `hfs_volume_probe` symbol: it cannot mount the volume it hands to
the host.

A third suite, `test_disk_mode`, runs the mode machine with a fake transport:
the opt-in gate, descriptors and SET_ADDRESS, a whole-disk 4 TB window,
INQUIRY, a read and a write above 128 GiB landing on the fake medium, the
read-only DATA PROTECT refusal, eject releasing the medium and a bus reset.

None of the three touches a device, and the HAL suite proves the sequence, not
the silicon: only a 6G run can do that.

A storage-backed decode has its own differential test: `tests/library` mounts a
real journaled HFS+ image (created by the script), decodes `/Music/first.mp3`
through the VFS and the read-ahead window, and requires the PCM to match a
resident-buffer decode of the same bytes byte for byte (`audio-formats.md` §22).

Every host suite that links `firmware/codec/codec.c` also links the vendored
libmad objects, because the codec registry references the MP3 descriptor even
when the suite never decodes an MP3. The object rules live in one fragment,
`tests/libmad.mk`, included by `tests/codec`, `tests/player`, `tests/ui`,
`tests/vfs` (it proves the VFS media adapter against the real codec registry)
and `tests/library` (it decodes audio from a real volume). A suite that builds
the codec layer has to include it; a missing link shows up as an undefined
`helikon_mp3_codec_ops`.

The fragment deliberately defines no directory targets: the first target of an
included makefile becomes the includer's default goal, which silently turned
`make -C tests/vfs` into a no-op until it was removed.

The HelikonDB reader is tested against a generated fixture, with an optional
real corpus. The storage path — a catalog *and one audio file* read from a real
HFS+ volume through the read-only VFS — has its own macOS-only image test, which
creates and unmounts the image itself and never touches a device:

```sh
make -C tests/library macos
make -C tests/library macos-sanitize
make -C tests/library test IMAGE=/path/to/hfs-image
make -C tests/library sanitize IMAGE=/path/to/hfs-image
```

The image must hold `/.helikon/music.hdb` and `/Music/first.mp3`; `macos` and
`macos-sanitize` create it themselves and copy
`tests/codec/fixtures/mp3_cbr_stereo16.mp3` there, so a hand-built image has to
carry both files or the test fails on purpose.

For reference, the reader suite:

```sh
make -C tests/musicdb test
make -C tests/musicdb real REAL=/path/to/music.hdb
```

It also builds `test_library_index`, which is the library glue: it indexes every
track path in the fixture, checks that each path resolves to its own track, and
resolves a playlist built out of real fixture paths plus an absent one and a
refused one — asserting the queue order and that the two failure kinds are
counted separately.

The reference builder is:
`python3 tools/hdb_build.py build MUSIC_DIR --out music.hdb --sample 100`.

The functional library image embeds an HDB in the signed firmware and is
checked without a device:

```sh
make -C firmware BOARD=6g VERSION=14 BOOTSTATE_STATE=good UI=1 \
  HDB=../build/helikondb/first100/music.hdb \
  EXTRA_CFLAGS='-DHELIKON_DIAG_LAUNCHER' \
  BUILD=../build/firmware/6g-music image
./tools/check-loader-artifact.sh build/firmware/6g-music/out
```

It does not touch storage; the device run remains the acceptance gate.

The groups are:

- `test_lib` — mem/string/endian/CRC-32/fixed-point/bounded format;
- `test_boot_info` — D-28 page magic/version/size/CRC/board/range/manifest and
  `r0 = 0` fallback;
- `test_bootstate` — SHA-256 vector, slot parse/checksum, TRYING→GOOD
  promotion, two-slot sequence ordering, mismatch and corruption rejection;
- `test_block` — block bounds, read-only rejection, writable sector writes,
  aligned/unaligned write bounce, APM/raw partition scan, malformed signature
  and I/O error;
- `test_stages` — boot-stage order, completion, failure, timeout and the F2
  `WRITE` rung;
- `test_hfs_file` — one-path HFS+ read, offset, missing file, bounds and
  directory rejection;
- `test_ring`, `test_pool` — bounded ring wrap/overwrite and fixed pool
  exhaustion/double free;
- `test_screen`, `test_trace` — text pixels/clipping and trace wrap.
The target build is separate:

```sh
make -C firmware BOARD=6g          # ELF + raw image
make -C firmware BOARD=6g size     # map/size/headroom and symbol checks
make -C firmware BOARD=6g image    # signed development image
make -C firmware BOARD=6g VERSION=2 BOOTSTATE_STATE=trying image  # F2 image
```

`tools/check-loader-artifact.sh build/firmware/6g/out` verifies the F1-style
generated manifest, image and bootstate through the host loader core.
`tools/check-f2-artifact.sh build/firmware/6g/out` installs the exact F2 set in
a disposable HFS+ image, requires the loader to report `DEFAULT` before
promotion, promotes the slot through the firmware-side path, requires a clean
`fsck_hfs`, and then requires the loader to select `GOOD`. Both are host
evidence, not device runs.

The F2 disposable-image test is `tests/image/run-f2-macos.sh` (also reachable as
`make -C tests/image f2-test`). It creates a 16 MiB journaled HFS+ image, starts
the bootstate `TRYING`, promotes it through `hfs_catalog_overwrite_file` and
`hfs_txn`, checks that the second run is a no-op, verifies the manifest and image
hashes are unchanged, runs `fsck_hfs`, and asks the real loader host core to
select `GOOD`.

The crash matrix now includes an overwrite scenario: the harness injects write,
flush and partial-sector failures across the in-place overwrite transaction and
requires every recovered file to be exactly the old bytes or exactly the new
bytes, followed by a clean `fsck_hfs`.

The device bring-up ladder is recorded rung by rung: banner, ATA identify,
partition scan, HFS+ probe, read one known file, input, bootstate promotion,
browser, first UI, first audio. A rung is not skipped because a host test
passes. The F1 image `d7916411…` is built and host-verified and has one
owner-reported 6G device run (2026-11-04), accepted as the T-054 gate. The F2
image `1d6d4c92…` (76,080 bytes) is host-verified, but the write gate is
unfinished and unconfirmed due to a currently unfixable bug. It is **not** part
of Alpha 2.0.0 and has no confirmed 6G result.
[`f2-bootstate-test.md`](f2-bootstate-test.md) is its runbook.

### 5.8 The macOS differential: catalog order and a read of what macOS wrote

A reimplementation of Apple's `FastUnicodeCompare` cannot be proved correct by a
unit test: the expectation would be our own understanding of it again. What can
be checked is the effect. HFS+ stores a directory's entries in a B-tree ordered
by that comparator, so **macOS's own catalog is the oracle** — if our comparator
re-sorts the names macOS stored and reproduces Apple's order, the two agree on
every pair in the corpus.

`make -C tests/image unicode-order-test` (`run-unicode-order-macos.sh`) does that
and the read half against one disposable 24 MiB journaled image:

1. macOS creates 47 names under `/Order` in a shuffled order — digits, case
   pairs, punctuation, precomposed and decomposed Latin, canonically reordered
   combining marks, Greek, Cyrillic, Hebrew, Arabic, Devanagari, CJK, Hangul,
   kana, emoji, fullwidth forms, and both sides of the 255-unit name limit.
   Shuffled, because otherwise the creation order and the comparator order would
   be the same by accident.
2. macOS also writes a small tree under `/Tree`: a zero-length file, one byte, a
   whole block, a partial block, a 70,000-byte file that needs many blocks, and
   two levels of folders.
3. `unicode_order_host list IMAGE /Order` reads the catalog back through our
   iterator — that is Apple's order. `unicode_order_host sort` re-sorts the same
   names with our comparator. The two files must be identical.
4. `unicode_order_host canon` normalizes the requested names and compares the set
   with what macOS stored, which is the normalization half of the same question.
5. `unicode_order_host tree IMAGE /Tree` prints `path<TAB>kind<TAB>size` for
   every entry; the macOS side prints the same from `os.walk`. Names, kinds,
   sizes and nesting must match.
6. `unicode_order_host read IMAGE /Tree/sub/nested.bin` hashes to the SHA-256
   macOS wrote, which walks the extents path over a real fork.
7. `fsck_hfs -fn` on the image, no repair, as everywhere else in this directory.
8. **The writer half.** A second disposable image, and `hfs_image_test IMAGE
   order NAMES_FILE` creates `/Order` and the same 47 names through our writer
   (`hfs_catalog_create_folder`, `hfs_catalog_create_file`, one transaction per
   name). Then `fsck_hfs` has to accept the tree, macOS has to read exactly those
   names back, and `unicode_order_host list` has to produce the *same order*
   Apple's image produced for the same names — our B-tree's insertion order
   against Apple's, not just a set comparison. The names are UTF-8 in the file
   and go through `hfs_utf8_to_utf16be()`, so the encoder meets a real volume
   here.

`unicode_order_host` is host tooling in `tests/image/`. It opens the image
read-only, prints names and records on stdout and diagnostics on stderr, so
nothing but the answer can be compared — a warning that leaked into stdout would
turn a real difference into a pass. The corpus, the image and the mount live in a
temporary directory; the script never accepts an existing image or a device.

The first run of the ordering half was made to fail on purpose: inverting the
comparison in `hfs_fast_unicode_compare` produced a red test with the mismatched
pairs printed, so the differential is known to be able to fail rather than
observed to pass. The writer half has two oracles of its own — the same
inversion makes `fsck_hfs` report `Keys out of order` and writes the catalog in
reverse, so a writer that misplaces a key fails twice over.

`hfs_utf8_to_utf16be()` is the encoder the writer needs for a UTF-8 name; it is
the exact inverse of `hfs_utf16be_to_utf8()`, refuses overlong forms, surrogate
halves, NUL, anything past U+10FFFF and names over 255 units, and never
normalizes (that stays `hfs_unicode_normalize_name()`'s decision). Six tests in
`tests/hfsplus/test_unicode.c` cover round trips, the surrogate pair, every
rejection class, both length limits and that a composed and a decomposed name
reach the same canonical key through it.

## 6. Device tests

Device tests require a recorded inventory and backup. Minimum matrix:

- cold/warm boot, battery-disconnect boot;
- HDD, iFlash SD, iFlash CF, SSD;
- 100-cycle sleep/wake and resume;
- playback, seek, next/previous, volume, line-out/headphone, charging;
- USB read-only mount and log export;
- Helikon Disk Mode enumeration, read-only mount, writable 1 GB host transfer,
  safe eject, surprise unplug, journal replay and Apple Disk Mode fallback;
- DFU install/uninstall and Apple recovery;
- update/rollback and migration;
- per-target 7G/port matrix before a support claim.

Record the exact model/revision, adapter/card, firmware/bootloader hash, test,
result and evidence in [`device-matrix.md`](device-matrix.md).

### 6.1 Helikon Disk Mode and LBA48

Disk Mode has a separate bring-up ladder because it adds USB and a host-owned
write path. The canonical matrix is [`disk-mode.md`](disk-mode.md) §13.

- Host unit/mock: SCSI parser, CBW/CSW, descriptors, LUN mapping, LBA48
  encoding, malformed commands.
- P0: read-only Apple Disk Mode evidence on macOS.
- P1: read-only ATA/LBA48/APM probe on one 6G/iFlash SD.
- P2: enumeration on macOS; 100 connect/disconnect cycles.
- P3: read-only mount and file read.
- P4: 1 GB host write + SHA-256 read-back + `fsck_hfs -fn`.
- P5: LBA 2^28−1/2^28/2^28+1 and last LBA; >128 GiB volume or second partition.
- P6: surprise unplug, journal replay, Apple Disk Mode fallback, Helikon boot
  after host write.
- P9: SCSI/BOT fuzz, 100 cycles, throughput and battery.

No Disk Mode test may run on the only copy of the library. A verified backup is
required before P4.

## 7. Power-loss tests

Cut power at every journal and metadata boundary for:

- create, append, close, rename, delete, truncate, mkdir;
- settings save;
- database install;
- artwork install.

After every cut:

1. Boot/mount.
2. Capture the journal state.
3. Run `fsck_hfs -n`; zero repairs is required.
4. Compare the recovered tree with the committed state.
5. Repeat at least 100 times per operation.
6. Copy the image before/after replay for diagnosis.

A cut that leaves an unreadable directory, a non-idempotent replay, or an
ignored journal is a stop condition.

## 8. Battery, RAM and performance tests

- Battery: same model/card/album/volume/backlight/output; at least 3 runs; report
  mean and range; record the Rockbox baseline commit/hash.
- RAM: peak watermark on a 10k-track library after browse, play and database
  build; 72-hour soak.
- Performance: sequential/random read/write, 1 GiB copy, 1,000-file create/
  delete; publish method and firmware hash; no read regression claim without
  data.
- Do not run a benchmark on a volume containing the only copy of the data.

## 9. Skin, app and car tests

- **Skins:** 20-skin corpus, malformed/hostile skins, unsupported tags, frame/
  memory caps, fallback. Record licences.
- **Apps:** install/launch/suspend/resume/uninstall, permission denial, quota,
  crash isolation, escape attempts, update preserves private data.
- **Car:** 100 ignition cycles, resume, charging, no pops, receiver matrix;
  Tier 2 controls optional and legally reviewed.

## 10. How to add a test

1. Identify the layer and the invariant/goal it proves.
2. Add a deterministic fixture or generator; record its hash.
3. Write the smallest test that fails on the bug and passes on the fix.
4. Assert the specific error code/output, not just "non-zero".
5. Add it to the documented test command and `testing.md`.
6. Run sanitizers/fuzzers if the code parses input.
7. Run `./tools/static-analysis.sh` over the file you touched. A finding is
   either a fix or a sentence in the pull request saying why it is not one.
8. End the test with its report call, so a failure can fail the build.
9. Record evidence in the PR/log; update `validation.md` if it is a release gate.

Do not add a test that requires a private device, network or absolute path. Do
not add a test that "passes" by skipping when a fixture is missing without
reporting the skip.

## 11. Current vs future commands

**Current:** the host HFS+ read core has a real test command:

```sh
make -C tests/hfsplus test
make -C tests/hfsplus sanitize
make -C tests/bootloader test
make -C tests/bootloader sanitize
make -C tests/firmware test
make -C tests/firmware sanitize
make -C tests/vfs test
make -C tests/vfs sanitize
```

`tools/host-tests.sh` runs the portable suites in one go and is the command to
quote in a pull request: `./tools/host-tests.sh clang` for the suites,
`--sanitize` for the same code under ASan and UBSan, `--macos` for the
disk-image suites. The macOS disk-image runbooks keep their own commands:

```sh
make -C tests/image test            # disposable image, create/append/rename/truncate
make -C tests/image crash-test      # write/flush/partial-sector injection
make -C tests/image format-test     # the >128 GiB APM + HFS+ format path
make -C tests/image loader-test     # signed manifest through the read core
make -C tests/image ops-test        # randomized write round-trip
make -C tests/image write-diag-test # write diagnostic, inner and outer image
make -C tests/image outer-diag-test # outer-volume diagnostic
```

The target firmware build is:

```sh
make -C firmware BOARD=6g
make -C firmware BOARD=6g size
make -C firmware BOARD=6g test
make -C firmware BOARD=6g image
```

`firmware/Makefile` keys its object files on a configuration stamp that records
`BOARD`, `VERSION`, `UI`, `AUDIO`, `AUDIO_TRACK`, `HDB` and `EXTRA_CFLAGS`
(`../build/firmware/<board>/config.stamp`). Changing any of them rebuilds; an
unchanged rebuild stays incremental. This replaced a stamp that only tracked
`BOARD`/`VERSION`, under which a music-hub build left UI/AUDIO/HDB objects that
the next plain or F2 build linked — the F2 artifact check once validated a
music-hub image. When a build's size looks like another configuration's, check
the stamp before trusting the evidence.

Donor commands remain reference-only; they do not test Helikon-OS:

```sh
# Donor reference only.
make -C tools/hfsplus test
make -C tools/hfsplus sanitize
python3 tools/hfsplus/tests/check_integration.py
```

**Future:** add exact commands for B-tree/catalog/extents, fuzzing, model
property tests, device tests and power-loss runs when those trees exist. Do not
invent them before the files exist.

## 12. CI

Two workflows, both in `.github/workflows/`, both host-only.

**`tests.yml`** runs on every push to `main` and every pull request.

| Job | Runner | What it does |
|---|---|---|
| `portable` | ubuntu | `tools/host-tests.sh clang --keep-going` — the nine portable suites |
| `sanitize` | ubuntu | the same suites under ASan and UBSan |
| `fuzz` | ubuntu | `tools/fuzz.sh --replay`, then 20 guided seconds per harness; a red run uploads the reproducer and the log |
| `analyze` | ubuntu | `tools/static-analysis.sh --strict` over 74 translation units; a red run uploads `build/static-analysis/` |
| `macos` | macos-latest | `tools/host-tests.sh clang --macos` — the disk-image suites: `hdiutil`, `fsck_hfs`, the mount. `HOST_TESTS_MACOS_SKIP=format-test`, because a 140 GiB sparse image does not reliably fit a hosted runner |
| `build` | ubuntu | `make -C firmware BOARD=6g image` and `DISKMODE=1 image` with the distro ARM toolchain |

The Linux jobs exist so the code is not only correct on the owner's Mac, and
`--keep-going` is there so one run shows every red suite rather than the first.
That is not hypothetical: the first four runs of this workflow found that six
suites could not compile on glibc at all (`firmware/lib/error.h` was shadowed by
`<error.h>`), that `tests/ui` emitted Mach-O assembler for its HDB blob, and that
the boot-info parser compared an image address against a host page address. All
three were invisible on macOS. A red job uploads `build/host-tests/` as an
artifact.
The `build` job uses `arm-none-eabi-` with `-Wno-error`: the pinned Rockbox
`arm-elf-eabi` 9.5.0 stays the build of record, and a pull request must not fail
on a warning that toolchain does not emit.

**`preview.yml`** runs nightly and publishes a prerelease; see
[`release.md`](release.md) §1.1. Its first job is the same portable suite gate,
so a red tree publishes nothing.

CI cannot replace a device run. Power-cut, `fsck_hfs`-after-cut, battery, panel,
audio-measurement and USB-enumeration gates all need the hardware rows in
[`device-matrix.md`](device-matrix.md). A green CI run is evidence that the host
tests pass, and nothing more.

## 13. Related

- [`verification.md`](verification.md) — evidence template
- [`validation.md`](validation.md) — release matrix
- [`device-matrix.md`](device-matrix.md) — hardware rows
- [`hfsplus-write-engine.md`](hfsplus-write-engine.md) — writer invariants
- Paper §24 (validation strategy), §26 (build path), §27 (assembly)
