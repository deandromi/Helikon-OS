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
| Fuzzing and mutation sweeps | Malformed B-trees, extents, names, journal records and bitstreams do not crash or escape bounds; the writer fails closed | Host | `in progress` — 8 libFuzzer harnesses with a committed corpus, including a writer harness that mutates a whole volume and holds the writer to its two rules (§3), plus the HFS+ image and codec mutation sweeps (§3.2) | Reader, writer, journal, decoders |
| Model-based B-tree | Insert/update/delete, split/merge, key order, free-node map | Host | `in progress` — a 1,800-operation random sweep over the extents tree at 512-byte nodes, plus an ordered cascade that drains to the canonical empty tree, refills it from depth 0 and reuses it through a reopened handle (§4.1); index nodes are never merged, so a drained tree keeps one-child index nodes | Writer |
| Property tests | Allocation, ordering, commit atomicity, no lost/duplicate blocks | Host | `in progress` — the byte-range block layer has a 4000-range property test, the allocation bitmap has a 4,000-step model test (§3.3), the transaction overlay has one (§3.5), the journal commit path is cut at every write (§3.4), and the catalog runs random operations against a directory model plus `hfs_check_volume` (§3.6); a B-tree insert/delete model and the macOS differential remain `planned` | Writer/journal |
| Differential vs macOS | Same tree/names/metadata/`fsck_hfs` result on both sides; catalog order matches Apple's | Host + macOS | `in progress` — ordering and read differential run; writer-side tree comparisons partly covered | Writer |
| Bootloader diagnostics | Read-only volume/catalog/file report and splash | Host | `in progress` — 8 tests | Boot screen |
| Device | Boot, mount, playback, write, USB, sleep, recovery | 6G (then 7G/ports) | `not run` | Any hardware claim |
| Power-loss | Every metadata operation at every journal boundary | Device/rig | `not run` on a device; the journal commit path has a host crash-atomicity property test over every write boundary (§3.4) | Write release |
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

`tools/fuzz.sh` builds and runs the libFuzzer harnesses. Eight exist, each with
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
| `cuesheet` | `firmware/player/cuesheet.c` | track starts never go backwards and a pregap never follows its own start; every returned string stays inside the caller's buffer and inside its length cap; a refused sheet reports no tracks at all |
| `writer` | `firmware/hfs/hfs_catalog_write.c` over the shared writer fixture | a refused write changes nothing, and a commit never turns a volume the structural check called clean into an inconsistent one |

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

The `writer` harness is the one that fuzzes *writes* rather than a parser, and it
is the only one whose input is a whole volume: `tests/hfsplus/writer_fixture.c`
builds a valid 1 MiB image with the root folder seeded through the real writer,
and the harness applies fuzzer-chosen mutations - offset, width and value, up to
seven of them - before it probes, checks, opens the catalog and makes one call:
create, create folder, rename, write 1,500 bytes, truncate or delete. A refusal
must leave the image byte-identical; a commit must not add a finding to a volume
the check called clean. That is the same pair of rules `test_writer_mutations`
enforces, but the fields and values come from coverage rather than from a curated
list, which is what lets it reach combinations nobody wrote down. The fixture and
its geometry live in one place so the sweep and the harness cannot drift apart.

Anyone adding a parser - or a writer - should add a harness and a handful of seeds
in the same pull request. A crash, a hang or a violated invariant is a defect, not a "known
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

### 3.2 Mutation sweeps

The fuzzers above run on parsers that were given a harness. Two more layers get
the same treatment with no libFuzzer and no corpus: a valid input is corrupted a
few bytes at a time, and the *whole* read path is driven over the result. Nothing
is asserted about the answer — a corrupted input is allowed to be refused or to
produce nonsense — only that the code terminates, stays inside its buffers and
does not trip ASan or UBSan. That is the failure a hand-written test never finds,
because nobody thinks to corrupt the byte that matters.

| Sweep | File | What it drives |
|---|---|---|
| HFS+ | `tests/hfsplus/test_image_mutation.c` | `hfs_volume_probe` → `hfs_check_volume` → `hfs_catalog_open` → lookup/iterate/`read_file` → `hfs_extents_collect` over a mutated volume image |
| codecs | `tests/codec/test_codec_mutation.c` | `helikon_codec_probe`/`open`/`read`/`seek` over mutated FLAC and MP3 fixtures plus a built-in WAV (which also covers the RIFF chunk walker), then the same bytes through `probe_source`/`open_source`/`read` |

Both are deterministic — one seed, one sequence — and both print the fixture,
the seed and every changed byte on a finding, so a report replays exactly:

```sh
cd tests/hfsplus && ./test_image_mutation            # 1,500 passes
cd tests/hfsplus && ./test_image_mutation 7 40000    # another seed, longer
cd tests/codec   && ./test_codec_mutation            # 600 passes per fixture
MUTATION_TRACE=1 ./test_codec_mutation 7 20000       # and a trace per pass
```

The mutation policy is deliberate rather than uniform: half of the hits land in
the first 256 bytes (every format's header, and the volume header for HFS+),
widths are 1, 2 and 4 bytes aligned to their width, and half of the values are
extreme (`0`, `0xffff`, `0x7fffffff`, `0x80000000`, …) because those are the
values that make a bounds check fail. A quarter of the codec passes also truncate
the stream, which is how a decoder meets a half-written final frame.

Each sweep keeps counters and fails if it stops reaching the deep readers, so it
cannot quietly decay into a test of the probe: the HFS+ sweep printed
`40,000 iterations, 15,696 probed, 15,246 opened, 28,834 looked up, 14,991
entries, 14,788 overflow trees` on its last long run, and the codec sweep reports
its passes per fixture.

The codec sweep found two real defects on its first run
([#106](https://github.com/deandromi/Helikon-OS/issues/106),
[#107](https://github.com/deandromi/Helikon-OS/issues/107)): an out-of-bounds read
of the Xing TOC when seeking to the last frame, and a hard seek failure in the
last 1 % of a VBR file. Both are fixed and both have a hand-written regression
test in `tests/codec/test_codec.c`, because a mutation sweep proves that a bug
*was* found, not that it stays fixed.

Sanitizer scope: the vendored libmad objects are built with AddressSanitizer only
(`tests/libmad.mk`). Its fixed-point DSP overflows on any corrupt bitstream — all
upstream behaviour this project does not patch — while a read or write past a
buffer in the decoder is exactly what the sweep is looking for, and ASan keeps
that. Every remaining UBSan report is this project's code, so the codec suite now
runs with `UBSAN_OPTIONS=halt_on_error=1` and a finding fails the build.

### 3.3 Allocation property test

`tests/hfsplus/test_bitmap_props.c` allocates and frees random runs in a
128-block volume for thousands of steps against a model of what the bitmap
should say, committing every 16 steps and reopening the volume from the image so
the assertions are made against committed bytes, not a cached field. It names no
individual case; the failures it is built to catch are the ones a case-by-case
suite misses:

- a run handed out that overlaps an allocated one (a double allocation, which
  later overwrites another file's blocks);
- a free of a block that is not allocated, which inflates the free count until
  the volume claims blocks that do not exist;
- `free_blocks` drifting from the bits in either the volume header or the
  allocation fork;
- a search that misses a run that does exist, or refuses one that does not;
- the transaction overlay handing the same block out twice inside one commit
  window.

A second case builds a 5000-block volume whose bitmap spans two blocks, because
one bitmap block only describes `block_size * 8` blocks and a range can cross
that seam, and asserts that a refused free leaves `tx.count == 0`.

```sh
cd tests/hfsplus && ./test_bitmap_props             # 4,000 steps
cd tests/hfsplus && ./test_bitmap_props 7 20000     # another seed, longer
```

It found two defects on its first run, both fixed:
[#110](https://github.com/deandromi/Helikon-OS/issues/110) (the allocator never
wrapped, so a hint above the free space reported `NO_SPACE` with megabytes free)
and [#111](https://github.com/deandromi/Helikon-OS/issues/111) (`hfs_bitmap_free`
staged the first bitmap block before validating the second, leaving half-cleared
bits in the caller's transaction after a refusal).

### 3.4 Journal crash-atomicity property test

`tests/hfsplus/test_journal_props.c` asks the question a power cut asks: for
every write boundary in the commit path, is the volume either exactly as it was
or exactly as it should be, never in between?

The device keeps a write budget. A budget of `i` means the first `i` writes reach
the medium and every later one is silently lost — what a power cut looks like to
a caller whose write buffer was accepted and whose device then stopped existing.
For each random transaction the test records the before image, runs append and
replay with no budget (and checks the payload actually landed, so the sweep
cannot pass vacuously), then for *every* budget replays the whole path into it
and remounts: it reopens the journal from the medium and replays what the journal
says is committed. After that remount:

- every target sector holds either its whole payload or none of it;
- no two targets disagree about which — a half-applied transaction is a defect;
- no byte outside the journal and the targets moved;
- the journal is clean again.

The expected "committed" state comes from the transaction's own payload, not from
a reference run of the code under test, so a bug that misapplies a block
everywhere cannot hide by being reproduced everywhere. A third of the trials start
with the journal near its end so every replay wraps.

```sh
cd tests/hfsplus && ./test_journal_props             # 24 transactions
cd tests/hfsplus && ./test_journal_props 7 200       # another seed, longer
```

It found no defect (238 write boundaries over 24 transactions, plus 8 wrap
trials). To show it bites, two mutations were injected and reverted: committing
before the payload reaches the medium is caught by the sanity check, and a replay
that marks the journal clean before applying its blocks is caught by the sweep
with `FAIL transaction 0: 1 of 2 targets applied at write 6, the rest not`.

What it cannot prove: that the device honours the barrier. The budget model drops
whole writes and never reorders them, so a missing `flush()` between payload and
commit is invisible here — that is the hardware's job and the device matrix's.

The gate the sweep assumes is tested directly. Every write in this engine starts
from `hfs_txn_begin`, so that is where I-04 has to hold, and
`tests/hfsplus/test_journal.c` pins it: a journal header whose `start` and `end`
do not meet (committed, not replayed) and a volume header carrying the dirty flag
both have to answer `HFS_ERR_DIRTY` with the transaction left inactive, while a
clean journal still opens one. The last check is the one that matters: without it
a gate that refuses everything would pass.

```sh
cd tests/hfsplus && ./test_journal               # 10 cases
```

### 3.5 Transaction overlay property test

The overlay is why two updaters of the volume header can live in one
transaction: the second reads through the first one's staged bytes instead of
the committed ones, so it cannot silently drop the first one's change.
`tests/hfsplus/test_txn_overlay_props.c` models the staged bytes as a byte map
and hammers it with random stages and random ranges:

- `hfs_txn_overlay` patches exactly the staged bytes that intersect a range and
  leaves the rest of the caller's buffer alone — it never reads the disk, which
  is `hfs_txn_read`'s job, and the model expresses both that way;
- it answers 1 when anything intersected, 0 when nothing did, and a zero-length
  range touches nothing even when the offset sits inside a staged block;
- a later stage of the same bytes wins whatever its size: the exact range, a
  hole inside a run, or a range that overlaps the end of a run all leave the
  newest bytes in place, and the run keeps any bytes the new stage did not cover;
- runs that only touch stay separate entries — neighbours are different objects
  — while one stage that covers several of them folds them into a single entry,
  which is what keeps a 4096-byte node at one transaction slot (issue #115);
- a stage whose union with the runs it overlaps would exceed
  `HFS_TXN_BLOCK_MAX` is refused with `BUSY` and changes nothing;
- `stage` rules hold: a duplicate start sector is `BUSY`, the 33rd entry is
  `NO_SPACE`, a bad size is `ARGUMENT`, and a full transaction's overlay over
  the whole image still matches the model.

The expected answer is computed from the byte map, a second expression of the
contract rather than a call back into the code under test. The one prediction it
takes from the transaction's own runs is the union size, because a byte map
cannot tell two abutting entries from one run. After every step it also checks
that the byte map and the transaction's entries agree byte for byte and that no
two entries overlap.

```sh
cd tests/hfsplus && ./test_txn_overlay_props              # 4,000 steps
cd tests/hfsplus && ./test_txn_overlay_props 7 20000      # another seed, longer
```

It found no overlay defect. Teeth checked by mutation: widening the intersection
test to `block_end < offset || block_start > end` is caught by the boundary cases
with `FAIL rc == expected_overlap`.

### 3.6 Catalog operation property test

`tests/hfsplus/test_catalog_ops_props.c` runs a random sequence of creates,
deletes, renames, writes and truncates against a model of the directory tree and
asks the image after every operation:

- the children of every folder, by name, ID and kind, against the model;
- each folder's valence against the number of children the model says it has —
  a valence that drifts is a folder that later refuses to be deleted, or one
  that is deleted with its children still inside;
- each file's size and full contents, read back through the reader;
- the volume header's file and folder counts (which do not count the root
  folder, as `newfs_hfs` leaves a fresh volume at 0 and 0);
- `hfs_check_volume`, which independently re-walks the catalog and the extents
  tree, checks key order across leaf boundaries, the leaf chain, every fork
  against the allocation bitmap, the free-block count, and node ownership: every
  node reachable from the root must be marked used in the free-node map, every
  unreachable node free, and the header's free count must match the map.

The model is written from the documentation, not from the code: names are unique
per parent, a folder's valence is its child count, deleting a folder with
children is `NOT_EMPTY`, moving a folder inside its own subtree is `ARGUMENT`,
truncating past the end is `ARGUMENT`, and a refused call must not change one
byte of the image (checked against a snapshot). The fixture is a 1 MiB volume
with 4096-byte nodes — several file records per leaf, so inserts split nodes and
deletes merge them — and three trees, with the extents tree starting empty.

```sh
cd tests/hfsplus && ./test_catalog_ops_props              # 600 operations
cd tests/hfsplus && ./test_catalog_ops_props 2 1200       # another seed, longer
```

The committed sweep is 600 operations (about 90 creates, 66 deletes, 102
renames, 93 writes, 32 truncates and one case plus one composition collision),
plain and under ASan/UBSan. It found one
defect, issue #115: on a volume whose allocation block is 512 bytes, every
B-tree node cost one transaction slot per sector, so a root split needed 34 slots
and the catalog could never grow past depth 1. The fix is the staging coalescing
described in §3.5.

Names are mixed case, so the keys the tree holds are ordered by a folded
comparison while the bytes on disk keep their case: the model's own equality is
therefore case-insensitive too, written from the format rather than taken from the
comparator under test. After the random sweep a deliberate probe takes a name the
catalog holds, flips every letter's case, and requires both `hfs_catalog_create_file`
and `hfs_catalog_rename` to answer `HFS_ERR_BUSY` with the image byte-identical.
The sweep itself cannot reach that: it only ever asks for names the model says are
free. Teeth: replacing `hfs_catalog_name_compare`'s `hfs_fast_unicode_compare` with
a raw byte comparison makes the probe fail at once with `create fIjmjoaC: rc=0,
expected BUSY`, and passes again when the fold comes back.

Names are not ASCII either. Twelve letters of the alphabet are Latin-1
precomposed forms — `é`, `ü`, `ñ`, `ç`, `Å`, `Ö` and friends — which HFS+
canonical form decomposes into an ASCII base letter plus one combining mark. Every
call passes the composed spelling, one character per letter, and the catalog has
to store the decomposed one: the model keeps both, the typed form for the next
delete, write, truncate or rename to hand back to the writer, and the canonical
form for the children check to compare against the disk. So each entry point is
asked to normalize the name before it builds a key, and a path that skipped the
step shows up as a child whose stored bytes differ from the model's. A second
probe asks the other direction: the decomposed spelling of a name that exists must
be refused as `HFS_ERR_BUSY`, which is only true if the writer normalizes,
because FastUnicodeCompare does not decompose and would otherwise see two names.
Teeth, one mutation each: dropping `hfs_unicode_normalize_name` from
`create_object` fails at step 0 (`lists id 16 as üfwgRZh, model has it as
u + U+0308 fwgRZh`), dropping it from `delete_object` fails with `delete folder
üfwgRZh rc=-8` (`NOT_FOUND`), and dropping it from the new name in
`hfs_catalog_rename` fails at step 17. That the decomposition itself is the right
one is the Unicode unit vectors' job; what this file catches is a write path that
skips it, or applies it in one place and not another.

The last phase empties the catalog: files first, then folders with no children,
until only the root folder is left. It then requires the end state — the tree one
leaf deep holding exactly the root's two records, every other node back on the
free-node map, the root folder listing no children, the volume header counting no
files and no folders, `hfs_check_volume` clean — and three fresh creates, so a
catalog that drains but cannot be refilled fails. The committed sweep ends with
`24 deletes to empty, then depth 1, 62 of 64 nodes free and 3 children after the
refill`.

That phase is the end-state half, not the collapse half: with 4 KiB nodes a
catalog leaf is merged away long before it empties, so a leaked node in
`collapse_empty_leaf` does not show up here. The B-tree model test's 512-byte
nodes do reach it, and removing the `free_node` of the emptied leaf makes it stop
at `problems 0x00080000` (the node-ownership check).

### 3.7 What the property tests do not prove

They compare the writer against a model of the same host code, so they cannot
show that a device honours a flush, that the allocation bitmap matches what
macOS would do, or that `fsck_hfs` accepts the tree. Those stay device-matrix and
macOS-differential work.

## 4. Model-based B-tree tests

Status: `in progress`. §3.6 covers the catalog layer — creates, deletes,
renames, writes and truncates against a directory model, with `hfs_check_volume`
after every operation, which re-walks the tree and checks key order, the leaf
chain, every fork against the allocation bitmap, the free-block count and node
ownership (T-137: a leaked node or a reachable node marked free is a finding).

### 3.8 The write VFS, over a real journal

`tests/vfs/test_vfs_write.c` drives `firmware/vfs/vfs_rw.c` over a volume that
commits the way a device does. The fixture is the writer fixture plus a 64 KiB
in-FS journal (`writer_fixture_build_journaled`): an info block that names the
journal, a header with a valid Apple checksum, and a clean `start == end`. A
transaction therefore goes **append -> flush -> replay** through the real
`hfs_journal` and `hfs_txn`, not through the "apply the staged blocks by hand"
shortcut the pure writer tests use.

After every call the test checks two things that must both hold: the journal is
clean again, and `hfs_check_volume` reports zero problems. Every refused call is
preceded by a snapshot of all 1 MiB and followed by a byte comparison, so
"atomic" is an assertion about the image rather than a claim about the code.
The refusals cover a relative path, `/`, a nested path, `/..`, `/.`, a trailing
slash, a missing parent, a duplicate name, unlinking a folder, `rmdir` on a file,
a non-empty folder, truncating a folder, growing through truncate, and renaming
onto an existing name.

Two rules the test pins that are easy to lose in review:

- **A commit invalidates open handles.** A handle caches the fork it read; a
  write moves extents. The test reads through a handle, writes to the same file,
  and requires the old handle to answer `ARGUMENT` and a re-opened one to read
  the new bytes.
- **A long write is a short write.** `VFS_RW_IO_MAX` bytes per call is applied
  and counted, and the loop continues from there, so the caller can never
  believe 24 KiB landed when 8 KiB did.

A volume with no journal is refused with `UNSUPPORTED`: there is no
non-journaled writer to fall back to (I-02), and the test asserts that no byte
changes when a caller tries anyway.

#### 3.8.1 The same layer on a volume macOS formatted

The fixture proves the layer against a journal the test built. It cannot prove
that a volume Apple created is still sound after we write to it, so a second
run answers that:

```sh
make -C tests/image vfs-write-test        # macOS only
```

`tests/image/run-vfs-write-macos.sh` creates a disposable 16 MiB `HFS+J` image
with `hdiutil` (Apple's own journal layout, not ours), hands it to
`tests/image/vfs_write_host.c`, and then asks three outside questions:

1. `hfs-check` reports the volume clean (the read core's own structural check);
2. `fsck_hfs -fn` says `appears to be OK` and attempts no repair;
3. mounted read-only, macOS sees the tree the firmware says it left: a
   4096-byte `/helikon-vfs.txt`, the folder `/helikon-dir`, and
   `/helikon-dir/moved.txt` holding the text that was written before the rename
   — with the renamed-away source and the unlinked file both gone.

The host program writes 20 KiB in three `VFS_RW_IO_MAX` calls (the short-write
path), truncates to 4 KiB, creates and writes a file, renames it, and creates and
unlinks one, reading every result back through the read-only VFS in between. It
refuses a volume that is not journaled or whose journal is dirty: this is
evidence, not a repair tool.

Still not proved by either run: what a device does with a flush, and what
`fsck_hfs` says after a power cut mid-commit. Those are the crash matrix and the
device matrix, not host tests.

### 3.8.2 The signed staged-update client

`tests/update/test_update.c` drives `firmware/update/update.c` over the same
journaled fixture and against *really signed* fixture sets, so the Ed25519 path
is exercised rather than stubbed. The sets are minted by
`tests/update/make_update_set.py` from a fixed throwaway seed and committed as
bytes: signing needs a private key, no private key belongs in this repository,
and a test that needs Python and `cryptography` to run is a test that does not
run everywhere. `make -C tests/update fixtures-check` re-derives them and fails
if a committed byte has drifted.

What the suite requires:

- every refusal writes nothing: the test snapshots all 1 MiB before the call and
  compares after. A truncated image, an image whose hash is not the one the
  manifest signs, another board's set, a tampered signature, a development set
  under a release policy and a manifest naming more bytes than exist are all
  refused, each with its own error;
- `park` leaves the installed set alone — the recovery copy and the parked image
  exist, the live pair does not change, so a power cut at that point boots
  exactly what booted before;
- `commit` lands the manifest, the image and the boot state in **one**
  transaction: afterwards the journal is clean, `hfs_check_volume` reports
  nothing, the installed image and manifest are the staged bytes, and the boot
  state carries a TRYING slot for the new facts;
- the second install demotes the slot that described the replaced image, because
  the loader validates the selected GOOD slot against the manifest and a stale
  GOOD slot would turn the next boot into a recovery screen;
- a downgrade is refused (ARGUMENT) and so is a second commit.

The fixture sets are 8 KiB images with 4 KiB read chunks, so the streaming copy
loop runs more than once and the short-write path is covered.

### 4.1 The B-tree layer itself

`tests/hfsplus/test_btree_model.c` drives **one tree** rather than the catalog:
random inserts, deletes and in-place updates on the extents-overflow tree, whose
records are a 10-byte key and 64 bytes of data with no per-type body to keep
valid. The fixture is a 1 MiB volume with 512-byte nodes — about six records per
leaf — so a few hundred keys force splits, index growth, depth changes, leaf
merges and freed nodes. After every operation the test checks:

- `hfs_btree_find` returns every present key's record, byte for byte, and a
  missing or wrong body is a failure;
- the leaf chain holds exactly the model's keys, strictly ascending, with the
  right bodies — a split or merge that dropped, duplicated or reordered a record
  is caught here and nowhere else;
- a refused call leaves the image byte-identical. Two refusals are legitimate:
  `HFS_ERR_UNSUPPORTED` when a merge or rebalance would need a case the writer
  does not implement, and `HFS_ERR_NO_SPACE` when the tree is genuinely full;
  a delete that would empty the tree is **not** allowed to refuse (issue #125);
- `hfs_check_volume` reports nothing and skips nothing: key order, the leaf
  chain, forks against the bitmap, the free-block count and node ownership.

```sh
cd tests/hfsplus && ./test_btree_model                # 1,800 operations
cd tests/hfsplus && ./test_btree_model 2 2600         # another seed, longer
```

After the random sweep an ordered pass runs, because random deletes rarely empty a
leaf in the order that forces the cascades. It deletes the model's first key over
and over — the first record of the leftmost leaf, so the parent key is rewritten
every step — and then requires the canonical empty tree: `depth 0`, no root, no
chain, `leafRecords 0`, every node but the header node back on the free-node map,
and `hfs_check_volume` clean. Then it fills from empty with 120 ascending keys
(rightmost splits, a root that grows out of depth 0), requires depth 2 or more,
drains a second time, reopens the tree from the medium, and inserts three keys
through the reopened handle, so an emptied tree has to be usable and not just
consistent.

That pass found issue #125 on its first run: the last delete of a deep tree
answered `HFS_ERR_UNSUPPORTED`, because a long drain leaves one-child index nodes
behind and the empty-leaf path only handled some of those shapes. The fix (T-143)
is `collapse_empty_leaf`, which unlinks the emptied leaf, frees every ancestor
left without a child up to a root that either keeps records or returns to depth 0,
and stores the header on every success path — the first cut of it returned before
`store_header`, and the model test caught that at once with `leaf_next returned -3`
(the chain head still pointed at the freed leaf). A tree can now be emptied from
the top, which the extents and catalog writers both need: a truncate to zero
extents, or deleting the last file in a folder, reaches the same state.

The committed sweep is 1,800 operations: about 860 inserts, 570 deletes and 370
updates, reaching 270 keys at depth 3, with the tree growing from 64 to 70 nodes.
Longer sweeps over six seeds (2,600 operations, up to 513 keys and 128 nodes,
the whole fork) pass, plain and under ASan/UBSan.

It found issue #118: a tree whose free-node map covers exactly the nodes it has
could never grow, and both "the map is full" and "the fork has no extent slot"
were reported as `HFS_ERR_UNSUPPORTED` rather than `HFS_ERR_NO_SPACE`. The fix is
T-139: the map record widens into the header node's free space, the two capacity
outcomes answer `NO_SPACE`, and the attributes tree's fork is addressable too.
`test_btree_grow` pins each case; dropping the widening makes it fail, as does
turning either `NO_SPACE` back into `UNSUPPORTED`.

Teeth: dropping `map_set(header, node, 0)` from the free-node path makes the
model test stop at step 54 with `problems 0x00080000` from `hfs_check_volume`.

Index nodes merge as well (T-150). Something was missing there: a delete merged
two leaves and collapsed an emptied leaf through its ancestors, but nothing ever
paired two *index* nodes, so a deep tree that lost records kept a level per
surviving child. A one-child index node is not corrupt — every key below it is
reachable, key order holds, node ownership is right, and `hfs_check_volume` passes
— which is exactly why it went unnoticed for so long; it is a node that buys
nothing and can never be reused. `settle_index_nodes` runs whenever an operation
takes a record out of an index node: it merges the node with its next sibling (or
with its previous one when it is the last child) in key order when the combined
records fit, drops the absorbed node's record from the parent, returns the node to
the free-node map, and repeats one level up, because the parent's record count
just changed. When the two do not fit it moves a single child across from the
sibling, rewrites the one parent key that changed, and stops. `collapse_root` then
takes a level off a root left with one child.

The model test asks for that shape directly: `check_occupancy` walks the index
nodes from the root after every operation and rejects any with fewer than two
records. It failed on the first run of the existing sweep — `step 1000063, index
node 3 at level 1 holds 1 record(s)` — which is the evidence that the gap was
real and not a tidy-up. Teeth: disabling the merge fails the same step with the
same message, and disabling only the root collapse fails at `step 1000207` with
`index node 31 at level 0 holds 1 record(s)`.

The steal fallback is not reachable from this sweep — after a split a node is at
most half full, so a one-child node plus its sibling always fits — so it has a test
of its own (T-152). `tests/hfsplus/test_btree_index_merge.c` makes the shape
reachable by making the keys 236 bytes: a 512-byte node then holds two index
records and no more, and nine hand-built nodes can hold a full sibling next to a
node that is down to one child. Deleting the single record of the leftmost leaf
empties it, leaves the left index node with one child, and pairs it with the full
sibling: three records do not fit, so the sibling gives up its first child, the
parent key for it is rewritten, and both nodes stay. Teeth: replacing the steal
with `return HFS_OK` fails the test at `node 2 holds 1 records, expected 2`.

### 4.2 The write path under a corrupted volume

`tests/hfsplus/test_writer_mutations.c` asks the question the read-path sweep does
not: when the volume is already wrong, does the writer **fail closed**? It starts
from a valid 1 MiB volume with a seeded root, corrupts one structural field, and
then drives `hfs_volume_probe` → `hfs_check_volume` → `hfs_catalog_open`, and
then five write calls in the order a caller would make them — create a file,
rename it, write 1,500 bytes into it, truncate it back to zero, delete it — each
in its own transaction. Two rules hold for every iteration, and each of the five
calls is held to them:

- **a refused write changes nothing** — the image must be byte-identical to the
  mutated one, so nothing staged can reach the medium;
- **a write never adds damage** — a volume the check called clean must still be
  clean after the commit. On a volume that was already inconsistent the writer is
  allowed to commit (the anti-corruption gate, not the writer, decides whether a
  damaged volume is written at all), and the sweep only requires that the check
  still runs.

The curated set covers both volume-header copies, every fork record, and the
catalog's header node and first leaf — the fields a writer trusts. Fields whose
correct answer is a refusal (`nextCatalogID` 0, 1, 2 or `0xffffffff`; a full file count) are
marked as such and must be refused, not guessed.

The data half is what makes a mutation in the allocation bitmap, a fork record or
the extents-overflow tree reachable: the create only takes a catalog node, while
the 1,500-byte write allocates blocks, stages the bitmap and the volume header, and
the truncate has to give every block back. Both run before the delete so the
blocks the write took are freed by the file being removed.

```sh
cd tests/hfsplus && ./test_writer_mutations              # 34 curated + 400 random
cd tests/hfsplus && ./test_writer_mutations 12345 3000   # another seed, longer
```

The fixture's catalog has room to spare, so a create inserts into an existing
leaf and the delete leaves no node behind: node freeing is the B-tree model
test's coverage (§4.1), not this sweep's.

It found issue #120: a volume header whose `nextCatalogID` is below 3 made the
writer insert a record under a reserved CNID — the root's own ID when the field
read 2 — and the structural check did not look at the field, so the volume was
called clean until the first create. T-141 adds the writer guard and
`HFS_CHECK_CATALOG_ID` in the check. Teeth: skipping the file-count update fails
the sweep's pristine sanity case at once, and accepting a reserved CNID fails the
curated case.

Landed since this section was written: forced root changes and split/merge
cascades (T-143's ordered phase), mixed-case names whose folding decides both
order and uniqueness (T-144), the catalog drained to root-only and refilled
(T-146), names in composed and decomposed forms in the same model, so the
writer's normalization is exercised across splits instead of only by the unit
tests (T-149, §3.6), and index nodes that merge back into their siblings so a
drained tree gives the nodes back (T-150, §4.1).

Still `planned`:

- `fsck_hfs` on the result (macOS differential, §5) — the writer already runs
  against real `newfs_hfs` volumes in `run-ops-macos.sh`, but the model test's own
  fixture is hand-built and would not survive `fsck_hfs` for reasons unrelated to
  the tree.

A model test that only checks "the file is there" is not enough; it must check
the tree structure and `fsck_hfs` compatibility.

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
each tree's free-node map against the nodes it can actually reach (a node marked
used but unreachable is a leaked block, a node marked free but reachable would be
handed out over live records, and the header's free count must match the map),
the file and folder counts the volume header claims against the records actually
present, and the next catalog ID against the values a writer cannot use
(`HFS_CHECK_CATALOG_ID` when it is below 3, which would create a record under a
reserved ID, or `0xffffffff`, which cannot be followed because the increment
wraps to zero). The root
folder is excluded from the folder count because the format excludes it, and the
padding bits of the bitmap's last byte are ignored because macOS leaves them
zero — both rules came from running the check against real volumes, where the
first drafts produced false alarms.

The allocation file is read through its resolved extent map — the inline extents
plus whatever the extents overflow tree supplies — so a large volume's bitmap is
counted rather than skipped: the 140 GiB volume reads 36,594,065 free blocks both
ways in about 0.2 s. The pass is I/O-bound and belongs off the boot path.

A record and its thread record have to agree, in both directions: every file and
folder record must have a thread record keyed by its CNID that carries the same
parent and the same name bytes, and every thread record must name an item that
really exists under that parent and name with the CNID the thread is keyed by
(`HFS_CHECK_CATALOG_THREAD`). That is how a CNID is resolved back to a name, so a
missing thread is an item no inode lookup can find, and a thread naming the wrong
parent or name is worse: the lookup succeeds and returns the wrong file. The check
does it by searching the catalog through a second B-tree handle rather than a
table, so it costs one node buffer and no cap, and a catalog of any size is
checked in full. It found nothing on a pristine 16 MiB macOS `HFS+J` volume, which
is the rule: the pass must stay silent on what macOS writes.

```sh
make -C tests/hfsplus test_check            # 35 cases, one corruption each
make -C tests/image hfs-check
./tests/image/hfs-check disk.img            # 0 clean, 1 findings, 2 unusable
```

Its tests build a 125-block volume by hand and put exactly one thing wrong in
each case: two sibling folders in the wrong key order, a thread key that carries
a name, a file body cut from 248 bytes to 100, a header claiming one record more
than its leaf holds, two extents records swapped, an index key that sorts after
the subtree it points at, a child pointer past the last node, a leaf that claims
to be an index node, five thread-record disagreements (a file with no thread, a
thread with the wrong parent, the wrong name, the wrong kind, and a thread with
nothing behind it), and three clean catalogs — one folder, one file, and a real
depth-2 tree with an index node over two leaves — that must report nothing at
all. `test_check` also covers node ownership, one case each: a node marked used
that the tree cannot reach, a reachable node left marked free, and a header free
count that disagrees with the map. The clean cases matter most: a checker that
cries wolf is worse than none.

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
make -C tests/update test
make -C tests/update sanitize
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
pull-source open (`helikon_player_open_media`) and the short-read WAV path.
Playlist export is tested twice: the text in `tests/player/test_playlist.c`
(byte-exact M3U and PLS, the round trip back through the parser, refusals and a
failing sink), and the file half in `tests/vfs/test_vfs_playlist.c`, which writes
through the journaled write VFS on the fixture volume and reads the bytes back
through the read VFS. The resume bookmark is tested the same way: the text in
`tests/player/test_bookmark.c` (the exact record, the round trip, and every
refusal — a missing magic, a wrong version, an unknown or duplicate key, a CR, a
missing final newline, a relative path, a signed or overflowing number, a
control byte, a path at the length limit) and the file in
`tests/vfs/test_vfs_bookmark.c`, which saves through the journaled write VFS on
the fixture volume, replaces a longer record with a shorter one, reads every
field back through the read VFS, and requires the journal and
`hfs_check_volume` to be clean after each call. The
same suite holds the text parsers — playlists and cue sheets — because both are
files from somewhere else whose every line is a claim:

```sh
make -C tests/player test
make -C tests/player sanitize
```

`tests/firmware` also covers the ATA-to-block bridge (`test_ata_block`): the
geometry arithmetic that decides whether a 1 TB card is 1 TB or 128 GiB, the
read-only gate in front of the driver, and that the block ops really are the
driver's own functions.

`test_cuesheet` covers an album cue (one file, three tracks, frames checked
against `mm:ss:ff` arithmetic), the pregap and sub-index rules, the times that
cannot exist (`00:60:00`, `00:00:75`, two fields, four fields, letters, an
overflowing minute, a negative field), the numbering rules (duplicates,
`TRACK 00`, `TRACK 100`, a track before any `FILE`), one file per track, mixed
case with CRLF and a BOM, quoted and unquoted values, the 99-track cap, a line
over `HELIKON_CUESHEET_LINE_MAX`, and every refusal path including the NUL and
empty-buffer cases.

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

The fixed SCSI answers are checked field by field on that 4 TB disk, because a
4 TB window is what forces the 32-bit cases: `READ CAPACITY(10)` must saturate
at `0xffffffff` and `READ CAPACITY(16)` must state the true last LBA;
`READ FORMAT CAPACITIES` must report a capacity list length of eight, a padded
descriptor, the formatted-media code and the block length at byte twelve, and
still answer coherently when the host allocates only twelve bytes; `MODE
SENSE(6)` and `MODE SENSE(10)` must carry the write-protect bit and a mode data
length that counts their own tail. These are the answers no host on the common
enumeration path reads, which is exactly why they rot unnoticed.

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
The same suite then takes the user-facing path (`audio-formats.md` §24): it
resolves `/Playlists/first.m3u` against the catalog read through the VFS, checks
the queue order and the four counts, and plays each queued track from its catalog
path. Every file the fixture catalog names is a real file on that image, so a
catalog that drifts from the volume fails instead of passing on names alone. A
third phase drives `firmware/library/music_source.c` — the object that owns
volume + catalog + index + queue + player — from the same playlist through
`next`, `previous`, repeat-one, repeat-all, the end of the queue, a stale
playlist, a folder, a missing path, a too-small buffer and a track id past the
catalog (`audio-formats.md` §26). A fourth phase goes in at the bottom: it
presents the same image as a read-only 512-byte block device and mounts it
through `firmware/storage/mount.c` — partition scan, HFS+ probe, VFS, catalog —
then plays the playlist over that mount, which is the only test that reaches the
music path the way the device will (`audio-formats.md` §27).

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

The image must hold `/.helikon/music.hdb`, the files the catalog names,
`/Music/first.mp3`, `/Playlists/first.m3u` and `/Playlists/stale.m3u`; `macos`
and `macos-sanitize`
create it themselves and copy `tests/codec/fixtures/mp3_cbr_stereo16.mp3` into
each of them, so a hand-built image has to carry the same files or the test fails
on purpose. The CI macOS job runs it as `make -C tests/image library-test`, which
delegates to this suite; before that target existed the suite ran nowhere.

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

GitHub's scheduler is not a promise, and through November 2026 `schedule` events
have been dropped or arrived seven to eight hours late across public
repositories — this repository's nightly was scheduled for 04:17 UTC every day
and had never run. The workflow therefore also triggers on a push to `main`, and
its first job, `check`, skips the build when `preview-<today>` already exists.
The result is one prerelease per calendar day, from whichever trigger arrives
first, instead of none when the cron is silent.

Every job in both workflows carries a `timeout-minutes` and every `apt-get`
carries `-o Acquire::Retries=3`. A wedged toolchain download once held the
firmware job for sixty-five minutes with the whole pull request waiting on it;
a stuck install must fail in minutes, not in the six hours a job gets by
default.

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
