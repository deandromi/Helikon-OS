# HFS+ write engine — native writer and journal deep dive

> Purpose: the defining subsystem: native HFS+ metadata mutation, journal,
> transaction ordering and `fsck_hfs` compatibility.
> Status: living design document; D-03/D-04 accepted; writer interface frozen.
> The journal append/replay path, bounded transaction engine, allocation bitmap,
> B-tree insert/delete/split/growth, catalog/extents mutation, fork staging and
> Apple `FastUnicodeCompare` are implemented host-only; HFS+ normalization and
> the write diagnostic do not exist.
> Owner: HFS+ lead; journal lead
> Last updated: 2026-11-03
> Source: design paper §9, §10.4, §19, §21, §24, §28.2; `docs/architecture.md`

## 1. Scope

Helikon-OS writes **native HFS+**. Files created, renamed, truncated or deleted by
the device are ordinary HFS+ files visible to macOS. There is no namespace
overlay, no `/.rockpod-rw`, no hidden container and no FAT mount.

The writer is not a flag or an adapter. It is a full filesystem write
implementation with catalog B-tree mutation, allocation, extents, journal,
metadata parity and an external consistency oracle (`fsck_hfs`). The paper calls
it the largest single change from the donor and the critical path of the product.

**Current status:** D-03 (journaled writes), D-04 (Helikon-OS owns replay) and
D-06 (Apple `FastUnicodeCompare`) are accepted; the interface and failure model
in §10 are frozen. The Apple HFS+ journal append/replay path, a bounded
transaction engine, the allocation bitmap, B-tree insert/delete/split/growth,
catalog/extents mutation, fork block staging and catalog key comparison are
implemented and host-tested. HFS+ decomposition/normalization and the write
diagnostic are not implemented. Do not infer a device write or a complete
writer from this document.

## 2. Minimum surface

| Area | Required capability |
|---|---|
| Catalog B-tree | Insert/update/delete folder/file records; thread records; node split/merge; free-node map; leaf links; key ordering; CNID allocation |
| Allocation bitmap | Mark blocks used/free; update volume-header free-block count; keep allocation and extents consistent |
| Extents overflow | Insert/update/delete overflow records for files with more than eight extents |
| Volume header | Write count, modify date, free blocks, next CNID, alternate-header consistency |
| Journal | Append/replay transactions, or refuse writes on journaled volumes |
| File data | Allocate, write, zero-fill, truncate and synchronize data forks |
| Metadata | Timestamps, link counts, permissions, Finder info, flags |
| Unicode | Apple case-folding comparison and a normalization policy for create/lookup/rename |
| `fsck_hfs` | Every metadata change remains valid to macOS `fsck_hfs`; Helikon-OS does not repair |

## 3. Catalog B-tree mutation

The catalog is a B-tree keyed by parent CNID plus name (and a thread record for
each object). A mutation can change a leaf, split a node, merge nodes, update the
free-node map, update leaf links and change the root. This is the highest-risk
code in the product.

### 3.1 Required operations

- Insert a folder record, file record and thread record.
- Update a record without zeroing fields the writer does not understand.
- Delete a record and its thread; update parent/child counts where applicable.
- Allocate CNIDs monotonically; never reuse a live CNID; keep the volume header's
  next-CNID field consistent.
- Split a full node; merge/redistribute underflowed nodes; update the root.
- Maintain the free-node map and leaf sibling links.
- Preserve key order using Apple `FastUnicodeCompare` (see §7).

### 3.2 Structural rules

- Every node passes descriptor/offset-table validation before and after a
  mutation.
- A split/merge never leaves a dangling child pointer or a broken leaf chain.
- The tree stays balanced; no operation creates an unreachable record.
- A failed mutation leaves the old tree intact; partial node writes are not
  visible.
- The free-node map and allocation bitmap agree with the nodes actually in use.
- A record update preserves BSD info, Finder info, flags and timestamps unless
  the operation explicitly changes them.

### 3.3 Key ordering

Catalog keys are not byte-sorted UTF-8. The writer must implement Apple's HFS+
case-folding comparison for:

- B-tree key order;
- lookup;
- create;
- rename;
- duplicate detection.

A wrong comparison lets macOS and the device disagree about where a name belongs.
That is catalog corruption even if the file "looks fine" on the device.

## 4. Allocation bitmap

- Set/clear bits for allocated/freed blocks.
- Update the volume header's free-block count and allocation file metadata.
- Never allocate a block that is still referenced by a live extent.
- Never free a block that is still referenced.
- Keep allocation and extents consistent across create, append, truncate, delete,
  split and merge.
- A transaction that cannot allocate returns `NO_SPACE` before changing
  anything.
- Test allocation edge cases: full volume, last free block, fragmented free runs,
  block size 512/4096, bitmap tail bits.

## 5. Extents and overflow

- Inline extents cover the first eight runs; overflow records cover the rest.
- Insert/update/delete overflow records; keep the extents B-tree keyed by
  fork/type/start block.
- Truncate frees data blocks and removes/updates overflow records.
- Zero-fill newly allocated blocks when exposing a larger file, so old data does
  not leak.
- Do not silently drop a large/fragmented file because the overflow tree is
  unsupported; that is a read-path bug, not a write policy.
- Keep the read-only extents path and the write path consistent; shared
  validation, separate mutation.

## 6. Journal and transaction ordering

### 6.1 Release path: journaled writes

D-03 makes journaled writes the required path for every writable volume. A
journaled volume cannot be safely written by ignoring the journal. The release
design is a block transaction log:

1. Reserve/append journal blocks.
2. Write transaction records with sequence numbers and checksums.
3. Flush/barrier.
4. Commit the journal header.
5. Apply the metadata blocks.
6. Flush/barrier.
7. Mark the journal clean / acknowledge.

Replay on mount re-applies committed records and discards incomplete ones. D-04
makes Helikon-OS the replay owner. Replay must be idempotent and bounded, and it
must finish before the first write. A read-only mount never modifies the
journal.

### 6.2 Transaction grouping

A transaction groups all catalog, bitmap, extents and volume-header changes for
one logical operation. One writer is serialized at the filesystem boundary.

- Data is written before the metadata that exposes it.
- A flush/barrier is required before commit and after commit.
- A transaction that cannot fit its dirty set fails with `NO_SPACE` before
  changing anything.
- A failed transaction aborts and preserves the last consistent state.
- No auto-repair, no zeroing, no "try again and hope".

### 6.3 Crash-consistency rules

- A cut before commit leaves the old state; incomplete journal records are
  discarded.
- A cut after commit but before apply leaves committed records for replay.
- A cut during apply is recovered by idempotent replay.
- A cut after apply but before clean is recovered by replay (idempotent).
- Any invalid journal/checksum/sequence mounts read-only; it is not "cleaned".
- A read-only mount never modifies the journal.
- A write never starts before a dirty journal is replayed.

### 6.4 Journal failure

A damaged or invalid journal is not a license to delete it. Mount read-only,
report `JOURNAL_DIRTY`/`CORRUPT`, preserve the journal for diagnosis, and ask the
user to repair on macOS from a backup. Do not zero the journal header.

### 6.5 On-disk format: Apple HFS+ journal

The writer uses Apple's HFS+ journal format, not a private transaction log.
macOS must be able to validate and replay the same journal. The reference is
Apple Technical Note TN1150, "HFS Plus Volume Format", Journal section.

- The volume header's `journalInfoBlock` names the allocation block holding the
  journal info block. The journal is inside the volume
  (`kJIJournalInFSMask`), not on another device.
- `JournalInfoBlock` contains `flags`, `device_signature[8]`, `offset`, `size`
  and reserved words. `offset` is relative to the start of the volume; `size`
  covers the journal header plus the journal buffer. `kJIJournalNeedInitMask`
  is set only for a journal that has never been initialized.
- The journal header is one sector (`jhdr_size`) at `offset`. Its fields are
  `magic` (`0x4a4e4c78`), `endian` (`0x12345678`), `start`, `end`, `size`,
  `blhdr_size`, `checksum` and `jhdr_size`. The single-sector header update is
  the atomic commit point.
- `start` and `end` are byte offsets from the start of the journal header.
  `end == start` means the journal is clean. `end` may be less than `start` when
  the circular buffer wraps. The journal is never allowed to be exactly full.
- The journal buffer is circular: block-list headers and block contents may wrap
  at `size`, resuming at `jhdr_size`.
- A transaction is one or more block lists. Each block list starts with a
  `block_list_header`: `max_blocks` (reserved on disk), `num_blocks`,
  `bytes_used`, `checksum`, `pad`, then `block_info[]`.
- The first `block_info` is the continuation marker. `next == 0` ends the
  transaction; non-zero means another block list follows. The remaining
  `block_info` entries describe data blocks. Each has `bnum` (target sector),
  `bsize` (bytes, multiple of 512) and `next` (meaningful only in the first
  entry). `bnum == 0xffffffffffffffff` means "skip this block".
- `bytes_used` includes the block-list header and data. The next block list is
  `bytes_used` bytes after the current header, wrapping at the end of the
  journal buffer.
- The journal header checksum and each block-list header checksum use Apple's
  `calc_checksum`: set the checksum field to zero, then
  `cksum = (cksum << 8) ^ (cksum + byte)` over the structure, and store
  `~cksum`. A block-list checksum covers the 32-byte header including the first
  `block_info`.
- The journal files (`.journal_info_block` and `.journal`) are contiguous and
  must not be exposed as ordinary files. The writer locates them through the
  volume header and journal info block, not by name.

### 6.6 Commit, clean and replay

Append:

1. Reserve space from `end` for the block lists and data, wrapping as needed.
   If the transaction cannot fit without making `start == end`, return
   `HFS_ERR_NO_SPACE` before writing.
2. Write the block-list headers and block contents, then flush/barrier.
3. Update the journal header's `end` to the end of the last block list,
   recompute the header checksum, write the one-sector header and flush. This
   is the commit point.
4. Apply the committed blocks to their home sectors, then flush/barrier.
5. Advance `start` past the applied transaction, recompute the header checksum,
   write the one-sector header and flush. If all transactions are applied,
   `start == end` and the journal is clean.

Replay:

1. Read the volume header. If the volume is not journaled, stop.
2. Read the journal info block at `journalInfoBlock`; validate
   `kJIJournalInFSMask`, clear `kJIJournalOnOtherDeviceMask`, and validate
   `offset`/`size` against the volume.
3. Read the journal header; validate magic, endian, `size`, `jhdr_size`,
   `blhdr_size`, checksum and `start`/`end` bounds. If `start == end`, stop.
4. Check the volume header's last-mounter signature against a known HFS+
   implementation. If it is not a known signature, mount read-only and do not
   replay; TN1150 explicitly warns that replaying an unknown format can corrupt
   the volume.
5. Walk from `start` to `end`. For each block list: read the header (with wrap),
   validate checksum, `num_blocks`, `bytes_used` and each `block_info`; copy
   `bsize` bytes from the journal to sector `bnum`; skip the all-ones block
   number. If the first `block_info`'s `next` is zero, the transaction is
   complete and `start` advances past it; otherwise continue with the next
   block list.
6. Flush/barrier after all blocks are applied.
7. Update `start` to the new position (or to `end` when all transactions are
   applied), recompute the header checksum, write the one-sector header and
   flush.

Replay is idempotent: applying the same block image again writes the same
bytes. A crash before the `start` update simply replays the same committed
transaction again. A crash during the header update leaves either the old or the
new header; the checksum and bounds validation decide. A replay loop is bounded
by the journal size and must detect a non-advancing offset.

### 6.7 Journal validation and failure

The journal is accepted only if all of these hold:

- the volume header says journaled and the journal info block is in-volume;
- `offset + size` is within the volume and the journal files are contiguous;
- the journal header magic, endian, size, header size and checksum are valid;
- `start` and `end` are within `[jhdr_size, size]`;
- every block-list header has a valid checksum, `num_blocks >= 1`,
  `bytes_used >= blhdr_size`, and the sum of data `bsize` fits in
  `bytes_used - blhdr_size`;
- every target `bnum` is within the volume and every `bsize` is a non-zero
  multiple of 512;
- the last-mounter signature is known.

Any failure mounts read-only with `HFS_ERR_JOURNAL_DIRTY` or
`HFS_ERR_JOURNAL_CORRUPT` as appropriate. The journal is preserved byte for byte;
it is never deleted, truncated, zeroed or marked clean by the read-only path.

Known signature policy and exact `blhdr_size` selection are open items for
T-009; both must be tested against macOS-created journals before the writer is
enabled on a real volume.

## 7. Unicode, names and metadata parity

### 7.1 Names

- HFS+ stores decomposed UTF-16.
- Apple's `FastUnicodeCompare` is accepted (D-06) and implemented for catalog
  ordering and comparison. The folding table is generated from Unicode 2.1.9 by
  `tools/gen-hfs-unicode-table.py`, not copied from Apple's APSL table.
- The HFS+ decomposer is not implemented. Until it is, the writer refuses names
  containing a code point with a canonical decomposition (including Hangul
  syllables) with `HFS_ERR_NAME_INVALID`; decomposed input is accepted. This is
  the explicit-refusal half of D-06, not composed-name support.
- Reject names over 255 UTF-16 units, embedded NUL, `/`, and reserved `.`/`..`.
- Decide and test the `:`/`/` mapping against macOS. The donor maps `/` to `:`
  and rejects a literal `:`; do not assume that is the final policy.
- Test composed/decomposed names, case variants, emoji/non-BMP, length
  boundaries and names created by macOS.

### 7.2 Metadata parity

- Preserve BSD info, Finder info, flags and timestamps when updating an existing
  record.
- Do not rewrite a record with a zeroed field the parser did not understand.
- Keep file/folder counts, link counts and parent/child relationships consistent.
- Keep the primary and alternate volume headers consistent.
- Update the volume header's write count, modify date, free blocks and next CNID.
- Do not claim parity until macOS and `fsck_hfs` agree on a differential corpus.

## 8. `fsck_hfs` compatibility

`fsck_hfs` is the external oracle. The writer must remain something macOS can
validate without repairs.

Required checks mirror the oracle's rules:

- catalog node/offset/leaf-chain/free-node consistency;
- key order and duplicate detection;
- CNID uniqueness and next-CNID bounds;
- allocation bitmap vs extents vs free counts;
- volume header/alternate header agreement;
- extents overflow records;
- thread records and parent/child links;
- timestamps/metadata sanity where the oracle checks it.

After every crash/power-cut test: boot, mount, run `fsck_hfs -n`, capture the full
output, and compare the recovered tree with the committed state. **Zero repairs**
is the gate. A single catalog or bitmap inconsistency blocks release. Helikon-OS
does not implement a repairer; repair is a macOS/user action from a backup.

## 9. Non-journaled prototype boundary

D-03 chooses journaled writes as the product path. A non-journaled writer is not
the chosen path. If one is built for bring-up, it may target only a volume
explicitly formatted without a journal:

- It is a **disposable-volume prototype**, not a release path.
- Journaled volumes mount read-only under this prototype.
- It must still implement catalog, bitmap and extents writes; it is not a
  shortcut around B-tree mutation.
- A power loss may require `fsck_hfs`; the prototype must not be used on a real
  library.
- The prototype is labelled "prototype only" in code, UI and docs.

The release path is journaled. Do not silently use the prototype on a journaled
volume or disable journaling to make a write fit. D-03 does not authorize a
non-journaled library write.

## 10. Frozen writer interface and failure model

D-03 and D-04 are accepted; this section is the interface freeze for T-002. It is
a design contract, not an implementation. Names are frozen for the writer
implementation; changing them requires an explicit review.

### 10.1 Transaction lifecycle

```c
struct hfs_txn;

int  hfs_write_open(struct hfs_volume *volume);
int  hfs_txn_begin(struct hfs_volume *volume, struct hfs_txn **tx);
int  hfs_txn_commit(struct hfs_txn *tx);
void hfs_txn_abort(struct hfs_txn *tx);
```

- `hfs_write_open()` probes the volume, resolves journal state and, if the
  journal is valid and dirty, runs replay before returning writable. It returns
  `HFS_ERR_JOURNAL_DIRTY` if replay is required but not complete, and
  `HFS_ERR_JOURNAL_CORRUPT`/`HFS_ERR_READ_ONLY` for an invalid journal.
- `hfs_txn_begin()` acquires the single writer lock. A second active transaction
  returns `HFS_ERR_BUSY`. There are no nested transactions.
- Operations stage changes in the transaction. They do not publish metadata.
- `hfs_txn_commit()` validates the dirty set, writes the journal, applies
  metadata, flushes, marks the journal clean and releases the lock. It returns 0
  only after the clean marker is durable.
- `hfs_txn_abort()` discards staged changes and releases the lock. It never
  leaves a metadata change visible.
- No transaction handle is valid after commit or abort.

### 10.2 Public operations

The VFS uses these operations; there is no direct catalog/bitmap/extents call
outside the engine.

```c
int hfs_txn_create(struct hfs_txn *tx, struct hfs_file *parent,
                   const char *name, enum hfs_kind kind,
                   const struct hfs_meta *meta, struct hfs_file **out);
int hfs_txn_write(struct hfs_txn *tx, struct hfs_file *file,
                  uint64_t offset, const void *data, size_t length,
                  size_t *done);
int hfs_txn_truncate(struct hfs_txn *tx, struct hfs_file *file,
                     uint64_t size);
int hfs_txn_rename(struct hfs_txn *tx, struct hfs_file *parent,
                   struct hfs_file *file, const char *newname);
int hfs_txn_remove(struct hfs_txn *tx, struct hfs_file *file);
int hfs_txn_set_meta(struct hfs_txn *tx, struct hfs_file *file,
                     const struct hfs_meta *meta);
```

- `hfs_txn_create` handles file or folder records, the thread record, CNID
  allocation and the parent link in one transaction.
- `hfs_txn_write` allocates and zero-fills data blocks, writes data before the
  metadata that exposes it, and updates the fork/extents/size in the same
  transaction. Large writes may be split by the caller; each commit exposes a
  consistent prefix, never a size larger than the written data.
- `hfs_txn_truncate` frees data blocks and overflow extents, updates size and
  timestamps, and preserves metadata it does not understand.
- `hfs_txn_rename` updates the catalog key and thread name; it never leaves both
  old and new names visible after commit.
- `hfs_txn_remove` deletes the record and thread, updates parent counts and frees
  blocks/extents. A non-empty directory returns `HFS_ERR_NOT_EMPTY`.
- `hfs_txn_set_meta` updates only the requested fields; BSD info, Finder info,
  flags and unknown fields are preserved.

### 10.3 Internal staging primitives

These are engine-private and all take the transaction:

```c
int hfs_catalog_stage_insert(struct hfs_txn *tx, ...);
int hfs_catalog_stage_update(struct hfs_txn *tx, ...);
int hfs_catalog_stage_delete(struct hfs_txn *tx, ...);
int hfs_bitmap_stage_alloc(struct hfs_txn *tx, uint32_t blocks, ...);
int hfs_bitmap_stage_free(struct hfs_txn *tx, uint32_t start, uint32_t blocks);
int hfs_extents_stage_insert(struct hfs_txn *tx, ...);
int hfs_extents_stage_update(struct hfs_txn *tx, ...);
int hfs_extents_stage_delete(struct hfs_txn *tx, ...);
int hfs_header_stage_update(struct hfs_txn *tx, ...);
int hfs_journal_stage_records(struct hfs_txn *tx, ...);
```

Staging validates node/bitmap/extent invariants immediately. A staging error
aborts the transaction before any metadata is published.

### 10.4 Commit protocol

1. Validate the transaction, volume state, writer lock, power gate and dirty-set
   budget. If anything fails, return before writing metadata.
2. Reserve journal space, CNIDs and data blocks. `NO_SPACE` is returned here,
   before any media write.
3. Write and zero-fill new data blocks, then flush/barrier. Data is never
   exposed before it is durable.
4. Build journal records: sequence number, target block map, new block images,
   lengths and checksums.
5. Append the records to the journal and flush/barrier.
6. Write the commit record/header with the transaction sequence and checksum,
   then flush/barrier. This is the atomic commit point.
7. Apply the committed metadata blocks to their home locations and update both
   volume headers as required. Flush/barrier.
8. Mark the journal clean/acknowledged and flush/barrier.
9. Release the transaction and writer lock.

Crash behavior:

| Cut point | Recovered state |
|---|---|
| Before commit record | Old state; incomplete records are discarded |
| After commit, before apply | Committed records replay to the new state |
| During apply | Idempotent replay completes the apply |
| After apply, before clean | Replay is idempotent; clean is re-established |
| During clean | Replay is idempotent; clean is re-established |

A write never starts before a dirty journal is replayed. A read-only mount never
writes or replays.

### 10.5 Replay contract

- `hfs_write_open()` scans the journal before the first write.
- Records with a valid sequence/checksum and a commit marker are applied in
  order; records without a commit marker are discarded.
- Replay is idempotent: re-applying a committed record writes the same block
  image and does not depend on the current partial state.
- Replay is bounded by the journal size; a record that does not fit or fails
  validation makes the volume read-only (`JOURNAL_CORRUPT`), not "clean".
- The journal is preserved on any replay failure; it is never deleted,
  truncated, zeroed or ignored.
- After replay, the journal clean marker is flushed before the first user write.

### 10.6 Failure model and error taxonomy

| Error | Meaning | Required behavior |
|---|---|---|
| `HFS_ERR_IO` | Media read/write/flush failed | Stop; do not retry blindly; return read-only |
| `HFS_ERR_NO_SPACE` | Dirty set, journal, CNID or blocks do not fit | Return before any metadata write; volume unchanged |
| `HFS_ERR_READ_ONLY` | Volume mounted read-only | No write path, no journal mutation |
| `HFS_ERR_JOURNAL_DIRTY` | Valid dirty journal needs replay | Mount read-only; replay before first write |
| `HFS_ERR_JOURNAL_CORRUPT` | Invalid journal/checksum/sequence | Preserve journal; mount read-only; report for repair |
| `HFS_ERR_CORRUPT` | Structural catalog/bitmap inconsistency | Stop; read-only; never auto-repair |
| `HFS_ERR_NAME_INVALID` | Name fails Unicode/length/reserved rules | Reject before staging |
| `HFS_ERR_NOT_EMPTY` | Directory removal with children | Reject before staging |
| `HFS_ERR_UNSUPPORTED` | Unsupported object or format | Reject; no partial write |
| `HFS_ERR_BUSY` | Another transaction is active | Reject; caller retries after abort/commit |
| `HFS_ERR_POWER` | Below the write power gate (D-12) | Refuse the transaction before media writes |

No partial success:

- A committed transaction is either fully applicable by replay or was never
  committed.
- An aborted transaction leaves no staged metadata visible.
- A failed commit returns an error and stops writing; the next mount replays or
  mounts read-only. It does not "finish quickly".
- `NO_SPACE` is decided before the first media write.

### 10.7 Transaction boundaries

- One VFS logical operation is the default transaction boundary: create, write
  chunk, truncate, rename, remove, set metadata.
- A compound operation groups only the changes needed to keep one logical
  operation atomic. It may not span a user prompt, a network wait or an audio
  buffer refill.
- A large write may be split into multiple committed prefixes. Each prefix has a
  consistent file size and extents; the final commit exposes the final size.
- `fsync`/close commits the pending write transaction. No transaction remains
  open across a UI event loop.
- The dirty-set budget is D-05; until it is recorded, the implementation uses a
  conservative fixed cap and returns `NO_SPACE` before media writes. The cap is
  documented and tested, not guessed at runtime.
- One writer per volume (I-15). Readers see the old state or the committed new
  state, never a partially applied transaction.

### 10.8 Invariant mapping

- **I-03:** every catalog, bitmap, extents, volume-header and journal change
  takes a transaction handle; no direct metadata write exists.
- **I-05:** a dirty or invalid journal mounts read-only; replay belongs to the
  write engine and completes before the first write; a read-only mount never
  mutates the journal.
- **I-15:** one writer per volume; the power gate is checked before writes; a
  transaction that cannot fit returns `NO_SPACE` before changing anything.
- **I-04/I-06/I-07:** `fsck_hfs` zero repairs, no silent journal bypass and no
  destructive repair remain release gates.

## 11. Test gates

| Gate | Evidence |
|---|---|
| Model-based B-tree | Insert/split/merge/delete, key order, leaf links, free-node map |
| Allocation/extents | Property tests, full volume, fragmentation, truncate, `NO_SPACE` |
| Journal | Commit/replay/abort at every boundary; idempotent replay |
| Power loss | 100 cuts per operation; `fsck_hfs` zero repairs |
| macOS differential | Same tree/names/metadata; `diskutil verifyVolume` |
| Unicode | Composed/decomposed/case/length fixtures; macOS-created names |
| Metadata parity | BSD/Finder/flags/timestamps/CNID/volume header |
| Device | Disposable volume first; then a backed-up library; adapter matrix |

No gate is passed by a host test alone. No write test runs on the only copy of
the library. No release without the `docs/validation.md` scorecard.

## 12. Open decisions

D-03 (journaled writes) and D-04 (Helikon-OS-owned replay) are accepted. The
remaining writer decisions are:

- D-05 cache/dirty-set limits.
- D-06 is accepted; the HFS+ decomposer implementation and macOS name corpus
  remain follow-up work, not an open policy choice.
- D-11 anti-corruption scope (separate from metadata journaling).
- D-12 battery/charger write gate.

Do not implement a policy before the owner records it. If a test forces a choice,
record the conflict in `docs/decisions.md`.

## 13. Related

- [`invariants.md`](invariants.md) — I-01–I-15
- [`architecture.md`](architecture.md) — layers and boundaries
- [`testing.md`](testing.md) — test layers
- [`validation.md`](validation.md) — release gates
- [`failures.md`](failures.md) — error handling
- [`decisions.md`](decisions.md) — open choices
- Paper §9, §10.4, §19, §21, §24, §28.2
