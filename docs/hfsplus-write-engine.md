# HFS+ write engine — native writer and journal deep dive

> Purpose: the defining subsystem: native HFS+ metadata mutation, journal,
> transaction ordering and `fsck_hfs` compatibility.
> Status: living design document; **no implementation exists**
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

**Current status:** design only. Do not infer an implementation, a test result or
a device run from this document.

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

A journaled volume cannot be safely written by ignoring the journal. The release
design is a block transaction log:

1. Reserve/append journal blocks.
2. Write transaction records with sequence numbers and checksums.
3. Flush/barrier.
4. Commit the journal header.
5. Apply the metadata blocks.
6. Flush/barrier.
7. Mark the journal clean / acknowledge.

Replay on mount re-applies committed records and discards incomplete ones. Replay
must be idempotent and bounded.

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

## 7. Unicode, names and metadata parity

### 7.1 Names

- HFS+ stores decomposed UTF-16.
- Implement Apple's `FastUnicodeCompare` for catalog ordering and comparison.
- Choose a normalization policy: decompose before insertion, or explicitly
  refuse a name that could create two catalog keys that normalize to the same
  string. The policy is D-06 and must be documented and tested.
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

The paper allows a non-journaled writer only on a volume explicitly formatted
without a journal:

- It is a **disposable-volume prototype**, not a release path.
- Journaled volumes mount read-only under this prototype.
- It must still implement catalog, bitmap and extents writes; it is not a
  shortcut around B-tree mutation.
- A power loss may require `fsck_hfs`; the prototype must not be used on a real
  library.
- The prototype is labelled "prototype only" in code, UI and docs.

The release path is journaled. Do not silently use the prototype on a journaled
volume or disable journaling to make a write fit.

## 10. API shape (design)

The paper proposes an explicit transaction API; names are illustrative until the
interface is frozen (T-002):

```c
int  hfs_begin(struct hfs_txn *tx);
int  hfs_catalog_insert(struct hfs_txn *tx, ...);
int  hfs_catalog_update(struct hfs_txn *tx, ...);
int  hfs_catalog_delete(struct hfs_txn *tx, ...);
int  hfs_bitmap_alloc(struct hfs_txn *tx, uint32_t blocks, ...);
int  hfs_bitmap_free(struct hfs_txn *tx, uint32_t start, uint32_t blocks);
int  hfs_extents_insert(struct hfs_txn *tx, ...);
int  hfs_journal_commit(struct hfs_txn *tx);
int  hfs_txn_abort(struct hfs_txn *tx);
```

Rules:

- The VFS sees ordinary files; there is no "native view" and "overlay view".
- Every mutation takes a transaction handle.
- No direct catalog/bitmap/extents write outside the engine.
- Errors are specific: `NO_SPACE`, `READ_ONLY`, `CORRUPT`, `JOURNAL_DIRTY`,
  `NAME_INVALID`, `IO`, `UNSUPPORTED`.
- `WRITE` means a native transaction, not a sector write.

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

- D-03 journal strategy (release journaled vs prototype only).
- D-04 journal ownership (device replay vs macOS-assisted).
- D-05 cache/dirty-set limits.
- D-06 Unicode policy.
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
