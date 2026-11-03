# Handoff — Helikon-OS

> Purpose: the latest session handoff and the reusable template.
> Status: living document
> Owner: session author; next agent reads this first
> Last updated: 2026-11-03
> Source: `docs/state.md`, `docs/tasks.md`, repository history

Keep the newest handoff at the top. Move older entries to `docs/log/` when this
file grows. Do not rely on chat history for current state.

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
