# Handoff — Helikon-OS

> Purpose: the latest session handoff and the reusable template.
> Status: living document
> Owner: session author; next agent reads this first
> Last updated: 2026-11-23
> Source: `docs/state.md`, `docs/tasks.md`, repository history; D-54;
> [`disk-mode.md`](disk-mode.md)

Keep the newest handoff at the top. Move older entries to `docs/log/` when this
file grows. Do not rely on chat history for current state.

---

# Handoff — 2026-11-23 (Alpha 2.1.0 published: Disk Mode)

## What happened

- **Alpha 2.1.0 "Disk Mode" is published as a normal GitHub release** (tag
  `alpha-2.1.0` on `a2bb583`, 13 assets). It is not marked pre-release. The notes
  and quick start are archived in
  [`releases/alpha-2.1.0/`](releases/alpha-2.1.0/RELEASE_NOTES.md).
- Three firmware sets, all built for this cut:
  - launcher v21 `8a2a18c2…` (84,744 bytes) — built with `UI=1
    EXTRA_CFLAGS=-DHELIKON_DIAG_LAUNCHER`. The first attempt omitted the flag, and
    the image linked to 80,840 bytes with `ui/launcher_run.o` garbage-collected:
    the launcher entry is behind `HELIKON_DIAG_LAUNCHER`, so a `UI=1` build alone
    produces the plain boot image. `nm | grep helikon_launcher_run` is the check.
  - Music hub v22 `036911d2…` (658,604 bytes) — same flags plus
    `HDB=../build/helikondb/first100/music.hdb`; verify with `nm | grep
    helikon_hdb_start`, because without it the 208,620-byte blob is dropped and
    the image is byte-for-byte the launcher size.
  - Disk Mode v23 `e73f32f3…` (40,556 bytes) — `DISKMODE=1`, whole disk as one
    writable LUN, `DISK MODE` confirm then `WAITING FOR THE HOST` /
    `HOST CONNECTED` / `EJECTED - SAFE`.
- Loader `79a1c02b…` (107,560 bytes) rebuilt from the pinned donor commit; the
  uninstall DFU is unchanged (`203e8364…`). Companion rebuilt with
  `scripts/build-dmg.sh --universal --tag alpha2.1`; the script now takes
  `--tag` so the DMG name and its bundled Read Me cannot claim the wrong alpha.
- Every set passed `tools/check-loader-artifact.sh` (image, manifest and
  bootstate through the host HFS+ core), and the uploaded assets verify:
  `gh release download alpha-2.1.0` then `shasum -a 256 -c SHA256SUMS` is clean.
- Verification for the cut: `./tools/host-tests.sh clang` 9 suites,
  `--sanitize` 8 suites, `./tools/fuzz.sh --replay` 8 harnesses / 47 corpus
  files, `./tools/static-analysis.sh` 80 units 0 findings, and
  `./tools/host-tests.sh --macos` 10 suites including the 162 s image suite.
- One false alarm worth remembering: running `--sanitize` and `--macos`
  concurrently makes the firmware suite fail with `Rename failed ...
  build/lib/mem-0.o`, because both clean the same `tests/firmware/build`. The
  suites are not parallel-safe; run them one at a time.
- **No 2.1.0 artifact has been on a device.** The notes, the quick start, the
  device matrix and `docs/state.md` all say so. Nothing here relaxes the rule
  that a support claim needs a recorded run.

## Open, next

- The physical test round the owner is planning: launcher, Music hub, Disk Mode
  and the loader on the 6G, in that order, with `fsck_hfs -fn` after each. The
  Disk Mode run is the one that matters for the 128 GiB goal: boot it, confirm
  the host enumerates, format or partition from the host, then eject and check
  `fsck_hfs`.
- The ~20 s artist-open stall from the 2026-11-08 image is still in the Music
  hub and still untested on the current build.
- F2 (firmware-owned write) remains blocked; nothing in 2.1.0 depends on it.

---

# Handoff — 2026-11-23 (the merge that could not fit)

- **T-152 — the steal fallback of index merging now has a test.** T-150 merged two
  index nodes when their records fit and moved one child across when they did not,
  and recorded the second branch as unexercised because after a split a node is at
  most half full. `tests/hfsplus/test_btree_index_merge.c` makes the shape reachable
  instead of hoping for it: 236-byte keys mean a 512-byte node holds two index
  records and no more, and nine hand-built nodes hold a full sibling beside a node
  down to one child. Deleting the leftmost leaf's only record forces the choice, and
  the test pins the aftermath — staged but not written until commit, six keys find,
  chain 5 -> 6 -> 7, left node `[k2 -> LA2] [k4 -> LB1]`, right `[k5 -> LB2]`, root
  keys k2 and k5, emptied leaf erased and free, header 5 records / 5 of 12 free.
- Teeth: replacing the steal with `return HFS_OK` fails at `node 2 holds 1 records,
  expected 2`. Suite green plain and under ASan/UBSan.
- Host-only, hand-built fixture. No device run.

---

# Handoff — 2026-11-23 (a VFS call with no mount)

Short one after T-150, from reading rather than from a failing test: `vfs_stat`,
`vfs_file_open` and `vfs_dir_open` never looked at `vfs`. `&vfs->catalog` on NULL
is a small non-NULL address, so the catalog's own guard did not fire and `/`
answered `NOT_FOUND` — a missing mount indistinguishable from a missing file —
while a path with a component read low memory. Three guards and a test that drives
all nine entry points with no mount (it fails before the fix, passes after). Filed
as #138, recorded as T-151.

---

# Handoff — 2026-11-23 (index nodes that finally merge)

## What happened

- **T-150 — the B-tree merges index nodes.** A delete merged leaves, moved a record
  between leaves, and collapsed an emptied leaf through its ancestors, but nothing
  ever paired two index nodes, so a tree that lost records kept a one-child index
  node per surviving child. That shape is legal — the keys are reachable, key order
  holds, `hfs_check_volume` passes — which is why it survived: correct and wasteful,
  with nodes that can never be reused.
- `settle_index_nodes` runs whenever an operation takes a record out of an index
  node: merge with the next sibling (or the previous one, when the node is the last
  child) in key order when the combined records fit, drop the absorbed node's record
  from the parent, free the node, repeat one level up; otherwise move one child
  across from the sibling, rewrite the one key that changed, and stop. `collapse_root`
  takes a level off a root left with one child.
- `test_btree_model` grew the invariant: `check_occupancy` walks index nodes from the
  root after every operation and rejects any with fewer than two records.

## Evidence

- The new invariant failed on the existing sweep before the fix: `step 1000063,
  index node 3 at level 1 holds 1 record(s)`.
- After the fix: the committed 1,800-step sweep, seeds 2, 5, 9, 21, 33 and 77 at
  2,600 steps, the cascade still ending at `depth 1, 68 of 70 nodes free`;
  `make -C tests/hfsplus test` and `sanitize` green; `test_writer_mutations`
  unchanged (`34 curated + 400 random`, same counts).
- Teeth: disabling the merge fails at the same step with the same message;
  disabling only the root collapse fails at `step 1000207` with `index node 31 at
  level 0 holds 1 record(s)`.
- **Not covered:** the steal fallback. After a split a node is at most half full, so
  one child plus a half-full sibling always fits and no seed reaches it. It exists
  for the shape where a sibling *is* full; it is unexercised, and that is written in
  `testing.md` §4.1 rather than implied.
- Host-only: hand-built fixture, no device run.

## Open, next

- The unit half of the writer is still thin at the *insert* end of index merging: a
  targeted fixture that starts from a full sibling would be the way to exercise the
  steal path deliberately (small node, few records per node).
- Everything else on the queue is unchanged: the catalog writer's remaining
  macOS-differential work, `fsck_hfs` on generated trees, and T-056's device half.
# Handoff — 2026-11-23 (a writer fuzzer, and names that are not ASCII)

## What happened

- **T-148 — a fuzz harness for the writer.** `tests/hfsplus/writer_fixture.c` lifts
  the mutation sweep's volume builder out into one place (a valid 1 MiB image with
  the root folder seeded through the real writer). `tests/hfsplus/fuzz/fuzz_writer.c`
  mutates up to seven bytes of it and runs the ladder a caller would: probe →
  structural check → catalog open → one call (create, create folder, rename, write,
  truncate, delete). Two rules are the oracle, and both come from the fail-closed
  contract rather than from a model of the code: a refused call leaves the image
  byte-identical, and a commit never adds a finding to a volume the check called
  clean. A violation aborts with a message so libFuzzer saves the input. Nine seeds
  are committed; `./tools/fuzz.sh --replay` covers eight harnesses. The guided run is
  CI-only because Xcode's clang has no `libclang_rt.fuzzer` — do not "fix" that by
  vendoring one.
- **T-149 — composed and decomposed names in the catalog model.** HFS+ stores
  canonical names (`é` → `e` + U+0301) and every write entry point normalizes what it
  is given. The sweep only generated ASCII, so a path that skipped the step was
  invisible. Each item now keeps both spellings, twelve Latin-1 precomposed letters
  are in the alphabet, children are read back through the reader's path lookup, and
  `composition_probe` requires the decomposed spelling of an existing name to answer
  `BUSY` — which only holds if the writer normalizes, since FastUnicodeCompare does
  not decompose.

## Evidence

- `make -C tests/hfsplus test` and `sanitize`: green, the committed 600-step sweep
  plus seeds 2, 3, 5, 9, 17 at 1,200–1,500 steps, each with 1 case and 1 composition
  collision, the drain still ending at `depth 1, 62 of 64 nodes free`.
- `./tools/host-tests.sh clang`: 9 suites. `./tools/static-analysis.sh`: 80 units,
  0 findings.
- Teeth, three mutations, one per normalization site: `create_object` fails at step
  0, `delete_object` answers `NOT_FOUND` for the accented name, and the new name in
  `hfs_catalog_rename` fails at step 17. All three were reverted and the suite re-run
  green.
- Host-only. No device run, no `fsck_hfs` on the model test's own hand-built fixture
  (its header is not one `newfs_hfs` would write) — the macOS differential in §5 is
  what covers that.

## Open, next

- **Index-node merging** in `firmware/hfs/hfs_btree_tree.c` is now the last reason a
  drained tree keeps one-child index nodes; `test_btree_model`'s 512-byte nodes are
  the proving ground (merge two sparse sibling index nodes, fix the parent key,
  delete the sibling's record from the grandparent, propagate upward).
- The writer fuzzer's corpus grows from CI findings; a reproducer becomes a corpus
  file and a fix in the same PR.

---

# Handoff — 2026-11-23 (an empty-able B-tree, the journal gate, folded names, the data sweep)

## What happened

- **T-143, issue #125 — the writer could not empty a deep tree.** `hfs_btree_delete`
  answered `HFS_ERR_UNSUPPORTED` when an emptied leaf's parent was a one-child index
  node below the root, which a long drain leaves behind because index nodes are never
  merged. `collapse_empty_leaf` replaces that path: unlink the leaf, free every
  ancestor left without a child, rewrite the parent key when the first record went,
  and end at either a root that keeps records or the canonical depth-0 tree. Every
  success path stores the header (the first cut did not, and the new test caught it
  as `leaf_next returned -3`).
- **T-143 — the ordered cascade phase of `test_btree_model`.** Drain in ascending key
  order, canonical empty tree in memory and from the reopened medium, fill 120
  ascending keys from empty (root grows out of depth 0, depth ≥ 2 required), drain
  again, insert three keys through the reopened handle.
- **T-142 — the dirty-journal gate.** `test_journal` now drives both kinds of dirt at
  `hfs_txn_begin` — a header whose `start` and `end` do not meet, and a volume header
  with the dirty flag — and requires `HFS_ERR_DIRTY` with the transaction inactive,
  plus a clean journal that still opens one.
- **T-144 — mixed-case names in the catalog model.** Names come from a mixed-case
  alphabet, the model's uniqueness rule folds case, and a probe flips an existing
  name's case and requires `HFS_ERR_BUSY` from both create and rename.
- **T-145 — the write-path sweep drives the data path.** Five calls per iteration:
  create, rename, write 1,500 bytes, truncate to zero, delete. Only the last three
  allocate blocks, so this is what makes a mutated bitmap or fork record reachable.

## Evidence

`make -C tests/hfsplus test` — 164 cases — and `sanitize` exit 0; `test_btree_model`
1,800 random steps + 513 cascade steps and 2,200-step runs over six seeds; the
mutation sweep 34 curated + 400 random and 1,500-iteration runs over seeds 1/2/3
(4,478 data writes and truncates); `./tools/static-analysis.sh` 80 units, 0 findings.
Teeth are recorded per task row in `docs/tasks.md`. Host-only: no device run, F2
stays unconfirmed, and none of this is a device claim.

- **T-147 — the structural check now pairs records with their thread records.**
  New bit `HFS_CHECK_CATALOG_THREAD`: a file or folder record needs a thread
  record keyed by its CNID carrying the same parent and name bytes, and a thread
  record needs an item that exists under the parent and name it claims. The
  lookups search the catalog through a second B-tree handle, so there is no table
  and no cap. `test_check` is 35 cases. It is silent on a pristine macOS volume
  and on the same volume after the writer's random run.

## Open / next

- The writer has no fuzzer: the seven harnesses cover ID3, Vorbis, HDB, the
  manifest, the journal, playlists and cue sheets. A coverage-guided harness over
  the *writer* — one valid image plus fuzzer-chosen mutations, then probe, check,
  open and one catalog call — would explore paths the curated mutation sweep
  reaches only one field at a time. It needs the fixture builder lifted out of
  `test_writer_mutations.c` first.
- T-146 landed after this handoff was first written: `test_catalog_ops_props` now
  drains the catalog too (files, then childless folders), requires the end state —
  one leaf deep, the root's two records, every other node free, no children, the
  header counting nothing, the check clean — and then refills with three creates.
  It is the end-state half only: with 4 KiB nodes a catalog leaf is merged away
  before it empties, so a leaked node in `collapse_empty_leaf` passes here and the
  B-tree model test is what catches it (`problems 0x00080000`).
- Index nodes are never merged, so a drained tree keeps one-child index nodes until
  the leaf under them goes. Emptiness now collapses them; merging two sparse index
  nodes is still unwritten and is the remaining depth-1-to-depth-2 gap.
- The macOS differential (`fsck_hfs` on an image the writer produced) is still
  `planned`: it needs a host tool that runs the writer against a real `newfs_hfs`
  volume, not the hand-built fixture.

---

# Handoff — 2026-11-23 (CI: missed nightly and an unbounded job)

## What happened

- **The nightly never ran.** `preview.yml`'s `cron: '17 4 * * *'` has produced no
  `schedule` run at all; the only two runs are manual dispatches, and the first of
  those failed because the gate lacked Pillow and mutagen (fixed in #103). GitHub's
  scheduler is dropping or hours-delaying `schedule` events for public repos
  through November 2026, so the workflow now also runs on a push to `main` and its
  new `check` job skips the build when `preview-<today>` already exists: one
  prerelease per day, from whichever trigger is first.
- **A job sat 65 minutes in `apt-get install gcc-arm-none-eabi`** during the
  T-140 pull request. Cancel + re-run finished in 2m17s. Both workflows now set
  `timeout-minutes` on every job and pass `-o Acquire::Retries=3` to every
  `apt-get`.

## Evidence

`preview-2026-11-22` published from a manual dispatch; `gh run list
--workflow=preview --event=schedule` is empty, which is the finding. Both workflow
files parse (`yaml.safe_load`), and the first push to `main` after this merge runs
the new top-up path end to end.

## Open / next

- Watch the first scheduled window after this merge. If the cron stays silent,
  the push top-up still publishes the day's preview, and the absence of
  `schedule` runs is the platform's, not the workflow's.

---

# Handoff — 2026-11-23 (write-path mutation sweep: T-140; reserved CNID fix: T-141)

## What happened

- **`tests/hfsplus/test_writer_mutations.c`** sweeps the *write* path: a valid
  volume, one corrupted structural field (34 curated, plus a seeded random tail),
  then `probe → check → catalog open → create file` in a transaction. The rules
  are "a refused write changes nothing" (image byte-identical) and "a write never
  turns a clean volume inconsistent". Fields whose correct answer is a refusal are
  marked and must be refused.
- **It found issue #120, fixed as T-141.** A volume header whose `nextCatalogID`
  is below 3 made `create_object()` insert a record under a reserved CNID — the
  root's own CNID when the field read 2 — and `hfs_check_volume` never looked at
  the field. The writer now refuses with `HFS_ERR_FORMAT` below 3, at
  `0xffffffff` (the increment wraps to zero) and on a `0xffffffff` volume count;
  the check reports the new `HFS_CHECK_CATALOG_ID` (0x00200000) for both bad IDs.
- `test_check` is 30 cases: the new one sets `nextCatalogID` to 2 and expects the
  finding; the fixture now leaves 16, what a formatter writes.

## Evidence

`make -C tests/hfsplus test` (164 cases) and `sanitize` exit 0; `test_check` 30/30;
the committed sweep 34 curated + 400 random, and 3,000-iteration runs over seeds
12345/7/99/555 — 2,971 commits, every clean-before case still clean. Teeth:
skipping the file-count update fails the pristine sanity case, accepting a
reserved CNID fails the curated case, and removing the check's new bit fails
`test_check`. Host-only: no device run; F2 stays unconfirmed.

## Open / next

- The sweep's committed size is small (0.02 s); longer seeds are worth running
  when the writer changes. It drives create only — delete, rename and data writes
  are the next operations to sweep.

---

# Handoff — 2026-11-23 (B-tree model test: T-138; growth fix: T-139)

## What happened

- **A model test for the tree, not the catalog.** `tests/hfsplus/test_btree_model.c`
  drives the extents-overflow tree with random inserts, deletes and in-place
  updates on a 1 MiB volume with 512-byte nodes, and after every operation checks
  `find` against a sorted model, the leaf chain (exactly the model, strictly
  ascending, right bodies) and `hfs_check_volume` with no findings and no skips. A
  refusal (`UNSUPPORTED` for an unimplemented merge case, `NO_SPACE` when the tree
  is full) must leave the image byte-identical.
- **It found issue #118, fixed as T-139.** `grow_locked()` refused with
  `HFS_ERR_UNSUPPORTED` when `total_nodes` reached the bits the map record held —
  a tight map could never grow. The map record now widens by two bytes into the
  header node's free space (`widen_map()` moves the free-space offset entry with
  it); a map that cannot widen, and a fork with no free extent slot, answer
  `HFS_ERR_NO_SPACE`; `fork_offset()` also maps the attributes fork (352).
- **`test_btree_grow` is 5 cases** now: the original grow, a tight map that
  widens and is seen by a fresh reader, a map that fills the header node (NO_SPACE,
  nothing staged), a fork with eight extents (NO_SPACE), and the attributes tree.

## Evidence

`make -C tests/hfsplus test` 163 cases and `sanitize` exit 0; `tests/bootloader`
44 cases and `sanitize` exit 0; `./tools/host-tests.sh clang` 9 suites;
`./tools/static-analysis.sh` clean; 6G cross-build 80,840 bytes. The committed
model sweep is 1,800 operations (861 inserts, 572 deletes, 367 updates, 270 keys,
depth 3, 64 → 70 nodes); 2,600-operation runs over six seeds reach 513 keys and
128 nodes, the whole fork. Host-only: no device run, and the F2 write path stays
unconfirmed.

## Open / next

- **A test bug cost an hour again.** `MAX_ITEMS` was 512 and a long sweep reached
  513, overwriting the neighbouring model array and producing a phantom "record
  body differs". Guarded now (`model_count >= MAX_ITEMS` skips the insert) and
  raised to 1,024. When a model test fails on one record's *body*, suspect the
  model's own storage before the writer.
- Queued next: the writer mutation sweep (corrupt a volume, drive the writer,
  require fail-closed and no new findings), and forced root-change/split-merge
  cascades driven from the model rather than by chance.

---

# Handoff — 2026-11-23 (node ownership in the structural check: T-137)

## What happened

- **`hfs_check_volume` now checks the free-node map.** It walked each tree's leaf
  chain, key order, index nodes and record bodies but never the map, so a leaked
  node (used, unreachable) or a reachable node marked free was invisible. The new
  pass reads node 0 through `hfs_fork_read` (the tree reader refuses an `empty`
  tree), copies the map record, marks the header used, walks everything reachable
  from the root through the index child pointers, and compares that against the
  map for `total_nodes`, plus the map's clear-bit count against the header's
  `freeNodes`. Catalog, extents and attributes trees each get it. Bounded: past
  16,384 nodes or a 512-deep walk it records `HFS_CHECK_SKIPPED_NODES` instead of
  guessing.
- **New flags:** `HFS_CHECK_BTREE_NODES 0x00080000` (finding),
  `HFS_CHECK_SKIPPED_NODES 0x00100000` (skip, kept out of `problems`).
- **`test_check` 29 cases** (three new: leaked node, reachable node marked free,
  free count disagreeing with the map), all passing.
- **Teeth:** dropping `map_set(header, node, 0)` from `hfs_btree_free_node` makes
  the catalog model test stop at step 95 with `problems 0x00080000`.

## Evidence

`make -C tests/hfsplus test` and `sanitize` exit 0 (`test_check` 29/29; catalog
model 600 steps default seed, 700 steps over seeds 1/2/3/7/11/424242, no
sanitizer report); the 6G cross-build and bootloader suites are recorded below.
Host-only: no device run, and the F2 write path stays unconfirmed.

## Open / next

- **A stale test binary cost an hour.** After restoring a mutated
  `hfs_btree_tree.c`, `tests/hfsplus/test_catalog_ops_props` kept failing at step
  95 with `0x80000`; `make clean` and a rebuild pass. Rule: when a suite fails
  only sometimes, `rm` the binary before believing it.
- Queued next: a B-tree-level model test (random insert/delete/update against a
  tree model, forced root changes, `FastUnicodeCompare` names, `fsck_hfs` on the
  result), then a writer mutation sweep (corrupt the volume, drive the writer,
  require fail-closed and no new findings).

---

# Handoff — 2026-11-23 (catalog operation property test: T-136, and the staging fix it found: T-135)

## What happened

- **A model-based test for catalog operations.** `tests/hfsplus/test_catalog_ops_props.c`
  runs a random sequence of creates, deletes, renames, writes and truncates on a
  1 MiB volume with 4096-byte B-tree nodes, and after every operation checks the
  image against a directory model: each folder's children by name/ID/kind, each
  folder's valence against its child count, each file's size and full contents
  read back through the reader, the volume header's file and folder counts, and
  `hfs_check_volume` (key order across leaves, the leaf chain, every fork against
  the allocation bitmap, the free-block count). Refusals are held to the same
  standard: a delete of a folder with children, a folder moved into its own
  subtree, a truncate that would grow the file, and a `NO_SPACE` staged write must
  leave the image byte-identical.
- **It found a real defect (issue #115).** With 512-byte allocation blocks, a
  B-tree node cost one transaction slot per sector and a root split needed 34 of
  the 32 slots, so the catalog could never grow past depth 1 — a permanent
  refusal, not corruption. Raising `HFS_TXN_MAX_BLOCKS` to 96 made the same sweep
  pass, which isolated the cause.
- **The fix (T-135).** `hfs_txn_stage_replace` now patches the bytes it is given:
  the exact range is replaced, a range that lies inside or overlaps a staged run
  is merged into it while the union fits in `HFS_TXN_BLOCK_MAX`, and a union that
  would not fit is refused with `BUSY` and changes nothing. Runs that merely touch
  are kept separate. `hfs_fork_stage_map` stages runs of whole allocation blocks
  instead of one block at a time, so a node is one entry and a root split six.
  The journal needed no change: it already writes an entry of any sector-multiple
  size.
- **Two tests were rewritten for the new contract.** `test_txn.c` gains explicit
  cases (patch in place, grow by overlap, adjacent runs stay separate, over-limit
  merge refused) and `test_txn_overlay_props.c` now models the staged bytes as a
  byte map, cross-checks it byte for byte against the transaction's entries after
  every step, and predicts only the union size from the entries themselves,
  because a byte map cannot tell two abutting entries from one run.

## Evidence

- `make -C tests/hfsplus test` and `sanitize` exit 0 with both new suites in the
  list; the catalog sweep is 600 operations (90 creates, 66 deletes, 102 renames,
  93 writes, 32 truncates) and a 1,200-operation run over a second seed passes.
- `./tools/host-tests.sh clang` 9 suites pass, `--sanitize` 8 suites pass,
  `./tools/static-analysis.sh` reports 0 findings, the 6G cross-build is
  80,840 B.
- Docs: `docs/testing.md` §3.5 (rewritten), §3.6, §3.7, §4; `docs/tasks.md`
  T-135/T-136; `docs/state.md` transaction and catalog rows; `docs/file-map.md`.

## Open / next

- The B-tree itself still has no model test: §4 lists what is missing (a tree-level
  insert/delete model, forced root changes, `FastUnicodeCompare` names in the same
  model, `fsck_hfs` on the result).
- `hfs_check_volume` does not check the free-node map or node ownership; the
  catalog test would catch a leaked node only through the free-block count.
- Sweep 10 (a mutation sweep over the writer path) is still queued.
- The F2 write path remains host-only and unconfirmed; none of this is a device
  claim.

---

# Handoff — 2026-11-22 (transaction overlay property test: T-134)

## What happened

- **A property test for the staged-block overlay.**
  `tests/hfsplus/test_txn_overlay_props.c` stages random blocks of random sizes
  into a transaction and reads random ranges back through `hfs_txn_overlay`. The
  expected answer is produced by applying the model's own staged list to a copy of
  the caller's buffer, so the test states the contract a second way instead of
  comparing the code with itself: a read returns the image with exactly the
  staged bytes that intersect it patched in, staging order deciding overlaps, and
  the function returns 1 when it touched anything and 0 when it did not.
- **The overlay never reads the disk.** Its signature takes no device and its
  body only copies out of the staged list, so a range that no staged block covers
  leaves the caller's buffer untouched and returns 0. The model mirrors that:
  reading a range outside the image is not clamped, because the overlay does not
  look at the image at all. Zero-length ranges return 0 before anything else.
- **The staging rules are checked alongside it**: a duplicate first sector is
  `BUSY`, replacing a staged sector with a different size is `BUSY`, the 33rd
  staged block is `NO_SPACE`, and a bad size is `ARGUMENT`. A final case fills
  every slot and compares a whole-image overlay byte for byte.
- **No defect found.** 4,000 steps and 2,362 overlay checks in the default sweep,
  60,000 steps over three further seeds, plain and under ASan/UBSan.
- **Teeth proven by mutation.** Widening the adjacency test in `hfs_txn_overlay`
  to `block_end < offset || block_start > end` is caught by the boundary cases
  (`FAIL rc == expected_overlap`). Reverted; `git diff --stat` clean.

## Evidence

- `tests/hfsplus/test_txn_overlay_props` passes; `make -C tests/hfsplus test` and
  `make -C tests/hfsplus sanitize` exit 0.
- `docs/testing.md` §3.5, `docs/tasks.md` T-134, `docs/state.md` transaction row.

## Open / next

- Sweep 10 is the writer path: a mutation sweep over `hfs_btree_tree.c` and
  `hfs_catalog_write.c` (76.6 % / 87 % statement coverage today). The F2 write
  path stays host-only and unconfirmed; it is not in Alpha 2.0.0.
- The B-tree insert/delete model property test is still the largest uncovered
  invariant in `docs/testing.md` §4.

---

# Handoff — 2026-11-22 (journal crash-atomicity property test: T-133)

## What happened

- **A property test for the commit path.** `tests/hfsplus/test_journal_props.c`
  gives the device a write budget: a budget of `i` means the first `i` writes
  reach the medium and every later one is silently lost, which is what a power
  cut looks like to a caller. For every budget from 0 to the number of writes the
  whole append+replay path performs, it runs the transaction into that budget,
  remounts (reopens the journal from the medium and replays what it says is
  committed), and then requires: every target sector holds either its whole
  payload or none of it; no two targets disagree; no byte outside the journal and
  the targets moved; and the journal is clean again. A third of the trials start
  the journal near its end so every replay wraps, and each wrap trial ends with an
  idempotence check.
- **The expected state is built from the transaction's own payload**, not from a
  reference run of the code under test. The first version did use a reference run
  and would have passed a commit-before-payload mutation, because both runs were
  wrong the same way; that mistake is worth remembering for the next property
  test.
- **No defect found.** 238 write boundaries over 24 transactions plus 8 wrap
  trials, plain and under ASan/UBSan.
- **Teeth proven by mutation, twice.** Committing before the payload reaches the
  medium trips the sanity check; a replay that marks the journal clean before
  applying its blocks trips the sweep with `FAIL transaction 0: 1 of 2 targets
  applied at write 6, the rest not`. Both mutations were reverted.

## What it cannot prove

The device honours the barrier only in hardware. The budget model drops whole
writes and never reorders them, so a missing `flush()` between the payload and the
commit is invisible here. That stays the device matrix's job (`docs/testing.md`
§3.4 says so on the page).

## Open / next

- The same write-budget model applies to the transaction engine above the
  journal: cut a `hfs_txn_commit` at every write and require the volume to be
  all-or-nothing. That needs the fixture image plus the model from
  `test_bitmap_props.c`.
- Writer-path mutation sweep (`hfs_btree_tree.c` / `hfs_catalog_write.c`) is
  still the weakest-covered area at 76.6 % / 87 %.

# Handoff — 2026-11-22 (the allocator property test, and the two defects it found: T-132)

## What happened

- **A property test for the allocation bitmap.** `tests/hfsplus/test_bitmap_props.c`
  allocates and frees random runs for thousands of steps against a model,
  commits every 16 steps and reopens the volume from the image, so every
  assertion is made against committed bytes. It also checks the two things a
  case-by-case suite never does: the transaction overlay must not hand the same
  block out twice inside one commit window, and a refused free must leave the
  transaction exactly as it was.
- **#110 — the allocator never wrapped.** `hfs_bitmap_find`/`alloc` searched from
  the hint to the end of the volume and gave up. The sweep failed on step 10
  with 81 free blocks: `alloc(run 5, hint 121) -> NO_SPACE`. A catalog tree high
  on a full upper half could not grow, and a hint equal to the volume size was
  refused outright. Fixed: the from-hint pass runs first (fork growth still
  prefers the adjacent block), then a wrapped pass over `[0, hint)`; a hint at or
  past the end starts at block 0.
- **#111 — a refused free staged half a range.** `hfs_bitmap_free` validated and
  staged one bitmap block at a time, so a range that crossed the seam and hit an
  already-free block returned `FORMAT` with the first block already staged in
  the caller's transaction. Whoever committed next wrote a bitmap whose bits and
  `free_blocks` disagreed. Fixed: the whole range is validated before anything
  is staged.

## Evidence

- With the fixes reverted, the sweep fails at step 10 (`CHECK(rc == HFS_OK)`) and
  the two-block case prints `FAIL a refused free left 1 block(s) staged in the
  transaction`. With them, both pass.
- 60,000 steps over three seeds under ASan/UBSan: no double allocation, no
  double free, no drift, no sanitizer finding.
- `make -C tests/hfsplus test` and `sanitize` green (19 + 1 + 3 + 5 + 2 + 6 + 1
  groups as before, plus the new test).

## Open / next

- The property test covers alloc/free on one bitmap block set and the seam. It
  does not model allocation-file *overflow extents* — `map_bitmap_block` returns
  `UNSUPPORTED` beyond the eight inline extents, so a volume whose bitmap itself
  is fragmented is still unsupported (recorded in `docs/state.md`).
- Next property targets from `docs/testing.md` §1: B-tree insert/delete against a
  model, and journal replay idempotence.

# Handoff — 2026-11-22 (mutation sweeps, and the two seek bugs they found: T-131)

## What happened

- **Two mutation sweeps now drive the read paths with corrupt input.**
  `tests/hfsplus/test_image_mutation.c` corrupts a valid volume image and runs
  probe → `hfs_check` → catalog lookup/iterate/`read_file` → extents overflow.
  `tests/codec/test_codec_mutation.c` does the same to FLAC and MP3 fixtures
  through probe/open/read/seek and the streaming source path, with truncation
  passes. Both are deterministic (seed + iteration count on the command line),
  print every changed byte on a finding, and fail if they stop reaching the deep
  readers — a sweep that only tests the probe proves nothing.
- **The codec sweep found two real defects on its first run.**
  [#106](https://github.com/deandromi/Helikon-OS/issues/106): `mp3_seek` indexed
  `xing.toc[100]` on a `uint8_t[100]` when seeking to the last frame, because
  `frame == total_frames` is accepted and `frame * 100 / total` is 100. The
  multiply also overflowed 32 bits for a huge Xing frame count.
  [#107](https://github.com/deandromi/Helikon-OS/issues/107): the same TOC anchor
  lands inside the final frame for the last bucket, so a seek to the end of a
  VBR file returned `NOT_FOUND`. Both are fixed in `mp3_seek` (64-bit clamped
  bucket; retreat through earlier anchors) with hand-written regression tests in
  `tests/codec/test_codec.c` that fail without the fix.
- **Sanitizer scope got sharper.** `tests/libmad.mk` builds the vendored libmad
  objects with AddressSanitizer only — its fixed-point DSP overflows by design on
  corrupt input, while a buffer overrun is exactly what the sweep hunts. With the
  noise gone, `tests/codec` runs with `UBSAN_OPTIONS=halt_on_error=1`: any
  remaining UBSan report is project code and fails the suite.

## Evidence

- HFS+ sweep: ~260,000 mutated volumes under ASan/UBSan across seeds, no memory
  error, no hang, no finding. Last long run printed `40,000 iterations, 15,696
  probed, 15,246 opened, 28,834 looked up, 14,991 entries, 14,788 overflow trees`.
- Codec sweep: 62,400 mutated passes (12,000 per fixture per seed) with no memory
  error and no hang after the two fixes.
- `make -C tests/hfsplus sanitize` and `make -C tests/codec sanitize` green;
  the codec suite prints one `PASS codec` line and no sanitizer note at all.
- Without the fix, the regression test prints
  `mp3_codec.c:720:49: runtime error: index 100 out of bounds for type
  'uint8_t[100]'` and `FAIL MP3 vbr seek last frame`.

## Open / next

- The sweeps mutate metadata and bitstreams; they do not model a crash in the
  middle of a *write*, which stays the crash-injection suite's job.
- Exact MP3 frame seeking (decode-and-discard to the requested frame) is still
  not implemented: a TOC seek can land up to one bucket (~1 %) early. Do not
  claim frame-accurate seeking.
- `docs/testing.md` §3.2 is the place to record a new sweep; anyone adding a
  binary format parser should add one there and in `tests/<suite>`.

# Handoff — 2026-11-22 (dead artist bitmaps deleted: T-130)

- `launcher_assets.c/.h` lost the six `helikon_artist_*` assets and 4,569 lines;
  nothing drew them, and the header one carried a second copy of the mockup
  `12:34`. The generator no longer emits them.
- Image sizes unchanged (767,960 B hub, 80,020 B plain) because `--gc-sections`
  was already dropping them. The saving is source, compile and the stale-clock
  copy — do not claim a flash win for this.
- Evidence: `make -C tests/ui preview` 3/3 PASS, host suites 9, static analysis
  80 units / 0 findings.

# Handoff — 2026-11-22 (the daily preview ran: T-117)

## What happened

- **`preview.yml` had never produced a release.** The first manual dispatch failed
  in 43 s: the gate installed `clang` alone, and `tests/musicdb` needs Pillow and
  mutagen to build the HDB fixture. The build job had the same gap one step later —
  no `cryptography` for the Ed25519 manifest signature in
  `tools/make-firmware-image.py`. Both are installs; both fixed.
- **`preview-2026-11-22` is published** as a prerelease from `main`: `payload-6g/`,
  `payload-6g-diskmode/`, `README-PREVIEW.md`, `CHANGELOG.md` and a tarball.
  Run [`45071068475`](https://github.com/deandromi/Helikon-OS/actions/runs/45071068475).

## Evidence

- Three jobs green (gate, build, publish); gate = `./tools/host-tests.sh clang`.
- Downloaded tarball: 6G `firmware.img` 82,860 B, Disk Mode 43,784 B; the 6G image
  hashes to the `3b842118…` in its own `SHA256SUMS`.
- No hardware anywhere: a preview is unproven by construction.

## Open / next

- The 04:17 UTC schedule has not been observed; only the dispatch. Watch tomorrow's
  run before trusting the cron.
- The preview images are 2,840 B larger than a pinned-toolchain build (distro
  compiler, `-Wno-error`), as documented in `docs/release.md` §1.1.

# Handoff — 2026-11-22 (Music flow truth and typography: T-128)

## What happened

- **The hub stopped showing a clock.** It blits `helikon_launcher_background` as
  its masthead, and that owner-artwork tile carries a mockup `12:34` and a full
  battery. `mask_baked_hub_clock()` clears the corner and `draw_battery()` draws
  the honest outline, so the hub no longer claims an RTC or a fuel gauge.
- **The 3×5 diagnostic face is gone from `firmware/ui/music.c`.** `draw_text` and
  `draw_text_right` now place a baseline in the band the caller used and give the
  atlas a pixel limit derived from the old character budget, so no layout moved.
  Footers, wall captions, wall counts and the whole Tracks list are Plex now, and
  the `·` separators survive because the text reaches the atlas as UTF-8.
- **The wall headers lost the mockup magnifier**: there is no search, so the strip
  no longer promises one. `tools/make-launcher-assets.py` no longer draws it and
  the two committed strips were cleared.
- **`tests/ui/test_music.c`** probes the hub's real corner, the wall's corner and
  the colour count of both text bands (16–17 colours for atlas text, 2 for the 3×5
  face), so neither defect can return quietly.

## Evidence

- `./tools/host-tests.sh clang` → 9 suites.
- `./tools/static-analysis.sh` → 80 units, 0 findings.
- `make -C tests/ui preview` → 34 screens re-rendered from the firmware sources.
- Music-hub image 767,960 B (was 768,200 B); plain 6G image 80,020 B.
- Visit: `.unslopovich/visits/2026-11-22-2200-firmware-music-ui.md`, 17 → 20 / 24.

## Open / next

- **T-129**: the launcher masthead still carries the same baked clock and battery.
  That is owner artwork; decide regenerate-without-them or record the exception.
- `NO CREDITS LISTED` and `▶ PLAYING` remain owner decisions from 2026-11-21.
- The companion and the boot splash were not revisited in this pass.
- Nothing here was seen on a panel; the renders are host builds.

# Handoff — 2026-11-22 (ATA as a block device: T-127)

## What happened

- **`firmware/storage/ata_block.c/.h`** — `helikon_ata_block_init` presents the
  ATA driver as the block device the mount expects: the driver's own
  read/write/flush/power functions are the ops table, and the device is read-only
  unless the caller asks otherwise.
- **The geometry is the point.** `helikon_ata_block_info` refuses a sector size
  below 512 or not a power of two, a zero sector count, and a sector-count product
  that overflows 64 bits — the bug class where a 1 TB card is described as
  128 GiB. An ATA handle with no valid IDENTIFY is `UNSUPPORTED`, never a device
  built from a failed init's placeholders.
- **`tests/firmware/test_ata_block.c`** replaces the driver with counted stubs and
  checks the arithmetic, the refusals, the read-only gate (no driver call), the
  out-of-range refusal (no driver call) and delegation on a valid read.

## Evidence

- `make -C tests/firmware test` and `sanitize` → `PASS ata block` among 14
  binaries.
- `./tools/host-tests.sh clang` → 9 suites.
- `./tools/static-analysis.sh` → 80 units, 0 findings.
- `make -C firmware BOARD=6g image` → 80,020 B unchanged (`--gc-sections`).

## Open / next

- **Boot-time call site**: `helikon_ata_init` → `helikon_ata_block_init` →
  `helikon_storage_mount` → `helikon_storage_open_catalog` → `music_source`, in
  that order, before the UI starts. That is T-056's device half and it needs the
  hardware; none of it is claimed to work on a 6G yet.
- The hub then calls `helikon_music_source_play_playlist` on the mounted volume,
  and a dirty journal needs a read-only badge (policy undecided).

# Handoff — 2026-11-22 (mounting the volume: T-126)

## What happened

- **`firmware/storage/mount.c/.h`** — the step the firmware never had:
  `helikon_storage_mount` scans the partition map (APM, MBR, whole-disk HFS+),
  tries each partition as HFS+ and keeps the first that mounts, then mounts it
  read-only through the VFS with fixed 4 KiB sector, 8 KiB catalog and 8 KiB
  extents scratches. `helikon_storage_open_catalog` opens `/.helikon/music.hdb`
  through the mounted volume; a volume without a catalog still mounts.
- **Decisions**: "it mounts" beats "it is partition N" (a 6G disk carries non-HFS+
  firmware partitions); a dirty journal is *reported*, never cleared (I-04); the
  catalog is a separate call; a sector bigger than the fixed scratch is `BUFFER`.
- **`tests/library` fourth phase** presents the image as a read-only 512-byte
  block device and goes up through this exact path to playback.

## Evidence

- `make -C tests/library macos` → `PASS storage mount: block device -> partition
  -> HFS+ -> catalog, playlist played` plus the three earlier `PASS` lines;
  `macos-sanitize` green.
- `./tools/static-analysis.sh` → 79 units, 0 findings.
- `make -C firmware BOARD=6g image` → 80,020 B unchanged (`--gc-sections` drops
  `storage/mount.c`: nothing calls it yet).

## Open / next

- **ATA glue + boot-time call site** are all that stand between this and a
  firmware that mounts its own disk: `helikon_block_device` over the ATA read
  path, then `helikon_storage_mount` before the UI starts. That is T-056's device
  half and the first thing that needs the hardware.
- The Music hub then calls `helikon_music_source_*` on the mount.
- Dirty-journal policy for the UI (read-only badge) is undecided.

# Handoff — 2026-11-22 (the music path in one object: music_source, T-125)

## What happened

- **`firmware/library/music_source.c/.h`** — binds a mounted VFS and an HDB,
  builds the index once, and plays a playlist that lives on the volume: VFS read
  → playlist parser → index → queue → `hdb_track` → path → VFS open → codec →
  player. Then `next`/`previous` step the queue, closing and reopening the file
  each time. No allocation: index + queue + 8 KiB playlist buffer + the player's
  31 KiB workspace and 16 KiB window, ~68 KiB of `.bss`.
- **Rules in the header, each one a check**: one file at a time and a failed open
  leaves nothing playing; a missing path is `NOT_FOUND` with the queue unmoved;
  the end stops rather than wrapping; `REPEAT_ONE` replays the same id; a playlist
  that resolves to nothing stops playback.
- **`tests/library` third phase** drives it over the disposable journaled image
  and prints `PASS music source: one object over the volume, 3 queued, walked 5,
  2, 0 and stopped at the end`. The image gained `/Playlists/stale.m3u`.
- **Real defect found by that phase**: the playlist base was `/Playlists/`, so
  `..` popped the empty trailing component instead of `Playlists`; relative
  entries landed under `/Playlists/Music/...` and `../../escape.m4a` was
  *accepted*. Fixed to `/Playlists`, with the reason in a comment.

## Evidence

- `make -C tests/library macos` → three `PASS` lines (playlist, music source,
  library); `macos-sanitize` the same, sanitizer-clean.
- `./tools/static-analysis.sh` → 78 units, 0 findings.
- `make -C firmware BOARD=6g image` → 80,020 B unchanged (`--gc-sections` drops
  the object: nothing calls it); music-hub image builds at 768,200 B.

## Open / next

- **The mount.** Nothing above `tests/library` calls this, because the firmware
  mounts no volume at boot (T-056 device half). That is the single blocker
  between this object and a Music hub that plays the library.
- The hub then needs a call site: a playlist row that calls
  `helikon_music_source_play_playlist`, next/previous bound to wheel
  left/right, and a Now Playing path from `helikon_music_source_path`.
- No device run of any of it; the embedded-blob player is still what the device
  image does.

# Handoff — 2026-11-22 (one file, twelve tracks: the cue sheet reader, T-124)

## What happened

- **`firmware/player/cuesheet.c/.h`** — reads `FILE`, `TRACK`, `INDEX`, `TITLE`
  and `PERFORMER` into at most 99 tracks and 16 files, allocation-free: strings
  point into the caller's buffer. `mm:ss:ff` becomes CD frames (75/s) with every
  field required; `INDEX 00` is the pregap, `INDEX 01` the start, `INDEX 02`+
  ignored.
- **Refusals are counted, not fatal**, and each one is a guess that would start a
  track in the wrong place: duplicate/backwards/`00`/`100` track numbers, a
  `TRACK` before any `FILE`, impossible times, a track with no `INDEX 01`, a
  pregap after its own start (clamped), a start that goes backwards (dropped), a
  line over `HELIKON_CUESHEET_LINE_MAX`.
- **`tests/player/test_cuesheet.c`** — nine groups, including 99 tracks on a whole
  disc and eight times that cannot exist.
- **`tests/player/fuzz/fuzz_cuesheet.c` + 8 seeds** — asserts the invariants:
  starts never go backwards, no pregap after its start, every track's file
  exists, `complete == (refused == 0)`, every string inside the buffer and cap.

## Evidence

- `make -C tests/player test` and `sanitize` → `PASS cuesheet` (6 binaries).
- `./tools/fuzz.sh --replay cuesheet` → 8 seeds clean.
- `./tools/static-analysis.sh` → 77 units, 0 findings.
- `make -C firmware BOARD=6g image` → unchanged 80,020 B; the music-hub image
  builds at 768,200 B (the parser is collected until something calls it).

## Open / next

- No caller and no path resolution: nothing maps `FILE` to a volume path, and
  nothing calls `helikon_player_seek_ms` for a cue track. That caller is the
  storage-backed Music hub (T-058), same as the queue's.
- A `\"` inside a quoted value stays escaped by design (documented in the header).
- T-093 still wants A-B repeat over cue markers.

# Handoff — 2026-11-22 (a playlist that is a file: T-123 host proof)

## What happened

- **`tests/library` stopped pretending.** The image held a catalog naming six
  paths and one file called `/Music/first.mp3`, so nothing ever checked that the
  catalog described the volume. `run-library-macos.sh` now copies the audio
  fixture to all six catalog paths, keeps `/Music/first.mp3` for the differential
  decode, and writes `/Playlists/first.m3u` with one `..` entry, one absolute
  entry, one absent entry, one entry that escapes the root and an `#EXTINF` line.
- **`test_playlist_queue()`** walks the user's path: read the playlist through
  the VFS, build the library index over the VFS-backed catalog, resolve into the
  queue, then resolve each queued id back through `hdb_track` to its path and
  play it from the volume. Expects `entries 4, added 3, missing 1, refused 1`
  and queue order `5, 2, 0`.
- **The suite ran nowhere in CI** and now does: `tests/image` gained a
  `library-test` target (a delegation to `tests/library`) and
  `tools/host-tests.sh` added it to `MACOS_TARGETS`.

## Evidence

- `make -C tests/image library-test` → `PASS playlist: 4 entries -> 3 queued, 1
  missing, 1 refused`, then `PASS library: HFS+ -> VFS -> HDB (6 tracks, 3
  albums, CRC OK) and audio decodes from the volume`.
- `make -C tests/library macos-sanitize` → the same, under ASan/UBSan.
- Deliberate break: expected id 5 → 4 prints `FAIL the queue follows the
  playlist order`, exits 1.
- `./tools/host-tests.sh clang` → 9 suites pass.

## Open / next

- No firmware call site: the hub still plays its embedded blob and the firmware
  mounts no VFS. That is T-058's remaining half, and it needs the mount first.
- No allocation by design: the index is 8 KiB of `.bss` at 1024 slots; a library
  larger than that sets `complete = 0`, and lookups may then miss.
- Cuesheet support, playlist export and resume state remain in T-093.

# Handoff — 2026-11-22 (a playlist becomes a queue: T-123)

## What happened

- **`firmware/library/library.c/.h`** — the glue between the playlist parser, the
  HDB and the queue. An HDB track carries a *string offset* for its path, so
  resolving a playlist by scanning the catalog per entry would read it once per
  pair; instead the index is built once per mount: one pass, one FNV-1a hash per
  track, a fixed 1024-slot open-addressed table (8 B per slot, load factor one
  half, no allocation). A probe is a hash plus one path comparison to confirm.
  `complete` says whether every track was indexed, so `NOT_FOUND` is never read
  as proof of absence when it is not.
- **`helikon_library_queue_from_playlist(index, queue, text, length, base,
  result)`** parses, resolves and replaces the queue with the track indices in
  playlist order, counting `entries`, `added`, `missing` and `refused`
  separately — a stale path and a malformed line are different questions.
  `_Static_assert(HELIKON_QUEUE_CAPACITY >= HELIKON_PLAYLIST_MAX_ENTRIES)`: a
  whole playlist has to fit, or an entry is dropped for an unexplainable reason.
- **Byte-exact comparison, documented**: a playlist whose case differs from the
  file's is reported missing rather than guessed, because folding would import
  the filesystem's table into the library.
- **`tests/musicdb/test_library_index.c`** builds its playlist out of the
  fixture's own track paths, so it checks the index against the catalog instead
  of against strings someone typed.

## Evidence

- `make -C tests/musicdb test` → `PASS hdb`, `PASS library index (6 tracks, 6
  slots used)`; `sanitize` green.
- `./tools/host-tests.sh clang` → 9 suites pass.
- `./tools/static-analysis.sh` → 76 units, 0 findings (`firmware/library` added
  to both the source list and the include path).
- `make -C firmware BOARD=6g image` → unchanged at 80,020 B; `library/library.c`
  is compiled into every image and garbage-collected until something calls it.

## Open / next

- No firmware call site yet: the Music hub still plays its embedded blob, and the
  chain that would use this is VFS mount → `hdb_vfs_open` → index → playlist →
  queue → player, which is the storage-backed player work, not this module.
- A case-insensitive comparison (HFS+ semantics) would need the HFS+ fold table;
  that is a deliberate follow-up, not an oversight.
- Cuesheets, playlist export and `bookmark`/resume state remain in T-093.

# Handoff — 2026-11-22 (playlist import: T-093's first slice)

## What happened

- **`firmware/player/playlist.c/.h`** — M3U, M3U8 and PLS import as a *streaming*
  parser: `helikon_playlist_open(parser, text, length, base, format)` and
  `helikon_playlist_next(parser, entry)`, one entry at a time. A 512-entry array
  of paths does not belong on a 6G.
- **The rules are in the header**: LF/CRLF, BOM, trimmed lines, `#EXTINF`
  title+duration for the next path (fractional truncated, `-1` unknown), other
  `#` lines ignored, UTF-8 (not percent-encoded) paths resolved against the
  playlist's folder, `.` dropped and `..` popped, absolute entries kept, escapes
  above the root / control characters / invalid UTF-8 refused **and counted**,
  over-long paths and over-capacity files refused rather than truncated, a NUL in
  the buffer refused at open, PLS read by numeric index so `TitleN` before or
  after `FileN` both work.
- **Nine host test groups** in `tests/player/test_playlist.c`, including the
  four-bad-entries case that must still deliver the good one after them.
- **A `playlist` fuzz harness** with nine seeds; its invariants are properties
  of the output (absolute, terminated, no control characters, never over the cap,
  `next` never fails after `open`). Its first finding, twelve seconds into the
  guided CI run: `File00000087161458=` made the PLS index scan walk 87 million
  indices. Bounded by the entry cap now; the reproducer is a corpus file.
- **A test-suite bug found on the way**: `tests/player` compiled
  `firmware/lib/mem.c` without `-DHELIKON_HOST_TEST`, so `memcpy` resolved to the
  freestanding shim and clang's idiom recognition made `helikon_memcpy` call
  itself — a stack overflow at `-O2` that ASan hid. The suite now uses
  `-DHELIKON_HOST_TEST` and `-idirafter` for `firmware/lib`, like `tests/codec`.

## Evidence

- `make -C tests/player test` → `PASS playlist import`, plus the four existing
  binaries; `make -C tests/player sanitize` green.
- `./tools/fuzz.sh --replay` → 6 harnesses clean (8 playlist seeds).
- `./tools/static-analysis.sh` → 75 units, 0 findings (the new file included).
- `make -C firmware BOARD=6g image` unchanged at 80,020 B; the audio+UI image
  builds at 768,200 B with `playlist.c` compiled, so it is warning-clean on the
  target too.

## Open / next

- Nothing in the firmware calls the importer yet: the caller needs VFS path
  resolution to a library id before the queue can be filled from a playlist.
- Cuesheets (`FILE`/`TRACK`/`INDEX` with frame offsets) are a different shape and
  are not implemented; T-093 also wants export, bookmarks and resume state.

# Handoff — 2026-11-22 (the writer's half: our volume, judged by macOS)

## What happened

- **`hfs_utf8_to_utf16be()`** in `firmware/hfs/hfs_unicode.c`: the inverse of the
  decoder, refusing overlong forms, surrogate halves, NUL, anything past
  U+10FFFF and names over 255 units, and normalizing nothing. Six tests in
  `tests/hfsplus/test_unicode.c`. It is what a write path needs to turn a UTF-8
  name into a catalog key; nothing in the firmware calls it yet, so the image
  size did not move (80,020 B).
- **`hfs_image_test IMAGE order NAMES_FILE`** creates `/Order` and one file per
  name through our writer, one transaction each, using the same names file the
  macOS corpus comes from.
- **The writer half of the differential** in `run-unicode-order-macos.sh`: a
  second disposable image, our writer builds it, and three oracles judge it —
  `fsck_hfs -fn` accepts the tree, macOS reads all 47 names back as files, and
  our catalog order equals **the order Apple's image produced for the same
  names**.
- **Both oracles were proved able to fail.** Inverting the comparator made our
  writer build the catalog in reverse and `fsck_hfs` report `Keys out of order`;
  restored, clean and identical to Apple's order.

## Evidence

- `make -C tests/image unicode-order-test` →
  `PASS: catalog order, canonical form, tree read, file content and a written
  volume macOS agrees with`.
- `make -C tests/hfsplus test` → 30 unicode tests, including the six new ones.
- `./tools/host-tests.sh clang` 9/9 and `--sanitize` 8/8;
  `./tools/static-analysis.sh` 74 units, 0 findings;
  `make -C tests/image test` (existing image modes) still passes.

## Open / next

- The encoder is not called by the firmware, so a write path that takes a UTF-8
  name (a VFS `create`/`rename`, or the desktop app's transport) is the next
  consumer; until then it is exercised only from `tests/image`.
- The writer corpus creates files only. Folders, renames and deletes against
  Apple's order are the next shapes worth adding to the same script.

# Handoff — 2026-11-22 (macOS is the oracle: catalog order and a read differential)

## What happened

- **The comparator now has an outside oracle.** HFS+ stores a directory in a
  B-tree ordered by `FastUnicodeCompare`, so a volume macOS wrote *contains*
  Apple's ordering. `tests/image/run-unicode-order-macos.sh` creates a disposable
  24 MiB journaled image, has macOS write 47 names under `/Order` in a shuffled
  order (digits, case pairs, punctuation, precomposed and decomposed Latin,
  reordered combining marks, Greek, Cyrillic, Hebrew, Arabic, Devanagari, CJK,
  Hangul, kana, emoji, fullwidth, `a`×255 and `a`×254+`b`) and a small tree under
  `/Tree` (0, 1, 4096, 10000 and 70,000-byte files, two folder levels).
- **`tests/image/unicode_order_host.c`** is the read-only tool: `list` (catalog
  order), `tree` (`path<TAB>kind<TAB>size`), `read` (file bytes), `sort` (our
  comparator), `canon` (our normalization). Answer on stdout, diagnostics on
  stderr, so `cmp` can only see the answer.
- **Result:** 47 names in Apple's order == our order; canonical forms identical;
  eight tree entries identical; the 70,000-byte file's SHA-256 matches; `fsck_hfs`
  clean. Wired in as `make -C tests/image unicode-order-test` and added to
  `tools/host-tests.sh` `MACOS_TARGETS`, so the macOS CI job runs it.
- **It was made to fail first.** Inverting the comparison in
  `hfs_fast_unicode_compare` turned the ordering check red with the mismatched
  pairs printed; restored, green. A differential that has never failed is not
  evidence.
- **A bug in my own tool, caught by the tool**: `walk()` reused the loop's
  iteration result (1 = an entry) as the recursion error, so `tree` failed on the
  first folder while `list` was fine. Separate variable now, with a comment.

## Evidence

- `make -C tests/image unicode-order-test` →
  `PASS: catalog order, canonical form, tree read and file content match macOS`
  (47 names, 8 tree entries, SHA-256 match, `fsck_hfs` clean).
- Mutation: inverted comparator → `FAIL: our comparator orders the corpus
  differently from macOS`, with the differing pairs printed; revert → pass.

## Open / next

- The writer half of the differential is still partial: the ordering corpus is
  read-only. The same `/Order` listing could be produced by our writer on a
  disposable image and compared with macOS reading *it* — macOS as a consumer of
  what we built, rather than as the producer of what we read.
- `unicode_order_host` prints `other` for anything that is not a folder or a
  file; if a future corpus includes a symlink, the macOS side has to be extended
  the same way rather than silently skipped.

# Handoff — 2026-11-22 (a property test, static analysis, and a test that could not fail)

## What happened

- **`tests/firmware/test_bounce_props.c`** — 4000 deterministic ranges over a
  model disk through `helikon_block_read_bytes`/`write_bytes`: read-back
  equality, refusal of out-of-range ranges, refusal of an unaligned range with
  no scratch, a read-only refusal, and after every write a full-disk snapshot
  comparison so a byte moved outside the range fails. Verified against an
  injected `chunk + 1` in `write_aligned` (red) and clean code (green): 4000
  iterations, 856 refused ranges, 3140 refused without scratch, 3142 writes.
- **The test harness could pass a failing test.** `tests/firmware/test_check.h`
  counts failures; only `helikon_test_report()` turns the count into an exit
  status, and the new test returned 0 instead — `make` printed the FAIL lines and
  `PASS` anyway. The header now installs an `atexit` guard that prints and calls
  `_Exit(1)` when a process ends with failures counted and no report. A probe
  binary with one failing `CHECK` and `return 0` now exits 1.
- **`tools/static-analysis.sh`** — `clang --analyze` over every firmware and
  bootloader translation unit: firmware freestanding (`-ffreestanding
  -nostdinc`), bootloader hosted, libmad and the donor-facing glue skipped, an
  argument filters paths, `--strict` exits 1. **The analyzer exits 0 whether or
  not it finds something**, so the script counts findings by scanning each log
  and takes the status from that.
- **Three findings, all fixed**: a dead store in `firmware/vfs/vfs.c`; the FLAC
  predictor shift guard (`shift >= 31` for a 5-bit signed field, now
  `shift < 0 || shift >= 32`); and the `usb_msc.c` division by `msc->sector_size`
  that the analyzer cannot refine through a struct field — the loop now reads it
  into a local once. Run: **74 translation units, 0 findings.**
- **`tests/bootloader/test_loader_error.c`** — `bootloader/loader_error.c` was
  the only file in the tree with no test at all. 17 distinct non-empty messages,
  no `%` in any of them, no gap from 0 down to `UNSUPPORTED`, and the fallback
  for out-of-range values.
- **CI gained an `analyze` job** running `--strict` on every push and pull
  request, with the per-unit logs uploaded on failure. `docs/testing.md` §3.1
  and §12 record it.

## Evidence

- `./tools/static-analysis.sh` → `74 translation unit(s), 0 with findings`.
- `make -C tests/firmware test` → all groups `PASS`, including
  `PASS bounce props`.
- `make -C tests/bootloader test` → `17 loader error messages, all distinct and
  format-safe`.
- `make -C tests/usb test` → `PASS usb_hal`, `PASS disk_mode`.
- Property test mutation check: injecting `chunk + 1` into `write_aligned` turned
  it red at the outside-the-range comparison; restoring it turned it green.

## Open / next

- The fuzz corpus and the property test are the two nets; the third the plan
  calls for is **differential** work: the same tree through macOS and through the
  writer, plus Unicode order against Apple's `FastUnicodeCompare`.
- A stale object from a mutation experiment made the property test look red once.
  `tools/host-tests.sh` cleans each suite first for exactly this reason; when a
  single `make` is used, delete `build/` after touching the firmware sources by
  hand.

# Handoff — 2026-11-22 (fuzzers with invariants; the corpus is the regression)

## What happened

- **Five libFuzzer harnesses**, one per parser that reads untrusted bytes:
  `id3v2`, `vorbis` (including the FLAC metadata walk), `hdb`, `manifest`
  (manifest + bootstate) and `journal`. Each states its invariant in the harness
  and in `docs/testing.md` §3 — a target that only proves "does not crash" is
  worth little.
- **`tools/fuzz.sh`** builds and runs them; `--replay` builds the same harness
  against `tools/fuzz-replay-main.c` with ASan and UBSan and walks the committed
  corpus. That matters because Xcode's clang has no `libclang_rt.fuzzer`: the
  guided mode is Linux/CI, the replay mode works here.
- **The corpus is committed** under `tests/<suite>/fuzz/corpus/<harness>/`, with
  seeds for the formats (a v2.3 tag, a v2.4 UTF-8 tag, a truncated tag, a Vorbis
  comment block, a FLAC file, an HDB header, four manifest shapes, four journal
  headers including a bad checksum and a wild block list).
- **The journal harness carries I-04**: a header whose checksum does not match
  is never reported clean, and a checksum-consistent but hostile journal may
  fail to replay but never writes outside the device.
- **First thing it found was in my seed, not the code**: the "truncated" tag
  seed encoded 2048, not 16, so `helikon_id3v2_tag_size()` answered 2058 for 14
  bytes — correct behaviour, wrong assertion. The harness now asserts the
  property that actually matters.
- **CI gained a `fuzz` job**: replay, then 20 guided seconds per harness, with a
  reproducer uploaded on failure.
- **It found a real gap on its first guided run**: a 128-byte database whose
  header claimed six megabytes opened successfully, because `hdb_open` validated
  offsets against the header's `file_size` and had no way to see the real one.
  `struct hdb_io` now carries `size`; the blob and the VFS adapter fill it and
  `hdb_open` refuses a mismatch. The reproducer is a corpus file now, and
  `make -C tests/library macos` still passes over a real HFS+ volume.

## Evidence

```
./tools/fuzz.sh --replay        id3v2 5, vorbis 4, hdb 3, manifest 4, journal 4 files: clean
coverage of the corpus          firmware/codec/tag/id3v2.c: 64.2 % regions, 59.9 % lines
```

## Open / next

- The guided run has not executed on GitHub yet; the `fuzz` job is the first
  place it will.
- Next: the macOS differential read and the Unicode order differential, then the
  bounce/partition property tests and the `loader_error` table test.

---

# Handoff — 2026-11-22 (the soak's lost block: an extent that grew without its count)

## What happened

- **The open writer bug is closed.** The soak failed at seed 3, operation 157:
  an append of three blocks to a fork whose inline extents were nearly full left
  the fork claiming 12 blocks while its extents covered 11, with no overflow
  record at logical 11. The lost block was a hole no reader could reach and a
  block the bitmap kept allocated.
- **Cause:** `append_tail_extent()` had a fast path that extends the last extent
  when the next block follows it, and that path bumped `count` but neither
  `inline_blocks` nor `record_blocks`. The inline count is what the *next*
  overflow record is keyed by, so the record was written one logical block early
  whenever an append extended an extent it had just created.
- **Fix:** the path moves the matching counter. Nine lines, including the comment
  that says why the counter matters.
- **Regression:** `test_append_extends_new_extent` in
  `tests/hfsplus/test_catalog_write.c` arranges the bitmap so three appended
  blocks are 66, 67 and 69 — first starts an extent, second extends it, third
  needs the record — then reads all twelve blocks back. It fails without the fix
  and passes with it.
- **The soak is green:** `run-ops-macos.sh 8 300`, 8 seeds, structural check and
  `fsck_hfs` clean after each. The default run is now 4 seeds x 200 operations.
- **The harness prints better:** with `HFS_OPS_VERBOSE=1` it now prints the
  operation description *before* the checks, so a failing step names itself. The
  old order printed only "ok" and the operation that broke the volume was the one
  line missing from the log — which is why this looked like "a create corrupted an
  unrelated file" for a while.

## Evidence

```
./run-ops-macos.sh 8 300          PASS: 8 seeds x 300 operations, hfs-check clean,
                                  fsck_hfs clean (10 s)
make -C tests/hfsplus test        19 cases, including the new one
without the fix                   FAIL append extends the new extent
```

## Open / next

- No device run; this is host evidence on disposable 16 MiB images.
- Next: the fuzzers (ID3v2, Vorbis, HDB, manifest/bootstate, journal replay),
  the macOS differential read, the Unicode order differential.

---

# Handoff — 2026-11-22 (one test command, CI on every push, a preview every night)

## What happened

- **One test command.** `tools/host-tests.sh` runs the nine portable suites
  (`hfsplus firmware bootloader vfs codec musicdb player ui usb`) with one
  compiler, one log per suite under `build/host-tests/`, and stops at the first
  red suite with its tail. `--sanitize` repeats the eight suites that have a
  `sanitize` target under ASan and UBSan; `--macos` runs `tests/image`'s seven
  disk-image drivers; `--keep-going` runs everything; `HOST_TESTS_IMAGE` adds
  `tests/library`. The header says what each mode needs and that none of it
  touches a device.
- **A wrong assumption, found by writing it down.** `tests/image`'s `test` target
  runs `run-macos.sh`, so it is not portable: it is behind `--macos`, not in the
  Linux list. The first draft of the runner had it in both.
- **CI.** `.github/workflows/tests.yml`: portable suites on ubuntu, the same
  under sanitizers, the macOS disk-image suites on `macos-14`, and both firmware
  images (`BOARD=6g image`, `DISKMODE=1 image`) with `arm-none-eabi-` plus
  `-Wno-error`. `--keep-going` so one run shows every red suite.
- **Preview.** `.github/workflows/preview.yml` at 04:17 UTC and on dispatch:
  host-suite gate, `tools/preview-build.sh` builds `payload-6g/` and
  `payload-6g-diskmode/`, the run packs them and publishes `preview-YYYY-MM-DD`
  as a prerelease with `README-PREVIEW.md`, `CHANGELOG.md` and hashes. A re-run
  replaces the day's release.
- **Docs.** `testing.md` §2 and §11 tell a reader to run the one command and §12
  is now the real CI (it was "CI (future)"); `release.md` §1.1 defines the
  preview channel and its limits; `file-map.md` and `state.md` gained rows;
  T-116/T-117 are in `tasks.md` with the honest status: local evidence green,
  no GitHub run yet.

## What the first CI runs found

Four runs on the pull request, each red for a different reason, and none of them
something a macOS host run could show. `firmware/lib/error.h` was resolving to
glibc's `<error.h>` in the suites that put `firmware/lib` after the system
directories (so the freestanding `string.h` cannot shadow libc's), which left
every `HFS_*`/`HELIKON_*` name undeclared on Linux — 70 includes, six suites.
`error.h` and `endian.h` are now `helikon_error.h` and `helikon_endian.h`.
`tests/ui` wrote Mach-O assembler for its HDB blob; the rule now picks the
section and symbol prefix from `uname -s`. The boot-info parser compared the
image end against the page's *host* address, which passed on macOS and failed
with `-no-pie` on Linux; `struct helikon_boot_info_expect` now carries an
explicit `image_ceiling`. Fixtures need `mutagen`, Pillow and `cryptography`,
which the macOS runner installs into a venv because Homebrew's python refuses
`pip install --user`. And the `macos-14` label cancelled the job in one second
twice; `macos-latest` runs.

## Evidence

```
./tools/host-tests.sh clang                    9 suites passed
./tools/host-tests.sh clang --sanitize         8 suites passed
./tools/host-tests.sh --macos                  10 suites passed (image 148 s)
TOOLCHAIN_PATH="$HOME/.rockbox-toolchain/bin" ./tools/preview-build.sh /tmp/preview-test
                                               both payloads + README + CHANGELOG + SHA256SUMS
PR #89 checks                                  portable pass, sanitizers pass,
                                               disk-image (macos) pass, firmware builds pass
```

## Open / next

- **The nightly has not fired yet.** `preview.yml` is committed and its gate is
  the job that now passes, but no `preview-YYYY-MM-DD` release exists. The first
  scheduled run (04:17 UTC) or a manual dispatch is what proves it end to end.
- **The macOS job takes ~8 minutes**, mostly `format-test` (a 150 GiB sparse
  image and `fsck_hfs`) and its queue wait. Fine for now; worth watching.
- **Firmware image is 80,020 bytes** after the expect-struct change (was 80,008).
- **The soak's open writer bug is fixed** (see the next entry): `append_tail_extent`
  did not carry the block count forward when it extended an extent, so an
  overflow record could be keyed one logical block early. `run-ops-macos.sh 8
  300` is green and `test_append_extends_new_extent` pins it.
- **Next in the queue:** the fuzzers, the macOS differential read and the Unicode
  order differential.

---

# Handoff — 2026-11-22 (structural check: chains, key order, bodies, index nodes)

## What happened

- **T-020 keeps growing.** `firmware/hfs/hfs_check.c` now walks each metadata
  tree's leaf chain, checks catalog and extents key order across leaf boundaries,
  validates every catalog record body against its type, and checks each index
  node's child pointer, level and key against the subtree it points at. The
  allocation file is read through its resolved extent map, so a large volume's
  bitmap is counted instead of skipped (PRs #84, #85, #86).
- **Host coverage:** `tests/hfsplus/test_check` is 26 cases, including three
  clean catalogs (one folder, one file, one depth-2 index tree) that must report
  nothing; `make -C tests/hfsplus test` and `sanitize` green.
- **Real volumes:** a 64 MB macOS `HFS+J` volume with nested folders, 16 files
  and non-ASCII names across a 128-node catalog is clean; a 32 MiB image is clean
  and its tampered twin still reports only the free-block mismatch; the 140 GiB
  volume formatted by `tools/disk-mode-format.sh` is clean with 36,594,065 free
  blocks counted through the overflow extents.
- **Images unchanged:** 80,008 B firmware, 40,524 B Disk Mode. Nothing calls the
  checker from an image yet; it stays a host/diagnostic path.
- Built binaries are no longer tracked: `tests/hfsplus/test_check` had slipped
  into #82 and is ignored now.

## Evidence

```
make -C tests/hfsplus test && make -C tests/hfsplus sanitize   # 26 cases
make -C tests/image hfs-check && ./tests/image/hfs-check VOL    # 0/1/2
make -C tests/image test                                       # F2 promotion PASS
./tools/coverage.sh                                            # 82.2% of lines
```

**Coverage:** `./tools/coverage.sh` reports 82.2 % of `firmware/` + `bootloader/`
lines reached by the host suites (16,126 lines, 2,872 never executed). Weakest:
`hfs_btree_tree.c` 76.6 %, `layer3.c` 81.7 %, `flac_decoder.c` 81.0 %,
`usb_msc.c` 77.0 %, `usb.c` 76.9 %, `hfs_catalog_write.c` 87.0 %, `layer12.c`
46.9 %, `inner_image.c` 59.4 %, `disk_mode.c` 69.9 %, `partition.c` 60.9 %. Only
`bootloader/loader_error.c` is never executed at all.

## Open / next

- **T-020 next slices:** the free-node map and node ownership, the attributes
  tree's index keys, then quarantine and the charging/idle scrub policy.
- **Still device-free:** every remaining T-110/T-112/T-115 item needs silicon.
- **Before any device claim:** nothing here has run on a 6G.

---

# Handoff — 2026-11-22 (both halves have a Disk Mode image; neither has run)

## What happened

- **Firmware:** the Disk Mode image now asks first — SELECT hands the disk over,
  MENU or 30 s powers off — and shows capacity and LBA28/48 on the status line.
  `helikon_board_storage_init_writable()` gives Disk Mode a writable block
  device; every other caller keeps the read-only attach. `disk_mode` restore
  mode now clears the board's read-only flag itself, and the host test starts
  from a read-only device so the write proves it.
- **Bootloader:** new separate DFU-only image,
  `tools/build-usb-disk-bootloader.sh` + `bootloader/hardware/helikon_usb_disk_hw.c`
  + `donor-usb-disk.patch`. It links the same `usb.c`, `usb_*.c` and
  `disk_mode.c`, builds the seam over `ata_*`/`udelay`/`PWRCON`, polls
  `helikon_usb_irq()` instead of touching the donor VIC, and calls the mode
  after `storage_init()`, before any mount.
- The entry is `noreturn`: every path ends in `power_off()`, so a future edit
  cannot fall through into the donor's mount.

## Evidence (host and build only, no device run)

- `make -C tests/usb test` / `sanitize` PASS, including the new read-only-device
  regression.
- Firmware Disk Mode image: 40,524 bytes, no mount/player symbols.
- Bootloader image: 108,296 bytes, sha256 `de9b67e3…`; DFU 116,432 bytes,
  sha256 `e27d3b38…`; `_moveend 0x2203ae40` vs `MOVE_AREA` end `0x22040000`,
  20,736 bytes headroom; two runs produced identical hashes.
- Neither image has enumerated, mounted or written anything.

## Open / next

- The owner's run on the 6G: firmware image first (it has the status screen and
  the script), bootloader image second, and record both in `device-matrix.md`.
- Suspects if nothing enumerates: PHY/clock sequence, FIFO split, EP0 status
  stage, and, in the bootloader image, the polled IRQ cadence.
- D-56 (VID/PID) is still open.

---

# Handoff — 2026-11-21 (thirty-third block: the host script that actually formats the thing)

## What happened

- `tools/disk-mode-format.sh`: the macOS half of the whole-disk LUN. It refuses
  more than it does — wrong OS, a slice instead of a whole disk, a device that
  is not a block device, the disk macOS booted from, a missing or empty
  `--backup` record, and any confirmation other than `FORMAT <volume>`.
  `--dry-run` prints the three commands and changes nothing.
- When the checks pass it runs exactly: `diskutil unmountDisk force`,
  `diskutil partitionDisk <disk> APM "Journaled HFS+" <name> 0b`, then
  `diskutil eject`. `APM` is the map an iPod needs and `0b` uses the whole disk,
  which is how the >128 GiB volume appears in one step.
- `docs/disk-mode.md` §11.1 records the procedure, the guards and exactly how
  far the script has been tested; `docs/device-matrix.md` gained the Disk Mode
  row, still `not run`; `docs/tasks.md` T-112 moved to `in progress` with the
  host half named.

## Evidence (host only, no iPod)

- `bash -n`, `--help`, the boot-disk refusal, the missing-backup refusal, the
  slice refusal, and a full `--dry-run` against a 2 GiB disk image attached with
  `hdiutil` (which exercises the `diskutil info` parsing and the three commands'
  text).
- Nothing was formatted on a device, and nothing was mounted on an iPod. The
  script has never seen a real Disk Mode LUN.

## Open / next

- The owner's run: flash `build/firmware/6g-diskmode/`, connect, confirm macOS
  sees one new external disk, `--dry-run`, read it, then the real run, then
  `fsck_hfs -fn`, then the normal firmware. Record it in `device-matrix.md`.
- If enumeration does not happen, the suspects are unchanged: PHY/clock
  bring-up, the FIFO split, the EP0 status stage, the VIC line.
- D-56 (VID/PID) is still the owner's decision before any release ships the
  placeholder.

---

# Handoff — 2026-11-21 (thirty-second block: an image whose only job is to hand over the disk)

## What happened

- `firmware/boot/firmware_main.c` gained a Disk Mode entry behind
  `HELIKON_DISK_MODE_IMAGE`. It runs as early as ATA allows: initialise the
  disk, attach the block device, start the VIC line 19, the USB core and the
  mode in restore mode, then loop on `helikon_usb_pump()` /
  `helikon_usb_pop()` / `helikon_disk_mode_event()` until the host ejects or
  MENU is pressed.
- `firmware/Makefile`: `DISKMODE=1` adds the define and, more importantly,
  moves the build to `build/firmware/6g-diskmode/`. Without the variant
  directory a stale object from the normal build would poison the image.
- The screen is deliberately minimal: CONNECT USB / WAITING FOR THE HOST / HOST
  CONNECTED / EJECTED - SAFE, plus the state name from
  `helikon_disk_mode_state_name()`.
- Why this shape: the host must own every sector while the LUN is open, so this
  image never mounts HFS+. That is checkable, not just claimed: the linked image
  contains no `hfs_volume_probe` symbol because nothing calls the mount path and
  `--gc-sections` drops the tree.

## Evidence (host and build only, no device run)

- `make -C firmware BOARD=6g DISKMODE=1` PASS: raw image 40,524 bytes,
  8,344,980 bytes headroom, `nm` shows `helikon_usb_init`,
  `helikon_disk_mode_start` and `helikon_usb_ep1_send`, and no
  `hfs_volume_probe`.
- `make -C firmware BOARD=6g` PASS: unchanged at 80,004 bytes.
- All ten host suites PASS after the change.
- Nothing has enumerated, mounted or written on a device, and no part of this
  boot path has run on hardware.

## Open / next

- The host-side runbook: backup gate, DFU install of the Disk Mode image,
  `diskutil`/`newfs_hfs` writing an APM with a >128 GB HFS+ volume,
  `fsck_hfs -fn`, eject, then the normal firmware. It should be a script that
  refuses to run without a verified backup.
- `device-matrix.md` rows for P2–P5 stay `not run` until the owner does them.
- If the 6G does not enumerate, the first suspects are, in order: PHY/clock
  bring-up (`usb_hw.h` values are donor facts), the FIFO split, the EP0 status
  stage, and the VIC line. Each has a host test that pins the sequence, so a
  failure is a silicon or sequence question, not a code-shape question.

---

# Handoff — 2026-11-21 (thirty-first block: Disk Mode decides when a host may write)

## What happened

- `firmware/usb/disk_mode.c/.h`: the mode machine between the transport and
  the protocol modules.
  - **The gate.** No `opt_in`, no mode: `helikon_disk_mode_start()` returns
    `HELIKON_ERR_UNSUPPORTED`. D-54 says Disk Mode is explicit, so the refusal
    lives in the module a caller cannot forget, not in the caller.
  - **The window.** Restore mode exposes the whole disk, writable; the data
    volume is forced read-only on the mode's private copy of the block device,
    so the MSC layer answers DATA PROTECT without trusting a caller.
  - **The phase machine.** CBW → data stage → CSW, one command at a time, one
    512-byte slice in flight so the TX FIFO cannot overrun. The next CBW is
    armed when a CSW completes.
  - **Release.** START STOP UNIT with LoEj flushes the medium and moves the mode
    to `RELEASED`; a disconnect flushes and does the same. That is the signal the
    firmware uses to know the volume is its own again.
- `firmware/usb/usb_events.h`: the event vocabulary moved out of the HAL header,
  so the mode module builds on the host without a controller.
- `usb_msc.c`: START STOP UNIT now records an eject request (`take_eject()`), and
  the flag is cleared by a BOT reset.
- `tests/usb/test_disk_mode.c`: a fake transport, the real MSC/control modules
  and a sparse 4 TB disk. It pins: the gate, the whole-disk window, the 18-byte
  device descriptor, SET_ADDRESS 9, SET_CONFIGURATION arming a 31-byte CBW,
  INQUIRY (36 bytes then CSW), READ(10) at LBA `0x10000000` returning that
  sector, WRITE(10) at the same LBA reaching the medium, START STOP UNIT
  ejecting and flushing, a read-only window refusing a write with DATA PROTECT,
  bus reset returning to `WAITING` and disconnect to `RELEASED`.

## Evidence (host only, no device run)

- `make -C tests/usb test` PASS: `PASS usb`, `PASS usb_hal`, `PASS disk_mode`.
- `make -C tests/usb sanitize` PASS for all three binaries.
- `make -C firmware BOARD=6g` PASS: the stack compiles for ARM inside the
  firmware build and the image is unchanged at 80,004 bytes, because nothing
  calls it yet (`--gc-sections`).
- Nothing enumerated, nothing mounted, no device run.

## Open / next

- The caller: an explicit owner action that starts Disk Mode, stops the firmware
  from mounting the volume, runs the USB event loop, and returns to the firmware
  after `RELEASED` with a journal check. That is the last code between this and
  a laptop writing an APM.
- The Disk Mode screen: state, capacity and "safe to unplug" from
  `helikon_disk_mode_state_name()`.
- Then the host-side format runbook: backup gate, DFU install, `diskutil`
  `partitionDisk`/`newfs_hfs` with a >128 GB HFS+ partition, `fsck_hfs -fn`, and
  a `device-matrix.md` row that stays `not run` until the owner does it.

---

# Handoff — 2026-11-21 (thirtieth block: the controller driver, proved against a mock core)

## What happened

- `firmware/hal/6g/usb_hw.h`: every S5L8702 USB fact in one place — OTG base
  `0x38400000`, PHY `0x3C400000`, IRQ 19, clock gates 2/35, the PHY offsets and
  values, the DesignWare register map and bit fields, the FIFO budget
  (`0x360` RX, `0x40` non-periodic TX, one `0x180` EP1 TX), and the seam the
  driver is written against. Values are donor facts; each is labelled.
- `firmware/hal/6g/usb.c/.h`: the device driver over that seam. PHY sequence,
  clock gates, core reset, FIFO programming and flush, GUSBCFG PHYIF16/TRDT(5),
  interrupt masks, EP0 arming, `SET_ADDRESS` written to DCFG before the status
  stage, EP1 IN / EP2 OUT configured on CONFIGURATION, PIO pumps, the RX drain
  for SETUP/OUT packets, the endpoint-interrupt path, bus reset, disconnect, and
  a 16-slot event queue. Every poll is bounded; no allocation, no threads.
- `firmware/hal/6g/usb_target.c`: the only file that touches real memory, timer E
  and VIC IRQ 19, plus the 32-bit FIFO packing the core requires.
- `tests/usb/test_usb_hal.c`: a mock DesignWare register file, mock FIFOs and a
  mock VIC/clock. It checks the PHY values and delays, GUSBCFG, the three FIFO
  registers and their starts, GAHBCFG/DCFG/DCTL, every mask bit, EP0's arming,
  a SETUP packet's bytes, an 18-byte EP0 IN answer, ENUMDNE → high speed,
  SET_ADDRESS, EP1/EP2 configuration at 512 bytes, a 1024-byte bulk IN, a CBW
  arriving on EP2 with its completion event, bus reset clearing address and
  configuration, disconnect, a spurious call, and shutdown.

## Bugs the mock found (all fixed)

- AHBIDLE was waited on with the wrong polarity; a core that is idle would have
  timed out and aborted init. It is a "wait for set" bit, unlike CSRST and the
  FIFO-flush bits.
- The event queue pushed its overflow notice recursively, which could recurse
  again; it now drops and counts.
- EP0's NAK was cleared by writing the read-only status bit instead of CNAK.
- RXFLVL was left asserted after a drain that had nothing to consume, which in
  the mock (and on a core with a refused packet) re-raises the line forever.

## Evidence (host only, no device run)

- `make -C tests/usb test` PASS: `PASS usb`, `PASS usb_hal`.
- `make -C tests/usb sanitize` PASS for both binaries; the sanitizer builds are
  separate files, so they cannot poison a later plain run.
- Full host matrix re-run after the change: firmware, hfsplus, vfs, codec,
  musicdb, player, ui, image, bootloader, usb all pass.
- Cross-build: `make -C firmware BOARD=6g` links `hal/6g/usb.o` and
  `usb_target.o`, image 80,004 bytes, 8,304,508 bytes headroom.
- **Not claimed:** nothing enumerated, nothing mounted, no device run. The mock
  proves the sequence the driver writes, not what the silicon does with it.

## Open / next

- `firmware/usb/disk_mode.c`: the mode state machine and its opt-in gate, the
  LUN window (whole disk for restore/format, or the largest HFS+ partition),
  flush on eject, the "firmware must not mount while the host owns the LUN"
  rule, and the UI status screen. Wire `firmware/usb/*` and the HAL into the
  build behind that mode.
- Then the host-side format runbook: backup gate, DFU install of the Disk Mode
  image, `diskutil`/`newfs_hfs` writing an APM with a >128 GB HFS+ partition,
  `fsck_hfs -fn`, and a `device-matrix.md` row that stays `not run` until the
  owner does it.
- D-56 still needs the owner: the VID/PID in the descriptors is a labelled
  placeholder.

---

# Handoff — 2026-11-21 (twenty-ninth block: the USB device core answers a laptop's SCSI)

## What happened

- Owner goal: be able to format the iPod above 128 GB over USB from a laptop.
  That needs a working USB mass-storage device, not just LBA48 addressing, so
  this block built the host-visible half of Helikon Disk Mode.
- `firmware/usb/usb_descriptors.c/.h`: device descriptor (18 B, `0x1209/0x0001`
  marked as a placeholder pending **D-56**), one configuration (32 B, MSC
  class 0x08 / subclass 0x06 / protocol 0x50, bulk IN `0x81`, bulk OUT `0x02`,
  512-byte bulk packets), and string descriptors built as UTF-16LE at runtime
  with an ASCII-only helper. No serial string: a device serial is a personal
  identifier and `disk-mode.md` §7 forbids exposing one.
- `firmware/usb/usb_control.c/.h`: chapter 9 GET/SET_DESCRIPTOR,
  SET/GET_CONFIGURATION, SET_ADDRESS, GET_STATUS, endpoint halt via
  endpoint-recipient SET/CLEAR_FEATURE, plus MSC class GET_MAX_LUN (0) and
  BULK_ONLY_RESET. Answers are `{stall|in|out|no data, data, length}` so the
  packet pump has nothing to interpret.
- `firmware/usb/usb_msc.c/.h`: the bulk-only transport and the SCSI command set
  over a window (`base_lba`, `sector_count`) of a `struct helikon_block_device`.
  CBW validation (signature, reserved bits, CDB length, LUN 0 only), CSW with
  residue, INQUIRY, REQUEST SENSE, READ CAPACITY(10) and (16) via
  `SERVICE ACTION IN`, READ FORMAT CAPACITIES, MODE SENSE 6/10,
  READ/WRITE/VERIFY(10/16), SYNCHRONIZE CACHE(10/16), START STOP UNIT and
  PREVENT/ALLOW. FORMAT UNIT is refused on purpose: a host formats by writing a
  partition map and a filesystem through the LUN, which keeps every byte
  visible. A read-only window answers DATA PROTECT; an LBA outside the window
  answers CHECK CONDITION / LBA OUT OF RANGE.
- `tests/usb`: a host harness that speaks CBW/CSW the way a laptop does, against
  a sparse 4 TB fake disk (sector `n` lives at `n % 8192`, so a 64-bit LBA needs
  no 4 TB array).

## Bugs the tests found (all fixed)

- A READ command's data stage was capped to `in_length`, which only fixed
  responses set — every READ returned zero bytes. The length now comes from
  `blocks × sector_size` unless the command filled `in_buffer`.
- `helikon_usb_msc_command()` reset the whole state, including sense data, at the
  start of every CBW, so REQUEST SENSE could never see the error it was asked
  about. A new `begin_command()` clears transport state only; the public reset
  still clears sense, and REQUEST SENSE clears it once the response is built.
- Endpoint halt was handled under the device recipient; SET_FEATURE/CLEAR_FEATURE
  with the endpoint recipient now own it.
- Test-harness bugs: capacity saturation needs a disk larger than 2 TiB (a 1 TB
  disk fits in 32 bits), the out-of-range CDB needed READ(16) and a correctly
  byte-ordered LBA, and an OUT command has no `in_capacity` to check.

## Evidence (host only, no device run)

- `make -C tests/usb test` PASS (`PASS usb`), `make -C tests/usb sanitize` PASS
  with no ASan/UBSan report. `sanitize` now builds `test_usb_sanitize`, so a
  sanitizer build can no longer poison the next plain run.
- The suite pins: descriptor bytes, every control request including a STALL for
  an unknown request, a STALL for BULK_ONLY_RESET's flag, INQUIRY vendor string
  and removable bit, CAPACITY(10) = `0xffffffff`, CAPACITY(16) = 4 TB − 1,
  a WRITE(10) at `0x10000004` read back byte for byte, a 4-sector write in
  64-byte pieces, sense survival, a read-only window refusing a write, FORMAT
  UNIT refused, SYNCHRONIZE CACHE reaching the block flush, a malformed CBW, LUN
  1 refused, a partition window mapping to the right absolute LBA, and an
  overlong window refused at init.
- No firmware image changed this block, so no existing image was rebuilt or
  re-flashed; nothing is claimed for a device.

## Open / next

- S5L8702 HAL: `firmware/hal/6g/usb.c/.h` from `docs/usb-device-stack.md` §2
  (OTG `0x38400000`, PHY `0x3C400000`, IRQ 19, clock gates, FIFOs, GUSBCFG
  PHYIF16/TRDT, EP0 + EP1 IN + EP2 OUT) behind a build flag, compile-only.
- `firmware/usb/disk_mode.c`: explicit opt-in, LUN choice (whole disk for
  restore/format, or the largest HFS+ partition), no firmware mount while the
  host owns the LUN, flush on eject, journal check/reboot after release.
- Then the host-side runbook: backup gate, DFU install, `diskutil`/`newfs_hfs`
  writing an APM with a >128 GB HFS+ partition, `fsck_hfs -fn`, and a recorded
  `device-matrix.md` row that stays `not run` until the owner does it.
- D-56 needs the owner: pick a VID/PID route before any release ships the
  placeholder.

---

# Handoff — 2026-11-21 (twenty-eighth block: the 128 GiB boundary is pinned on the host)

## What happened

- The owner asked whether storage above 128 GB is possible yet. The honest
  answer needed evidence, not prose, so the boundary got tests instead of a
  claim.
- What existed already: a runtime LBA48 probe (`ata_geometry.c` words 83/86,
  100–103, 60–61, 106/117–118), `READ/WRITE MULTIPLE EXT` in the HAL for every
  LBA once LBA48 is selected, 64-bit byte offsets through block, partition and
  HFS+, and the 128 GiB boundary cases in `test_ata_geometry.c`.
- What was missing was proof that the layers above the HAL keep a large card's
  geometry straight. Three host checks now do:
  - `tests/firmware/test_ata_geometry.c` parses a 1 TB card (1,953,125,000
    sectors → exactly 1,000,000,000,000 bytes, LBA48 selected) and a 512 GB one,
    and pins a 128 GiB card staying LBA28 on the nose.
  - `tests/firmware/test_block.c` scans an Apple map whose HFS+ partition starts
    above 2^28 sectors, one that straddles the limit, and asserts the 64-bit byte
    offsets, lengths and the map's 32-bit fields.
  - `tests/hfsplus/test_volume.c` probes a volume with a base above 128 GiB using
    a reader that refuses any offset below the base, so a 32-bit truncation would
    fail immediately; it also proves a base that would wrap is refused, not
    wrapped.
- `docs/disk-mode.md` §10.1 was stale: it told the reader to add
  `HELIKON_HAVE_LBA48`, which D-54 replaced with a runtime decision. §10.2
  claimed CE-ATA devices "must be rejected", while `ata_geometry.c` follows the
  donor rule and selects the LBA48 path for them. That contradiction is now
  recorded as open decision **D-55** instead of being silently resolved either
  way.

## Evidence (host only, no device run)

- `make -C tests/firmware test` PASS (needs `clean` first: the shared build dir
  keeps sanitizer objects from `make sanitize`).
- `make -C tests/hfsplus test` PASS, `PASS base above 128 GiB`, 15 tests in the
  volume suite.
- No firmware or bootloader image changes; nothing was re-flashed or claimed.
- Docs: `disk-mode.md` §10.1/§10.2, `decisions.md` D-55, `tasks.md` T-109,
  `state.md` Disk Mode paragraph, this handoff and the log.

## Open / next

- The only thing that can turn this into support is a device run: T-109's probe
  image on a 6G/iFlash SD records IDENTIFY 83/86/100–103, the CE-ATA bit and the
  APM geometry, and a card above 128 GiB would then need a recorded row in
  `device-matrix.md`. Until then the correct sentence is "the addressing path is
  host-proven; no device has mounted a volume above 128 GiB".
- Capacity migration (§11 option A) does not exist: a host-side tool that adds a
  larger HFS+ partition in the free space, with a verified backup and `fsck_hfs`.
- D-55 needs the owner: keep the donor CE-ATA→LBA48 rule (and fix §10.2's
  wording) or reject CE-ATA devices explicitly.
- Unchanged from the previous block: T-058's hub call site and the VFS mount,
  T-059's device half, the artist-tab hint, splash colour-only states,
  cross-track gapless.

---

# Handoff — 2026-11-21 (twenty-seventh block: next/previous has an order to walk)

## What happened

- T-058's next missing piece: the play order. The queue stores track ids and
  answers what happened, so the Now Playing screen can bind next/previous
  without inventing its own list logic.
- `firmware/player/queue.c/.h`: two orders over the same ids (natural and a
  seeded Fisher-Yates shuffle), `HELIKON_QUEUE_CAPACITY` 512, no allocation.
  `next`/`previous` return `MOVED`, `WRAPPED`, `END`, `RESTART` or `EMPTY`
  instead of an error code. With repeat off the ends are hard stops and the
  position does not move, so a caller that ignores the answer cannot silently
  wrap; repeat-all wraps; repeat-one answers `RESTART` and keeps the id, which is
  the caller's cue to restart the track rather than seek to another.
- Shuffle keeps the playing id first when it is enabled mid-track and the
  natural order returns around the current id when it is turned off. A zero seed
  is nudged rather than frozen, so a seed always produces an order.
- Tests caught one real bug on the way: `queue_shuffle()` and
  `queue_position_of()` compared the order slot (an index into `ids`) with a
  track id, so "shuffle keeps the playing id" failed and shuffle-off restored
  the wrong position. Both now compare `ids[order[i]]`.

## Evidence (host only, no device run)

- `make -C tests/player test` and `sanitize` PASS (`PASS play queue`).
- `test_queue.c` pins the natural walk, both hard stops, repeat-all wrapping in
  both directions, repeat-one restarting without moving, an unknown repeat mode
  refused, a shuffled walk that visits all eight ids exactly once, two fresh
  permutations from the same seed matching step for step, shuffle on/off keeping
  the playing id, the empty queue, the empty set clearing a list, the capacity
  boundary (accepted, and one past refused) and every null path.
- `firmware/Makefile` puts `player/queue.c` in the AUDIO set; the music-hub image
  still links at 768,176 B because nothing calls it yet and `--gc-sections`
  drops it.
- Docs: `audio-formats.md` §23, `tasks.md` T-058, `state.md`, `file-map.md`.

## Open / next

- The hub call site is now the only thing missing before the queue is real: build
  the queue from the list being played, bind next/previous to
  `helikon_queue_next`/`previous` plus `helikon_player_open_media` for the
  resolved track, and keep the embedded blob as the fallback. It needs a mounted
  VFS in the firmware, which does not exist yet — that mount is the first piece
  of the next block.
- After that: I2S/DMA output on the device, an underrun/read-latency measurement,
  and the device run. T-059's device half still waits on a real signed firmware
  image (T-051, blocked with T-055).

---

# Handoff — 2026-11-21 (twenty-sixth block: audio is decoded from a real volume, not a fake)

## What happened

- T-058's "no storage-backed playback" line is now narrower: the whole chain
  HFS+ → VFS → `vfs_media` → read-ahead window → codec → player ring is executed
  on a real journaled HFS+ volume, on the host, with the PCM required to match.
- `tests/library` already mounted a real `hdiutil` image to read the catalog. It
  now also decodes audio from it: `run-library-macos.sh` copies
  `tests/codec/fixtures/mp3_cbr_stereo16.mp3` to `/Music/first.mp3` before the
  volume is unmounted and `fsck_hfs` checked, and `test_library.c` opens that
  path through the VFS, wraps it in the read-ahead window and plays it through
  `helikon_player_open_media` with a silent host HAL (no DMA, I2S or PMU).
- The assertion is differential: the same bytes are also read back into a
  resident buffer and decoded there, and the two PCM streams must be identical
  frame for frame. A VFS splice, extent or window error cannot pass it, and a
  decoder that read past the end would diverge immediately.
- New `macos-sanitize` target (and `SAN=1` support in the Makefile) runs the same
  image test under ASan and UBSan. The script now always rebuilds the binary
  first, because the target does not track `CFLAGS` and a sanitizer run must
  never silently reuse the plain build.

## Evidence (host only, no device run)

- `make -C tests/library macos` → `PASS library: HFS+ -> VFS -> HDB (6 tracks,
  3 albums, CRC OK) and audio decodes from the volume`; `fsck_hfs -fn` reports
  the volume OK with no repair.
- `make -C tests/library macos-sanitize` PASS, with the ASan runtime linked
  (`otool -L` shows `libclang_rt.asan_osx_dy…`).
- The rest of the matrix is unchanged and was re-run: `tests/{firmware,hfsplus,
  vfs,codec,musicdb,player,ui}`, `tests/image`, `tests/bootloader`.
- Docs: `audio-formats.md` §22, `testing.md` (the library section and the
  `libmad.mk` includer list), `file-map.md`, `tasks.md` T-058, `state.md`.

## Open / next

- The Music hub still plays its embedded blob. The next T-058 step is the call
  site: resolve the selected track to `hdb_track.path`, open it through the VFS
  and call `helikon_player_open_media`, keeping the embedded blob as the fallback
  when there is no volume. That needs a VFS parameter on `helikon_music_run` and
  its two callers (`launcher_run.c`, `firmware_main.c`), plus the UI tests
  updated for the NULL case.
- After that: next/previous with a queue, an underrun/read-latency measurement,
  then a device run. T-059's device half still waits on a real signed firmware
  image (T-051, blocked with T-055).

---

# Handoff — 2026-11-21 (twenty-fifth block: the state row pays its debt)

## What happened

- Documentation only. The `state.md` "VFS and player" row had grown into a dozen
  stacked "Host workspace (date, block)" notes, several still calling merged work
  "unmerged" — a reader could not tell which parts of the player were merged,
  which were device-proven and which were still missing. It was flagged twice in
  earlier handoffs and is now rewritten.
- The new row sorts the facts the way a reader needs them: what two device runs
  actually proved (WAV and FLAC replay), what is merged but host-only (with the
  PR number for each slice: #35 registry and MP3, #36 pull sources, #37 FLAC/MP3
  pull decoders plus the VFS media adapter, #38 ID3v2, #40 gapless trim, #41
  media open, #42 seek, #43 Vorbis comments, #44/#51 the library-over-VFS read
  path, #64 the MPEG family, #66 read-ahead), and what does not exist
  (storage-backed ring wiring, Music-hub call sites, next/previous, underrun
  measurement, ALAC/AAC, cross-track gapless, any device run of storage-backed
  playback).
- One wrong fact fell out of the rewrite: `state.md` called PR #21's Music-flow
  image "device-blocked and unmerged". PR #21 is merged (`f3b511d`); what is open
  is its device report, where selecting an artist took almost 20 s.

## Evidence

- Documentation only: `git diff --check`; no build or test claim changed. The
  PR numbers were read from `git log`, not remembered.
- `docs/state.md` "VFS and player" row: 4,762 → about 2,150 characters, with no
  fact dropped that was not either merged elsewhere or still open.

## Open / next

- Back to code: T-058's Music-hub call sites (open a track through the VFS, play,
  seek, next/previous with a queue), then the I2S/DMA run and a device
  measurement. T-059's device half still waits on a real signed firmware image
  (T-051, blocked with T-055).
- Unchanged and still recorded: the artist tab row has no on-glass hint, splash
  pass/fail differ by colour alone, cross-track gapless is missing, the Music hub
  is still a second launcher.

---

# Handoff — 2026-11-21 (twenty-fourth block: the player stops re-seeking for every decoder call)

## What happened

- T-058's next missing piece after the pull-source model and seek: the
  read-ahead policy. The decoders ask for small pieces at absolute offsets (a
  FLAC frame header, a 4 KiB frame window, an MP3 reservoir refill) and every one
  of those cost a VFS seek plus a read. On a 1.8" disk the seek is the cost, so
  the decoder could starve the ring without ever decoding slowly.
- New `firmware/player/read_ahead.c/.h`: `helikon_read_ahead_init()` wraps any
  `struct helikon_codec_source` in a caller-provided window. A request inside the
  window is a hit; one that crosses the window end is a straddle (served partly
  from memory, finished with one refill); a request at least as large as the
  window bypasses the copy entirely and reads straight through. Media is
  immutable while a track is open, so the window is never invalidated — a
  backward seek inside it costs no storage access, which is what makes a
  reservoir re-read cheap. A short read is not end of stream; only a zero read
  latches the end, and a downstream error is returned unchanged instead of
  becoming a truncated file.
- `helikon_player_open_media()` owns both the policy object and a 16 KiB buffer
  and wraps the caller's source before the probe, so the Music hub keeps passing
  a plain source. `struct helikon_player` grew by 16 KiB of `.bss`; the raw image
  does not grow.
- `tests/player/test_read_ahead.c` measures what the device pays for: reads, not
  time. 64 one-KiB pulls over 64 KiB cost 16 window reads; a backward read costs
  none; a window-sized request bypasses once and leaves the window intact; EOF
  latches; the read-ahead is a pass-through when given no buffer; a media error
  mid-refill surfaces as an error with no partial window behind it.

## Evidence (host only, no device run)

- `make -C tests/player test` and `sanitize` PASS (`PASS read ahead`).
  `test_player` gained one seam assertion: a WAV opened through a media that
  returns 37 bytes at a time now reaches the media as one 16 KiB request.
- Full matrix: `tests/{firmware,hfsplus,vfs,codec,musicdb,player,ui}`,
  `tests/image` and `tests/bootloader` pass; sanitizers pass for `player`,
  `hfsplus`, `vfs`, `codec`, `firmware` and `bootloader`.
- Cross-builds: plain 6G unchanged; the music-hub image still links at 768,176 B
  (the window is `.bss`).
- Docs: `audio-formats.md` §21, `tasks.md` T-058, `state.md` player row,
  `file-map.md`, `tests/ui/Makefile` and `tests/player/Makefile` include the new
  module.

## Open / next

- T-058 now needs its Music-hub call sites: open a track through the VFS, play,
  seek, and next/previous with a queue. Then the I2S/DMA run, an
  underrun/read-latency measurement and one device run.
- The `state.md` "VFS and player" row is still a stack of "Host workspace"
  notes; it needs the rewrite pass recorded in the previous block, with PR
  numbers instead of "unmerged" sentences.
- Everything else recorded in the previous two blocks is unchanged: T-051/T-055
  block the device half of T-059, the artist tab row still has no on-glass hint,
  splash pass/fail still differ by colour alone, cross-track gapless is missing.

---

# Handoff — 2026-11-21 (twenty-third block: the loader fills the boot-info page)

## What happened

- T-059, the last piece of the accepted D-28 entry ABI: the loader test image now
  writes the 4 KiB boot-info page at `0x087ff000` and passes it in `r0`. Until
  this block the page was a documented contract that nothing filled, so the
  loader jumped with `r0 = 0` and any firmware had to re-read the disk for facts
  the loader had already verified.
- New `bootloader/boot_info_page.c/.h`: `helikon_boot_info_page_build()` takes a
  `struct helikon_loader_handoff` (plain facts, no pointers into the loader's
  stack) and fills magic/version/size, loader version, board, image version,
  load address, size, SHA-256, bootstate sequence and state, journal state,
  flags, loader stage time, CRC-32 over bytes 0–71, and the 256-byte verified
  manifest copy. The writer **refuses** (`HELIKON_ERR_FORMAT`) when the manifest
  bytes disagree with the stated board, image version, load address, size or
  SHA-256, and refuses (`HELIKON_ERR_ARGUMENT`) a short, unaligned or missing
  page. The CRC is a documented duplicate of `firmware/lib/crc32.c` (same
  polynomial, reflected, init/final 0xffffffff) because the loader does not link
  the firmware library.
- `firmware_loader.c` now keeps the verified manifest bytes in
  `struct helikon_loader_result`, because the page needs a byte-for-byte copy and
  the loader has no second read of the file.
- `helikon_loader_hw.c` fills the page after manifest/image verification and before
  the jump, passes its address in `r0`, and on a writer refusal shows
  "Boot-info page refused" and jumps with `r0 = 0` — the value the firmware
  parser already answers with `HELIKON_ERR_NOT_FOUND`, which sends it back to the
  read-only diagnostic path. `journal_state` is `0` (unknown): only the firmware
  mounts, and a dirty journal already stops the boot before this point.
  `loader_stage_ms` is loader work only, measured from the start of
  `helikon_loader_main()`, because the donor main paints the splash first.
- `tests/bootloader/test_boot_info_page.c`: 24 checks that round-trip the filled
  page through the **real** `firmware/boot/boot_info.c` parser rather than a
  copy of its rules — every field survives, reserved bytes stay zero, the four
  writer refusals hold, the parser rejects a corrupted header and a corrupted
  manifest copy, and a zero pointer reports `NOT_FOUND`.

## Evidence (host only, no device run)

- `make -C tests/bootloader test` PASS and `sanitize` PASS (`PASS boot info
  page`, 24 checks); `make -C tests/image test` and `loader-test` PASS;
  `./tools/check-loader-artifact.sh` PASS.
- Loader test image `7c4edc96…`, 107,560 bytes (was `8c161d0e…`, 106,760), DFU
  `18c595b5…` (115,696 bytes); `_moveend 0x2203ac20`, **21,472 bytes** of
  `MOVE_AREA` headroom (was 22,264).
- Build wiring: `tools/build-loader-bootloader.sh` copies `boot_info_page.c/.h`
  and `firmware/boot/boot_info.h` into the donor `helikon/` directory, and
  `donor-loader.patch` adds `helikon/boot_info_page.c` to the donor `SOURCES`.
  Two build-only bugs surfaced and were fixed on the way: the SOURCES hunk
  header counted one line too few (so `hfs_unicode.c` was silently dropped from
  a re-applied patch and the link failed), and `struct helikon_bootstate` nests
  its fields under `.slot`.
- Docs: `LOADER.md` §4 (page fill, refusals, `journal_state`, stage-time
  meaning), `LOADER_TEST.md` status, `device-matrix.md` (new row, `not run`),
  `tasks.md` T-059 (now `in progress`, host evidence, device run blocked on
  T-051), `state.md` (loader rows), `file-map.md`.

## Open / next

- The device half of T-059 needs a real signed firmware image (T-051, blocked
  with T-055): the loader test payload ignores `r0`, so no host or device run can
  yet show firmware reading the loader's version and stage time. Until then the
  page is host-verified only.
- `docs/state.md` "VFS and player" row still needs its rewrite pass (stacked
  "Host workspace" notes, some saying "unmerged" for merged work).
- Music hub is still a second launcher; the artist tab row still has no on-glass
  hint; splash pass/fail still differ by colour alone; cross-track gapless is
  still missing. All recorded, none scheduled.
- Next code line on the roadmap: the read-ahead policy and music-hub call sites
  for T-058, or the T-053 key-store wiring into the hardware loader.

---

# Handoff — 2026-11-21 (twenty-second block: the MP3 family is executed, not claimed)

## What happened

- Picked the next P1 line after the UI treatment: the MPEG audio coverage gap in
  T-097. Every MP3 fixture was MPEG-1 Layer III at 44.1 kHz, so the wrapper's
  MPEG-2/2.5 and Layer I/II branches were a claim in a comment, and the frame
  arithmetic those branches need (576 samples per frame instead of 1152) was
  never exercised.
- Three fixtures were added and generated with the committed script:
  `mp3_mpeg2_stereo16` (22.05 kHz, 64 kbps), `mp3_mpeg25_stereo16`
  (11.025 kHz, 32 kbps) and `mp3_layer2_stereo44` (MPEG-1 Layer II, 128 kbps, no
  Xing frame, produced by ffmpeg because LAME is Layer III only).
- `check_mp3_family()` in `tests/codec/test_codec.c` pins, per fixture: the
  sample rate, the raw frame count from the Xing frame (or the file), the LAME
  encoder delay and padding, the trimmed decoded length, the gapless flag, a
  non-silent peak, and a frame-index seek that must resume.
- **Nothing was broken.** Measured: MPEG-2 4,608 raw frames -> 3,456 decoded;
  MPEG-2.5 3,456 -> 2,304; Layer II 2,304 -> 2,304 with no gapless claim. The
  576-sample header parse, the gapless trim and the seek divisor all agree with
  the encoders' own numbers, which is exactly what the fixtures were for.

## Evidence (host only, no device run)

- `make -C tests/codec test` PASS and `make -C tests/codec sanitize` PASS with
  the new family checks and the three committed fixtures.
- Fixtures are 1,461-1,879 bytes and regenerated by
  `python3 tests/codec/make_mp3_fixtures.py` (lame, plus ffmpeg for Layer II).
- Docs updated: `audio-formats.md` §13.5 (the measured table) and §13.6 (what is
  left), `tests/codec/README.md` (the full MP3 fixture table), `file-map.md`,
  `tasks.md` T-097 and the `state.md` audio row.

## Open / next

- Layer I still has no fixture: no common encoder emits it, so the branch stays
  unexercised rather than claimed. If a Layer I file ever appears, the fixture
  comes from a synthesised bitstream, not an encoder.
- The rest of audio-formats §13.6: MP3 resume, half-rate decode, the ARMv5E
  synth accelerator, then the T-100/T-101 device gates.
- The `state.md` "VFS and player" row has grown into a dozen stacked
  "Host workspace (date, block)" notes, several of which still say "unmerged"
  for work that is merged. It needs one rewrite pass with the merged PR numbers
  instead of another appended sentence; that is the next documentation debt.
- Cross-track gapless (the player-level delay carry between tracks) is still
  not implemented; only the per-file trim is.

---

# Handoff — 2026-11-21 (twenty-first block: the review becomes four fixes)

## What happened

- The owner approved **every** treatment batch the twentieth block proposed and
  asked for `.unslopovich/chart.md`. Four PRs landed, one per surface, each with
  its own tests and its own "no device run" note:
  - **#57 firmware Music flow.** Footers rule at 226 and speak one grammar
    (`MENU BACK` / `SELECT OPEN`); empty lists say `NO TRACKS IN LIBRARY`; the
    error screen maps the player stage to a cause sentence, says what SELECT
    does and drops the duplicate mapping and the `OUT -` row; `STOPPED AT THE
    START` when nothing played; OPENING keeps its footer instead of
    `PLEASE WAIT`; `YEAR -`, a lone dash and unrated stars are gone; the artist
    tab row and the Overview facts line lose their counts; the visualizer
    baseline moves FAINT (1.23:1) -> RULE; the wall caption uses a middle dot.
    `tools/render-ui-mockup.py` also stopped crashing: `render_legacy()` used
    `extend` on single-path mockups, and a default run now skips the legacy group
    with a note when the owner's licensed Benguiat faces are unreadable.
  - **#58 artist tab row.** It is now the focus stop `docs/ui.md` 6.5 always
    described: the wheel enters it from the first body item, the wheel up steps
    back through tabs, the wheel down returns to the body, LEFT/RIGHT change the
    tab while focused and move the body selection otherwise, SELECT returns, and
    MENU keeps its one meaning. Labels moved to x=26/94/178 so the 2x16 focus
    bar keeps an 8 px gutter and nothing shifts on focus.
  - **#59 companion.** The welcome is a first-run card
    (`com.helikon.companion.hasSeenSplash`, `--skip-splash` still works, the app
    menu reopens it); the 248 pt rail is a single top band; the window floor is
    720x480 and it opens at 900x508; `HColor.faint` (2.22:1) is gone and every
    text use is `muted` (4.72:1) with the floors recorded in the palette; the
    app-wide `NSApplication.appearance` override is dropped, so only the window
    stays light.
  - **#60 boot splash label.** The label colours moved to
    `bootloader/splash_label.h`: deep green `#007A3D` (4.79:1 on panel) and deep
    red `#C81E1E` (4.78:1), from 2.10:1 and 3.14:1. No owner artwork, palette or
    mask byte changed. `tests/bootloader/test_splash_color.c` measures the
    RGB565 values the panel shows and fails below 4.5:1.
- `.unslopovich/chart.md` records the standing context and the accepted
  exceptions (paper palette and serif, 11 px caps on a fixed 320x240 panel, muted
  5.00:1, no dark mode or text scaling, owner boot artwork, no footer hint row on
  artist and album pages, `SORT: NAME` as truthful inert metadata).
- The firmware report's visualizer item was **corrected** in its visit file: the
  baseline was always drawn; the defect was 1.23:1 contrast, not a missing row.
- The mockups were made honest about generated art. `artist_portrait()` drew a
  person glyph for an artist with art and a monogram for one without, so the
  artists list looked like favouritism and contradicted `docs/ui.md` 6.4, and
  the same artist carried different counts on the list and on its own page.
  Each artist now has its own generated line art, missing art is a monogram
  everywhere, and the counts agree across screens.
- A follow-up review re-scored the treated surfaces and left the remaining
  problem on the record: firmware 13 -> 17/24, companion 10 -> 16/24, splash
  14 -> 17/24 (`.unslopovich/visits/2026-11-21-1605-follow-up.md`). The biggest
  open item is discoverability, not correctness: the artist tab row is reachable
  only by scrolling up past the first item and no footer mentions it, so two of
  the three panes can stay invisible to a first-time user.

## Evidence (host only, no device run)

- `make -C tests/ui test` PASS, including new probes for the focused tab row (bar
  absent at rest, 2 px wide when focused, underline kept) and the reworded error
  screen.
- 34 music-flow and now-playing PNGs re-rendered from the changed code path and
  byte-identical across two clean runs; `clipping-check.txt` PASS.
- Plain 6G image 80,004 B; music-hub image 768,184 B with MP3 linked; F2 image +
  `./tools/check-f2-artifact.sh` PASS.
- hfsplus, vfs, firmware, bootloader (test + sanitize, with the new splash
  colour check), image (test, loader-test), codec and musicdb suites PASS.
- Companion: `swift build` PASS, `scripts/build-app.sh --debug` PASS, a host
  harness compiled against the shipped `AppModel.swift` proves first-run ->
  persisted -> skipped -> reopened, and the launched window measures 900x508.
  Screenshots are still refused (Screen Recording), so nothing visual was seen.
- Loader test image is now `8c161d0e…` (106,760 bytes); the same tree with the
  previous label colours builds `90925547…` at the same size, so the colour change
  adds no bytes. It has **not** been on a device.

## Open / next

- **Dropped, not done:** the Music hub is still a second launcher. Removing it
  (`draw_hub`, `run_hub`, `helikon_music_hub_*`, `tools/make-launcher-assets.py`,
  docs and tests) is a navigation change the owner has not answered, and the hub
  is the only pinned second entry point while the launcher cells are stubs.
- The boot splash states still differ by colour alone. Different words or a shape
  live in the owner mask: an owner decision, recorded in the asset README.
- The splash label change invalidates the device-tested loader hash. The last
  device-tested image remains `6bc6927c…`; the new build needs a run before any
  claim.
- The `--:--` placeholder in `draw_top_bar()` is gone: the HAL has a monotonic
  timer, not an RTC, so the time slot is empty until one exists and
  `FONT FALLBACK` is the only string that uses it (it reports a missing font
  atlas, a build error). The mockups keep showing `12:34` as the intended state.
- Not fixed from the review: five launcher cells that open a build note,
  `SORT: NAME` staying inert (accepted in the chart), and the tab-row hint.

---

# Handoff — 2026-11-21 (twentieth block: every interface reviewed)

## What happened

- Owner asked for a full design review of **every** interface, not a change to
  any of them. Three reviews were produced with the Unslopovich method (read the
  running thing or the committed render, walk the top tasks as three users, then
  the detector, then the report) and saved as visits:
  `.unslopovich/visits/2026-11-21-{1529-firmware-music-ui,1531-companion,1533-boot-splash}.md`.
- **Firmware UI, 13/24.** The design language holds; the defects are truth and
  navigation. `draw_top_bar()` never draws a real time: the clock slot is
  `--:--` or the word `FONT FALLBACK` on every music screen
  (`firmware/ui/music.c:1095`). Five of six launcher cells open a build note
  (`launcher_run.c:19`). An empty Tracks list is headed `EMPTY` / `NO ITEMS`
  (`music.c:1010`). `AUDIO CODEC/12C` reaches the user as a cause line
  (`music.c:2717`). Counts repeat up to three times on the artist page,
  `SORT: NAME` is inert, footers use three grammars on four baselines, an
  unrated album draws five empty stars, and the artist/album pages print no
  button hints at all. The two navigation items (LEFT/RIGHT as tab switching,
  and the Music hub as a second launcher) are recorded as **Ask first**.
- **Companion, 10/24.** The shell is honest and unreadable in places: the splash
  gates every launch with no opt-out (`AppModel.swift:7`), `HColor.faint`
  measures 2.22:1 for real sentences, the main pane carries three build notes,
  a 248 pt sidebar has no destinations, and the window cannot go below
  1120×720. Screenshots were refused (Screen Recording permission), so the
  companion review is from source; `swift build` passes and the app launches.
- **Boot splash, 14/24.** Pass and fail are the same two words with different
  hues: `HFS+ DETAILS` in green `#00BF63` (2.08:1 on the paper) or red
  `#FE3131` (3.14:1). The label is already composited at runtime, so a truthful
  status word is cheap; it changes owner artwork and the recorded image hash,
  so it is **Ask first**.
- No firmware, companion or bootloader behaviour was changed in this block. The
  three reports are the deliverable; the treatment batch is proposed and not
  started, because the largest wins move navigation or owner artwork.

## Evidence (host only, no device run)

- `node detect.js` (Unslopovich detector): 3 `fixed-text-size` hits in
  `Design/Theme.swift:34,38,42`, confirmed in context. It cannot parse C, so the
  firmware UI was reviewed by hand from `firmware/ui/*.c` and the committed
  renders in `assets/ui/` and `assets/bootloader/`.
- Contrast measured, not eyeballed: firmware `MUSIC_MUTED` on `MUSIC_PAPER`
  5.00:1 (passes AA); companion `faint` 2.22:1, `muted` 4.72:1; splash green
  2.08:1, red 3.14:1.
- `swift build` in `tools/desktop`: PASS (30.9 s). The app launches; the window
  reports 1120×748. `screencapture` refused the window rect, so no screenshots.
- The recorded clipping check for the Music flow still passes (16 screens,
  2026-11-08); geometry findings are not clipping.

## Open / next

- Treatment needs an owner yes/no on: LEFT/RIGHT as tab switching, the Music
  hub as a second launcher, the `FONT FALLBACK` slot, the companion's forced
  light appearance, and any change to the boot splash (new artwork + a new
  recorded image hash).
- Safe-as-defects, no navigation change: the `EMPTY`/`NO ITEMS` empty state, the
  wall caption separator, the lens of unknown-value dashes, the OPENING footer
  losing its button hints, the visualizer hole, the raw error code, and the
  footer grammar/baseline.
- No `.unslopovich/chart.md` yet. Creating one would record the accepted
  exceptions (paper palette, 11 px caps, fixed panel, owner artwork) so the next
  review does not re-litigate them.

---

# Handoff — 2026-11-21 (nineteenth block: a write that grows a file)

## What happened

- **A real append bug, found by the new tests.** `hfs_catalog_append_file()`
  allocated fresh blocks and wrote the data there, even when the file ended
  inside its last block. A 1200-byte file that received 600 more bytes kept
  bytes 1200–1535 as zeros and put the new data at 1536, past the size the
  record reported. The append path now fills the partial last block through the
  fork map first, then allocates, and only counts the bytes it actually wrote.
  `get_be32(fork + 12)` expectations and the raw-image checks in
  `append past overflow` moved with it.
- Two supporting fixes the growth path forced: `hfs_bitmap_find()` takes the
  transaction, so a second allocation in one transaction no longer hands out a
  block the first one staged (BUSY otherwise); and `stage_volume_block()`
  composes a partial write with the block already staged in the transaction
  instead of replacing it with the committed content (a second partial write to
  the same block used to drop the first one).
- `hfs_catalog_write_file()` is the general entry point: the part of the range
  that exists is an in-place overwrite, a range past the end fills the gap with
  zeros in 4 KiB chunks (no caller buffer the size of the hole) and then
  appends. One call is bounded by the transaction budget and returns `NO_SPACE`
  when the gap exceeds it.
- Tests: `write grows file` (empty file, write at 1000), `write across the end`
  (1200-byte file, write at 1000 with 600), `write grows fragmented file` (20
  single-block extents, write inside the last block). The image test gained a
  `grow` stage on the truncated direct file — 500 bytes written 400 bytes past
  the end, with macOS verifying the zero gap and the written range — and
  `run-crash-macos.sh` a grow matrix (34 write + 4 flush + 34 partial cuts).

## Evidence (host only, no device run)

- `tests/hfsplus` test + sanitize PASS (17 catalog-write tests, 118 HFS+ total);
  `tests/image` test (…/truncate-to-size/**grow**/Unicode, `fsck_hfs` clean at
  every stage) and `crash-test` PASS (444 cuts); `tests/vfs`,
  `tests/library macos`, `tests/player`, `tests/codec`, `tests/firmware`,
  `tests/bootloader`, `tests/musicdb`, `tests/ui` PASS; the VFS, firmware and
  bootloader sanitizer builds PASS.
- Builds: plain 6G **80,004 B**; music hub 768,396 B; F2
  `VERSION=2 BOOTSTATE_STATE=trying` 80,004 B with `tools/check-f2-artifact.sh`
  PASS.

## Open / next

- The VFS is still read-only: `vfs_file_write`, `vfs_file_truncate` and a
  writable mount are the T-065 surface that now has a writer to sit on.
- `hfs_fork_overlay()` is still inline-only (sixteenth block).
- A gap larger than the transaction budget needs a caller loop; nothing wraps
  that yet in a helper.
- No device run for any of this.

---

# Handoff — 2026-11-21 (eighteenth block: a certificate chain for the release key)

## What happened

- `bootloader/certificate.c/.h` implements the D-20 chain: a 120-byte
  root-signed release-key certificate (key ID, issuer ID, inclusive
  image-version range, flags, certified key, signature) and a 140-byte
  root-signed revocation bundle, plus `helikon_key_store_verify` as the
  `helikon_signature_verify_fn` the manifest check already takes.
- Rotation is by **signed image version**, not by date. The loader has no
  trusted clock, so an expiry date could only be compared against a value an
  attacker supplies; a version range is signed, deterministic and testable. Two
  certificates may name one key ID only when their windows do not overlap, and
  the store picks the one covering the booting image.
- Fail-closed rules: unknown issuer, bad signature, unknown format version,
  empty range, undefined flag bits, a certificate that does not cover the
  version, a revoked ID, and a revocation bundle that is present but does not
  authenticate — the last one rejects every key, not just the listed ones.
- `tools/make-key-certificate.py` mints both structures offline from a PKCS8
  key (raw, hex or a C initialiser). Ed25519 is deterministic, so the committed
  test vectors can be reproduced from the throwaway seeds in the test comment.
- `tests/bootloader/test_certificate.c`: 11 tests — parse and root signature,
  version range, tampered signature/key/range bytes, bad version and flag bits,
  foreign issuer, empty root table, revocation parse/verify/tamper/non-canonical
  slot, store resolution, store version range, store rotation across two
  windows, revoked key, unauthenticated bundle, and bad arguments.

## Evidence (host only, no device run)

- `tests/bootloader` test + sanitize: PASS (42 bootloader tests, 11 of them new);
  the rest of the matrix is unchanged and green.
- No firmware image changed: the hardware loader still resolves the build-local
  key table, so the existing loader-test image and its owner-reported runs are
  untouched.

## Open / next

- Wiring the store into `bootloader/hardware/helikon_loader_hw.c` needs the
  owner's root public key and the certificate naming the first release key, plus
  a recorded loader device run. Until then T-053 is `in progress`, not
  implemented.
- The manifest already carries `flags` bit 0 for development images; the
  certificate has the same bit. The policy that a production certificate may not
  sign a development manifest is enforced by the caller today and should move
  into the store when the release build wires it.
- No device run for any of this.

---

# Handoff — 2026-11-21 (seventeenth block: a file shrinks to any size)

## What happened

- `hfs_catalog_truncate_to()` replaces the size-zero-only truncate as the real
  implementation; `hfs_catalog_truncate_file()` is now its wrapper. It keeps the
  blocks the new size needs, frees the rest — splitting the extent that
  straddles the new end — rewrites the retained inline extents and the overflow
  records from the retained prefix, and zeroes the bytes past the new end inside
  the last kept block, so a later grow cannot re-expose old content. A size
  above the current size is `ARGUMENT`: this call never grows a fork.
- The overflow rewrite clears every existing record and writes the retained tail
  back in groups of eight, which keeps it independent of how an append happened
  to group the records it found. The record's logical size, block count and
  dates are staged in the same transaction.
- Tests: `truncate to size` (1200 → 600 bytes: one block freed, the kept block
  zeroed past 88, exact readback, grow and same-size behaviour) and
  `truncate across overflow` (20 single-block extents → 9 blocks + 100 bytes:
  eight inline extents kept, the first record rewritten with two entries, the
  second cleared, the tail zeroed at block 68, then size zero returns everything
  to the bitmap). The image test gained a `truncate-size` mode on the nine-block
  direct overflow file, `run-macos.sh` a matching stage with its own byte-count
  and pattern check, and `run-crash-macos.sh` a truncate-to-size matrix: 21 write
  cuts, 4 flush cuts and 21 partial-sector cuts, every one fsck-clean.

## Evidence (host only, no device run)

- `tests/hfsplus` test + sanitize PASS (14 catalog-write tests, 115 HFS+ total);
  `tests/image` test (…/overwrite/**truncate-to-size**/Unicode, `fsck_hfs` clean
  at every stage) and `crash-test` PASS (372 cuts); `tests/vfs`,
  `tests/library macos`, `tests/player`, `tests/codec`, `tests/firmware`,
  `tests/bootloader`, `tests/musicdb`, `tests/ui` PASS; the VFS, firmware and
  bootloader sanitizer builds PASS.
- Builds: plain 6G **79,960 B**; music hub 768,396 B; F2
  `VERSION=2 BOOTSTATE_STATE=trying` 79,960 B with `tools/check-f2-artifact.sh`
  PASS.

## Open / next

- Growth is append only: there is no write at an arbitrary offset that extends
  the file with a zero-filled gap. That and a `vfs_file_truncate`/`vfs_file_write`
  surface are the remaining pieces of T-065.
- `hfs_fork_overlay()` is still inline-only (see the sixteenth block); it is the
  same map form away.
- No device run for any of this.

---

# Handoff — 2026-11-21 (sixteenth block: a fragmented file is writable in place)

## What happened

- `hfs_catalog_overwrite_file()` staged a range through the fork's eight inline
  extents only. A file that had overflowed past them — the normal state of any
  track the allocator split — refused an in-place write with `UNSUPPORTED`, so
  "rewrite this tag" worked only on small, contiguous files.
- `hfs_fork_io.c` gained `hfs_fork_stage_map()`: it walks a collected logical
  extent list, confirms the extents cover the whole range *before* staging a
  block, and only then copies each covered block. A hole is a clean refusal
  with nothing half-written. `hfs_fork_stage()` keeps its inline contract and
  now delegates to the map form.
- `hfs_catalog_write.c` gained `fork_map()`, which builds the fork's logical
  map from the catalog record read in the same transaction plus the
  extents-overflow records. The overwrite path uses it, so the data write and
  the record update stay in one transaction. The map is bounded by
  `HFS_EXTENT_COUNT + HFS_OVERFLOW_EXTENT_MAX`; beyond that the write refuses
  rather than staging a partial range.
- Tests: `overwrite across overflow` in `tests/hfsplus/test_catalog_write.c`
  overwrites across the eighth-inline/first-overflow boundary and wholly inside
  the second overflow record, then checks timestamps, size, extents and the
  untouched bytes on both sides. `map across extents` in
  `tests/hfsplus/test_fork_io.c` pins the map form, the hole refusal and the
  size bound. `tests/image/hfs_image_test.c` gained an `overwrite` mode;
  `run-macos.sh` runs it on the 80-block fragmented file after the append stage
  — two windows, one straddling the boundary — and `check-overflow.py` verifies
  both windows as macOS sees them, between two `fsck_hfs` runs.

## Evidence (host only, no device run)

- `tests/hfsplus` test + sanitize: PASS (12 catalog-write tests, 5 fork-io
  tests); `tests/image` test (create/append/rename/truncate/delete/overflow/
  **overwrite**/Unicode, `fsck_hfs` clean at every stage) and `crash-test`
  PASS; `tests/vfs`, `tests/library macos`, `tests/player`, `tests/codec`,
  `tests/firmware`, `tests/bootloader`, `tests/musicdb`, `tests/ui` PASS; the
  VFS, firmware and bootloader sanitizer builds PASS.
- Builds: plain 6G **79,912 B** (was 78,228 B: the map code plus the static map
  buffer); music hub 768,396 B; F2 `VERSION=2 BOOTSTATE_STATE=trying` 79,912 B
  with `tools/check-f2-artifact.sh` PASS. The recorded F2 candidate
  `1d6d4c92…` (76,080 B) is the 2026-11-04 artifact, not this build.

## Open / next

- `hfs_fork_overlay()` is still inline-only. It is used by the B-tree read path
  through a transaction; a B-tree fork that overflowed would read stale nodes.
  The writer grows B-tree forks by extending the last extent, so this is a
  ceiling, not a live bug — record it before any volume can grow a B-tree past
  eight extents.
- No write path grows a file except an append at EOF, and truncate only reaches
  size zero. A general write-at-offset and a sized truncate are the next pieces
  of T-065.
- No device run for any of this.

---

# Handoff — 2026-11-21 (fifteenth block: a fragmented file reads through the VFS)

## What happened

- Found and fixed a real read bug: `vfs_file_read()` handed the extents reader's
  status straight back. The reader fills the buffer but its `done`-style output
  stayed unread, so `*done` was 0 and `file->position` never moved — any caller
  reading a fragmented file (the normal case for a library on a used volume)
  would loop on the same bytes. The overflow path now sets `*done` and advances
  the position exactly like the inline path.
- New fixture `test_fs_image_make_overflow()` — the standard catalog fixture with
  `current.img` grown to two blocks, one inline extent plus one record in an
  extents-overflow B-tree (blocks 24..25), and the second block filled with
  `'B'`. The base fixture is unchanged, so every existing test still sees the
  three-record, inline-only catalog.
- `tests/vfs` gained `overflow_file_read`: read the inline half, read the
  overflow half, EOF, and a seek across the boundary. Nine VFS tests now.

## Evidence (host only, no device run)

- `tests/vfs test` + `sanitize`: PASS (9 tests).
- `tests/hfsplus`, `tests/image` (test + crash), `tests/library macos`,
  `tests/player`, `tests/codec`, `tests/firmware`, `tests/bootloader`,
  `tests/musicdb`, `tests/ui`: PASS.
- Builds unchanged: plain 6G 78,228 B; music hub 768,396 B; F2 78,228 B with the
  artifact check PASS.

## Open / next

- The firmware `hfs_file.c` adapter and the bootloader's `hfs_loader.c` still use
  the inline-only reader. A fragmented firmware image on a real volume would be
  rejected there; the loader is the next place the overflow splice is needed.
- No device run for any of this.

---

# Handoff — 2026-11-21 (fourteenth block: an eighty-extent file, and a fork that grows where it ends)

## What happened

- The truncate/delete walk no longer stops after eight overflow records. It was
  bounded by a guard of eight, so a file appended past 64 extents could be
  created and read but never truncated or deleted; the bound is now
  `HFS_OVERFLOW_EXTENT_MAX`, the HFS+ extents ceiling.
- B-tree growth allocates at the fork's end. `grow_locked()` passes the block
  right after the fork's last extent to `hfs_bitmap_alloc()`. A volume-header
  fork (catalog/extents/attributes) holds eight extents and cannot use the
  extents-overflow tree, so allocating at the lowest free block spent a slot on
  every growth; on the 192-file image the catalog fork ran out and the tree
  could no longer grow (`UNSUPPORTED`). Adjacent growth keeps the fork one
  extent and is what macOS does.
- The image test grew again: 192 one-block files (96 freed single blocks),
  eighty single-block appends, so the fork ends with eight inline extents and
  seventy-two overflow extents over nine records, 327,680 bytes, then truncate
  and delete it. This is the case that both the old guard and the old allocator
  would have refused.
- A test-side bug surfaced too: the verification called `hfs_extents_collect()`
  with a 32-extent array while the file had 72 overflow extents, so the *test*
  reported `UNSUPPORTED` and hid the real state. The array is now
  `HFS_OVERFLOW_EXTENT_MAX`.

## Evidence (host only, no device run)

- `tests/hfsplus test` + `sanitize`: PASS.
- `tests/image test` + `crash-test`: PASS; `fsck_hfs -fn` clean at every stage,
  macOS reads the 327,680 bytes with a per-block pattern, both overflow files
  gone after cleanup.
- `tests/library macos`, `vfs`, `player`, `codec`, `firmware`, `bootloader`,
  `musicdb`: PASS.
- Builds unchanged: plain 6G 78,228 B; music hub 768,396 B; F2 78,228 B with the
  artifact check PASS.

## Open / next

- The `hfs_extents_collect()` capacity is a caller contract; every caller has to
  pass `HFS_OVERFLOW_EXTENT_MAX` when a fork may be large.
- A volume-header fork still has a hard eight-extent limit. Keeping B-tree
  growth adjacent is a mitigation, not a guarantee; a defragmented/grown
  catalog on a very holey volume can still fail closed. Worth a diagnostic
  message that names the fork.
- Truncate-then-grow on the same image is still uncovered.
- D-05 cache limits, the transaction-block ceiling and the device power-cut
  matrix remain.

---

# Handoff — 2026-11-21 (thirteenth block: appends at the tail, no extent ceiling)

## What happened

- `hfs_catalog_append_file()` no longer materialises the whole extent list. It
  reads the eight inline extents from the fork, walks the overflow records to
  the last one (`find_last_overflow_record()`), and grows only that tail
  (`append_tail_extent()`): extend the last extent when contiguous, add an
  inline extent while there is room, fill the tail record up to eight extents,
  otherwise flush it and open the next record at `start + blocks`. Each append
  now stages one or two overflow records instead of every record from index 8.
- Consequence: the writer-local `APPEND_EXTENT_MAX` (32) ceiling is gone. A fork
  grows to the HFS+ extents limit, and an append to a large fragmented file no
  longer fails closed with `UNSUPPORTED`.
- The image test grew to match: 112 one-block files (56 freed single blocks),
  forty single-block appends, so the fork ends with eight inline extents and
  thirty-two overflow extents over four full records. macOS reads all 163,840
  bytes with a per-block pattern and `fsck_hfs -fn` stays clean; the direct
  delete (`Overflow Direct`) and both cleanups still pass.
- `check-overflow.py` and the mount checks take the new 163,840-byte size.

## Evidence (host only, no device run)

- `tests/hfsplus test` + `sanitize`: PASS (catalog-write 11 tests, including
  `append past overflow`).
- `tests/image test` + `crash-test`: PASS; `fsck_hfs -fn` clean at every stage.
- `tests/library macos`, `vfs`, `player`, `codec`, `firmware`, `bootloader`,
  `musicdb`: PASS.
- Builds unchanged: plain 6G 78,228 B; music hub 768,396 B; F2 78,228 B with the
  artifact check PASS.

## Open / next

- The truncate path walks overflow records in chunks of eight with a guard of
  eight records; that guard is a separate (documented) limit from the append
  path and should be revisited with the same tail walk.
- Truncate-then-grow on the same image (reuse of a cleared record) is not
  covered yet.
- D-05 cache limits, the transaction-block ceiling and the device power-cut
  matrix remain.

---

# Handoff — 2026-11-21 (twelfth block: deleting what overflows)

## What happened

- The overflow image exercise now also deletes an overflowed fork directly: the
  `overflow` stage leaves a second file (`Overflow Direct`, nine blocks: eight
  inline extents plus one overflow extent) and `overflow-cleanup` deletes it
  with no truncate in front of it. The delete frees nine blocks (asserted
  against `free_blocks`), the file is gone, and the extents tree must still end
  empty after the remaining truncate and delete.
- The earlier gap is closed: until now every cleanup truncated first, so the
  delete-on-overflow path had no coverage even though the delete code was
  changed in PR #47.
- Test-side refactor: `append_fragmented_file()` creates a file and appends one
  block per transaction, so both overflow files are built by the same helper;
  `HOLE_FILES` went from 24 to 48 (24 freed single blocks) to leave room for the
  second file.

## Evidence (host only, no device run)

- `tests/image test` and `crash-test`: PASS; `fsck_hfs -fn` clean after the
  overflow, cleanup and Unicode stages; macOS sees `Overflow Direct` at 36,864
  bytes and both overflow files gone after cleanup.
- No writer code changed in this block; the other suites are unaffected.

## Open / next

- Still open from the eleventh block: `APPEND_EXTENT_MAX` (32) caps a fork grown
  by append, D-05 cache limits, the transaction-block ceiling and the device
  power-cut matrix.
- Worth adding: an overflow file that is truncated and then grown again in the
  same image (reuse of a cleared overflow record).

---

# Handoff — 2026-11-21 (eleventh block: a file that really overflows)

## What happened

- The end-to-end overflow proof runs on a real macOS-formatted image:
  `tests/image/run-macos.sh` creates 24 one-block files, deletes every second
  one, appends twelve blocks to a new file, and requires eight inline extents in
  the freed holes plus four single-block overflow extents. The volume's empty
  extents tree takes its first record (`create_root()` on a real volume, not a
  fixture), macOS reads all 49,152 bytes back with a per-block pattern,
  `fsck_hfs -fn` is clean, and a cleanup stage truncates and deletes the file
  and requires the extents tree to be empty again.
- Three real writer bugs were found and fixed on the way:
  1. **Deletes leaked every block the file owned.** `hfs_catalog_delete_file()`
     removed the catalog and thread records but never freed the fork, so the
     bitmap kept the blocks allocated (deleting 12 files left `free_blocks`
     unchanged). A delete now truncates the fork first.
  2. **A one-block delete failed with `HFS_ERR_BUSY`.** The truncate staged the
     volume header (free count) and the counts stage collided with it: the
     bitmap used `hfs_txn_stage_replace()` while `hfs_volume_stage()` used the
     plain stage, which rejects an already-staged sector. Both now merge through
     the new `hfs_txn_read()` overlay plus `hfs_txn_stage_replace()`, which also
     stops the second updater from dropping the first updater's fields.
  3. **A freed B-tree node was not erased.** `fsck_hfs` reported
     `Unused node is not erased (node = 2)` after the deletes. `free_node()` now
     stages a zeroed node before it clears the free-node bit, at every free site.
- Two more gaps closed so the exercise can exist at all:
  `hfs_catalog_append_file()` walked only the eight inline extents and rejected
  a second overflow append with `UNSUPPORTED`; it now collects inline plus
  overflow extents (`collect_fork_extents()`, capped at 32) and rewrites the
  overflow records. `hfs_catalog_read_file()` falls back to `hfs_extents_read()`
  for an overflowed fork, so the bytes can be read back.
- `firmware/Makefile` now keys its objects on a configuration stamp that
  includes `UI`, `AUDIO`, `AUDIO_TRACK`, `HDB` and `EXTRA_CFLAGS`. The old stamp
  only tracked `BOARD`/`VERSION`, so the F2 build after a music-hub build
  silently linked the music-hub objects and `check-f2-artifact.sh` validated a
  768,396-byte UI image instead of the 78,228-byte F2 image.
- New host tests: `append past overflow` (`test_catalog_write.c`, both the
  extents and a full readback), `header updates compose` (`test_bitmap.c`), and
  the freed-node-erased assertion in `test_btree_tree.c`.

## Evidence (host only, no device run)

- `tests/hfsplus test` + `sanitize`: 17 binaries, all pass (catalog-write now
  11 tests, bitmap 7, btree-tree 2).
- `tests/image test` (overflow, cleanup, Unicode, F2) and `crash-test`: PASS,
  `fsck_hfs -fn` clean at every stage.
- `tests/library macos`, `tests/vfs`, `tests/player`, `tests/codec`,
  `tests/firmware`, `tests/bootloader`, `tests/musicdb`: PASS.
- Builds: plain 6G 78,228 B; music-hub (UI/AUDIO/HDB) 768,396 B; F2
  `VERSION=2 BOOTSTATE_STATE=trying` 78,228 B with `tools/check-f2-artifact.sh`
  PASS on the plain image.

## Open / next

- No device run for any of this. The writer's device evidence is still the
  2026-11-04 outer-volume run.
- The macOS image test never deletes a file that still has overflow extents
  without truncating it first; adding that path (delete → free overflow →
  extents tree empty) is worth a scenario.
- `APPEND_EXTENT_MAX` (32) is a hard ceiling for a fork grown by append; a
  fragmented large file will hit `UNSUPPORTED` before HFS+ does. Extent-list
  growth or a proper run-length merge is the next writer task.
- D-05 cache limits, the transaction-block ceiling and the device power-cut
  matrix remain.

## Rollback

Revert this commit; the writer returns to leaking deleted blocks, refusing a
second overflow append and leaving freed nodes unerased. No on-disk format
changed, so images written by the new code stay readable either way.

---

# Handoff — 2026-11-21 (tenth block: emptying the tree again)

## What happened

- `hfs_btree_delete()` on the last record of the root leaf now returns the tree
  to the canonical empty state instead of leaving a root leaf with zero records:
  the node goes back to the free map, depth/root/first/last/leaf_records are
  zeroed in the header, and `btree->empty` is set. A reopened handle therefore
  sees the same shape a freshly formatted volume has, and the next insert calls
  `create_root()` again.
- `tests/hfsplus/test_btree_tree.c` extends `insert into empty tree`: after two
  records, delete both, check the on-disk header is empty and the node is free,
  reopen (`empty == 1`), then insert again and check depth 1 with one record.

## Evidence

- `tests/hfsplus` test + sanitize PASS.
- `tests/image` test and crash-test PASS (delete paths), `tests/library` still
  reads the catalog from the real image.
- No device run.

## Open / next

- Still unproven end to end: fragment a disposable image, append a file that
  needs more than eight inline extents (so `hfs_extents_overflow_set()` runs
  `create_root()` on the real extents tree), then require `fsck_hfs` clean.
- A volume whose extents tree was emptied by macOS (possible depth-1 root leaf
  with zero records) is not modelled: our open would reject that shape. Open
  question until a real image shows it.
- Music hub on a mounted volume; T-058 queue/next-previous; ALAC; rest of T-099.

---

# Handoff — 2026-11-21 (ninth block: the first insert into an empty B-tree)

## What happened

- `hfs_btree_insert()` now handles an empty tree instead of returning
  `UNSUPPORTED`: `load_header()` accepts the empty shape (depth 0, no root, no
  leaves), and `create_root()` allocates one free node from the header map,
  builds a one-record leaf (kind 0xff, height 1, offset table at 14), stages it,
  then writes depth 1, root, first leaf, last leaf, one leaf record and the map
  back into the header — all inside the caller's transaction, so a failure
  leaves the empty tree untouched on disk.
- This is the path a real volume needs for its extents overflow file: it starts
  empty and could not take its first overflow record before. `firmware/hfs/
  hfs_extents_write.c` (`hfs_extents_overflow_set`) inserts through the same
  function, so appending a file that exceeds its eight inline extents now has
  the infrastructure it needs on a real volume.
- `tests/hfsplus/test_btree_tree.c` gains `insert into empty tree`: empty
  header fixture, first insert creates the root, on-disk header shows depth 1 /
  root / first / last / one record / the node marked used, the result reopens
  through the real `hfs_btree_open()` (`empty == 0`, one record iterated), and a
  second insert takes the normal root-leaf path (two records).

## Evidence

- `tests/hfsplus` test + sanitize PASS (including `insert into empty tree`).
- Full matrix green: `vfs`, `player`, `codec`, `firmware` (clean+test+sanitize),
  `bootloader`, `image` test/crash/write-diag/outer-diag/loader, `musicdb`, `ui`,
  `library` (real-volume catalog read still passes).
- Builds: plain 6G 78,036 B, music-hub 768,396 B, F2 image plus
  `check-f2-artifact.sh`: PASS.
- Host-verified only: **no device run**. Writing an overflow record through the
  writer on a fragmented real image is still unproven.

## Open / next

- End-to-end proof for the overflow path: fragment a disposable image, append a
  file that needs more than eight inline extents, and require `fsck_hfs` clean.
  That exercises `hfs_extents_overflow_set` → `create_root` → `delete`/`insert`
  on a real volume.
- Music hub on a mounted volume; T-058 queue/next-previous; ALAC; rest of T-099.

---

# Handoff — 2026-11-21 (eighth block: the catalog reads from a real volume)

## What happened

- **Real bug found and fixed:** `hfs_btree_open()` rejected an empty B-tree
  (depth 0, no root, no leaves, no records). Every freshly formatted HFS+
  volume has exactly that in its extents overflow file, so a real macOS-formatted
  6G volume would have failed `vfs_mount()` with `FORMAT` — the T-056 device half
  could never have mounted. `struct hfs_btree` gained `empty`; open accepts the
  empty shape (still validating node size, fork size, node/free counts and key
  length), `hfs_btree_read_node()` returns `NOT_FOUND`, `hfs_btree_find`/
  `delete`/`update` return `NOT_FOUND`, `insert` returns `UNSUPPORTED` (a
  create-root path is T-033), and `catalog_seek_parent()` returns `NOT_FOUND`.
- `firmware/db/hdb_vfs.{c,h}`: adapts one open VFS file to `struct hdb_io`
  (stat for size, seek + short-read loop, no allocation), so the HDB reader
  serves an embedded blob or `/.helikon/music.hdb` inside the volume.
- `tests/library/`: new host suite. `test_library.c` mounts an image read-only,
  checks the journal is clean, stats and opens `/.helikon/music.hdb`, runs
  `hdb_open`, reads track 0 / album 0 / the title index, and verifies the
  whole-file CRC through the VFS; `run-library-macos.sh` creates a disposable
  16 MiB journaled HFS+ image with `hdiutil`, writes the fixture catalog, unmounts,
  checks `fsck_hfs -fn` (clean, no repair) and runs the test.
- `tests/hfsplus/test_btree.c` gained `empty btree`: an empty header opens, reads
  refuse with `NOT_FOUND`, iteration ends immediately, and an empty-looking
  header with a stray root is still `FORMAT`.

## Evidence

- `make -C tests/library macos`: PASS — `HFS+ -> VFS -> HDB (6 tracks, 3 albums,
  CRC OK)`, fsck clean, journal clean after unmount.
- `tests/hfsplus` (test + sanitize) PASS with the new case. Full matrix green:
  `vfs`, `player`, `codec`, `firmware` (clean+test+sanitize), `bootloader`,
  `image` test/crash/write-diag/outer-diag/loader, `musicdb`, `ui`.
- Builds: plain 6G 77,992 B (hfs core grew), music-hub 768,396 B, F2 image plus
  `check-f2-artifact.sh`: PASS.
- Host-verified only: **no device run**. The test runs against a disposable
  image, never an iPod.

## Open / next

- The Music hub still opens the embedded catalog; switching it to
  `hdb_vfs_open` on a mounted volume is the next T-056/T-067 step and needs the
  boot-time mount, which is device-facing.
- Creating a root leaf when inserting into an empty B-tree (T-033) so the
  extents overflow file can actually gain its first record.
- T-058 queue/next-previous and call sites; ALAC; rest of T-099.

---

# Handoff — 2026-11-21 (seventh block: FLAC Vorbis comments, T-099 slice)

## What happened

- `firmware/codec/tag/vorbis.{c,h}`: bounded FLAC `VORBIS_COMMENT` reader.
  `helikon_vorbis_comment_parse()` takes a block body;
  `helikon_flac_tags_buffer()` walks a resident `fLaC` metadata chain;
  `helikon_flac_tags_source()` walks the same chain over `read_at`, needs a
  reported size, and reads at most 4 KiB of the comment body (a bigger block is
  parsed prefix-wise, keeping complete comments).
- Fields: title, artist, album, album artist, genre, year (first four digits of
  DATE), track and disc (leading number of `3/12`). First value wins, values are
  UTF-8 validated and truncated on a code-point boundary at 96 bytes, an invalid
  sequence drops the value, and a missing `=` or a length past the block is
  `FORMAT`. No allocation; the source path uses one static 4 KiB block buffer
  (single-threaded, 2 KiB codec stack budget).
- New committed fixture `tests/codec/fixtures/tagged16.flac` = `stereo16.flac`
  plus eight `metaflac` comments; `make_fixtures.py` recreates it, and
  `tests/codec/README.md` documents it.
- `firmware/Makefile` builds `codec/tag/vorbis.c` in the AUDIO set. Nothing calls
  it yet, so `--gc-sections` drops it from the image (same as the codec layer
  before the hub called it).

## Evidence

- `tests/codec` test + sanitize PASS with `check_vorbis_tags()`: synthetic
  `fLaC` + STREAMINFO + comment block (8 tags incl. a 2-byte UTF-8 value and
  `7/12`/`2/2` pairs), first-value-wins, invalid UTF-8 dropped, missing `=`,
  count past the block, no comment block, non-FLAC buffer, the same bytes
  through a 61-byte source, a source with no size, null arguments; plus the real
  `tagged16.flac` from a buffer and a 173-byte source and the untagged
  `stereo16.flac` (empty block → OK, empty fields).
- Rest of the matrix unchanged and green (`player`, `vfs`, `hfsplus`,
  `firmware`, `bootloader`, `image` test/crash/write-diag/outer-diag/loader,
  `musicdb`, `ui`).
- Builds: plain 6G 77,856 B; music-hub 768,396 B (vorbis.o collected); F2 image
  plus `check-f2-artifact.sh`: PASS.
- Host-verified only: **no device run**, no library-index caller.

## Open / next

- T-067 library index should call the ID3v2 and Vorbis readers and own the track
  record; only then do these modules earn image bytes.
- Remaining T-099: Ogg Vorbis comments, APE, MP4/ASF tags, multi-value fields,
  artwork, ReplayGain, cuesheets, chapters.
- T-058 queue/next-previous, read-ahead policy, Music-hub call sites.

---

# Handoff — 2026-11-21 (sixth block: player seek, T-058)

## What happened

- `helikon_player_seek_ms()` (`firmware/player/{player.c,player.h}`): target is
  `ms × rate / 1000` frames, clamped to the last frame; a zero-length stream is
  `UNSUPPORTED`. Compressed audio seeks the codec first (a refused seek leaves
  playback untouched), then the PCM stream is stopped (mute + ring drop) and
  restarted with the remaining frames. The resident WAV path restarts at the
  matching byte offset.
- New `start_frame` field: `position_ms()` reports `start_frame + frames played
  in this segment`, `play_chunk()` keeps the byte offset coherent, and both
  opens/`stop` reset it. `play_chunk`'s start branch moved into a shared
  `player_start_stream()` (start frame, remaining frames), used by open-seek and
  restart.
- Semantics: a seek that lands where playback already is does nothing; a paused
  player is re-paused after the restart; a seek before the first chunk only
  moves the start point, so open → seek → play works.

## Evidence

- `tests/player` test + sanitize PASS with `check_seek_player()`: resident WAV
  (50 ms → 2205, `5000 − 2205` frames, buffer starts at sample 2205/2206, same
  seek is a no-op, past-end clamps to one frame), media WAV with 37-byte reads
  (seek before start starts nothing, then `5000 − 1102`, data `(1102, 1103)…`,
  pause preserved across a seek), FLAC (20 ms → `2048 − 882`, ten frames
  identical to the whole-file decode at frame 882), MP3 (10 ms →
  `total_frames − 441` on a LAME-trimmed file), closed player → `ARGUMENT`.
- Full matrix green (`codec`, `vfs`, `hfsplus`, `firmware`, `bootloader`,
  `image` test/crash/write-diag/outer-diag/loader, `musicdb`, `ui`, `player`).
- Cross-builds: plain 6G 77,856 B; music-hub 768,396 B; F2 image plus
  `check-f2-artifact.sh`: PASS.
- Host-verified only: **no device run**, no Music-hub caller.

## Open / next

- Next/previous needs a queue (open the following source, keep the ring fed
  across the boundary); that is also the road to cross-track gapless.
- Read-ahead policy, Music-hub call sites, I2S/DMA run, device evidence.
- ALAC; rest of T-099 metadata.

---

# Handoff — 2026-11-21 (fifth block: the player opens a pull source, T-058)

## What happened

- `helikon_codec_probe_source()` (`firmware/codec/codec.c`) resolves a
  descriptor from a `read_at` source: one shared 4 KiB head, registry walk,
  `NOT_FOUND` skipped, `NEED_MORE` accepted only from a `streaming` descriptor
  (info zeroed, open resolves it), `UNSUPPORTED` when nothing claims the head.
  Static head buffer, so not reentrant; the firmware is single-threaded.
- `helikon_player_open_media()` (`firmware/player/player.c`) is the media
  counterpart of `helikon_player_open()`: probe, open into the embedded codec
  workspace, then take the player's `wav` view from `codec.info` and mark the
  player compressed. HAL bring-up moved into a shared `player_start_audio()`
  used by both opens, so a HAL failure closes the codec on either path.
- WAV stereo reads now loop on short answers until the request is met or the
  source returns nothing, using absolute offsets: a chunked adapter can no
  longer truncate a chunk or lose frame alignment. Mono already looped. A
  trailing partial frame is re-requested, so no carry buffer is needed.
- `tests/player` gained a `sanitize` target with the same flags as the codec
  suite.

## Evidence

- `tests/player` test + sanitize: PASS. New `check_media_player()` drives a
  5000-frame stereo WAV through a 37-byte-chunk media (open, `duration_ms ==
  113`, 5000-frame stream start, 100 frames matching the fixture pattern), then
  the FLAC fixture through the same media (44.1 kHz stereo, 46 ms, 2048-frame
  start), then `"HELO"` → `UNSUPPORTED` and a NULL player → `ARGUMENT`.
- Matrix: `tests/codec` (test + sanitize), `tests/vfs`, `tests/hfsplus`,
  `tests/firmware` (clean, test, sanitize), `tests/bootloader`, `tests/image`
  (test, crash, write-diag, outer-diag, loader), `tests/musicdb`, `tests/ui`,
  `tests/player` all pass.
- Cross-builds: plain 6G 77,856 B; music-hub config with the MP3 fixture
  768,280 B; `VERSION=2 BOOTSTATE_STATE=trying` image plus
  `check-f2-artifact.sh`: PASS.
- Host-verified only: **no device run**, no Music-hub caller.

## Open / next

- Read-ahead ring and refill policy, then the Music-hub call sites that turn a
  selected track into a VFS handle plus `vfs_media` (pause/resume/seek through
  that path at hub level).
- ALAC; rest of T-099 metadata; cross-track gapless; T-100/T-101 device work.

---

# Handoff — 2026-11-21 (fourth block: MP3 gapless trim)

## What happened

- The Xing parser reads the LAME/FFmpeg extension (quality field, then the
  36-byte tag; delay/padding at bytes 21-23, 12 bits each). `LAME`, `Lavc` and
  `Lavf` strings are accepted.
- `struct helikon_codec_info` gained `encoder_delay` and `padding` (output
  frames) and `HELIKON_CODEC_FLAG_GAPLESS`; the MP3 probe **and** open fill
  them, so a caller can plan the trim before opening.
- The MP3 read loop drops the encoder delay from the first decoded block and
  reports end of stream at `frames × frame_samples - delay - padding`; a seek
  clears the trim and rebases the emitted counter. Files without a LAME tag
  play untrimmed and do not claim the flag.
- `tests/ui` needed `codec/tag/id3v2.c` in its audio link too (the MP3 wrapper
  references the tag-size helper since #38).

## Evidence

- `tests/codec` test + sanitize: PASS. New gapless cases on both MP3 fixtures:
  `encoder_delay == 576`, `padding > 0`, GAPLESS flag, decoded length exactly
  `total - delay - padding`, and the same length after `seek(0)`.
- Matrix: `tests/vfs`, `tests/hfsplus`, `tests/firmware`, `tests/bootloader`,
  `tests/musicdb`, `tests/player`, `tests/ui`, `tests/image` all pass.
- Cross-builds: plain 6G 77,856 B; music-hub 768,156 B.
- Host-verified only: **no device run**, no player wiring.

## Open / next

- Cross-track gaplessness (queue keeps the ring fed across a file boundary),
  resume, MPEG-2/2.5 delay values, T-100 device run.
- T-058 read-ahead + ring; ALAC; rest of T-099 metadata.

---

# Handoff — 2026-11-21 (third block: ID3v2 tag reader)

## What happened

- `firmware/codec/tag/id3v2.{c,h}`: bounded ID3v2.2/v2.3/v2.4 **text** tag
  reader. `helikon_id3v2_tag_size()` owns the header arithmetic (10-byte
  header, sync-safe size, optional footer); `helikon_id3v2_parse()` fills a
  fixed UTF-8 struct (title, artist, album, album artist, genre, year, track,
  disc, version) with no allocation. Encodings: Latin-1, UTF-16 with/without
  BOM (surrogate pairs combined, unpaired skipped), UTF-8 (bounded, truncated
  sequences dropped). Errors: `NOT_FOUND` (no tag), `FORMAT` (frame overruns
  the tag), `UNSUPPORTED` (unsync/compression/encryption/grouping).
- The MP3 wrapper now uses `helikon_id3v2_tag_size()` to skip the tag instead
  of carrying its own copy of the sync-safe arithmetic.
- `firmware/Makefile` builds `codec/tag/id3v2.c` in the AUDIO player set;
  `tests/codec`, `tests/vfs` and `tests/player` link it.

## Evidence

- `tests/codec` test + sanitize: PASS, with new cases — v2.3 Latin-1 with a
  `3/12` pair, v2.4 sync-safe frames with UTF-8 and UTF-16LE+BOM, tag size,
  missing tag, unsynchronized tag, frame overrun.
- `tests/vfs`, `tests/player`, `tests/hfsplus`, `tests/firmware`,
  `tests/bootloader`, `tests/musicdb`: pass.
- Cross-build: music-hub 767,684 B with the tag reader linked.
- Host-verified only: **no device run**. No Music-hub caller yet.

## Open / next

- T-099 remainder: APE, Vorbis comments, MP4/ASF, artwork, ReplayGain,
  chapters, multi-value fields; then wire tags into the library/index and the
  Music hub.
- T-058 read-ahead + ring wiring; gapless; ALAC/AAC.

---

# Handoff — 2026-11-21 (second block: FLAC and MP3 stream, VFS feeds the codec)

## What happened

- **FLAC is a pull decoder.** `firmware/codec/flac/flac_decoder.{c,h}` decodes
  through `struct flac_reader` (resident buffer or source + 8 KiB window). The
  bit reader fetches by absolute offset and refills on a miss; metadata goes
  through the same reader, so a head ending inside a large metadata block is
  `NEED_MORE` and `open` resolves it. New `helikon_flac_open_source()`;
  `helikon_flac_open()` unchanged. Descriptor `streaming = 1`, workspace
  73,832 B.
- **MP3 refills around libmad's boundaries.** 32 KiB window, refilled from
  `stream.next_frame`; the reservoir is libmad's own copy, so a refill never
  invalidates it. ID3v2 is skipped by reading the 10-byte header and sync-safe
  size, then the window is filled at the first frame; the guard-for-final-frame
  trick now pads the window once instead of using a separate tail buffer;
  refills loop until the window is full so short-read media still expose the
  Xing frame. Workspace 62,856 B.
- **VFS feeds the codec.** `firmware/player/vfs_media.{c,h}` binds an open
  `vfs_file_handle` plus its stat size to a `struct helikon_codec_source`;
  `read_at` is an absolute seek + read, EOF is a clean zero-byte read.
  `tests/vfs` proves the composition on the HFS+ fixture (arbitrary-order
  absolute reads, EOF, null context, registry refusal of a non-audio file).
- `helikon_codec_open_source()` with an explicit descriptor that does not
  recognize the head now returns `UNSUPPORTED` instead of leaking `NOT_FOUND`.
- `tests/libmad.mk` no longer defines directory targets: the first target of an
  included makefile became the includer's default goal and silently made
  `make -C tests/vfs` a no-op.

## Evidence

- `tests/codec` test + sanitize: PASS (FLAC and MP3 source decodes byte-equal to
  the resident-buffer decode; 37/61-byte short reads; unknown media length;
  bounded requests; FLAC source seek).
- `tests/vfs` test + sanitize: PASS including the new media test.
- Matrix: `tests/hfsplus`, `tests/firmware`, `tests/bootloader`, `tests/image`
  (test, write-diag, outer-diag, loader-test), `tests/musicdb`, `tests/player`,
  `tests/ui`: all pass.
- Cross-builds: plain 6G 77,856 B; music-hub 767,624 B with 52 MP3/FLAC symbols;
  `VERSION=2 trying` image + `check-f2-artifact.sh`: PASS.
- Host-verified only: **no device run**, no support claim.

## Open / next

- T-058: the read-ahead policy, the music-hub call sites for `vfs_media`, and a
  real I2S/DMA run from a storage-backed file. The seam exists; nothing plays
  from the library yet.
- Gapless bookkeeping, resume, MPEG-2/2.5 corpus coverage, ARMv5E synth.

---


# Handoff — 2026-11-21 (streaming source model, WAV converted)

## What happened

- **The codec layer takes a pull source.** `struct helikon_codec_source`
  (`firmware/codec/codec.h`) is an absolute `read_at(context, offset, buffer,
  length, *read)` plus a total `size` (0 = unknown). `helikon_codec_open_source()`
  opens over one; `helikon_codec_memory_span()` reports whether the codec still
  holds the whole stream contiguously. `helikon_codec_open()` is unchanged for
  every existing caller — it wraps the caller's buffer in a memory source whose
  context is the codec itself (never a stack frame).
- **Descriptor `streaming` flag + head-only probes.** `ops->probe` now takes
  `(data, size, total_size)`: `data/size` is a head of at most
  `HELIKON_CODEC_PROBE_BYTES` (4096) and `total_size` is the true length or 0.
  A probe that cannot decide returns the new `HELIKON_ERR_NEED_MORE`
  (`firmware/lib/helikon_error.h`, `-19`, with a `failure_reason` string); the registry
  accepts that only from a `streaming` descriptor, lets `open` resolve the head,
  and treats `codec->info` as authoritative afterwards. `helikon_codec_probe()`
  always sees the whole buffer and never returns `NEED_MORE`.
- **WAV converted.** `firmware/player/wav.{c,h}` is now one RIFF chunk walker
  over two backends; `helikon_wav_parse()` keeps its signature,
  `helikon_wav_parse_prefix()` parses a head shorter than the stream and
  `helikon_wav_parse_source()` walks a source. PCM is read on demand at
  `pcm_offset + frame × frame_bytes`; mono duplicates through a 256-frame stack
  buffer; short reads yield whole frames only. WAV decodes without ever holding
  the payload.
- **FLAC and MP3 refuse a pull source** (`HELIKON_ERR_UNSUPPORTED`) instead of
  reading a header prefix as if it were the file; both carry `streaming = 0`.
- **Two broken suites repaired.** `tests/player` and `tests/ui` had failed to
  link since the MP3 merge (both build `codec.c`, whose registry references
  `helikon_mp3_codec_ops`; neither linked libmad). The object rules now live in
  `tests/libmad.mk`, included by `tests/codec`, `tests/player` and `tests/ui`.
  Neither suite appeared in the T-097 verification list — that is why it slipped.
- **Also fixed**: `codec.h` had its extension/registry prototypes outside the
  include guard (an `#endif` sat mid-file) since T-097.

## Evidence

- `make -C tests/codec test` + `sanitize`: PASS, including new source tests —
  bounded requests (one chunk requests exactly `chunk × 4` bytes), 37-byte short
  reads, unknown media size, a `JUNK` chunk that pushes `data` past the probe
  prefix (`NEED_MORE` → resolved in `open`), and the refusals/flag checks.
- Regression matrix: `tests/hfsplus` (+ sanitize), `tests/vfs` (+ sanitize),
  `tests/firmware` (test + sanitize after `clean`), `tests/bootloader`
  (+ sanitize), `tests/image` (test, write-diag, outer-diag, loader-test),
  `tests/musicdb`, `tests/player`, `tests/ui`: all pass.
- Cross-builds: plain 6G (77,856 B), music-hub config with the MP3 fixture
  (765,392 B raw, MP3 and `helikon_wav_parse_source` symbols linked),
  `VERSION=2` `BOOTSTATE_STATE=trying` image + `check-f2-artifact.sh`: PASS.
  `helikon_codec_open_source` is GC'd out of the image until T-058 calls it.
- Host-verified only: **no device run**, no support claim.

## Open / next

- FLAC streaming next, then MP3 (libmad keeps `main_data` boundaries into the
  buffer, so a refill must preserve the unconsumed tail; the tail bounce buffer
  from the MP3 work is the starting point).
- T-058 wiring (VFS → `read_at` → codec → ring) is the next milestone after
  that; the VFS half exists.
- T-056 device half, T-100/T-101 device gates, ALAC/AAC unchanged.

---

# Handoff — 2026-11-17/18 (T-097 descriptor extensions + MP3/libmad port)

## What happened

- **T-097 framework extensions landed.** Codec descriptors now declare
  `extensions` (NULL-terminated lowercase list, case-insensitive match after
  the final dot; a path with no dot never matches), a descriptor-level
  `capabilities` mask (`SEEK|GAPLESS|RESUME`) alongside the per-file
  `HELIKON_CODEC_FLAG_SEEK`, and `stack_bytes`, `min_clock_hz`,
  `recommended_clock_hz`. New `helikon_codec_matches_extension()` and
  `helikon_codec_registry()`. FLAC descriptor updated; WAV/FLAC/MP3 registry
  order kept. `helikon_codec_probe` now refuses zero-size buffers with
  `HELIKON_ERR_ARGUMENT`.
- **MP3 decoder ported** (`firmware/codec/mp3/` wrapper + vendored
  `firmware/codec/libmad/`): libmad 0.15.1b from the pinned Rockbox mirror
  commit `97986f2cdaec1585856d…` (no ARM/Coldfire asm, C paths
  `FPM_DEFAULT`), Helikon port shim `libmad_port.h`, whole-buffer wrapper
  with ID3v2 skip, full-header probe, Xing/Info frames+TOC, fixed→s16 clip
  output, mono→stereo duplication, 64-recoverable-error bound, CBR/Xing TOC
  seek. Measured workspace 30,976 B (`_Static_assert` inside
  `HELIKON_CODEC_WORKSPACE_MAX`), declared stack 8192 B, clocks 54/216 MHz.
- **Deep-dive fixes debugging the decode path** (worth knowing for the next
  codec port):
  - libmad `frame.sbsample` and `frame.sbsample_prev` must point at the
    SAME buffer (Rockbox single-core wiring): layer code writes `sbsample`,
    the Rockbox-modified `synth.c` reads `sbsample_prev`. Wired after init
    or to two arrays, the synth produced all-zero PCM (or worse).
  - After a seek, `mad_synth_mute` does NOT clear `synth.pcm.length`; the
    wrapper clears it to avoid replaying pre-seek samples.
  - After a seek, libmad's `main_data` chain is empty: every frame whose
    `main_data_begin` exceeds the local chained data fails with
    `BADDATAPTR` (recoverable) and produces no PCM until the chain rebuilds.
    Short fixtures can yield zero emitted frames after a mid-file seek; the
    VBR fixture was enlarged (8×1152 samples) so decode visibly resumes.
  - libmad's last frame needs `N + MAD_BUFFER_GUARD` bytes in the stream;
    the wrapper re-buffers the tail through a workspace bounce buffer once.
  - The seek re-aligner only accepts headers of the file's own MPEG
    version/layer: main-data bytes carry plausible-looking foreign-layer
    headers that made libmad jump past real frames.
  - MP3 probe corroborates a candidate header with its declared successor
    position (`offset + frame_bytes` must start another same-version/layer
    header); a valid-looking header inside a corrupt-magic FLAC stream would
    otherwise claim MP3.
- **Build confusions resolved.** `tests/codec/Makefile` now defines
  `HELIKON_HOST_TEST` like every other suite; without it the freestanding
  `memset`/`memcpy` shims in `firmware/lib/mem.c` shadow libc and clang's
  idiom recognition turned a bounded fill loop into infinite recursion
  (261k frames, stack-guard SIGSEGV). The repo-local
  `firmware/lib/string.h` shadows `<string.h>` on the test include path, so
  host tests call the `helikon_*` mem names (documented convention).
  `firmware/Makefile` gained the libmad object rule: `-DNDEBUG` plus
  conversion suppressions, with GCC-per-target extras
  (`-Wno-implicit-fallthrough -Wno-shadow`; GCC 9 does not know clang's
  `-Wno-implicit-int-conversion`). `libmad_port.h` sets `HAVE_LIMITS_H`
  only under hosted builds and defines `INT_MAX`/`UINT_MAX` for the
  freestanding target.
- **FLAC UBSan fix.** `flac_decoder.c` mid-side doubling used `(mid << 1)`
  on a negative value; now `(mid * 2)` — UBSan clean for Helikon code. One
  upstream note remains: `synth.c:1155` `D[0] - p` pointer arithmetic
  (documented in `firmware/codec/libmad/README.md`, accesses in bounds).
- **Fixtures.** `tests/codec/make_mp3_fixtures.py` (lame-based) generates the
  four committed MP3 fixtures (CBR stereo, VBR stereo with Xing TOC, mono,
  ID3v2). `.gitignore` gained `tests/codec` build outputs and
  `tests/vfs/test_vfs` artifacts.

## Evidence

- `make -C tests/codec test` + `sanitize`: PASS.
- Regressions: `tests/hfsplus` (107 checks) + sanitize, `tests/vfs`,
  `tests/firmware` (test + sanitize after `clean`), `tests/bootloader`
  (test + sanitize), `tests/image` incl. write-diag/outer-diag/loader,
  `tests/musicdb`: all pass.
- Cross-builds: `make -C firmware BOARD=6g` (plain), full music-hub config
  (`UI=1 HDB=… AUDIO=<mp3> EXTRA_CFLAGS=-DHELIKON_DIAG_LAUNCHER`,
  763,276 B raw image with the MP3 path linked), `VERSION=2`
  `BOOTSTATE_STATE=trying image` + `check-f2-artifact.sh`: PASS.
- Host-verified only: **no device run**; no support claim.

## Open / next

- T-056 device half (storage-backed Music-hub on the 6G) remains the open
  device work; the MP3 path links but nothing storage-backed plays it.
- MP3 codec remaining: streaming source model, gapless (LAME
  delay/padding), resume, half-rate, ARMv5E synth accelerator, MPEG-2/2.5
  and Layer I/II execution-path coverage (`audio-formats.md` §13.5).
- T-098/T-100/T-101 device gates unchanged.

---

# Handoff — 2026-11-17 (T-056 read-only VFS, host part)

## What happened

- **T-056 host part implemented.** `firmware/vfs/` (`vfs.h`/`vfs.c`) is a
  read-only VFS over the HFS+ read core: `vfs_mount` (read-only probe,
  dirty journal recorded, never replayed), `vfs_stat`, `vfs_file_open/read/
  seek/position/close`, `vfs_dir_open/next/close` and a READ_ONLY stub for
  writes. Handles are `{generation, slot}` values; slot reuse mintes a new
  generation, so stale/aliased handles refuse with `HELIKON_ERR_ARGUMENT`.
  Paths are exact UTF-8 with the read core's existing traversal rules.
- **Extents handle is lazy.** A volume whose extents file has zero blocks
  mounts without opening the extents B-tree; fully-inline forks read through
  `hfs_fork_read` and never touch it. Overflow forks open it lazily on first
  read using the caller-provided extent scratch; a failure is sticky and
  reads fail closed with `HELIKON_ERR_FORMAT`. `vfs_mount` still fails fast
  when a real (non-empty) extents file is malformed.
- **Tests.** New `tests/vfs/` suite (8 tests over the shared generated HFS+
  fixture): argument checks, mount+stat, path validation matrix, NOT_HFS,
  open/read/EOF, seek SET/CUR/END with bounded past-size seek, handle
  generation/close-stale, directory semantics (`NOT_DIR`/`IS_DIR`, entries,
  close-stale) and the write stub. `make -C tests/vfs test` and `sanitize`
  both pass. Regressions: `tests/hfsplus`, `tests/firmware` (after
  `clean` — stale sanitizer objects had broken the plain link) and
  `tests/bootloader` unaffected. `firmware/vfs` is built into the 6G image;
  cross-build passes, raw image 77,812 bytes with ~8.3 MB headroom.
- **Docs.** `testing.md` records the `tests/vfs` commands; `file-map.md`
  adds `firmware/vfs/` and `tests/vfs/` to the current tree and updates the
  vfs row; `tasks.md` T-056 is `in progress` with evidence; `state.md`
  runtime-tree row notes the F3 start.

## What is not done / next

- T-056 device half: browse a real library on the 6G through the VFS,
  storage-backed Music-hub wiring, UI responsiveness on a large directory.
- No device run for the VFS; nothing here is device-validated.
- Candidate next coding-spree items (device-free): storage-backed music
  browse wiring over the VFS plus HDB, Rockbox codec framework port start
  (MP3/libmad), `docs/hfsplus-write-engine.md` D-05 cache-limit work.

## Commands run

- `make -C tests/vfs test`, `make -C tests/vfs sanitize`
- `make -C tests/hfsplus test`
- `make -C tests/firmware clean && make -C tests/firmware test`, then
  `make -C tests/firmware sanitize` and a final `clean`
- `make -C firmware BOARD=6g` + `size` (pinned toolchain)

---

## What happened

- **Branch cleanup, owner-ordered.** Every topic branch is merged or rescued and
  deleted; only `main` exists locally and on GitHub, matching the owner's order.
  All merges were squash merges with no history rewrite. Rescued into `main`
  via the PRs: #27 (T-109 LBA48/APM probe code), #28 (T-110 VIC/IRQ entry path),
  #29 (T-112 loader reads with the live ATA sector size; union-resolved the
  "accept uninitialized journal" change in `hfs_volume` with the dirty-volume
  read-only guard and test), #21 (cleaned Music flow / Now Playing), and #30
  (the F2 ATA SRST + iFlash power-cycle + early-ATA + `HLO1` handoff-marker
  chain, rescued from a local-only branch). Worktrees removed; a pre-cleanup
  bundle is at `Helikon-OS-pre-cleanup-2026-11-05.bundle`.
- **Decision batch.** The owner accepted all remaining proposed decisions;
  PR #31 records D-05, D-07–D-12, D-15, D-16 and D-29–D-52 as accepted
  (2026-11-17). Still open: D-08 (D-44 records the accepted WASM-first
  direction), D-13, D-14, D-47; D-17 name clearance; the Benguiat
  font-embedding licence gate (D-52) and the AAC patent decision (inside
  accepted D-49) remain open legal decisions.
- **Codec research committed.** [`audio-formats.md`](audio-formats.md) §12:
  Rockbox codec-framework internals, licence verification (libmad GPL-2+,
  libalac MIT, libm4a MIT+GPL-2+, libfaad 2005-02-02 snapshot GPL-2+ pinned,
  ffmpeg helpers LGPL-2.1+), framework contracts to adopt, mechanisms to
  replace, size accounting and the port order MP3 → ALAC → AAC-LC.
- **Workspace rule (P0.1).** `HELIKON_CODEC_WORKSPACE_MAX` stays 128 KiB for
  the MP3/ALAC ports; every descriptor carries measured `workspace_bytes`/
  `stack_bytes`; an AAC cap raise happens only after measurement, inside the
  512 KiB `CODEC_SIZE` envelope.
- Docs corrected to existing reality: FLAC and the bounded codec framework
  shipped in PR #19/e9f6ad6 with the recorded 2026-11-08 device run; stale
  "no decoder exists yet" lines fixed in `audio-formats.md` and `state.md`.

## What is next

1. T-109 device run on the 6G/iFlash SD (probe image; record IDENTIFY/APM/
   geometry values in `device-matrix.md`) — the code is merged, the run is not.
2. Spree order from accepted decisions: T-056 VFS keystone → T-097 codec
   descriptor extensions and framework prep → MP3 (libmad) → ALAC
   (libalac + libm4a) → AAC-LC behind the D-49 patent gate.
3. F2/T-055 bug remains the P0 blocker; PR #30 changed the ATA path but did not
   unblock it. The ~20 s Artist-open stall from PR #21 needs a device retest.
4. Housekeeping: `handoff.md` is 3,696 lines; move the older entries to
   `docs/log/` next session.

## Do not claim

- No support claim for anything: F2 write gate, Music flow post-merge, Disk
  Mode, LBA48, >128 GiB, MP3/ALAC/AAC playback all remain unproven or unfinished.
- No new device runs happened in this session; nothing in `device-matrix.md`
  changed.
- The decisions batch does not license device work ahead of the writer gate.

---

# Handoff — 2026-11-11 (Helikon Disk Mode plan and D-54)

## What happened

- Owner accepted D-54: a separate, explicit, exclusive Helikon Disk Mode may
  expose a writable USB LUN; normal firmware USB stays read-only.
- Added [ADR-0005](adr/0005-helikon-disk-mode.md) and the canonical
  [`disk-mode.md`](disk-mode.md) plan: data-volume LUN default, whole-disk expert
  mode, BOT/SCSI stack, LBA48, capacity migration and the test matrix.
- Added T-108–T-115 to [`tasks.md`](tasks.md); T-108 is this docs task and T-109
  is the first runtime step (read-only ATA/LBA48/APM probe).
- Updated architecture, firmware-architecture, invariants (I-16), security,
  release, failures, glossary, FAQ, competitive roadmap, companion research,
  file map, state, roadmap, device matrix and the build prompt.
- No USB stack, LUN, LBA48 write, Disk Mode device run or >128 GiB volume exists.

## What is next

1. T-109: build and run the read-only ATA/LBA48/APM probe on the 6G/iFlash SD;
   record IDENTIFY 83/86/100–103/60–61/106/117–118 and the real partition map.
2. T-108: host evidence collector; T-110: USB device-core research; no writable LUN.
3. Do not start T-112 until T-109/T-111 pass and a verified backup exists.

## Do not claim

- That Helikon Disk Mode, a writable LUN, LBA48 or >128 GiB exists.
- That F2/T-055 is unblocked by this line.
- That a host write is transactional firmware I/O; it is host-owned Disk Mode
  I/O and must be tested with `fsck_hfs`.

---

# Handoff — 2026-11-08 (codec framework, FLAC and Music-flow device runs)

## What happened

- PR #17 (four-buffer WAV ring fix) merged as `2420c64` after the owner's 6G run
  on image `18f5fe39…` reported clean audio with working pause/resume.
- PR #20 (binding Music-flow and Now Playing design in `docs/ui.md` §6) merged
  as `5fa0510`; PR #18 was closed as superseded.
- PR #19 (codec registry and native FLAC decoder) merged as `e9f6ad6` after the
  owner's 6G run on image `6a2328ae…`: FLAC audio clean, pause/resume works.
  The branch was rebuilt after merging `main` `5fa0510` and produced the
  identical SHA-256, so the tested image matches the merged tree. Host decode is
  bit-exact over 879,952 frames with a 65,608-byte workspace.
- PR #21 (cleaned Music flow and Now Playing UI) is open and device-blocked.
  On image `5292c8ed…` the device stalls for almost 20 s when an artist is
  selected; repeated presses are then processed and eventually start a song.
  Do not merge it.
- Host suites pass on both branches: codec, player, UI, firmware and image
  checks.

## What is next

**Test packaging (owner rule, 2026-11-09):** one combined image per device test.
Do not ask the owner to test separate images for separate features; split only
to isolate a specific failure.

1. Diagnose the Artist-open stall on `firmware/T-088-music-flow` (blocking work
   before the first frame and queued input reaching the player), fix it, rebuild
   and device-test the fixed image before merge.
2. Build the combined FLAC + UI image only after PR #21 merges.
3. Start MP3 on a fresh branch after the FLAC tranche; ALAC and AAC remain
   behind the provenance/licence review.
4. Attach the 2026-11-08 screen photos and `fsck_hfs` output when available.

## Do not claim

- That PR #21, the cleaned Music flow or the new Now Playing screen is
  device-proven.
- That the UI meets the T-088 page-open or 30 fps target.
- That 24-bit output, >48 kHz, seek timing, storage/VFS playback, next/previous,
  gapless, MP3, ALAC or AAC exist.
- That the 2026-11-08 runs are a support matrix; one 6G/iFlash SD only.

---

# Handoff — 2026-11-06 (Alpha 2.0.0 release)

## What happened

- Published Alpha 2.0.0 “Launchers and Interfaces” as a normal GitHub release
  at tag `alpha-2.0.0`, from a clean `release/alpha-2.0.0` branch based on
  `main` at `f5f8bfb`.
- **Correction:** T-055/F2 was removed from the Alpha 2.0.0 release. The write
  gate is unfinished and unconfirmed due to a currently unfixable bug. The
  published release body, release notes, quick start and `SHA256SUMS` were
  corrected, and the F2 firmware zip was removed from the release assets. The
  source tag remains immutable; the live release no longer covers F2.
- The release set is loader `7f13e7e5…`, launcher `b3218ccd…`, Music hub
  `de6491a9…`, plus the uninstaller, test public key and a universal
  `Helikon-Companion-0.1.0-alpha2.dmg`. The firmware images are storage-free.
- Added the DMG tooling (`tools/desktop/scripts/build-dmg.sh`, universal support
  in `build-app.sh`) and the archive at `docs/releases/alpha-2.0.0/`.
- Host evidence: 105 HFS+ tests, 31 bootloader tests, 10 firmware groups,
  MusicDB reader, UI previews, disposable-image writer test and loader artifact
  checks. The 346-cut crash matrix is documented but was not rerun in this cut.
- The owner reports the release-set 6G test is complete; per-image photos,
  timings and `fsck_hfs` were not attached. The companion DMG was mounted,
  launched and quit on the build host.

## What is next

1. Attach the launcher and Music-hub 6G screen photos, timings and post-run
   `fsck_hfs` to `device-matrix.md`.
2. T-055/F2 remains blocked and unconfirmed; T-051 production handoff is
   blocked with it. The owner must resolve or re-scope T-055 before either can
   proceed.
3. T-053 release-key certificate verifier.
4. Keep Alpha 2.0.0 described as a storage-free alpha with no firmware-owned
   filesystem write.

## Do not claim

- That Alpha 2.0.0 includes a firmware write or T-055/F2.
- That T-055/F2 has a confirmed 6G result.
- That the release is stable, supported or production firmware.
- That the firmware is signed by a release key, or that the companion is
  notarized.
- That the music image reads the user's library or plays audio.
- That the companion has library, transfer, metadata, podcast, audiobook,
  scrobble or install functionality.

---

# Handoff — 2026-11-06 (companion app reset)

## What happened

- Owner-directed reset: removed the complete speculative companion feature set
  and the sidebar navigation. The app is now a welcome screen and an empty shell.
- Deleted `CompanionCore`, all feature views, the settings/install/scrobble/
  podcast/audiobook/transfer/library/metadata surfaces and the 21 core tests.
- Kept the owner banner crop and the welcome screen, reduced to Open companion.
- The shell has a brand-only sidebar and an empty-state page. It does not
  enumerate or read a volume, does not use the network and does not write.
- Updated [`../docs/companion-app.md`](../docs/companion-app.md),
  [`../tools/desktop/README.md`](../tools/desktop/README.md), the state row,
  T-083, the desktop test row and the file map.
- Evidence: `swift build` and `swift build -c release` pass; the app bundle
  builds and contains the welcome banner; host screenshots of the welcome screen
  and empty shell were reviewed.

## What is next

1. Write the companion capability inventory against the current Helikon-OS
   surface: launcher, read-only library, HelikonDB and diagnostics.
2. Add one honest host capability with a small test target instead of restoring
   the deleted feature list.
3. Keep the splash and shell as the visual base.

## Do not claim

- That library, metadata, transfer, podcast, audiobook, scrobble or install
  functionality exists; the code was deliberately removed.
- That the shell has been run against a device.
- That this reset is a release or support claim.

---

# Handoff — 2026-11-05 (companion app welcome screen)

## What happened

- Added a full-window welcome screen to the companion app. It opens at launch
  and can be reopened from the app menu or Settings.
- Used the owner-supplied `assets/branding/helikon-os-poster.png` as the source.
  The app bundles a 1600×1054 crop of the Halftone photo area at
  `tools/desktop/Sources/HelikonCompanion/Resources/helikon-os-banner.png`;
  nothing was redrawn, recoloured or generated.
- Added **Open companion** and **Add music…** actions, plus a `--skip-splash`
  developer launch option. The splash avoids touching a mounted volume until the
  user enters the app, so macOS volume permission is requested in context.
- Updated [`../docs/companion-app.md`](../docs/companion-app.md) and
  [`../tools/desktop/README.md`](../tools/desktop/README.md).
- Evidence: `swift test` passes 21 tests; `swift build -c release` passes; the
  debug app bundle and resource bundle were built and the welcome screen was
  visually reviewed on the host. No device write or transfer was performed.

## What is next

1. Check the splash on the minimum window size and with large accessibility text.
2. Run the macOS task matrix and a real device transfer on a backed-up volume.
3. Continue the installer only after the signed release exists.

## Do not claim

- That the splash is a device result, a release screen or a UI test pass.
- That the poster was recreated or that the banner is new artwork.

---

# Handoff — 2026-11-05 (companion app chrome cleanup)

## What happened

- Removed the second title bar above every app screen. The page title and
  subtitle now appear once; the sidebar and the page header carry the navigation
  context.
- Removed the ordinal numbers from the sidebar and the section breadcrumb, so
  Overview, Library, Transfer and the rest are identified by name and icon only.
- Replaced all-caps UI labels with title case: meta labels, tags, table headings,
  device facts, brand text, footer, generated-cover lettering and the tiny
  device diagram labels. Uppercase now survives only for acronyms and
  device-supplied volume names.
- Moved Add music into the Overview and Library page headers now that the global
  top bar is gone. The sidebar keeps the device card and refresh action.
- Removed the generated initials from placeholder covers; sample covers are now
  abstract geometry only.
- Updated [`../docs/companion-app.md`](../docs/companion-app.md) and
  [`../tools/desktop/README.md`](../tools/desktop/README.md).
- Evidence: `swift test` passes 21 tests; `swift build -c release` passes;
  host screenshots of Overview, Library and Transfer were reviewed. The app was
  run only against a mounted volume and did not write to it.

## What is next

1. Run the macOS companion task matrix and a real device transfer on a backed-up
   test volume.
2. Review the post-cleanup screens on the target window sizes and with the
   accessibility font setting.
3. Continue the installer only after the signed release exists.

## Do not claim

- No device transfer, UI test suite, notarization or support claim exists.
- No firmware or installer behaviour changed in this session.

---

# Handoff — 2026-11-05 (repository consolidation)

## What happened

- Snapshotted the 30-file dirty worktree on
  `rescue/2026-11-05-multiagent` and tagged it `wip/2026-11-05-pre-cleanup`;
  every old branch tip is preserved under an `archive/*` tag.
- Merged F2/T-055 (seven commits) into `main`. Rebuilt the current tree: the
  normal F2 image is 76,160 bytes, SHA-256 `f33f2791…`; the older `9f21f490…`
  artifact is not reproduced by the merged tree. `tools/check-f2-artifact.sh`
  passes.
- Cherry-picked the two companion-app commits onto `main`; `swift test` passes
  21 tests and `swift build -c release` passes.
- Added the D-52/home-screen concept docs, the companion-app docs and the
  F2/loader docs while keeping `main`'s newer competitive, audio and manual-audit
  content.
- Deleted the stale or already-merged branches and worktrees. `main` is the only
  integration branch; old tips remain recoverable from the archive tags.

## What is next

1. Run the F2 device gate on a backed-up 6G and record the promotion,
   post-run `fsck_hfs` and loader `GOOD` evidence.
2. Run the macOS companion-app task matrix on a disposable HFS+ image and a
   backed-up 6G.
3. Owner decision on D-52 and the home concept.
4. Decide whether to add CI, branch protection and a PR template; neither exists
   yet.

## Do not claim

- No F2 device run, app device run, design acceptance or support claim exists.
- No CI, branch protection, release signing or reproducible release build exists.
- The `9f21f490…` image is historical; the current source rebuilds to
  `f33f2791…`.

---

# Handoff — 2026-11-05 (macOS companion app MVP)

## What happened

- Built the first macOS-first companion app under [`../tools/desktop/`](../tools/desktop/):
  a SwiftPM/SwiftUI host with a parchment/ink visual system, device detection,
  library scanning, metadata search, transfer planning and verification,
  podcast/audiobook search and download, an offline Last.fm queue and a disabled
  installation guide.
- The host core is separate from the UI in `CompanionCore`. It reads HFS+ volume
  metadata through `diskutil`, reads audio tags through AVFoundation, persists a
  JSON library, and keeps every write inside a destination the user chooses.
- Transfers use a temporary file, SHA-256 verification, a verified backup before
  replacement and an optional ID3v2.3 tag write into an MP3 copy. Metadata search
  uses MusicBrainz and the Apple Search API; podcasts use Apple/RSS; audiobooks
  use Open Library/LibriVox; Last.fm is only used after opt-in credentials.
- Added [`../docs/companion-app.md`](../docs/companion-app.md),
  [`../tools/desktop/README.md`](../tools/desktop/README.md), the current-tree
  entries in [`file-map.md`](file-map.md), the T-083 status and the desktop test
  row. A developer preview launch argument loads a fictional sample library.
- Evidence: `swift build -c release` passes; `swift test` passes 21 tests; the
  ad-hoc app bundle builds with `scripts/build-app.sh`; a mounted 32 MiB HFS+
  test image with `.helikon` was detected as Helikon-OS, read/write. No physical
  device run, no installer and no signed release.

## What is next

1. Run the macOS task matrix on a disposable HFS+ image and a backed-up 6G:
   import, tag, rate, transfer, conflict/failed-transfer recovery, podcast and
   audiobook transfer, and a real Last.fm account session.
2. Decide the host-store boundary and the device database schema before writing
   native device metadata; the MVP store is JSON, not the paper's SQLite target.
3. Build the installer only after a signed firmware image, certificate verifier
   and restore rehearsal exist. Keep the install action disabled until then.
4. Add M4A/FLAC/Vorbis tag writers and a real Last.fm metadata provider only if
   the owner keeps them in scope.

## Do not claim

- That the companion app is released, signed, notarized or supported.
- That the installer works or that a signed firmware image exists.
- That a transfer has passed on an iPod, leaves a device database consistent or
  writes native library metadata.
- That Windows/Linux are supported.
- That the sample library is real music or album artwork.

---

# Handoff — 2026-11-05 (home-screen concept set)

## What happened

- Two earlier home attempts were rejected: a bento dashboard was too dense and
  button-like; an album shelf with a now-playing strip read as a botched Now
  Playing screen. The owner asked for two home concepts and a separate album
  browser.
- Added five host mockups:
  - **Phone-style launcher** — six frameless coloured line-icon cells on short
    accent plinths (Music, Podcasts, Audiobooks, Playlists, Games, Settings), a
    small mountain divider with end diamonds, no playback;
  - **Neoclassical index** — four numbered Benguiat sections (Listen, Browse,
    Play, Settings) with descriptions, counts and an hourglass, no playback;
  - **Album wall** — a 4×3 cover browser with a selected-album caption and
    search icon; a browser, not a home candidate.
  - **Podcast wall** — the same wall for shows, with green new-episode dots and
    a latest-episode caption.
  - **Audiobook wall** — the same wall for books, with author, duration and
    reading-progress caption.
  - **Album view** — sleeve, year/rating, title, artist, genre, play and
    Tracks/Notes/Credits tabs with a three-row visible track list.
  - **Artist view** — plum portrait band, name, genre, play and
    Overview/Albums/Tracks tabs with biography and facts.
- All three use the off-white palette and 1 px rules. Home concepts have no
  now-playing hero, strip, progress bar or green playback dot.
- Headers use the owner-supplied ITC Benguiat Std; body text remains IBM Plex
  Sans 12/11. D-52 records the proposed D-23 exception and the licensing
  boundary: the `.otf` files are not committed or redistributed, and
  `BENGUIAT_DIR` is required to regenerate the mockups.
- Added the concepts to [`ui.md`](ui.md) §5, updated
  [`../assets/ui/README.md`](../assets/ui/README.md),
  [`../assets/fonts/README.md`](../assets/fonts/README.md) and
  [`decisions.md`](decisions.md) (D-52).
- Extended `tools/render-ui-mockup.py` to render the three 1× frames and their
  `@2x` previews. It asserts launcher labels and fixed columns; the host frames
  have no clipping or overlap. The icons and covers are generated line art, not
  album artwork.
- Updated the UI row in [`state.md`](state.md) and the asset/tool rows in
  [`file-map.md`](file-map.md).

## What is next

1. The owner chooses a home concept: launcher, index, or a hybrid. The album wall
   remains the album browser, not a home candidate.
2. Confirm D-52 scope: home headers only or all first-party headers, and whether
   the wordmark changes globally.
3. Resolve ITC Benguiat licensing/embedding before firmware headers depend on it;
   use the tracked Plex Semibold fallback if it cannot ship.
4. If accepted, implement the chosen home and album wall in T-057, the header/body
   type in T-042, and record a real 6G panel run.

## Do not claim

- That a firmware home screen, renderer or accepted theme exists.
- That the launcher, index or album wall is the owner’s chosen design.
- That any of these host mockups is a 6G panel result or a support claim.
- That the generated icons/covers are album artwork or licensed images.
- That the ITC Benguiat `.otf` files are part of the repository or redistributable.

---

# Handoff — 2026-11-05 (full manual coverage audit)

## What happened

- Read the Rockbox iPod Classic manual section by section, including the
  installation/update, file browser/context menu/virtual keyboard, database,
  WPS, playlists/queue, hotkeys/quick screen/shortcuts, sound, playback,
  general/display/system/startup/bookmark/voice settings, themes, time/date,
  plugins/apps/viewers, advanced UI/theme/`.cfg`/ROLO/battery chapters and
  Appendices A–G.
- Added a **section-by-section coverage audit** to
  [`competitive-roadmap.md`](competitive-roadmap.md) §12 and a **features
  Rockbox does not have** section in §13.
- The audit found no critical untracked area after the pass. Remaining breadth
  gaps are now T-103 (display/startup settings), T-104 (first-party utilities),
  T-105 (device health), T-106 (library doctor/smart navigation) and T-107
  (per-output profiles/hearing protection).
- Expanded T-093 (queue/playlist viewer/bookmarks/auto-resume), T-094 (file
  operations/keyboard/hotkeys/shortcuts/main menu), T-095 (full settings
  inventory/`.cfg`/rollback), T-072 (theme settings), T-074 (voice/talk clips)
  and T-075 (RTL/runtime language).
- Updated the roadmap lanes, differentiation bets, validation rows, FAQ,
  state, README, file-map, firmware-architecture module table and design-paper
  Appendix I.
- The audit is a plan; no utility, page, setting or health feature exists.

## What is next

1. Owner reviews D-29/D-34/D-49–D-51 and the T-103–T-107 scope.
2. Start T-094/T-093 with the existing UI/library tasks once the writer line
   allows; T-103–T-107 are H2 breadth and do not jump the queue.
3. Keep recording/FM/line-in marked `N/A`; do not add native plugin or video
   parity.

## Do not claim

- That every Rockbox feature is implemented or that Helikon-OS has full parity.
- That the new utilities, device health, library doctor or per-output profiles
  exist.
- That the manual audit is device evidence or that it changes an accepted
  decision.

---

# Handoff — 2026-11-05 (audio-format parity plan)

## What happened

- Reviewed the Rockbox iPod Classic manual Appendix B.1–B.2 and §7.21, the
  Rockbox 2014 iPod Classic codec benchmark (`b320bba`, 216/54 MHz), Apple’s
  6G audio specification, the Rockbox `ipod6g.h` platform limits and the
  CS42L55 codec metadata.
- Added [`audio-formats.md`](audio-formats.md): all **37 audio format families
  (36 in Appendix B.1 plus Opus)**, the measured 6G decode costs, the 24-bit/48 kHz-class
  codec ceiling, Tier 1 (v1), Tier 2 (full Rockbox audio parity at v1.x) and
  Tier 3 (beyond Rockbox, measurement-gated), plus decoder architecture,
  metadata/artwork rules and the test corpus.
- Revised D-34 so Tier 2 is a hard v1.x parity requirement, not an optional
  subset. Added D-49 (decoder provenance/licensing), D-50 (output ceiling and
  resampling) and D-51 (metadata/artwork/gapless).
- Added T-096–T-102 (all `planned`): corpus/provenance, codec framework, output
  ceiling, metadata, gapless/seek/resume, benchmarking and Tier 3 evaluation.
  T-068/T-069 now cover the Tier 1 and Tier 2 implementations.
- Updated [`roadmap.md`](roadmap.md), [`tasks.md`](tasks.md),
  [`decisions.md`](decisions.md), [`competitive-roadmap.md`](competitive-roadmap.md),
  [`validation.md`](validation.md), [`faq.md`](faq.md), [`state.md`](state.md),
  [`README.md`](README.md), [`file-map.md`](file-map.md),
  [`firmware-architecture.md`](firmware-architecture.md) and
  [`design-paper.md`](design-paper.md).

## What is next

1. The owner reviews D-34 and D-49–D-51. No decoder work starts until they are
   accepted or narrowed.
2. Start T-096 (corpus/provenance) and T-097 (codec framework) after D-34 is
   accepted. T-098 (output ceiling) needs the audio measurement rig.
3. The writer line (T-055/F2 → T-051) remains ahead of any audio device testing.

## Do not claim

- That Helikon-OS plays any Rockbox audio format today.
- That the 6G supports native 24/96, native DSD or “hi-res” output.
- That the 2014 Rockbox benchmark is Helikon-OS evidence.
- That D-34 or D-49–D-51 are accepted owner decisions.
- That Tier 2 parity is complete; no decoder and no corpus exist yet.

---

# Handoff — 2026-11-05 (competitive horizon planning)

## What happened

- Reviewed the official *Rockbox Manual for iPod Classic*, version
  `0d5c2b0604-261105` (2026-11-04, 285 pages), for the competitor’s documented
  features, install model, filesystem support, metadata limits, sound/playback,
  themes, plugins, USB, power and accessibility.
- Added [`competitive-roadmap.md`](competitive-roadmap.md): a Rockbox parity
  matrix with `match`/`exceed`/`differ`/`decline` labels, 12 leadership bets,
  horizon lanes H1–H4, a v1 “no-regret” threshold, explicit declines, a
  differential evidence protocol and design sketches for the installer, update
  state machine, library index, checksum sidecar, service mode, theme package
  and sandbox capabilities.
- Extended [`roadmap.md`](roadmap.md) with the competitive baseline, horizon
  lanes and differentiation bets; extended [`tasks.md`](tasks.md) with
  T-060–T-095 (all `planned`); extended [`decisions.md`](decisions.md) with
  D-29–D-48 (all `proposed`) and C-11.
- Added Appendix I to [`design-paper.md`](design-paper.md) as a forward addendum
  that does not change revision 2 or the accepted decisions D-01–D-28.
- Updated [`README.md`](README.md), [`file-map.md`](file-map.md) and added
  [`log/2026-11-05.md`](log/2026-11-05.md).
- Host checks still pass: `make -C tests/hfsplus test`,
  `make -C tests/bootloader test` and `make -C tests/firmware test` all exited
  0. This is docs-only; no device run was made.

## What is next

1. The owner reviews D-29–D-48 and accepts, narrows or rejects them. They remain
   `proposed`; none is a release gate.
2. If D-29 is accepted, start T-060 (parity scorecard) and T-061 (differential
   harness) against a pinned Rockbox baseline.
3. Keep the writer line ahead of every H1 task. F2/T-055 still needs its 6G run;
   T-051 still needs the production handoff.

## Do not claim

- That Helikon-OS matches or beats Rockbox. No differential run exists.
- That any T-060–T-095 feature is implemented.
- That D-29–D-48 are accepted owner decisions.
- That the competitive matrix is a support list or a device matrix.
- That the Rockbox manual is Helikon-OS evidence or a source of assets.

---

# Handoff — 2026-11-04 (F2 implementation, host-verified)

## What happened

- Implemented T-055/F2 on branch `firmware/f2-bootstate`; this record is carried into
  `main` before the F2 device gate.
- Added real ATA PIO write and cache flush, with a board-level write gate that
  stays closed until the read-only self-test, a clean journal and a matching
  bootstate have passed.
- Added `firmware/boot/bootstate.c`: bootstate parse, slot checksum validation,
  two-slot sequence ordering and `TRYING`→`GOOD` promotion against the SHA-256
  of the running image.
- Added `hfs_catalog_overwrite_file`: in-place data and catalog-record update
  through `hfs_txn`, with no allocation or file-size change.
- Added the `WRITE` boot stage and F2 PASS-screen fields.
- Added `VERSION` and `BOOTSTATE_STATE=trying|good` handling to the image build,
  plus a config stamp so a version change rebuilds the objects.
- Moved the F2 BSS to `0x08890000` and reserved
  `0x08800000`–`0x0888ffff`. The loader test image keeps its own BSS,
  framebuffer and LCD DMA scratch in that range; the previous 601 KB F2 BSS
  started at `0x08800000`, zeroed the active LCD DMA buffers and blanked the
  display immediately after the jump. The linker now asserts
  `__bss_start >= 0x08890000`.
- Made the PMU interrupt-latch clear best-effort. The device probe reported
  `STAGE 49` (the latch-clear step) with `-1`; the mask registers are already
  set, so a failed latch read no longer fails `helikon_pmu_init` and blocks
  backlight init.
- Made the ATA write-cache hint (`SET FEATURES 0x02`) best-effort. The iFlash
  SD bridge rejects it with `-1`; storage bring-up now continues, and the
  journal still requires a successful cache flush before commit.
- Made the redundant PMU HDD-enable write best-effort. The loader has already
  powered the disk to read the image; a failed warm-PMU enable write must not
  fail storage bring-up before ATA identify runs.
- Tried and then removed a warm ATA path. The ATA step probe showed `HDD 0`,
  stage 2 and `IDENTIFY -1` on both the first and retry attempts, with zero ID
  words: the controller reset left the iFlash SD bridge unable to answer
  IDENTIFY. The warm IDENTIFY on the loader's still-configured controller also
  failed, so it was removed; the loader handoff is PIO-only.
- Implemented the adopt path. F2 reads sector 0 on the loader's live
  controller before any reset, skips IDENTIFY, and falls back to the donor
  reset only when there is no live handoff. PMU preinit no longer clears
  AUTOENA, so the loader's HDD power stays up. If the loader did power ATA
  down, adopt re-enables `ATA_CONTROL`, restores the rail and waits 2 s. When
  a soft enable never raises PIO ready, a second phase runs the controller
  reset without IDENTIFY and reads sector 0. The
  adopt read also probes LBA48. The normal F2 image is `f33f2791…` (76,160
  bytes); the ATA adopt probe is `beec14c2…` (77,136 bytes). Device result
  pending.
- Fixed the handoff boundary: the loader test image now builds with ATA DMA
  disabled and leaves the controller active before the jump, so F2 does not
  have to recover a DMA session. The current loader build is `8f80e40d…`
  (106,088 bytes); the earlier device-tested loader `6bc6927c…` still used
  ATA DMA.
- Matched the donor IDENTIFY sequence: wait for `!BSY`, issue IDENTIFY, wait
  for DRQ, read the ID table, and do not require DRDY before the command or a
  post-transfer end status. The previous driver waited for DRDY/end, which this
  iFlash SD bridge does not assert in the handoff state.
- Added `tests/firmware/test_bootstate`, the writable/unaligned block tests,
  `tests/image/f2_host.c`, `tests/image/run-f2-macos.sh`, the overwrite crash
  scenario and `tools/check-f2-artifact.sh`.
- Updated `state.md`, `tasks.md`, `device-matrix.md`, `testing.md`,
  `firmware-architecture.md`, `file-map.md`, `log/2026-11-04.md` and this
  handoff.

## Evidence

```text
F2 image:       76,160 bytes, SHA-256 f33f2791…
F2 ATA probe:  77,136 bytes, SHA-256 beec14c2…
BSS:            0x08890000..0x08922e60 (loader scratch reserved below)
bootstate:      TRYING, sequence 1
host groups:    10, sanitizer-clean
loader artifact: DEFAULT -> promoted sequence 2, readback GOOD -> loader GOOD
fsck_hfs:       clean in the disposable-image and artifact tests
crash matrix:   overwrite covered 22 write cuts + flush + partial modes;
                create/append/truncate/delete/rename matrices still pass
```

## What is next

1. Run the F2 device gate using [`f2-bootstate-test.md`](f2-bootstate-test.md):
   build with `VERSION=2 BOOTSTATE_STATE=trying`, copy the three matching files,
   boot, press a button, record `WRITE`/`PASS`, run `fsck_hfs -fn`, boot again
   and require loader `GOOD`.
2. Perform the device power-cut-during-promotion check.
3. T-059 remains optional for the read path but is needed for the full ADR-0004
   handoff.
4. T-051 and T-053 follow F2; F3/T-056 is the next firmware layer.

## Do not claim

- No F2 device run and no device power-cut result exist.
- The image uses the development loader test key, not a release key.
- F2 writes only `/.helikon/bootstate`; there is no full VFS, browser, UI or
  audio.
- The host `fsck_hfs` and crash matrix are not device evidence.

---

---

# Handoff — 2026-11-04 (F1 device run accepted)

## What happened

- F1/T-054 is done. The owner reports the F1 development image (`d7916411…`,
  39,108 bytes) completed the read-only 6G device run and accepts it as the
  T-054 gate. F1 writes nothing and does not replay the journal.
- `firmware/f1-skeleton` is merged to `main` and pushed with this handoff.
- Docs updated: `state.md`, `tasks.md`, `device-matrix.md`, `testing.md`,
  `handoff.md`, `log/2026-11-04.md`, `firmware-architecture.md`, `verification.md`,
  `agents.md`, `file-map.md`, `roadmap.md`, `release.md`,
  `prompts/build-firmware.md` and `prompts/session-start.md`.

## What is next

1. T-055/F2: re-read `/.helikon/bootstate` through the VFS and promote it to
   `GOOD` through `hfs_txn`; flush, read back, confirm the loader sees `GOOD`,
   and run `fsck_hfs -fn`. This is the first firmware write.
2. T-059 remains optional for the read path: the current loader test image does
   not fill the boot-info page, so F1 shows `BOOT-INFO MISSING`/`INVALID` and
   continues. It is needed for the full ADR-0004 handoff path.
3. T-051 and T-053 follow F2: production handoff and the release-key verifier.

## Do not claim

- The F1 device run is owner-reported. The screen photo, stage timings and
  post-run `fsck_hfs -fn` output were not attached to this repository.
- The image is signed with the development loader test key, not a release key;
  it is not a product or a release.
- F2–F5 are not implemented; there is no firmware support claim.

---

# Handoff — 2026-11-04 (F1 firmware skeleton)

> Superseded by the F1 device run acceptance above.

## What happened

- Added the F1/T-054 read-only firmware tree: `firmware/boot` (entry,
  vectors, linker script, boot-info parser, panic, stack guards), `firmware/lib`
  (freestanding mem/string/CRC/endian/ring/pool/fixed/format), `firmware/board/6g`
  (BSP and runtime wiring), `firmware/hal/6g` (clock, PMU, backlight, LCD, ATA
  read, buttons, CPU), `firmware/block` (read-only block device, bounce reads,
  APM/MBR/raw partition scan), `firmware/diag` (stage machine, trace ring,
  crash record, RGB565 screen) and the HFS+ read adapter/file helper.
- `make -C firmware BOARD=6g image` builds a 39,108-byte signed development
  image, SHA-256 `d7916411…`; `__image_end` `0x080098c4`, `__bss_end`
  `0x08837f80`, image headroom 8,345,404 bytes. The manifest and bootstate verify
  through the host loader core: `loader: OK`, `bootstate: GOOD`.
- Added nine host test groups under `tests/firmware/` and the
  `make -C firmware BOARD=6g {,size,test,image}` targets. The F1 host groups
  pass with ASan/UBSan. The existing HFS+ (104) and bootloader (31) suites still
  pass with sanitizers; the disposable-image and loader fixtures still pass.
- Moved the canonical error enum to `firmware/lib/helikon_error.h` and kept the
  `HFS_ERR_*` aliases; added `BOOT_INFO_INVALID`. Updated the HFS and bootloader
  host include paths and the donor build scripts to copy `error.h`.
- Updated `state.md`, `tasks.md`, `file-map.md`, `failures.md`, `testing.md` and
  `agents.md`.

## What is next

1. Done — the owner reported the F1 device run complete; see the newer handoff
   above. Stop before F2 until the owner authorizes the first write.
2. T-059 is still optional for the first run: the current loader test image does
   not fill the boot-info page, so F1 shows `BOOT-INFO MISSING`/`INVALID` and
   continues on the read-only diagnostic path.
3. T-055/F2 follows only after F1 has a recorded device run.

## Do not claim

- Superseded: the F1 device run is owner-reported in the newer handoff. The
  photo, timings and post-run `fsck_hfs` were not attached.
- The image is signed with the development loader test key, not a release key;
  it is not a product or a release.
- The host tests are not a device run, and the boot-info page is not filled by
  the current loader.

---

# Handoff — 2026-11-04 (session-start prompt)

## What happened

- Added [`prompts/session-start.md`](prompts/session-start.md), a reusable
  full-procedure prompt for the start of any session. It forces a repo read,
  a git-state check, task selection by priority/dependency, a short **Session
  Brief** before edits, domain routing, a work loop, the end-of-session doc
  checklist and a hard-stop card.
- The prompt deliberately does not hardcode the current task; it derives it
  from `state.md`, `tasks.md` and the handoff, so it does not rot when the queue
  changes. It includes a dated example brief, marked as an example only.
- Updated the prompts README, docs index, `agents.md` (session-start instruction
  and index), `state.md` and `file-map.md`.
- No firmware code, image or device run was added.

## What is next

1. Paste `docs/prompts/session-start.md` at the start of the next session; it
   should produce the Session Brief and then start T-054/F1 if authorized.
2. Keep the example brief in the prompt marked as an example; the repo wins.
3. Use `build-firmware.md` for the actual F1–F5 execution.

## Do not claim

- No firmware code or device result exists.
- The prompts are execution contracts, not evidence.
- The Session Brief is a snapshot derived from the repo, not a new source of
  truth.

---

# Handoff — 2026-11-04 (firmware build prompt)

## What happened

- Added [`prompts/build-firmware.md`](prompts/build-firmware.md), a ~9.6k-word
  full-procedure prompt for an agent that builds the firmware F1–F5. It covers
  the hard stops, current state, required reading, phase gates, a ten-session
  execution plan, repository layout, the accepted memory/entry/runtime
  contracts, build/toolchain, coding standards, testing, device protocol,
  failure handling, reporting, anti-patterns, open questions, per-phase handoff
  and a literal F1 checklist.
- The prompt authorizes **F1/T-054 only** for the first run. F2–F5 are included
  as the destination, not as permission to build them in one pass.
- Updated `docs/prompts/README.md` with a `full-procedure` prompt exception,
  the docs index, `agents.md`, `state.md` and the T-054/T-055 links.
- No firmware code, image, manifest or device run was added.

## What is next

1. Give the prompt to a fresh firmware agent; it must start with F1/T-054.
2. The agent reads the required docs, creates the skeleton, runs the host tests
   and stops after the F1 device evidence (or reports a blocker).
3. T-059 remains the loader-side boot-info fill; F1 can use the zero-`r0`
   fallback.
4. T-055/F2 follows only after F1 is recorded.

## Do not claim

- No firmware code or device result exists.
- The prompt is an execution contract, not evidence.
- The map and ABI are accepted decisions, not measurements.

---

# Handoff — 2026-11-04 (ADR-0003/0004 accepted)

## What happened

- Decided and accepted **ADR-0003 / D-27**: fixed 64 MiB map. Image
  `0x08000000`–`0x087fefff` (8 MiB − 4 KiB), boot-info `0x087ff000` (4 KiB),
  BSS/stacks/pools `0x08800000` (8 MiB), UI `0x09000000` (8 MiB), audio
  `0x09800000` (24 MiB), database `0x0b000000` (8 MiB), reserve `0x0b800000`
  (8 MiB). Audio gets the upper end of its target; the database gets the top of
  its target.
- Decided and accepted **ADR-0004 / D-28**: `r0` passes a versioned, CRC-32
  protected boot-info page at `0x087ff000`; `r1 = 0`; IRQ/FIQ off, SVC mode; no
  return; boot-info is a cache of verified facts, not a trust anchor. The
  firmware re-reads bootstate before any write.
- Added **T-059**: loader fills the boot-info page before the jump; T-051 now
  depends on it. T-054 can start with the zero-`r0` fallback, so the ABI does not
  block F1.
- Updated `decisions.md`, `tasks.md`, `state.md`, `architecture.md`, the paper,
  ADR indexes, `agents.md`, ADR-0003/0004 and the handoff. No firmware code was
  written.

## What is next

1. T-054/F1: read-only firmware skeleton with a signed image, banner, ATA read,
   HFS+ probe, one file read, PASS and power-off. No filesystem writes.
2. T-059: fill the boot-info page in the loader; keep the no-args test payload
   path; host-test the page.
3. T-055/F2: promote `/.helikon/bootstate` to `GOOD` through `hfs_txn`.
4. Measure the audio buffer need before F5; do not treat the 24 MiB split as a
   measurement.

## Do not claim

- No firmware kernel, HAL, VFS, player or UI exists.
- The map and ABI are design decisions, not device measurements.
- No firmware boot, audio, UI, writable library or support claim.

---

# Handoff — 2026-11-04 (firmware architecture and ADRs)

## What happened

- Added [`firmware-architecture.md`](firmware-architecture.md), a ~19k-word
  firmware-internal research and design paper: boot boundary, memory map,
  runtime model, module architecture, boot/mount/read/first-write flows,
  developer ergonomics, aesthetic and interaction design, security, performance,
  testing, phases F1–F5, risks, open questions and appendices.
- Added two **proposed** ADRs: [`adr/0003-firmware-memory-map.md`](adr/0003-firmware-memory-map.md)
  (fixed 64 MiB map, 8 MiB − 4 KiB image cap, 4 KiB boot-info page at
  `0x087ff000`) and [`adr/0004-firmware-entry-abi.md`](adr/0004-firmware-entry-abi.md)
  (`r0` boot-info, no-return entry, CPU-state contract, validation and
  fallback). They are indexed as D-27/D-28 and were accepted in the newer
  handoff entry above.
- Added firmware tasks T-054–T-058 (F1 skeleton, F2 bootstate word, F3 VFS and
  browser, F4 UI, F5 audio) and made T-054/T-055 P0 because they produce the
  first real signed firmware image and unblock T-051.
- Updated the documentation index, `state.md`, `architecture.md`, `file-map.md`,
  `ui.md` (final splash status), `failures.md` (single-image fail-closed
  wording), `glossary.md` and `testing.md`.
- No firmware code, image, manifest or device run was added. No release key was
  generated and no signing claim is made.

## What is next

1. (Done — see newer entry) ADR-0003 and ADR-0004 were accepted; D-27/D-28
   record the decisions.
2. T-054/F1: read-only firmware skeleton with a signed image, banner, ATA read,
   HFS+ probe, one file read, PASS and power-off. No filesystem writes.
3. T-055/F2: promote `/.helikon/bootstate` to `GOOD` through `hfs_txn`, then
   close the loader loop; no other file written.
4. T-053: release-key certificate, rotation and revocation verifier.
5. Keep the host harness and the boot-stage/error vocabulary in sync with the
   paper as F1 starts.

## Do not claim

- No firmware kernel, HAL, VFS, player or UI exists.
- The memory map and entry ABI are proposals, not accepted decisions.
- No firmware boot, audio, UI, writable library or support claim.
- No release key, release signature or signed release image.

---

# Handoff — 2026-11-04 (loader test image passes)

## What happened

- Replaced the full TweetNaCl drop with `bootloader/ed25519.c`, a
  verification-only CC0 derivative. It has no key generation, signing or random
  source. RFC 8032 vectors, a fixed 192-byte manifest-length vector and tamper
  cases pass.
- Added `bootloader/keys.c` (public-key table) and refactored the loader so the
  Ed25519 signature is verified **before any image byte is read**; SHA-256 and
  size are checked after the read.
- Added a separate DFU-only loader test image:
  `build/loader-bootloader/out/bootloader-ipod6g-loader-test.ipod`
  (107,112 bytes, SHA-256 `3979de6f…`, `MOVE_AREA` headroom 21,920 bytes). It
  reads `/.helikon/firmware.manifest|img` and `bootstate`, verifies the signed
  manifest, reads the image directly to its signed DRAM address, executes a
  signed 12-byte ARM payload returning `HELO`, and disables production handoff.
- Added the owner Alpha splash in `assets/bootloader/`: `VERSION Alpha` and a
  green/red `HFS+ DETAILS` label. The loader image stores a 16-colour 4bpp
  base (38,400 bytes) plus a 4bpp label mask (592 bytes) and renders to RGB565;
  the PNG mockups preserve the source. Font: ITC Benguiat Std Book 9px,
  owner-provided and not committed.
- Added `tools/build-loader-bootloader.sh` (generated test key under `build/`,
  never committed; signed manifest/bootstate; `mks5lboot` dry run) and
  `tools/check-loader-artifact.sh` (exact generated files through the host HFS+
  core). Both pass.
- Added `bootloader/LOADER_TEST.md` with DFU-only install, expected screen, pass
  criteria, recovery and the no-firmware-boot limit.

## Evidence

- `make -C tests/bootloader test`: 31 tests; `sanitize` clean.
- `make -C tests/image loader-test`: real generated Ed25519 key, `loader: OK`,
  `bootstate: GOOD`; wrong key and unknown key ID rejected.
- `./tools/build-loader-bootloader.sh`: image/DFU dry run; exact artifact check
  passes; `fsck_hfs -fn` clean on the disposable volume.
- One owner-reported startup on `c6125103…`: splash displayed, startup completed
  after about 3 minutes, `Battery status ok` shown, and `JUMP OK` confirmed.
  `HFS+ DETAILS` stayed neutral on that pre-`lcd_update` image. Post-run
  `fsck_hfs` is still to record.
- A second owner-reported startup on `ca26b5a6…` completed in 1 minute
  20 seconds, with the longest stalls at `firmware manifest` and
  `verify firmware`.
- The first indexed build `9600332f…` booted fast but failed
  `Cannot read firmware manifest` / `not found (-4)`: the descent picked a
  separator at/after the parent and skipped a shared leaf. `3979de6f…` uses the
  last separator below the parent.
- Owner report 2026-11-04 on `3979de6f…`: everything works, very fast and
  consistent, startup 6.5 s. This closes the T-049 test-image scope. Post-run
  `fsck_hfs` remains to record; the production handoff is still disabled.
- D-20/ADR-0002 option A is accepted and T-006 is closed: offline Ed25519 root
  on two hardware tokens, certified release key, rotation/revocation/recovery.
  The certificate verifier and first release key are T-053; no release signing
  is claimed.
- T-050 is closed by owner decision: single-image fail-closed, no A/B rollback.
  D-26 removes the in-bootloader OF/recovery menu; native Menu+Play and DFU are
  the recovery paths.
- Published the bootloader-only Alpha release
  [`alpha-1.0.1`](https://github.com/deandromi/Helikon-OS/releases/tag/alpha-1.0.1):
  `Alpha 1.0.1 Bootloader Only - A Quick Start`. Assets include the loader image,
  DFU installer, test manifest/payload/bootstate, uninstaller, quick start and
  the final green/red splash PNGs. It is not a firmware release and uses a test
  key. The owner then made it a normal GitHub release.
- Replaced the splash with the owner's final green/red PNGs (`VERSION ALPHA`).
  The refreshed image is `6bc6927c…` (106,536 bytes, 22,496 bytes headroom); the
  temporary bottom stage line is removed. It is a visual-only refresh of the
  tested `3979de6f…` loader path. The final-splash device check passed at 6.5 s
  with a clean `fsck_hfs -fn`; the GitHub release assets and body were updated
  in place and the release was made a normal release.

## What is next

1. Keep the release evidence: `6bc6927c…`, 6.5 s, clean `fsck_hfs -fn`.
2. T-051: build the production firmware handoff. It needs a real signed firmware
   image for end-to-end device validation; no OF/recovery menu (D-26). The
   current image executes a signed 12-byte test payload.
3. T-053: implement the root/release-key certificate, rotation and revocation
   verifier. Generate the first release key only in the offline ceremony.
4. T-052: reduce the 6.5 s startup toward 2–3 s later; not a T-051 blocker.
5. Rebuild the inner-image and outer-volume diagnostics from current source
   before another device run; their tested artifacts predate the T-034/T-010
   fixes.

## Do not claim

- No firmware boot, kernel, audio, UI or writable HFS+ from the loader test.
- No A/B rollback; the owner accepted single-image fail-closed (T-050).
- No release signature, release key or support claim; D-20 is accepted but the
  certificate verifier and a real signed release image are not implemented.

---

# Handoff — 2026-11-04 (outer-volume test passed on the 6G)

## What happened

- The owner installed `32eeb541…` by DFU and reports **every screen stage
  PASS**. The run was slower than expected but completed.
- T-048 is closed by owner confirmation of the full backup/diff on 2026-11-04.
  The agent did not rerun the full diff; only the partial `.rockbox`/
  `.rockpod-rw` backup was visible here.
- Post-run on the mounted iPod: `/helikon-outer-test` absent, `fsck_hfs -fn`
  reported `The volume IPOD appears to be OK`, `diskutil verifyVolume` passed.
- `.rockpod-rw` was byte-identical to the 2026-11-03 backup
  (SHA-256 `2ac065…`); `.rockbox` differed only in `.DS_Store` metadata.
- This is the first successful outer-volume writer run on the real 6G volume.
  It is one run on one device; no power-cut or support claim.

## P0 progress

- T-048 is closed by owner confirmation of the full backup/diff.
- T-044 is implemented: Finder type/creator zero policy, preservation test and
  macOS `GetFileInfo` checks.
- T-038 is implemented: scripted macOS Unicode corpus (NFC/NFD, CJK, emoji,
  Hangul, combining, U+2126/U+F900, `:`/`/`, 255-unit and rejected 256-unit).
- T-010 now has rename, flush-failure and partial-sector injection; 280
  write/flush/partial cuts are fsck-clean.
- T-034 is implemented: index-node insert/split recursion, below-minimum
  insert, deep delete/merge and recursive parent-key propagation are fixed. The
  model now inserts, updates and deletes 60 records at depth 3.
- T-003 is closed: owner confirmed the full backup/diff, disposable restore
  rehearsal and Apple-restore/DFU path.
- T-004 is closed: `docs/fixtures.md` records the fixture matrix, generators and
  hash/evidence policy.
- T-010 is closed as a definition task: the matrix and evidence format are in
  `validation.md`, with 280 host write/flush/partial cuts. Device power-loss
  execution remains a validation gate.
- T-041 is implemented: licensed IBM Plex Sans and Manrope subsets, OFL texts,
  hashes and two 320×240 host mockups. The renderer is T-042.
- D-01/D-02 are accepted: Ed25519-signed manifest and two-slot read-only
  bootstate. T-011/T-027 are implemented: LOADER.md, SHA-256, manifest,
  bootstate, HFS adapter and host loader; loader tests and the macOS HFS+
  fixture pass. T-049 is the bootloader-image integration.

## Task list review

`docs/tasks.md` was regrouped on 2026-11-04. T-048 is closed and T-044 is now
implemented (Finder type/creator policy plus preservation and macOS
`GetFileInfo` checks; wider Unicode names are T-038). The active P0 line is
T-049 bootloader integration and T-006 signing. T-034 is implemented, and T-038
has a scripted Unicode corpus. UI work is P1/P2. T-017 is rejected and T-030 is
complete through T-032/T-040/T-047.

## What is next

1. The outer diagnostic bootloader is still installed and `helikon-outer-ok.txt`
   is still at the iPod root. Remove the opt-in file now, or keep it only if
   another controlled run is planned. To return to the normal boot path, put
   the iPod in DFU and run `tools/uninstall-bootloader.sh` (never `--single`).
2. T-049: integrate the host loader into a read-only firmware-loading bootloader
   image. No firmware image exists yet; do not jump to an unsigned or test image.
3. T-006: D-20 key ownership. ADR-0002 proposes an offline Ed25519 root plus
   delegated release key; the owner must accept or choose another option.
4. P1: T-012 device inventory, T-029 read-only diagnostic finish; T-042/T-013/
   T-024 are P2 and T-045 splash depends on owner design.
5. Rebuild the inner-image write diagnostic (`81350f…` predates the journal
   `max_blocks` fix) before another inner power-cut run.

## What must not be touched

- Do not treat one outer run as a power-cut, adapter or support claim.
- No `--single`; no journal deletion, format or destructive cleanup.
- Do not run the test on the library without the verified backup path.

## Device/test evidence

- 6G outer run `32eeb541…`: all stages PASS, test folder absent, fsck and
  `diskutil verifyVolume` pass.
- Partial backup comparison: `.rockpod-rw` identical; `.rockbox` only
  `.DS_Store`.
- Owner confirmed the full-library backup/diff; the power-cut matrix remains.

---

# Handoff — 2026-11-04 (outer run partial; truncate hang fixed)

## What happened

- The first outer image (`9eacde18…`) ran on the 6G. It reached rename and
  stopped before truncate without showing the report. The next boot showed
  `Cleanup: FAIL`, error `-14` (`HFS_ERR_BUSY`) because `/helikon-outer-test`
  already existed.
- Inspection found only `marker` (`HELIKON-OUTER-TEST`) and `renamed` (4096
  bytes, all `0xa5`). That places the stop after rename, before truncate.
  `diskutil verifyVolume` / `fsck_hfs -fn` reported the volume OK; no library
  file was touched.
- Root cause hypothesis: append/truncate updated a file record by deleting and
  reinserting it, which can trigger an unnecessary B-tree underflow/merge on
  the real catalog. Added `hfs_btree_update()` for same-key, same-length
  in-place leaf-record replacement; append and truncate now use it.
- Added a progress callback so the outer diagnostic shows the current stage on
  screen. If it stops, the last line identifies the stage.
- New image: 110,408 bytes, SHA-256
  `32eeb541bcd64b45e4319a6ed16fbe4e22cf3123d2d82738b517877e53a3265e`;
  DFU `6e2e7d40…`, 118,544 bytes. Host tests pass, including a multi-level B-tree
  update test and the 4 KiB journal path. No device run of this image.

## What is next

1. Remove the leftover `/helikon-outer-test` after inspecting it with
   `tools/cleanup-outer-test.sh /Volumes/IPOD`; the dry run identifies only
   `marker`/`renamed`.
2. `diskutil verifyVolume` and eject; reinstall the new DFU.
3. Run T-048 again. Record the visible progress stage or the final report,
   `fsck_hfs -fn`, backup diff and `diskutil verifyVolume`.
4. If it stops again, report the last visible stage; do not delete the folder
   or journal.

## What must not be touched

- No opt-in or write without the owner's backup/restore confirmation.
- No `--single`; no journal deletion, format or `rm -rf`.
- Do not treat the partial run as a pass or a library-write claim.

## Device/test evidence

- Partial outer run: `marker` + `renamed` 4096 `0xa5`, volume fsck OK, second
  boot `Cleanup: FAIL -14`.
- New image `32eeb541…`; DFU `6e2e7d40…`; 18,624 bytes `MOVE_AREA` headroom.

---

# Handoff — 2026-11-04 (outer-volume diagnostic built, journal compatibility)

## What changed

- Built the separate outer-volume write diagnostic:
  `bootloader-ipod6g-outer-diag.ipod` (110,408 bytes, `32eeb541…`), DFU
  `6e2e7d40…` (118,544 bytes). It requires `helikon-outer-ok.txt`, refuses a
  dirty journal and an existing `/helikon-outer-test`, and writes only that
  folder's marker/test files.
- Added `bootloader/OUTER_DIAG.md`, `tools/build-outer-diagnostic-bootloader.sh`,
  `tools/install-outer-diagnostic.sh`, `tools/make-outer-opt-in.sh` and
  `bootloader/hardware/donor-outer-diag.patch`.
- Added D-25 and T-048. T-047 is `implemented`; T-048 (device run) is `blocked`
  on the owner's verified full backup and restore rehearsal.
- Host test `make -C tests/image outer-diag-test` passes: sequence, root-count
  restoration, fsck, dirty-journal refusal, macOS journal replay, missing
  opt-in, marked/unmarked folder refusal, a second run and a patched 4 KiB
  journal-header image.
- Found and fixed a journal compatibility bug: `block_list_header.max_blocks`
  was zero and `BLHDR_FIRST_HEADER` was not set, so macOS rejected a committed
  journal. The writer now writes both; a unit test and the host dirty-journal
  replay test cover it.
- Generalized volume-header and bitmap staging to a journal-sector-sized block
  (512 or 4 KiB), with a 4 KiB journal-sector unit test. The outer diagnostic
  refuses block/journal size combinations the writer cannot stage.
- Host suite is now 112 portable tests (100 HFS+, 12 bootloader),
  sanitizer-clean.

## What is next

1. Owner: confirm the full backup was read back and the restore path rehearsed;
   then run T-048 with `tools/install-outer-diagnostic.sh`.
2. After the run: `fsck_hfs -fn`, macOS mount, `diff -qr` against the backup,
   `diskutil verifyVolume`; record in `device-matrix.md`.
3. Rebuild the inner-image write diagnostic (`81350f…` predates the journal
   `max_blocks` fix) before another inner power-cut run.
4. T-044 remaining: Finder creator/type policy and wider macOS differential.
5. T-041/T-042: font licences and anti-aliased 320×240 mockup; T-045 splash.

## What must not be touched

- No outer-volume write without the full verified backup, restore rehearsal,
  opt-in file and clean journal.
- No `--single`; no journal deletion, format or destructive cleanup.
- Do not delete a pre-existing `/helikon-outer-test` automatically; inspect it
  on macOS first.
- Do not treat the host outer test as a device run.

## Device/test evidence

- No new device run. Host outer test passes; dirty journal is replayed by macOS
  `fsck_hfs` in the host test.
- `diskutil verifyVolume /Volumes/IPOD` passed on 2026-11-04; the owner reports
  a full backup, but the backup has not been independently verified and no
  restore has been rehearsed in this session. No opt-in file was written.
- Outer image `32eeb541…`; DFU `6e2e7d40…`; 18,624 bytes `MOVE_AREA` headroom.
  This tested artifact predates the T-034 insert/leaf fixes; rebuild before the
  next device run.

---

# Handoff — 2026-11-04 (anti-aliasing, splash, outer-volume protocol)

## What changed

- D-24 accepted: UI text and icons use grayscale anti-aliasing with precomputed
  glyph masks/atlases and a readable 1-bit fallback. Subpixel/gamma/hinting
  remain open. T-041/T-042 updated.
- `docs/ui.md` now has a bootloader-splash section. The current 320×240 1-bit
  splash is not final; the owner will design a new one (T-045). The
  1-bit/grayscale rendering path and asset licence are open.
- Added `docs/outer-volume-test.md`: the real outer-volume write protocol. It
  requires a full verified backup of the entire iPod (music included), a
  rehearsed restore, a clean journal, and a separate T-047 diagnostic image.
  Host disposable-image validation is done; the device image is not built.

## What is next

1. Owner: decide the new splash design and provide the asset/licence.
2. Build T-047 outer-volume diagnostic and host-test it; then the owner can run
   it on the 6G **after** the full backup and restore rehearsal.
3. T-041/T-042: font licences, anti-aliased 320×240 mockup and renderer.
4. T-044 remaining: Finder creator/type policy and wider macOS differential.

## What must not be touched

- No outer-volume write on the iPod until the full backup is verified and the
  restore path is rehearsed.
- No `--single`.
- Do not treat the host outer-image test as a device run.
- Do not replace the current splash without owner approval.

## Device/test evidence

- No new device run. Host outer-image stages and PMU target 96 are recorded.

---

# Handoff — 2026-11-04 (metadata policy, outer-volume stages)

## What changed

- Timestamp policy is explicit: create/access/content/attribute on create;
  content/attribute on append/truncate; attribute-only on rename; parent folder
  dates on valence; volume modify date on create/delete/bitmap/rename;
  `backupDate` stays 0.
- BSD owner/group 0, admin/owner flags 0, file/folder mode, Finder info and
  textEncoding zero, special 0; preserved across updates.
- The direct outer-volume test now runs create/append, rename, truncate and
  delete, mounting in macOS after each stage and checking content, modes,
  current-year timestamps, renamed path and deleted path; `fsck_hfs` is clean
  after every stage.
- Target 96 is recorded as the post-commit PMU recovery pass. Physical 7G is
  removed from the active queue and marked `deferred` in the matrix.

## Evidence

- `make -C tests/hfsplus test` / `sanitize` — 98/98.
- `make -C tests/image test` — PASS through create/append/rename/truncate/delete,
  modes/timestamps, macOS mounts, `fsck_hfs` clean.
- `make -C tests/bootloader test` — 12/12.
- 110 portable tests total, sanitizer-clean.

## What is next

1. Device outer-volume test: requires a disposable iPod/volume and a verified
   full backup; host path is done. Do not use the library.
2. T-044 remaining: Finder creator/type policy and a wider macOS differential.
3. T-041: font licences and 320×240 typography mockup.
4. D-01/D-02/D-20 for a firmware-loading bootloader.

## What must not be touched

- No library write without a disposable volume and verified backup.
- No `--single`.
- Do not readd physical 7G to the active queue without owner direction.
- Do not treat the host outer-image test as a device run.

## Device/test evidence

- Target 96 PMU cut/recovery, 2026-11-04, owner-reported; inner fsck clean.
- Current image `81350f…`; DFU `ce356e…`.

---

# Handoff — 2026-11-04 (target 96 pass, metadata defaults, 7G deferred)

## What happened

- Target `96` passed: the post-commit cut recovered on the next boot, every
  stage passed, and the extracted inner image `fsck_hfs` reported
  `The volume HelikonInner appears to be OK`.
- Targets 7 and 56 remain the pre-commit clean controls. T-043 is recorded as
  implemented; further replay targets are optional coverage.
- Metadata defaults now cover BSD owner/group 0, admin/owner flags 0,
  file/folder mode, Finder info zero, textEncoding zero and special 0; metadata
  is preserved on append/truncate/rename. Host metadata tests pass.
- The direct outer-volume disposable image test now covers create/append,
  macOS content/mode/timestamp, rename, truncate, delete and `fsck_hfs` after
  each stage. This is the host half of the outer-volume validation.
- Physical 7G was removed from the active task queue by owner direction; 7G
  remains a future port in the target policy and the device matrix marks it
  `deferred`.

## Evidence

- T-043: target 96, `Recovery: RECOVERED`, all stages PASS, inner `fsck_hfs`
  clean.
- `make -C tests/hfsplus test` / `sanitize` — 98/98.
- `make -C tests/bootloader test` — 12/12.
- `make -C tests/image test` — outer disposable image passes `fsck_hfs` after
  create/append, rename, truncate and delete; macOS sees the file at 4096 then
  0 bytes, modes `-rw-r--r--`/`drwxr-xr-x`, current-year timestamps, the renamed
  path and the deleted path.
- 110 portable tests total, sanitizer-clean.

## What is next

1. T-044 remaining: timestamp-update policy, Finder creator/type policy, wider
   macOS differential after append/truncate/rename/delete.
2. Item 3: device outer-volume test still requires a disposable iPod/volume and
   a verified backup. The host outer-volume path is complete; do not run the
   device path on the library.
3. T-041: font licences and 320×240 mockup.
4. D-01/D-02/D-20 for a firmware-loading bootloader.

## What must not be touched

- No library write without a disposable volume and verified backup.
- No `--single`.
- Do not readd physical 7G to the active queue without owner direction.
- Do not treat the inner-image timestamp test as full metadata parity.

## Device/test evidence

- 6G target 96 PMU cut/recovery, 2026-11-04, owner-reported.
- Current image `81350f…`; DFU `ce356e…`.

---

# Handoff — 2026-11-04 (PMU cut targets 7 and 56)

## What happened

- Targets `7` and `56` both powered the diagnostic off early. After setting the
  target to `0` and rebooting, the owner saw every stage PASS with
  `Recovery: CLEAN` and `Cleanup: PASS`.

## Interpretation

- The cut count is 512-byte ATA writes. The measured one-extent sequence has
  journal commits at about 35, 89, 159, 227, 287, 349 and 403; clean markers at
  54, 108, 194, 244, 314, 368 and 422.
- Targets 7 and 56 are pre-commit, so a clean journal is correct. They are
  useful clean-cut evidence, not recovery evidence.
- Use replay-window targets `44`, `96`, `176`, `236`, `300`, `356`, `410` for a
  dirty journal and `Recovery: RECOVERED`.
- Inner-image `fsck_hfs` for the 7/56 runs is still pending.

## What is next

1. Run target 96 (or 44/176) and confirm `Recovery: RECOVERED`.
2. Extract and fsck the inner image after each cut.
3. Continue T-044 metadata parity and T-041 font/mockup work.

## Evidence

- Owner report, 2026-11-04; recorded in device-matrix §2.3.
- Current image `81350f…`; no new DFU rebuild was needed.

---

# Handoff — 2026-11-04 (PMU cut target 7 observation)

## What happened

- With `helikon-cut.txt` = `7`, the write diagnostic powered off during the
  early boot/write sequence. No report text was shown; that is expected for an
  early cut.
- The owner set the target to `0`, rebooted and got every stage PASS with
  `Recovery: CLEAN` and `Cleanup: PASS`.

## Interpretation

- The cut count is **512-byte ATA writes**, not journal entries. Target 7 is
  before the first journal commit, so the journal stays clean and
  `Recovery: CLEAN` is correct. This is a valid pre-commit cut, not a failed
  recovery.
- To see `RECOVERED`, use post-commit targets: `56`, `96`, `160`, then later
  `240`, `400`, `800`, `1200`, `1500`.
- The inner-image `fsck_hfs` for the target-7 run is still pending.

## What is next

1. Run target 56, then 96/160; confirm a dirty-journal recovery line.
2. Extract and fsck the inner image after each cut.
3. Continue T-044 metadata parity and T-041 font/mockup work.

## Evidence

- Owner report, 2026-11-04; recorded in device-matrix §2.3.
- Current image `81350f…`; no new DFU rebuild was needed for this observation.

---

# Handoff — 2026-11-04 (metadata timestamps, current write image)

## Branch / commit

- Branch: `main`
- Current head: the metadata-timestamp commit; check `git log -1`.

## What changed

- `struct hfs_catalog` now carries `now` in HFS+ epoch seconds. New file/folder
  records get create/access/content/attribute dates; append, truncate, rename
  and valence updates touch content/attribute dates; `backupDate` stays 0.
- The volume-header modify date is staged on create/delete and on bitmap
  alloc/free.
- The write diagnostic passes the 6G RTC time into the inner catalog; a failed
  RTC read falls back to 0.
- Current image: 110,920 bytes, SHA-256 `81350f…`, DFU 119,056 bytes,
  SHA-256 `ce356e…`, 18,112 bytes `MOVE_AREA` headroom.
- Host tests: 108 portable (96 HFS+, 12 bootloader), sanitizer-clean; timestamp
  test covers create, parent touch, append, truncate, delete and volume date.
- `write_diag_host` still passes the normal outer/inner sequence; the
  `helikon-cut.txt` parser reports target 7.

## What was verified

- `make -C tests/hfsplus test` / `sanitize` — 96/96.
- `make -C tests/bootloader test` — 12/12.
- `make -C tests/image write-diag-test` — PASS, both images fsck-clean.
- No PMU cut has run on hardware; T-043 is still the next device step.

## What is next

1. Run T-043 controlled PMU cuts on the 6G (targets in `WRITE_DIAG.md`).
2. Complete metadata parity: BSD owner/group/flags, Finder info/prototype,
   timestamp policy, macOS differential (T-044).
3. Outer-volume disposable write test, then library-write plan.
4. UI font licences and 320×240 mockup (T-041).

## What must not be touched

- Do not treat the RTC timestamp fallback as a metadata-parity claim.
- Do not write outer metadata or the library.
- Never use `--single`.
- Do not retry after a failed `fsck_hfs` or dirty journal.

## Device/test evidence

- No new hardware run. Current image hash and DFU hash are in
  `bootloader/WRITE_DIAG.md`; the last normal-mode 6G pass used `9e3881…`.

---

# Handoff — 2026-11-03 (controlled PMU cut support)

## Branch / commit

- Branch: `main`
- Current head: the PMU-cut support commit; check `git log -1`.

## What changed

- Added `helikon-cut.txt`: a root-level decimal write-count file on the outer
  volume. The core reads it after replaying any dirty inner journal and arms the
  ATA write path to call `power_off()` when the target count is reached.
- The normal write diagnostic now cleans up any leftover `diag` folder/file
  after recovery, so a cut can be followed by a normal verification boot.
- The report adds `Recovery: CLEAN/RECOVERED` and `Cleanup: PASS/FAIL`.
- Current image: 110,120 bytes, SHA-256 `789687…`, `_moveend` `0x2203b620`,
  18,912 bytes headroom; DFU 118,256 bytes, SHA-256 `06ac01…`.
- Host: `write_diag_host OUTER read-cut` verifies the target parser; normal
  outer/inner image test still passes twice. No hardware cut run yet.

## What was verified

- `make -C tests/image write-diag-test`: normal sequence passes twice, outer and
  inner `fsck_hfs` clean, no leftover `diag`.
- `write_diag_host OUTER read-cut` with `helikon-cut.txt` containing `7`
  reports `cut target: 7`.
- Written image builds and fits `MOVE_AREA`; DFU generated.
- No PMU cut has been run on hardware.

## What is next

1. Run T-043 on the 6G: targets 1, 3, 5, 7, 10, 15, 20, 30, 50, 100, 150, 190.
2. After each cut, set `helikon-cut.txt` to 0, reboot, confirm recovery/all PASS,
   extract the inner image and run `fsck_hfs -fn`.
3. If a cut leaves an unrecoverable journal or fsck repair, stop and preserve
   the image.
4. Then proceed to metadata parity and the disposable outer-volume test.

## What must not be touched

- Do not treat a PMU cut as a true battery/cable power-cut matrix; record it as a
  controlled PMU cut.
- Do not write outer metadata or the library.
- Never use `--single`.
- Do not retry after a failed `fsck_hfs` or dirty journal.

## Device/test evidence

- No PMU cut hardware run yet. Current image hash and DFU hash are in
  `bootloader/WRITE_DIAG.md`.
- The previous normal-mode 6G pass used `9e3881…`; see device-matrix §2.2.

---

# Handoff — 2026-11-03 (default UI typography)

## Branch / commit

- Branch: `main`
- Current head: the typography commit; check `git log -1`.

## What changed

- Added `docs/ui.md`: the default first-party UI typography, translated from the
  owner-supplied Dutch table.
- Added D-23 (accepted): IBM Plex Sans is the only UI family; main menu 14 px
  Regular, selected item 14 px Medium, screen titles 17 px Semibold,
  artist/album 12 px Regular, system information 11 px Regular; Manrope Bold is
  the logo/wordmark only with a variable size.
- Added T-041 (font licences + 320×240 mockup) and T-042 (renderer
  implementation).
- Updated the docs index, architecture §9, file map and state. No UI code or
  font binaries exist yet; line height, colours, margins and large-text
  behaviour remain open.

## What was verified

- Documentation only; no code changed.
- Link/table/whitespace checks pass.

## What is next

1. Verify IBM Plex Sans and Manrope OFL releases, subset/hash and licence files.
2. Build the 320×240 typography mockup and check clipping/contrast.
3. Continue the device power-cut matrix on the inner image.

## What must not be touched

- Do not bundle proprietary fonts or unlicensed themes/artwork.
- Do not treat line height, colours or margins as settled; they are open.
- Skins may not make the first-party UI unusable (D-10/D-15).

## Device/test evidence

- None; documentation only.

---

# Handoff — 2026-11-03 (write diagnostic hardware pass)

## Branch / commit

- Branch: `main`
- Current head: the hardware-evidence commit; check `git log -1`.

## What happened

- The separate write diagnostic was installed on the owner's 6G by DFU with
  `--dfusend`; `--single` was not used.
- The owner reports **every screen stage PASS**: outer HFS+, test image, inner
  HFS+, inner catalog, inner journal, folder, create, append, readback, rename,
  truncate, delete and splash.
- The inner image was extracted after the run:
  - size 16,777,216 bytes;
  - SHA-256 `156060075eef60c463f421ce459c2138a00e1c43de44da802b77ce89c79ca0e8`;
  - `fsck_hfs -fn` (hfs-751) exit 0, "The volume HelikonInner appears to be OK.";
  - no `diag` folder remained;
  - outer `diskutil verifyVolume /Volumes/IPOD` exit 0.
- This is the first hardware proof of the inner-image writer sequence and the
  outer-metadata guard. It is not a library-write, power-cut, adapter or release
  claim.

## What was verified

- `fsck_hfs -fn` on the extracted inner image: exit 0.
- Inner root after run: only the normal HFS+ private entries; `diag` absent.
- `diskutil verifyVolume /Volumes/IPOD`: exit 0.
- The owner's on-screen report was all PASS.
- The outer volume still contains the 16 MiB `helikon-test.img`.

## What is next

1. Record the exact adapter/storage model for this 6G run if known; the current
   matrix still says `open question`.
2. Test a second run without recreating the inner image (the host test says it
   is reusable).
3. Add power-cut injection on the inner image, then a disposable library-write
   plan with backup/recovery.
4. Continue metadata parity and the wider macOS name corpus.

## What must not be touched

- Do not treat this one run as support for library writes or other devices.
- Do not write the outer HFS+ metadata or the library.
- Never use `--single`.
- Do not retry writes after a failed `fsck_hfs` or dirty journal.

## Open questions

- Adapter and storage model of the tested 6G (`open question` in the matrix).
- Behavior on a fragmented outer file (guard currently fails closed on
  metadata-fork overflow).
- D-05 cache/dirty-set budget and metadata parity.

## Device/test evidence

- 6G write-diagnostic run, 2026-11-03, owner-reported all PASS.
- Inner image SHA-256 `156060…`; `fsck_hfs -fn` exit 0; outer verify exit 0.
- Recorded in `docs/device-matrix.md` §2.2.

---

# Handoff — 2026-11-03 (separate write diagnostic image)

## Branch / commit

- Branch: `main`
- Current head: the write-diagnostic commit; check `git log -1`.

## What changed

- Added `bootloader/inner_image.c/.h`: maps a root-level outer HFS+ file's
  data-fork extents to a block device, rejects out-of-range writes, and guards
  against overlap with outer volume headers, journal, catalog/extents/
  allocation forks. The outer volume remains read-only.
- Added `bootloader/write_diag.c/.h`: finds `helikon-test.img`, probes the inner
  volume, replays its journal, and runs create folder/file, append, readback,
  rename, truncate and delete through the real writer. Reports PASS/FAIL.
- Added `bootloader/hardware/helikon_write_diag_hw.c`, a separate donor patch
  and `tools/build-write-diagnostic-bootloader.sh`. The image keeps the splash
  and the outer read-only path; `ata_write_sectors` is used only through the
  inner-image mapping.
- Refactored the writer's large work buffers from the stack into static BSS
  (the bootloader stack is only 8 KiB). `HFS_MAX_NODE_SIZE` is now 8192; the
  writer remains single-threaded.
- Built `bootloader-ipod6g-write-diag.ipod`: 109,320 bytes,
  SHA-256 `9e3881297d8a793d6c0589e9779f1d624685f001b6e1cf12814c06c8b7b2aa99`,
  `_moveend` `0x2203b300`, 19,712 bytes below `MOVE_AREA`.
- Added `tools/make-write-diag-image.sh` and `bootloader/WRITE_DIAG.md`.
- Host tests: inner-image mapping/guard (4), write-diagnostic outer/inner image
  test (twice, inner and outer fsck clean, no leftover `diag`).
- Total portable host tests: 107 (95 HFS+, 12 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/bootloader test` — 12/12 pass (8 diag + 4 inner-image).
- `make -C tests/image write-diag-test` — PASS twice; outer and extracted inner
  images fsck-clean; no `diag` folder left.
- `make -C tests/hfsplus test` / `sanitize` — 95/95.
- `make -C tests/image test` and `crash-test` — still pass.
- The write image builds, fits `MOVE_AREA`, and `mks5lboot --mkdfu-inst`
  accepts it as Classic 6G. No hardware run.

## What is next

1. Record one 6G run with `helikon-test.img`, capture the on-screen report,
   copy the inner image back and run `fsck_hfs -fn`. Do not claim support until
   it passes.
2. If it fails, preserve the inner image and stop; recreate it before another
   attempt.
3. Continue metadata parity, wider macOS corpus and power-loss matrices.

## What must not be touched

- Never use `--single`.
- Never write the outer HFS+ metadata or the library.
- Do not add a write path to the production read-only image.
- Do not retry writes after a failed `fsck_hfs` or dirty journal.

## Open questions

- Actual 6G report and inner-image fsck result.
- Outer metadata overlap guard behavior on a fragmented real catalog (it fails
  closed if metadata forks have overflow extents).
- D-05 cache/dirty-set budget and metadata parity.

## Device/test evidence

- No hardware run of the write image. Host outer/inner image test only.
- Artifact hash and `MOVE_AREA` headroom are recorded in `bootloader/WRITE_DIAG.md`.

---

# Handoff — 2026-11-03 (folders, rename, below-minimum insert)

## Branch / commit

- Branch: `main`
- Current head: the folder/rename/below-minimum commit; check `git log -1`.

## What changed

- `hfs_catalog_create_folder()` and `hfs_catalog_delete_folder()` create/delete
  folder records (type 1, thread type 3), update `folderCount`, parent valence
  and next CNID, and refuse to delete a non-empty folder with `NOT_EMPTY`.
- `hfs_catalog_rename()` renames or moves a file/folder, preserves the record
  payload, updates the thread record and old/new parent valence, and rejects a
  folder move into its own descendant.
- B-tree insertion now supports a key below a non-root leaf's first key by
  updating the parent index key chain. Index-node below-minimum insertion at
  depth >2 still fails closed.
- Tests: file/folder create/delete, rename/move, case-only rename, duplicate
  collision, folder-cycle rejection, and a below-minimum model insert.
- The disposable image test now creates a folder and file, renames both, and
  deletes them; `fsck_hfs -fn` is clean before and after.
- Total portable host tests: 103 (95 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 95/95 pass.
- `make -C tests/hfsplus sanitize` — 95/95 pass with ASan/UBSan.
- `make -C tests/image test` — folder/file create, append, macOS mount/read,
  rename, delete, both fsck runs clean.
- `make -C tests/image crash-test` — 127 create/append/truncate/delete cuts
  still fsck-clean.
- No device write and no write-capable bootloader image.

## What is next

1. Metadata parity (timestamps, BSD/Finder info) and the wider macOS corpus.
2. Index-node below-minimum insert at depth >2.
3. The separate write-diagnostic image (see the bootloader handoff).

## What must not be touched

- No write on the library or the only copy.
- No write-capable production bootloader; any write diagnostic is a separate
  image, DFU-only, and limited to a preallocated test file.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.

## Open questions

- D-05 cache/dirty-set budget.
- Metadata parity fields and timestamp policy.
- `:`/`/` mapping and wider macOS name corpus (T-038).

## Device/test evidence

- No new device run. The disposable-image and crash tests are host evidence.

---

# Handoff — 2026-11-03 (full crash matrix)

## Branch / commit

- Branch: `main`
- Current head: the full crash-matrix commit; check `git log -1`.

## What changed

- Extended `tests/image/hfs_crash_test.c` and `run-crash-macos.sh` from the
  create transaction to create, append (data + bitmap + metadata), truncate and
  delete.
- Each scenario prepares a clean base image, then copies it for every write-fail
  cut point. Recovery reopens the copy, replays the journal, re-probes the
  volume, verifies the expected file state and data pattern, and runs
  `fsck_hfs -fn`.
- The crash harness now distinguishes:
  - create: absent before commit, size 0 after replay;
  - append: size 0 before commit, 4096 bytes of 0x5a after replay;
  - truncate: 4096 bytes before commit, size 0 after replay;
  - delete: present before commit, absent after replay.
- The matrix covered 127 cuts: create 25, append 43, truncate 34, delete 25.
  Every recovery was fsck-clean and left the journal clean.

## What was verified

- `make -C tests/image crash-test` — all four scenarios pass; 127 cuts total.
- `make -C tests/image test` — disposable smoke test still passes.
- `make -C tests/hfsplus test` / `sanitize` — 91/91.
- No device write and no write-capable bootloader image.

## What is next

1. Rename cut matrix; partial-sector writes, fsync failures and power-loss
   timing.
2. T-038: wider macOS name corpus and the `:`/`/` mapping.
3. Write diagnostic; only after that, plan a device write with a verified
   backup and recovery.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- Do not weaken the crash test by removing fsck or accepting a dirty journal.

## Open questions

- D-05 cache/dirty-set budget.
- Partial-sector and fsync-failure modelling.
- `:`/`/` mapping and wider macOS name corpus (T-038).

## Device/test evidence

- No new device run. Crash injection ran on disposable host images only.
- `fsck_hfs` version hfs-751, macOS host.

---

# Handoff — 2026-11-03 (journal crash injection)

## Branch / commit

- Branch: `main`
- Current head: the crash-injection commit; check `git log -1`.

## What changed

- Added `tests/image/hfs_crash_test.c` and `run-crash-macos.sh`: for each cut
  point, copy a clean 16 MiB journaled HFS+ image, fail the nth device write
  during a create transaction, reopen, replay the journal, and run
  `fsck_hfs -fn`.
- The recovery path re-probes the volume after replay so the in-memory catalog
  fork reflects the applied volume header. This fixed a recovery `FORMAT` that
  only appeared when the journal was dirty at open.
- The harness distinguishes cuts before the journal commit header (file absent)
  from cuts during replay (file present after replay) and verifies size 0.
- The create matrix covered 25 cuts; every recovery was fsck-clean and the
  journal was clean afterwards. The 26th cut succeeded without a fault.
- Portable host tests remain 99 (91 HFS+, 8 bootloader); the smoke image test
  still passes.

## What was verified

- `make -C tests/image crash-test`:
  - cuts 1-6: commit-cut, recovered `present=0`;
  - cuts 7-25: commit-cut during replay, recovered `present=1 size=0`;
  - cut 26: `commit-ok writes=25`;
  - `fsck_hfs -fn` clean after every cut.
- `make -C tests/image test` still passes.
- `make -C tests/hfsplus test` / `sanitize` — 91/91.
- No device write and no write-capable bootloader image.

## What is next

1. Extend the cut matrix to append (data + bitmap + metadata), truncate,
   delete and rename; then add partial-write/fsync-failure cases.
2. T-038: wider macOS name corpus and the `:`/`/` mapping.
3. Write diagnostic; only after that, plan a device write with a verified
   backup and recovery.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- Do not weaken the crash test by removing fsck or accepting a dirty journal.

## Open questions

- D-05 cache/dirty-set budget.
- Partial-sector and fsync-failure modelling.
- `:`/`/` mapping and wider macOS name corpus (T-038).

## Device/test evidence

- No new device run. Crash injection ran on disposable host images only.
- `fsck_hfs` version hfs-751, macOS host.

---

# Handoff — 2026-11-03 (NUL-prefixed private names)

## Branch / commit

- Branch: `main`
- Current head: the private-name fix commit; check `git log -1`.

## What changed

- Directory iteration no longer aborts on macOS private records whose HFS+
  names contain NUL code units. `hfs_utf16be_to_utf8_escaped()` maps NUL to
  U+2400 and invalid surrogates to U+FFFD.
- `struct hfs_entry` gains `name_escaped`; such entries are also marked
  `unsupported`, so they are listed rather than hidden (paper §8.3).
- `hfs_catalog_lookup` now escapes/skips such a record and continues, so a
  normal path after it is still found. `hfs_catalog_lookup_ascii` was already
  raw-key based.
- Added a fixture record with a NUL name under `.firmware`, plus escaped-decode
  tests; the macOS-created image now lists all root records including the
  `.HFS+ Private Data` entry.
- Total portable host tests: 99 (91 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 91/91 pass.
- `make -C tests/hfsplus sanitize` — 91/91 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 8/8 pass.
- `make -C tests/image test` — disposable image create/append/mount/delete and
  both `fsck_hfs -fn` runs still pass.
- Dumping the macOS-created image now completes and prints the escaped private
  directory instead of returning `NAME_INVALID`.

## What is next

1. T-038: wider macOS name corpus and the `:`/`/` mapping.
2. Power-loss/crash injection around `hfs_txn_commit` and journal replay.
3. Write diagnostic; only after that, plan a device write with a verified
   backup and recovery.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- Do not hide an unsupported on-disk entry; escaped names must stay visible.

## Open questions

- D-05 cache/dirty-set budget.
- `:`/`/` mapping and wider macOS name corpus (T-038).
- Fork-of-fork overflow and transaction-block budget for growth.

## Device/test evidence

- No new device run. The disposable-image run is host evidence recorded above.

---

# Handoff — 2026-11-03 (disposable-image fsck validation)

## Branch / commit

- Branch: `main`
- Current head: the disposable-image test commit; check `git log -1`.

## What changed

- Added `tests/image/`: a macOS-only disposable-image test. `run-macos.sh`
  creates its own 16 MiB journaled HFS+ image under `mktemp -d`, never accepts
  an existing image or device path, and cleans up on exit.
- `hfs_image_test.c` opens the image through `hfs_volume_probe`, opens and
  replays the journal if needed, creates a file, appends 4096 bytes through
  `hfs_txn`, reads it back through the read core, and later truncates and
  deletes it. It checks file/free-block counts at each step.
- The driver runs `fsck_hfs -fn` on the raw device after create and after
  delete, mounts the image in macOS to verify the 4096-byte content, and fails
  if `fsck_hfs` reports a repair.
- Raised `HFS_JOURNAL_MAX_BLOCK_LIST` from 4096 to 16384: macOS created an
  8192-byte block list on the 16 MiB test image. Larger lists still fail
  closed until a scratch-buffer design exists.
- Recorded T-039: directory iteration aborts on the macOS NUL-prefixed
  `.HFS+ Private Data` record. The image test uses the ASCII lookup probe, and
  the reader issue is documented rather than hidden.

## What was verified

- `make -C tests/image test`:
  - create: file_count 2 -> 3, free_blocks 3868 -> 3867;
  - `fsck_hfs -fn`: "appears to be OK";
  - macOS mount: 4096 bytes of 0x5a read back correctly;
  - delete: file_count 3 -> 2, free_blocks 3867 -> 3868;
  - `fsck_hfs -fn`: "appears to be OK" again.
- `make -C tests/hfsplus test` — 90/90 pass; `sanitize` 90/90; bootloader 8/8.
- No device write and no write-capable bootloader image. The test writes only
  its own temporary image.

## What is next

1. T-039: handle NUL-prefixed private names in directory iteration without
   aborting or hiding unrelated entries.
2. T-038: wider macOS name corpus and the `:`/`/` mapping.
3. Power-loss/crash injection around `hfs_txn_commit` and journal replay.
4. Write diagnostic; only after that, plan a device write with a verified
   backup and recovery.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- Do not turn `tests/image` into an arbitrary-image writer; it creates its own
  disposable image and refuses device paths.

## Open questions

- D-05 cache/dirty-set budget.
- `:`/`/` mapping and wider macOS name corpus (T-038).
- NUL-prefixed private-name representation (T-039).
- Fork-of-fork overflow and transaction-block budget for growth.

## Device/test evidence

- No new device run. The disposable-image run is host evidence recorded above.
- `fsck_hfs` version hfs-751, macOS host. The image was created with
  `hdiutil create -size 16m -fs HFS+J -layout NONE`.

---

# Handoff — 2026-11-03 (HFS+ name normalization)

## Branch / commit

- Branch: `main`
- Current head: the HFS+ normalization commit; check `git log -1`.

## What changed

- `hfs_unicode_normalize_name()` implements the HFS+ normalization variant:
  full canonical decomposition from Unicode 2.1.9, exception ranges
  U+2000–U+2FFF, U+F900–U+FAFF and U+2F800–U+2FAFF left composed, Hangul
  syllables decomposed algorithmically, and combining marks reordered by
  canonical combining class.
- The generated table now includes 931 fully expanded decompositions and 117
  CCC ranges alongside the fold table. `tools/gen-hfs-unicode-table.py`
  regenerates all three from the pinned UnicodeData-2.1.9.txt.
- Catalog create/delete/append/truncate normalize their name argument, so
  composed and decomposed input address the same catalog key. A composed name
  that duplicates a decomposed name returns `HFS_ERR_BUSY`.
- Added tests for composed/decomposed equivalence, Hangul, canonical ordering,
  HFS+ exception ranges, overlong results, buffer limits, duplicate detection
  and lookups through normalized keys.
- Total host tests: 98 (90 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 90/90 pass.
- `make -C tests/hfsplus sanitize` — 90/90 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 8/8 pass.
- A local 4 MiB HFS+ image created with macOS `hdiutil` and populated through
  macOS stored `Café` as `Cafe` + U+0301, `가` as U+1100 U+1161, U+2126 and
  U+F900 unchanged, and `a` + U+0301 + U+0323 as `a` + U+0323 + U+0301. The
  Helikon reader dumped the same names. This is host evidence, not a device
  run and not a committed fixture.
- No device write and no write-capable bootloader image.

## What is next

1. Broader macOS name corpus (CJK compatibility, emoji, length boundaries) and
   the `:`/`/` mapping (T-038).
2. Disposable HFS+ image + `fsck_hfs -n` zero repairs (T-032).
3. Write diagnostic.
4. Below-minimum B-tree insert remains open; fork-of-fork growth remains
   `UNSUPPORTED`.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- Do not copy Apple's APSL table or a GPL-2.0-only table; use the generator.

## Open questions

- D-05 cache/dirty-set budget.
- `:`/`/` mapping and the wider macOS name corpus (T-038).
- Fork-of-fork overflow and transaction-block budget for growth.

## Device/test evidence

- No new device run. The writer is host-only. The macOS image differential is
  host evidence recorded above.

---

# Handoff — 2026-11-03 (FastUnicodeCompare and D-06)

## Branch / commit

- Branch: `main`
- Current head: the FastUnicodeCompare commit; check `git log -1`.

## What changed

- D-06 is accepted: Apple `FastUnicodeCompare` for catalog key order, lookup,
  create, delete and duplicate detection; option (a), with explicit refusal of
  composed names until the HFS+ decomposer exists.
- Added `hfs_catalog_compare.c/.h`: native catalog key ordering (parent ID
  first, then FastUnicodeCompare over UTF-16BE names) with malformed-key
  fallback.
- Added `hfs_unicode_data.c`, generated by `tools/gen-hfs-unicode-table.py`
  from the pinned UnicodeData-2.1.9.txt. The table matches Apple's published
  table for all BMP code units but is generated from Unicode data, not copied
  from Apple's APSL table or a GPL-2.0-only source.
- `hfs_unicode_name_is_canonical()` rejects NUL, unpaired surrogates, names
  with no non-ignorable unit, and code points with canonical decompositions
  (including Hangul syllables). Catalog create/delete/append/truncate apply the
  guard.
- Catalog write tests now use the native comparator; added a case-insensitive
  duplicate test and composed/decomposed name assertions. New
  `test_catalog_compare.c` covers key ordering and malformed keys.
- Total host tests: 92 (84 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 84/84 pass.
- `make -C tests/hfsplus sanitize` — 84/84 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 8/8 pass.
- Generated folding table equals Apple's table for all 65,536 BMP code units
  in the verification script; case, ignorable, Greek, circled-letter and
  surrogate cases are covered by tests.
- Composed U+00E9 is refused; decomposed `e` + U+0301 is accepted.
- No device write and no write-capable bootloader image.

## What is next

1. HFS+ canonical decomposition and the macOS-created name corpus (T-038).
2. Disposable image + `fsck_hfs -n`.
3. Write diagnostic.
4. Below-minimum B-tree insert remains open; fork-of-fork growth remains
   `UNSUPPORTED`.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- Do not accept composed names or copy Apple's APSL table; use the generator.

## Open questions

- D-05 cache/dirty-set budget.
- HFS+ decomposition exception ranges and macOS name fixtures (T-038).
- Fork-of-fork overflow and transaction-block budget for growth.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree fork growth)

## Branch / commit

- Branch: `main`
- Current head: the fork-growth commit; check `git log -1`.

## What changed

- `hfs_btree_grow()` allocates one volume block through the bitmap, extends
  the catalog/extents fork in the primary and alternate volume headers, marks a
  new node free in the B-tree header map and increments `totalNodes`.
- `alloc_node()` calls growth automatically when the free-node map is empty.
- `store_header()` now writes `totalNodes`, which the reader previously derived
  from the fork size.
- Growth requires `node_size == block_size` and a free inline extent slot in the
  volume-header fork; otherwise it returns `UNSUPPORTED`.
- Added `test_btree_grow.c`: 2-node catalog tree, one free volume block,
  growth, and a persisted header/alternate/bitmap check.
- Total host tests: 77 (69 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 69/69 pass.
- `make -C tests/hfsplus sanitize` — 69/69 pass with ASan/UBSan.
- Catalog fork grows from 2 to 3 nodes; primary and alternate headers both
  show size 1536, 3 blocks, third extent at block 41; the bitmap marks block 41
  used; the tree header reports 3 total and 1 free node.
- No device write and no write-capable bootloader image.

## What is next

1. D-06 / FastUnicodeCompare.
2. Disposable image + `fsck_hfs`.
3. Write diagnostic.
4. Below-minimum insert and fork-of-fork overflow remain open.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Fork-of-fork overflow and transaction-block budget for growth.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (multiple extents-overflow records)

## Branch / commit

- Branch: `main`
- Current head: the multi-overflow-record commit; check `git log -1`.

## What changed

- Append now writes multiple extents-overflow records, eight extents per record,
  advancing the start block by the blocks covered by each record.
- Truncate walks the overflow records by start block, frees each extent, clears
  each record and stops when the file's total blocks are accounted for.
- The catalog test fragments the bitmap to 20 free blocks, appends 20 blocks and
  verifies eight inline extents plus two overflow records (8 extents and 4
  extents). Truncate frees all 20 and clears both records.
- Total host tests: 76 (68 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 68/68 pass.
- `make -C tests/hfsplus sanitize` — 68/68 pass with ASan/UBSan.
- Overflow record keys advance from startBlock 8 to 16; data and free-block
  counts are checked.
- No device write and no write-capable bootloader image.

## What is next

1. Fork growth.
2. D-06 / FastUnicodeCompare.
3. Disposable image + `fsck_hfs`.
4. Write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Fork-growth strategy and transaction-block budget.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (extents overflow)

## Branch / commit

- Branch: `main`
- Current head: the extents-overflow commit; check `git log -1`.

## What changed

- Added `hfs_extents_write.c`: one extents-overflow record for data-fork
  extents 8–15, keyed by fork type, file ID and start block. Supports
  set/get/clear through the extents B-tree.
- Append now puts extents beyond the first eight into that overflow record.
- Truncate reads the overflow record, frees its blocks, clears the record and
  frees the inline extents.
- The catalog test now fragments the bitmap to only ten free blocks, creates a
  second file, appends 10 blocks, and verifies eight inline extents plus two
  overflow extents (`66/1`, `68/1`), data and truncate cleanup.
- Total host tests: 76 (68 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 68/68 pass.
- `make -C tests/hfsplus sanitize` — 68/68 pass with ASan/UBSan.
- The extents B-tree record is inserted and deleted; overflow blocks are
  freed and `freeBlocks` restored.
- No device write and no write-capable bootloader image.

## What is next

1. Multiple overflow records / more than 16 extents.
2. Fork growth.
3. D-06 / FastUnicodeCompare.
4. Disposable image + `fsck_hfs`.
5. Write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Multi-overflow-record and fork-growth strategy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (append to non-empty files)

## Branch / commit

- Branch: `main`
- Current head: the non-empty append commit; check `git log -1`.

## What changed

- Append now accepts `offset == logicalSize` and merges new blocks into the
  existing inline extents.
- Existing extents are parsed and validated against `totalBlocks`; a fork with
  overflow extents returns `UNSUPPORTED`.
- New blocks are staged before bitmap/header/record metadata.
- The catalog test appends 1,200 bytes, then 600 more at offset 1,200, producing
  extents `41/1` and `43/4` with logical size 1,800 and five blocks.
- Total host tests: 76 (68 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 68/68 pass.
- `make -C tests/hfsplus sanitize` — 68/68 pass with ASan/UBSan.
- Data and free-block counts are checked after both appends and after truncate.
- No device write and no write-capable bootloader image.

## What is next

1. Extents-overflow records.
2. Fork growth.
3. D-06 / FastUnicodeCompare.
4. Disposable image + `fsck_hfs`.
5. Write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Extents-overflow and fork-growth strategy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (multi-extent data append/truncate)

## Branch / commit

- Branch: `main`
- Current head: the multi-extent append/truncate commit; check `git log -1`.

## What changed

- Append now finds individual free blocks, stages all data first, allocates each
  block through the bitmap, merges contiguous allocations into up to eight
  inline extents and rewrites the file record.
- Truncate frees every inline extent, not just one.
- The catalog test now forces a fragmented allocation (block 42 reserved) and
  appends 1,200 bytes across blocks 41, 43 and 44, producing extents 41/1 and
  43/2. It verifies data, free-block counts and truncate restoration.
- Total host tests: 76 (68 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 68/68 pass.
- `make -C tests/hfsplus sanitize` — 68/68 pass with ASan/UBSan.
- Data blocks are staged before bitmap/header/record metadata.
- No device write and no write-capable bootloader image.

## What is next

1. Extents-overflow records for files over eight extents.
2. Fork growth.
3. D-06 / FastUnicodeCompare.
4. Disposable image + `fsck_hfs`.
5. Write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Extents-overflow and fork-growth strategy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (one-extent data append/truncate)

## Branch / commit

- Branch: `main`
- Current head: the data-fork append/truncate commit; check `git log -1`.

## What changed

- Added `hfs_bitmap_find()` to locate a free run without staging metadata.
- Added `hfs_catalog_append_file()`: stages data blocks first, allocates the
  same contiguous run, writes one inline extent, updates logical size/total
  blocks and rewrites the file record.
- Added `hfs_catalog_truncate_file()`: frees the one inline extent and clears
  the fork.
- Extended the catalog test to append 600 bytes (two blocks), verify data and
  bitmap/free-block changes, then truncate and restore the free count.
- Total host tests: 76 (68 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 68/68 pass.
- `make -C tests/hfsplus sanitize` — 68/68 pass with ASan/UBSan.
- Data blocks are staged before bitmap/header/record metadata in the
  transaction.
- Append allocates blocks 41–42 on the test image; truncate frees them and
  restores `freeBlocks` 86.
- No device write and no write-capable bootloader image.

## What is next

1. Multi-extent/overflow files.
2. Fork growth.
3. D-06 / FastUnicodeCompare.
4. Disposable image + `fsck_hfs`.
5. Write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Extents/overflow and fork-growth strategy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (catalog parent valence)

## Branch / commit

- Branch: `main`
- Current head: the parent-valence commit; check `git log -1`.

## What changed

- Added `hfs_btree_find()` for exact key lookup.
- Empty-file catalog create now increments the parent folder valence through
  the folder’s thread record; delete decrements it.
- The catalog test seeds a root folder + thread, creates a file, verifies
  valence 1, file/thread records, file count/CNID, then deletes and verifies
  valence 0.
- Total host tests: 76 (68 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 68/68 pass.
- `make -C tests/hfsplus sanitize` — 68/68 pass with ASan/UBSan.
- No device write and no write-capable bootloader image.

## What is next

1. Data-fork allocation/write/truncate.
2. Inline extents, then extents/overflow mutation.
3. Fork growth.
4. D-06 / FastUnicodeCompare.
5. Disposable image + `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Data-before-metadata ordering for staged data blocks.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (empty-file catalog create/delete)

## Branch / commit

- Branch: `main`
- Current head: the empty-file catalog write commit; check `git log -1`.

## What changed

- Added `hfs_volume_write.c`: stages primary/alternate volume headers with
  file/folder counts, next CNID and incremented write count.
- Added `hfs_catalog_write.c`: builds native file and thread records, allocates
  a CNID, inserts both through the B-tree in one journal transaction, updates
  the volume header, and deletes both records again.
- Added transaction overlay support: staged blocks are visible to later
  tree reads and can be replaced within one transaction. This fixes multi-record
  catalog transactions that revisit the same leaf/header.
- Added one host test: create an empty file (file+thread records, CNID 16,
  file count 2) and delete it, with volume/tree header checks.
- Total host tests: 76 (68 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 68/68 pass.
- `make -C tests/hfsplus sanitize` — 68/68 pass with ASan/UBSan.
- The catalog test verifies file/thread records, key order, CNID allocation,
  file count and cleanup after delete.
- No device write and no write-capable bootloader image.

## What failed

- The first create attempt returned `HFS_ERR_BUSY`: the file and thread inserts
  staged the same leaf twice, and the transaction engine rejected the duplicate.
  Added a staged-block overlay and replace path; the second insert now sees the
  first insert and the leaf is staged once.
- The overlay initially used fork offsets as volume offsets; fixed with
  `hfs_fork_overlay()`.

## What is next

1. Parent folder valence updates.
2. Data-fork allocation/write/truncate and extents/overflow.
3. Fork growth when the free-node map is exhausted.
4. D-06 / FastUnicodeCompare before real catalog keys.
5. Disposable HFS+ image + `fsck_hfs`, then the write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Parent valence, fork growth and extents strategy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree root collapse)

## Branch / commit

- Branch: `main`
- Current head: the root-collapse commit; check `git log -1`.

## What changed

- When the root index has the deleted leaf as its only child and the leaf is
  still nonempty, collapse the root to that leaf: depth becomes 1, the old root
  is freed and the leaf remains root.
- Empty-parent collapse and fork growth remain unimplemented and fail closed.
- Total host tests: 75 (67 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 67/67 pass.
- `make -C tests/hfsplus sanitize` — 67/67 pass with ASan/UBSan.
- The root-collapse branch is defensive; the 20-record model test does not force
  it, so it remains untested by a dedicated case.
- No device write and no write-capable bootloader image.

## What is next

1. Empty-parent collapse.
2. Fork growth.
3. Catalog records + FastUnicodeCompare (D-06).
4. Extents/overflow, disposable image, `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Root-collapse test and fork-growth integration.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree previous-sibling redistribution)

## Branch / commit

- Branch: `main`
- Current head: the previous-sibling redistribution commit; check `git log -1`.

## What changed

- Added previous-sibling merge when an underfull leaf has no next sibling (or
  cannot merge/redistribute with it).
- Added previous-sibling redistribution of the previous leaf’s last record.
- The 20-record model test now leaves a one-record last leaf so the previous
  path is exercised.
- Total host tests: 75 (67 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 67/67 pass.
- `make -C tests/hfsplus sanitize` — 67/67 pass with ASan/UBSan.
- The leaf chain remains sorted and fully reachable after next/previous merge
  and redistribution.
- No device write and no write-capable bootloader image.

## What is next

1. Parent/root collapse.
2. Fork growth.
3. Catalog records + FastUnicodeCompare (D-06).
4. Extents/overflow, disposable image, `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Root-collapse policy and fork-growth integration.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree next-sibling redistribution)

## Branch / commit

- Branch: `main`
- Current head: the next-sibling redistribution commit; check `git log -1`.

## What changed

- When an underfull leaf cannot merge with its next sibling, move the next
  leaf’s first record into the underfull leaf.
- Update the next leaf’s parent key to its new first key.
- Stage the parent, current leaf and next leaf in one transaction.
- The 20-record model test still passes with a sorted, fully reachable tree.
- Total host tests: 75 (67 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 67/67 pass.
- `make -C tests/hfsplus sanitize` — 67/67 pass with ASan/UBSan.
- No device write and no write-capable bootloader image.

## What is next

1. Previous-sibling redistribution.
2. Parent/root collapse.
3. Fork growth.
4. Catalog records + FastUnicodeCompare (D-06).
5. Extents/overflow, disposable image, `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Merge/redistribution thresholds and root-collapse policy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree leaf merge)

## Branch / commit

- Branch: `main`
- Current head: the B-tree leaf-merge commit; check `git log -1`.

## What changed

- Added underfull-leaf detection and next-sibling merge.
- Merge appends the next leaf’s records when they fit, updates the leaf chain,
  frees the right node, removes its parent index record and propagates the
  parent’s first key when needed.
- The 20-record tree model test still covers insert, multi-level split, first
  and minimum key deletes, empty-leaf unlink and missing-key handling.
- Total host tests: 75 (67 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 67/67 pass.
- `make -C tests/hfsplus sanitize` — 67/67 pass with ASan/UBSan.
- Deletes continue to leave a sorted, fully reachable leaf chain.
- No device write and no write-capable bootloader image.

## What is next

1. Redistribution when merged records do not fit.
2. Parent/root collapse.
3. Fork growth when the free-node map is exhausted.
4. Catalog records + FastUnicodeCompare (D-06).
5. Extents/overflow, disposable image, `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Merge/redistribution thresholds and root-collapse policy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree first-record delete)

## Branch / commit

- Branch: `main`
- Current head: the first-record-delete commit; check `git log -1`.

## What changed

- Added recursive parent-key propagation for first-record deletion.
- Deleting a first record of a leaf now updates the parent index key, and
  propagates to the grandparent when the parent is the first child.
- Empty-leaf removal can now remove a child-0 leaf when the parent has more than
  one record and propagate the parent’s new first key.
- Extended the tree model test to delete first records 13 and 10 (including the
  tree minimum) successfully.
- Total host tests: 75 (67 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 67/67 pass.
- `make -C tests/hfsplus sanitize` — 67/67 pass with ASan/UBSan.
- The 20-record tree model test covers insert, multi-level split, eight deletes
  including first/minimum keys, empty-leaf unlink, missing-key `NOT_FOUND`.
- No device write and no write-capable bootloader image.

## What is next

1. B-tree merge/redistribution for underfull nodes.
2. Parent/root collapse when an index node loses its last record.
3. Fork growth when the free-node map is exhausted.
4. Catalog records + FastUnicodeCompare (D-06).
5. Extents/overflow, disposable image, `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Merge/redistribution thresholds and root-collapse policy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree partial delete)

## Branch / commit

- Branch: `main`
- Current head: the B-tree partial-delete commit; check `git log -1`.

## What changed

- Added `hfs_btree_delete`: traverse to the leaf, remove a non-first record,
  unlink/free an empty non-root leaf, update sibling links, remove its parent
  record and update the header.
- First-record deletion of a multi-record non-root node, parent/root collapse
  and merge/redistribution fail closed or remain unimplemented.
- Extended the 20-record tree model test with six successful deletes, a
  first-record `UNSUPPORTED` case and a missing-key `NOT_FOUND` case.
- Total host tests: 75 (67 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 67/67 pass.
- `make -C tests/hfsplus sanitize` — 67/67 pass with ASan/UBSan.
- The model test now covers insert, multi-level split, leaf-chain order,
  non-first delete, empty-leaf unlink, fail-closed first-record delete and
  missing-key delete.
- No device write and no write-capable bootloader image.

## What is next

1. First-record delete with parent-key propagation.
2. Merge/redistribution and parent/root collapse.
3. Fork growth when the free-node map is exhausted.
4. Catalog records + FastUnicodeCompare (D-06).
5. Extents/overflow, disposable image, `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Merge/redistribution thresholds and root-collapse policy.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree tree insertion)

## Branch / commit

- Branch: `main`
- Current head: the B-tree tree-insert commit; check `git log -1`.

## What changed

- Added `firmware/hfs/hfs_btree_tree.c/.h`: root-to-leaf traversal, free-node
  allocation from the header map, leaf/index split, parent record insertion,
  recursive split, new-root creation, leaf/index sibling link updates and
  header staging.
- Added a 20-record model test that forces multiple leaf/index splits and walks
  the leaf chain in order.
- Below-minimum keys fail closed with `HFS_ERR_UNSUPPORTED` rather than leaving
  a stale parent key.
- Total host tests: 75 (67 HFS+, 8 bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 67/67 pass.
- `make -C tests/hfsplus sanitize` — 67/67 pass with ASan/UBSan.
- The model test inserts 20 records, forces a multi-level tree, and checks
  `leafRecords`, depth, first/last leaf and sorted leaf-chain keys.
- No device write and no write-capable bootloader image.

## What failed

- The first traversal used the wrong comparison direction (`cmp >= 0` instead of
  `cmp <= 0`), routing every key above an index key to the first child. The model
  test caught the resulting unsorted root/leaf chain before commit.
- The split helper initially forgot to initialize the empty right node's free
  offset.
- Below-minimum insertion and fork growth are not implemented.

## What is next

1. B-tree delete/merge/redistribution.
2. Below-minimum key update path.
3. Catalog/extents fork growth when the free-node map is exhausted.
4. D-06 / FastUnicodeCompare before catalog keys.
5. Catalog records, disposable image, `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Delete/merge policy and fork growth.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree node split)

## Branch / commit

- Branch: `main`
- Current head: the B-tree split-helper commit; check `git log -1`.

## What changed

- Added `hfs_btree_node_split()`: splits a node at a record index, rebuilds the
  reverse offset tables, preserves/links fLink and bLink, and returns the right
  node's first key as the separator.
- Added 2 split tests (valid split and bad split indices). 7 node tests total.
- Total host tests: 74 (66 HFS+, 8 bootloader), sanitizer-clean.
- Updated state/testing/tasks/write-engine docs.

## What was verified

- `make -C tests/hfsplus test` — 66/66 pass.
- `make -C tests/hfsplus sanitize` — 66/66 pass with ASan/UBSan.
- Split test covers record movement, left/right counts, fLink/bLink and the
  separator key.
- No device write and no write-capable bootloader image.

## What failed

- The first split test used records without a key-length prefix; the separator
  extraction correctly rejected them. Tests now use valid B-tree records.
- The split helper initially forgot to initialize the empty right node's
  free-space offset; fixed.
- Tree-level parent insertion and root split are still missing.

## What is next

1. Free-node allocation from the header map record.
2. Tree traversal + parent insertion + root split (T-034).
3. Delete with merge.
4. D-06 / FastUnicodeCompare before catalog keys.
5. Catalog/extents mutation, disposable image, `fsck_hfs`, write diagnostic.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Tree-level split/merge policy and free-node bitmap layout.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (B-tree node and fork staging)

## Branch / commit

- Branch: `main`
- Current head: the B-tree-node/fork-staging commit; check `git log -1`.

## What changed

- Added `firmware/hfs/hfs_btree_write.{c,h}`: node-level record insert/delete,
  reverse offset-table maintenance, free-space checks and `NO_SPACE`.
- Added `firmware/hfs/hfs_fork_io.{c,h}`: stage full or partial fork blocks into
  a transaction, with read-modify-write for partial blocks; inline extents only.
- Added 5 B-tree node tests and 4 fork-staging tests. Total host tests: 72
  (64 HFS+, 8 bootloader), sanitizer-clean.
- Updated state/testing/tasks/write-engine/file-map docs.

## What was verified

- `make -C tests/hfsplus test` — 64/64 pass.
- `make -C tests/hfsplus sanitize` — 64/64 pass with ASan/UBSan.
- B-tree node insert/delete middle/end, no-space and bad arguments.
- Fork full-block, partial read-modify-write, bounds and overflow-unsupported.
- No device write and no write-capable bootloader image.

## What failed

- The first node insert/delete helpers forgot to write the new record count into
  the node descriptor; tests caught it before commit.
- The B-tree helpers use a temporary 32 KiB node buffer; a bootloader integration
  must replace this with a static/checked buffer.
- Tree-level split/merge, free-node allocation, leaf links and root changes are
  still missing.

## What is next

1. **T-034** — tree-level B-tree insert/delete with split/merge, free-node map,
   leaf links and root changes.
2. **D-06** — Unicode policy / FastUnicodeCompare before catalog keys.
3. **T-007/T-008** — catalog and extents mutation on the tree engine.
4. **T-004** — disposable HFS+ image fixture with `fsck_hfs -n`.
5. Then the write diagnostic and only after those pass a device write with a
   verified backup/recovery plan.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Allocation-file overflow extent support.
- Tree-level split/merge policy and free-node bitmap layout.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (allocation bitmap)

## Branch / commit

- Branch: `main`
- Current head: the allocation-bitmap commit; check `git log -1`.

## What changed

- Added `firmware/hfs/hfs_bitmap.{c,h}`: first-fit allocation, free, free-block
  count updates, primary/alternate volume-header staging and `NO_SPACE`.
- Added 5 bitmap host tests. Total host tests: 63 (55 HFS+, 8 bootloader),
  sanitizer-clean.
- Updated state/testing/tasks/write-engine/file-map docs.

## What was verified

- `make -C tests/hfsplus test` — 55/55 pass.
- `make -C tests/hfsplus sanitize` — 55/55 pass with ASan/UBSan.
- Bitmap tests cover first-fit alloc, two-block alloc, free, no-space and bad
  ranges. The staged bitmap and headers are inspected; journal/txn commit is
  tested separately.
- No device write and no write-capable bootloader image.

## What failed

- The first bitmap test checked the in-memory image before applying the staged
  transaction blocks; fixed the test to apply staged blocks.
- `hfs_bitmap_free` conflated out-of-range and non-free-block errors; split into
  `ARGUMENT` and `FORMAT`.
- The allocation file's overflow extents are not supported yet; a fragmented
  allocation file returns `UNSUPPORTED`.

## What is next

1. **T-034** — generic HFS+ B-tree mutation engine (insert/delete, split/merge,
   free-node map, leaf links) with a key-comparison callback.
2. **D-06** — Unicode normalization and `FastUnicodeCompare` policy. Catalog
   insertion cannot be correct without it; byte order is not a safe substitute.
3. **T-007/T-008** — catalog and extents mutation on the generic engine.
4. **T-004** — disposable HFS+ image fixture with `fsck_hfs -n`.
5. Then the write diagnostic and, only after those pass, a device write with a
   verified backup/recovery plan.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No byte-order substitution for HFS+ catalog keys.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode policy / FastUnicodeCompare.
- Allocation-file overflow extent support.
- Write diagnostic test-image placement and verification.

## Device/test evidence

- No new device run. The writer is host-only.

---

# Handoff — 2026-11-03 (journal and transaction engine code)

## Branch / commit

- Branch: `main`
- Current head: the journal/transaction implementation commit; check
  `git log -1`.

## What changed

- Added `firmware/hfs/hfs_journal.{c,h}`: Apple HFS+ journal header and
  block-list validation, checksum verification, circular wrap, append/commit of
  one transaction, idempotent replay and clean marking.
- Added `firmware/hfs/hfs_txn.{c,h}`: bounded staged-block transaction engine
  that commits through the journal and applies by replay.
- Added `HFS_ERR_NO_SPACE`, `HFS_ERR_READ_ONLY`, `HFS_ERR_BUSY`,
  `HFS_ERR_POWER` and `HFS_ERR_NOT_EMPTY`.
- Added 8 journal host tests and 7 transaction host tests.
- Updated test/state/handoff/task docs. 58 host tests total (50 HFS+, 8
  bootloader), sanitizer-clean.

## What was verified

- `make -C tests/hfsplus test` — 50/50 pass.
- `make -C tests/hfsplus sanitize` — 50/50 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 8/8 pass.
- Journal tests cover clean open, append/replay, corrupt checksum, wrapped
  transaction, no-space, bad target, corrupt header and dirty replay failure.
- Transaction tests cover one/two-block commit, abort, duplicate sector, block
  limit, dirty-journal refusal and bad block size.
- No device write and no write-capable bootloader image.

## What failed

- The first journal implementation used a zero-based wrap and would have moved
  the cursor into the journal header; fixed with a sector-aware wrap helper.
- The initial test set did not cover a wrapped transaction; added.
- The writer is **not complete**: catalog/bitmap/extents mutation, file
  operations and the write diagnostic bootloader do not exist.

## What is next

1. Implement the allocation bitmap read/modify/stage path.
2. Implement catalog B-tree insert/update/delete, starting with the
   no-split/empty-file case and then splits/merges.
3. Implement extents/overflow mutation.
4. Add a disposable HFS+ image fixture and `fsck_hfs` gate.
5. Only then build a write diagnostic that targets a disposable image file on
   the iPod (outer HFS+ volume read-only), never the library.
6. Bootloader path resumes afterwards; D-01/D-02/D-20 remain open.

## What must not be touched

- No write on the library or the only copy.
- No write-capable bootloader image until the writer passes host and disposable
  image tests.
- No journal deletion/truncation/zeroing or dirty-journal "clean" claim.
- No `--single` on the bootloader path.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode normalization/FastUnicodeCompare policy.
- Catalog split/merge implementation order.
- Write diagnostic test-image placement and verification.

## Device/test evidence

- No new device run. The diagnostic runs remain owner-reported in
  [`device-matrix.md`](device-matrix.md) §2.1. The writer is host-only.

---

# Handoff — 2026-11-03 (journal decisions and writer interface)

## Branch / commit

- Branch: `main`
- Current head: the journal-decision/writer-interface commit; check
  `git log -1`.

## What changed

- D-03 accepted: journaled HFS+ writes are the required write path; a
  non-journaled writer is not the product path and may only be a
  disposable-volume prototype.
- D-04 accepted: Helikon-OS owns journal replay; a dirty/invalid journal mounts
  read-only, replay is idempotent and must finish before the first write.
- T-001 marked implemented.
- T-002 marked implemented: §10 of `docs/hfsplus-write-engine.md` freezes the
  transaction interface, commit protocol, replay contract, error taxonomy,
  transaction boundaries and invariant mapping.
- T-009 marked implemented: §6.5–§6.7 document the Apple HFS+ journal format,
  commit/clean/replay algorithm, validation rules and failure handling.
- `agents.md` settled/open decision lists updated.
- The diagnostic read probe is intentionally skipped; `-8` is correct because
  `/rockbox.ipod` is not a disk file on this iPod.

## What was verified

- Documentation only; no writer code or device write.
- The writer interface references I-03, I-05 and I-15 and keeps one writer,
  journaled commit, idempotent replay and `NO_SPACE` before media writes.
- The diagnostic hardware evidence and image hash are unchanged.

## What is next

1. **T-007** — catalog B-tree mutation design.
2. **T-008** — allocation bitmap and extents/overflow design.
3. **T-004** — writer fixture plan.
4. Decide **D-05** (cache/dirty-set limits) and **D-06** (Unicode policy) before
   implementation.
5. **T-010** — power-loss matrix and evidence format.
6. Resume the bootloader path afterwards; D-01/D-02/D-20 remain open.

## What must not be touched

- No writer implementation before the catalog/bitmap/journal designs and tests.
- No write test on the library or the only copy.
- No journal deletion, truncation or "clean" claim for an invalid journal.
- No `--single` on the bootloader path.

## Open questions

- D-05 cache/dirty-set budget.
- D-06 Unicode normalization/FastUnicodeCompare policy.
- Journal record layout and replay bounds (T-009).
- Bootloader manifest/boot state/signing (D-01/D-02/D-20).

## Device/test evidence

- No new device run. The diagnostic runs remain owner-reported in
  [`device-matrix.md`](device-matrix.md) §2.1.

---

# Handoff — 2026-11-03 (first hardware report and report/lookup fix)

## Branch / commit

- Branch: `main`
- Current head: the formatter/lookup fix commit; check `git log -1`.
- Current artifact: `bootloader/artifacts/bootloader-ipod6g.ipod`, 67,144 bytes,
  SHA-256 `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c`.
- First image (superseded): `bd0ebc92…`, 66,920 bytes.

## What changed

- Replaced `%llu` with `%lu` in the report and partition lines. The Rockbox
  bootloader formatter supports `%c %s %l %d %u %x`, not long-long; a stray
  `%llu` shifted later varargs and corrupted the status lines.
- Added a build-script guard that rejects long-long conversion specifiers in the
  Helikon report sources.
- Added `hfs_catalog_lookup_ascii()`: a raw UTF-16BE key lookup for one ASCII
  root name. It does not decode unrelated names, so a single malformed root
  entry cannot hide `/rockbox.ipod`.
- `boot_diag_run()` uses the raw lookup for a single-component ASCII probe and
  keeps the strict decoded lookup for multi-component paths.
- Added tests: malformed-name root fixture, raw lookup, diagnostic read, report
  byte count. 35 HFS+ + 8 bootloader = 43 host tests.

## What was verified

- `make -C tests/hfsplus test` — 35/35 pass.
- `make -C tests/hfsplus sanitize` — 35/35 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 8/8 pass.
- `make -C tests/bootloader sanitize` — 8/8 pass with ASan/UBSan.
- Thumb image: `_movestart` `0x22020800`, `_moveend` `0x22030e40`,
  61,888 bytes below `MOVE_AREA`.
- `mks5lboot --mkdfu-inst` accepts the image as `Classic 6G` / `ipod6g` /
  `ip6g`, target 71.
- No `ata_write_sectors`, `storage_write_sectors`, `hfsplus_create_file` or
  `fat_write` symbol.

## What failed

- The first hardware run (image `bd0ebc92…`) booted and showed the splash.
  Volume and catalog reported `PASS`, but the report displayed `llu` instead of
  the byte count and shifted the write/journal/splash lines into dotted squares
  or `NOT IMPLEMENTED`. The read probe returned `-11`
  (`HFS_ERR_NAME_INVALID`).
- Cause of the display corruption: Rockbox `format()` does not implement
  long-long conversions. Cause of `-11`: the strict root lookup decoded an
  unrelated malformed name and aborted.
- Both are fixed in the current artifact. The fixed image has **not** been
  reflashed or rerun.

## What is next

The fixed image was rerun by the owner. The report formatting is correct:
volume, catalog, write (`NOT IMPLEMENTED`), journal, splash and partition size
displayed as expected. `read error: -8` (`NOT_FOUND`) is correct because
`/rockbox.ipod` is not a disk file on this iPod; the lookup itself worked. No
write was reported.

Choose one:

1. **Close the read evidence (small):** point the diagnostic probe at a real root
   file such as `/.rockpod-rw`, reflash once, confirm `HFS+ read: PASS`, and
   record it.
2. **Start the writer (recommended):** record D-03/D-04, then T-001/T-002.
   Native HFS+ writes are the defining promise.
3. **Start the firmware-loading bootloader:** decide D-01/D-02/D-20 first.

## What must not be touched

- No HFS+ or NOR write path in this image.
- No `--single`; no format, erase, repartition or journal deletion.
- Do not use the donor V3.0/V3.2 image or hash.
- Do not call the image validated or supported without a recorded rerun.

## Open questions

- Does the raw ASCII root lookup find `/rockbox.ipod` on the owner's volume?
- Should the diagnostic report the probe path or skipped malformed names?
- D-01/D-02/D-20 remain open for firmware loading.

## Device/test evidence

- Owner-reported first run (`bd0ebc92…`): splash shown; volume PASS; catalog
  PASS; read FAIL `-11`; report formatting corrupted.
- Owner-reported fixed-image run (`280ed7ae…`): report formatting correct;
  volume/catalog/write/journal/splash/partition as expected; read `-8` because
  `/rockbox.ipod` is not a disk file; no write.
- Recorded in [`device-matrix.md`](device-matrix.md) §2.1.

---

# Handoff — 2026-11-03 (diagnostic bootloader build)

## Branch / commit

- Branch: `main`
- Current head: the diagnostic-bootloader build commit; check `git log -1`.
- Artifact: `bootloader/artifacts/bootloader-ipod6g.ipod`, 67,144 bytes,
  SHA-256 `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c`.

## What changed

- Added `bootloader/hardware/helikon_diag_hw.c`: read-only ATA callback, donor
  partition scan, Rockbox mono-bitmap splash blit, report rendering, button wait
  and power-off.
- Added `bootloader/hardware/donor-bootloader.patch`: removes the donor
  firmware-loading/USB path, adds the Helikon sources and calls the diagnostic
  after `storage_init()`.
- Added `tools/build-diagnostic-bootloader.sh`: pinned donor export, patch,
  Thumb build, `MOVE_AREA` check, artifact copy and SHA-256 output.
- Added `bootloader/artifacts/` with the built image, `SHA256SUMS` and
  provenance/evidence notes.
- Fixed the splash packing: the source is row-major MSB-first; Rockbox mono
  bitmaps are vertical, eight pixels per byte. Added
  `helikon_splash_pack_rockbox()` and a round-trip host test.
- Rewrote `bootloader/DFU.md` for the real image: backup, manual hash, DFU scan,
  install, expected screen, power-off and recovery.

## What was verified

- 41/41 host tests pass; both suites are ASan/UBSan clean.
- The Thumb image links at `_movestart` `0x22020800`, `_moveend` `0x22030e40`,
  with 61,888 bytes of `MOVE_AREA` headroom.
- The image contains the Helikon diagnostic marker and no
  `ata_write_sectors`, `storage_write_sectors`, `hfsplus_create_file` or
  `fat_write` symbol.
- `mks5lboot --mkdfu-inst` accepts the image as `Classic 6G` / `ipod6g` /
  `ip6g` / target 71.
- The build script reproduces the exact image hash from a clean donor export.
- Donor partition/HFS+ tests pass in the donor checkout; this is reference
  evidence only.

## What failed

- The first splash blit used `lcd_mono_bitmap` on the row-major source. Rockbox
  expects vertical packing; the direct call would have read past the buffer.
  Fixed with an explicit packing step and test.
- The first image still linked the donor USB/FAT/HFS+ write stack (113 KB).
  Removing the unused USB mode and loader path reduced it to 67,144 bytes and
  removed write-capable symbols.
- The build script initially used GNU-only `patch --batch --forward`; macOS
  `patch` needed `-N`.

## What is next

1. Record the `.rockbox` + `/.rockpod-rw` backup and clean eject.
2. Run the first hardware diagnostic with [`../bootloader/DFU.md`](../bootloader/DFU.md):
   DFU scan, `--bl-inst`, confirm splash/report, press a button, unplug USB,
   confirm power-off.
3. Record the exact screen and result in [`device-matrix.md`](device-matrix.md).
4. If the run passes, decide D-01/D-02/D-20 for the firmware-loading bootloader.

## What must not be touched

- No HFS+ or NOR write path in this image.
- No `--single`; no format, erase, repartition or journal deletion.
- Do not use the donor V3.0/V3.2 image or hash.
- Do not call the image validated, supported or release-ready without a recorded
  device run.

## Open questions

- Does the image boot and show the splash/report on the owner's 6G?
- Does `/rockbox.ipod` exist and read through the Helikon HFS+ core?
- Does power-off behave correctly when USB is connected?
- D-01/D-02/D-20 remain open for firmware loading.

## Device/test evidence

- None. No Helikon-OS device run was performed.

---

# Handoff — 2026-11-03 (build)

## Branch / commit

- Branch: `main`
- Current head: the host read-core and diagnostic-bootloader commit on `main`;
  check `git log -1`.

## What changed

- Added `firmware/hfs/hfs_volume.{c,h}`: read-only HFS+ primary/alternate
  volume-header and journal probe with an explicit error taxonomy.
- Added `firmware/hfs/hfs_btree.{c,h}`: generic B-tree header-node, reverse
  offset-table and leaf-chain validation.
- Added `firmware/hfs/hfs_unicode.{c,h}`: exact UTF-16BE to UTF-8 decoding.
- Added `firmware/hfs/hfs_catalog.{c,h}`: catalog iteration, path lookup and
  inline file reads.
- Added `firmware/hfs/hfs_extents.{c,h}`: inline plus extents-overflow file
  reads.
- Added `tests/hfsplus/`: shared fixtures and 34 tests across volume, B-tree,
  Unicode, catalog and extents.
- Added `bootloader/diagnostics.{c,h}`, `report.{c,h}`, `splash.{c,h}` and
  `bootloader/README.md`: a read-only diagnostic core and 320×240 splash.
- Added `tests/bootloader/`: 7 diagnostic/splash host tests.
- Added `bootloader/DFU.md`: a blocked install runbook for the future image;
  it has no image or hash and must not be executed yet. It includes the
  `.rockbox` + `/.rockpod-rw` backup commands and the owner-selected manual
  SHA-256 procedure.
- Added the owner-supplied poster to the root README and a 320×240 startup crop
  under `assets/startup/`; the startup crop is not wired into firmware yet.
- Added `.gitignore` for the host test binaries and macOS metadata.
- Applied the thermo-nuclear quality pass: shared leaf iteration, removed dead
  code and named structural bounds.
- No flashable bootloader image, kernel or writer exists yet. The diagnostic
  core is host-tested only and does not touch a device.

## What was verified

- `make -C tests/hfsplus test` — 34/34 pass (12 volume, 9 B-tree, 8 Unicode,
  3 catalog, 2 extents).
- `make -C tests/hfsplus sanitize` — 34/34 pass with ASan/UBSan.
- `make -C tests/bootloader test` — 7/7 pass.
- `make -C tests/bootloader sanitize` — 7/7 pass with ASan/UBSan.
- Test count and output are in `docs/log/2026-11-03.md`.
- No device write, bootloader install, DFU action or `--single` was attempted.
- The diagnostic core reports HFS+ write as `NOT IMPLEMENTED`; it never tests a
  write on a real library.
- Root `README.md` now includes the owner-supplied poster; the 25 goals are
  unchanged. The donor repository was not modified.

## What failed

- One journal fixture initially used flags `4` (need-init without in-FS); it was
  corrected to `1|4` to match the HFS+ journal-info semantics.
- The first B-tree fixture stored the reverse offset table in forward order; it
  was corrected to HFS+ reverse order.
- The first extents fixture used an 8-byte file with a second allocated block;
  it was corrected to a 516-byte file so the overflow boundary is meaningful.

## What is next

1. Define the bootloader manifest/boot-state flow and host image-loading core
   (T-027); D-01/D-02 remain owner decisions.
2. Build the hardware diagnostic bootloader image (T-029) after T-003 and the
   recovery rehearsal; D-21 accepts read-only diagnostic, manual SHA-256, no
   write test and wait-for-button/power-off. Verify Thumb/`MOVE_AREA`, record
   the SHA-256, then use `bootloader/DFU.md`.
3. Wire the 320×240 startup image into the phase 0 display path.
4. Keep the writer blocked until D-03/D-04 and T-002/T-004 are settled.

## What must not be touched

- No bootloader/NOR write path.
- No filesystem write path in this host slice.
- No `--single`.
- No format/erase/journal deletion.
- No claim that the host diagnostic core is a flashable bootloader or a device
  validation.

## Open questions

- D-03/D-04 (journal strategy/ownership) still block the writer.
- D-01/D-02 (manifest/boot state) still block the bootloader design.
- The next slice is the bootloader manifest/boot-state flow; D-01/D-02 must be
  recorded before any hardware boot path.

## Device/test evidence

- None. No Helikon-OS device run was performed.

---

# Handoff — 2026-11-03 (later)

## Branch / commit

- Branch: `main`
- Current head: the logo-removal/COC-cleanup commit on `main`; check `git log -1`.
- Remote note: the owner deleted `CODE_OF_CONDUCT.md` in `51f66a1`; that deletion
  is preserved, not reverted.

## What changed

- Removed `assets/logo/preview.html`, the dot-matrix overlay/mark text files and
  `docs/logo.md`.
- Kept `assets/logo/mount-helikon-sunset.jpg` as the owner-supplied mountain
  reference, with `assets/logo/README.md` noting that the owner will design the
  logo and that source/licence are open.
- Recorded the owner's deletion of `CODE_OF_CONDUCT.md`: D-19 is now `rejected`;
  all links to the deleted file were removed from the docs.
- No logo was generated and no mountain pixels were dotted.

## What was verified

- The mountain reference image is present and opens.
- No dot-matrix logo, preview HTML or `docs/logo.md` remains.
- No link points to the deleted `CODE_OF_CONDUCT.md`.
- Root `README.md` is unchanged.
- The remote deletion commit is preserved; the local branch was rebased, not
  force-pushed.

## What failed

- The first push was rejected because the remote already had the owner's
  `CODE_OF_CONDUCT.md` deletion. The branch was rebased onto `origin/main` and
  the deletion was preserved.

## What is next

1. Owner designs the logo using `assets/logo/mount-helikon-sunset.jpg`.
2. Resolve D-03/D-04 (journal strategy/ownership) before phase 2.
3. Record D-20 (signing-key ownership) before release work.
4. Do not recreate a code of conduct or logo preview without a new owner request.

## What must not be touched

- `assets/logo/mount-helikon-sunset.jpg` — owner reference; source/licence open.
- No `CODE_OF_CONDUCT.md` unless the owner explicitly asks for a new one.
- The native-HFS+ invariants, journal rules, `fsck_hfs` gate, bootloader
  read-only rule, or `--single` ban.

## Open questions

- D-20: signing-key ownership and release-signing policy.
- Logo direction and final asset: owner.
- D-03/D-04 and the other open decisions in `docs/decisions.md`.

## Device/test evidence

- None for Helikon-OS. No device run was performed.

---

# Handoff — 2026-11-03

## Branch / commit

- Branch: `main`
- Base: `main` at `9c558069b6e7b325451b3127b10adfbd41486315`
- Documentation commit: `6df63ef63bacc93d253db40d1bb7f2efdd289a6c`,
  fast-forwarded from `docs/license-coc-templates` and pushed to `origin/main`
- Merge: fast-forward on 2026-11-03; no PR for this follow-up. Later docs
  housekeeping commits are on `main`; check `git log -1`.

## What changed

- Read the Helikon-OS README (25 goals) and the full 3,178-line design paper
  (source commit `3f220a73d0cceb083b3c057c6fe914d88468800a`).
- Added `agents.md` as the canonical entry contract.
- Added the P0/P1/P2 documentation set under `docs/`: state, invariants,
  decisions, tasks, verification, file map, architecture, failures, device
  matrix, handoff, conventions, glossary, testing, validation, goals, roadmap,
  HFS+ writer deep dive, release, security, FAQ, design-paper copy, ADRs, logs
  and prompt stubs.
- Added root `CONTRIBUTING.md` and `SECURITY.md`.
- Fast-forwarded `docs/agent-handbook` to `main` and pushed `main`; PR #1 is
  merged.
- Added `LICENSE` (GPL-2.0-or-later), `NOTICE`, `CODE_OF_CONDUCT.md`
  (Contributor Covenant 2.1), and GitHub issue templates for bugs, features and
  device test reports; fast-forwarded the follow-up branch to `main` and pushed
  it.
- Did **not** modify the root `README.md`, the donor repository, any firmware,
  build system, CI or release asset. The requested README title, summary and 25
  goals were already present at the top, so no README change was needed.

## What was verified

- Repository cloned from `https://github.com/deandromi/Helikon-OS.git`; `origin`
  is correct; `main` exists; base commit recorded.
- Branched from `main` to `docs/agent-handbook` before writing, then
  fast-forwarded the branch to `main` and pushed it.
- README and paper fetched from raw GitHub URLs; full paper read in chunks. The
  paper copy preserves the body except for two trailing-space hard breaks
  normalized for whitespace checks (`diff -b` clean).
- Donor repository cloned to a temporary path for file references only; donor
  commit recorded; no donor files modified.
- Documentation set created and checked for:
  - `git diff --check`;
  - relative link and table inspection;
  - all 25 README goals present in `docs/goals.md`;
  - all 20 decisions present in `docs/decisions.md`;
  - roadmap phases 0–12 with exit criteria;
  - every task has an acceptance criterion;
  - no claim that Helikon-OS is implemented or hardware-validated;
  - no duplicate canonical files.
- No Helikon-OS build, test or device run was performed; there is no source tree.
- `main` and `origin/main` are at `6df63ef`; PR #1 state is MERGED. The requested
  README block is present verbatim (all 25 goal lines) and the README SHA-256 is
  unchanged from the base commit.
- License/COC/issue-template files parse/validate: YAML issue forms load; the
  license notice states GPL-2.0-or-later; the code of conduct has an enforcement
  contact and no placeholder remains.

## What failed

- Nothing failed in the documentation workflow.
- The initial docs pass needed a trim: `agents.md` was reduced from 363 to 290
  lines to keep it under the ~300-line contract limit.
- No device test was attempted; there is no Helikon-OS firmware.

## What is next

1. Owner reviews the merged docs on `main` and the conflict records C-01–C-09.
2. Resolve the phase-2 blockers first: D-03 (journal strategy) and D-04
   (journal ownership).
3. Freeze the writer interface and failure model (T-002), then the fixture plan
   (T-004) and backup/disposable-volume protocol (T-003).
4. Record the signing-key ownership policy (D-20/T-006) before any release work.
5. Start the 7G board-audit checklist (T-005) only when hardware access is
   available.

## What must not be touched

- Root `README.md` — owner-authored goals; do not edit.
- Donor repository `deandromi/hfsplus-rockboxed-ipod` — read-only reference.
- Any `/.rockpod-rw`, device backup, disk image, serial number or personal data.
- The native-HFS+ invariants, journal rules, `fsck_hfs` gate, bootloader
  read-only rule, or `--single` ban.
- `LICENSE` and `NOTICE` — the license choice is owner-approved; do not change
  it silently.
- Signing policy (D-20, not chosen).
- No code of conduct is currently adopted; `CODE_OF_CONDUCT.md` was later
  deleted by the owner (see latest handoff).

## Open questions

- D-03/D-04: journaled release writes and device-owned replay? Critical path.
- D-01/D-16: signature scheme and update transport.
- D-06: Unicode normalization/case-folding policy and fixture source.
- D-07: host-built database vs on-device incremental builder timing.
- D-10: exact Rockbox skin subset and corpus licences.
- D-13: 7G board audit and port scope.
- D-20: signing-key ownership and release-signing policy.
- Legal review of the GPL-2.0-or-later wording before a public release. No code
  of conduct is currently adopted.
- C-02–C-06: owner confirmation of README goal scope vs paper's narrower design
  response.

## Device/test evidence

- **None for Helikon-OS.** No boot, playback, write, journal replay, sleep, USB,
  update or recovery test was run.
- Donor V3.2 owner-reported hardware use exists in the donor repository but is
  not Helikon-OS evidence and is not recorded as a pass here.
- Do not add a device row as "pass" without the evidence template in
  `docs/verification.md`.

---

# Template — copy for the next session

```md
# Handoff — YYYY-MM-DD

## Branch / commit

## What changed

## What was verified

## What failed

## What is next

## What must not be touched

## Open questions

## Device/test evidence
```

## Template notes

- Put the newest handoff above this template; move older entries to
  `docs/log/YYYY-MM-DD.md`.
- Link the PR/commit; do not paste a diff.
- Say "not run" rather than implying a test happened.
- Record exact error codes and device states.
- If a hard stop was hit, say what was not attempted.
- Update `state.md` and `tasks.md` before writing the handoff.
