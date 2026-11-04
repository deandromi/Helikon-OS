# Failures — what errors mean and what not to do

> Purpose: error taxonomy, troubleshooting order and destructive-action
> warnings.
> Status: living document
> Owner: diagnostics lead; every agent must follow the warnings
> Last updated: 2026-11-04
> Source: design paper §8.3, §9–§11, §16, §20, §24, §28

Helikon-OS has no implemented error path yet. These are design meanings and the
safe response. Do not invent a numeric code or a recovery action that is not
recorded here or in the paper.

## 1. Error taxonomy

| Code | Meaning | Typical cause | Safe response | Never do |
|---|---|---|---|---|
| `NOT_HFS` | The candidate volume is not HFS+ v4. | FAT32, APFS, HFSX, wrong partition, corrupt signature. | Show the volume type and explain that Helikon-OS mounts HFS+ only. Read-only. | Do not mount FAT, "convert" in firmware, or format. |
| `JOURNAL_DIRTY` | The journal is present and not clean. | Power loss, interrupted write, previous crash. | Read-only mount; write engine replays a valid journal before first write, or asks the user to repair on macOS. | Do not ignore, delete, truncate or zero the journal. |
| `CORRUPT` | Structural validation failed. | Bad volume header, B-tree node, bitmap, extents, checksum. | Read-only mount or fail closed; report the exact structure and offer log export. | Do not auto-repair, zero metadata, or write "to see if it fixes it". |
| `NO_SPACE` | The transaction or allocation cannot fit. | Dirty set too large, bitmap full, extent table full. | Abort before changing anything; report how much was needed. | Do not continue a partial transaction or ignore the error. |
| `READ_ONLY` | The volume or mount is read-only. | Dirty journal, corruption, explicit read-only, missing write support. | Keep reading/playing; explain why writes are unavailable. | Do not silently retry writes or fake success. |
| `UNSUPPORTED` | The object or feature exists but is not implemented. | Symlink, hardlink, compressed file, resource fork, unsupported skin tag. | List the entry; show the reason; keep it visible. | Do not hide the file, return `NOT_FOUND`, or crash. |
| `NAME_INVALID` | The name violates the HFS+ name policy. | >255 UTF-16 units, NUL, `/`, reserved `.`/`..`, invalid normalization. | Reject the create/rename with a specific reason. | Do not silently truncate or rewrite the name. |
| `DB_STALE` | The database entry no longer matches the catalog. | File moved/deleted/renamed outside the database. | Fall back to a catalog scan and refresh the entry. | Do not report "file missing" from a stale DB. |
| `IO` | The block device or transport failed. | ATA error, short transfer, timeout, flush failure. | Propagate the error; retry only within the documented policy; fail the transaction. | Do not swallow a flush error or assume the device is fine. |
| `BOOT_INFO_INVALID` | The loader boot-info page failed validation. | `r0 = 0`, bad magic/version/size/CRC/board, image range mismatch, or a mismatched manifest copy. | Continue in the read-only diagnostic mode; show the stage and code; do not write bootstate. | Do not treat a RAM page as a trust anchor or guess the missing facts. |

`NOT_FOUND`, `NOT_DIR`, `IS_DIR`, `EXISTS` are VFS lookup errors, not filesystem
corruption. They must not trigger repair. `BOOT_INFO_INVALID` is the firmware
handoff error added by D-28; it is not a filesystem error.

**On-disk NUL names:** macOS HFS+ volumes contain private records whose names
include NUL code units (for example the `.HFS+ Private Data` directory). The
writer rejects NUL in a new name, but a read-only directory scan must not let
one such record abort the whole directory or hide unrelated entries. The read
path escapes NUL as U+2400, marks the entry `unsupported` and continues path
lookup (T-039).

## 2. Mount failure decision tree

```text
Mount requested
  |
  +-- Partition scan fails / no HFS+ candidate
  |      -> NOT_HFS; show volume type; read-only; do not format
  |
  +-- Volume header invalid
  |      +-- Alternate header valid -> mount read-only with recovered flag
  |      +-- Both invalid -> CORRUPT; fail closed; export log; macOS repair
  |
  +-- HFSX / APFS / FAT signature
  |      -> NOT_HFS; explain HFS+ v4 only
  |
  +-- Journal present and dirty
  |      +-- Read-only mount -> JOURNAL_DIRTY; never modify journal
  |      +-- Write engine -> replay valid journal before first write
  |             +-- Replay fails -> read-only; preserve journal; report
  |
  +-- Structural validation fails (B-tree/bitmap/extents)
  |      -> CORRUPT; read-only; no repair; export diagnostics
  |
  +-- Volume reports dirty/damaged by fsck_hfs
  |      -> read-only; ask user to repair on macOS first
  |
  +-- Clean -> mount read-write; enable transactions
```

The order matters: identify the volume, validate headers, inspect the journal,
validate structures, then decide read-only vs read-write. Never start with a
write or a "repair".

## 3. Journal and replay failure

- A journal with invalid header, bad checksum, impossible sequence or truncated
  record is **not** replayed. Mount read-only and report `JOURNAL_DIRTY` or
  `CORRUPT`.
- Replay must be idempotent: replaying the same records twice must produce the
  same state. A cut during replay must be resumable.
- Replay is bounded by the journal window and the dirty-set budget. It must not
  allocate unbounded memory.
- After replay, the journal is marked clean only after the applied metadata is
  durable.
- A replay failure must preserve the journal and the last consistent metadata.
  It must not zero the journal to "start fresh".

**Do not:** delete the journal, format the volume, run a "repair" tool from the
firmware, or write to the volume while the journal state is uncertain.

## 4. `fsck_hfs` failure

`fsck_hfs` is the external oracle. If it reports repairs, damage or an
unreadable catalog, the build is wrong.

1. Stop the test. Do not run another write.
2. Preserve the disk image and the full `fsck_hfs` output.
3. Record the last operation, cut point, firmware hash and adapter.
4. Compare the recovered tree with the expected committed state.
5. File a writer defect; do not "clean" the image and retry.

**Do not** run `fsck_hfs` with repair flags on the only copy, or use a repair
result as evidence that the writer is correct. A repair means failure.

## 5. Update failure

| Failure | Safe response |
|---|---|
| Bad manifest magic/board/version | Reject; keep the current image; show the expected vs actual field. |
| Bad hash/size/signature | Reject; keep the current image; log the manifest. |
| Truncated image | Reject; keep the current image; offer re-download. |
| Power cut during staging | Old image remains bootable; staging files are incomplete; verify before install. |
| Power cut during boot-state commit | The loader validates the single image and the boot-state; an inconsistent slot fails closed to recovery, not to a second image. |
| New image boots but fails self-test | Boot-state is not promoted to `GOOD`; the loader shows recovery. Recovery is DFU/Apple restore; there is no A/B rollback. |
| Bootloader update fails | Recover through DFU; never `--single`; Apple path preserved. |

No update is "installed" until the bootloader verifies it and the firmware
commits a boot-state record. No silent install. No writable USB mass storage.

**Single-image fail-closed (owner decision 2026-11-04, T-050).** The frozen
format has one image path. A failed, mismatched or unbootable image produces a
recovery screen and no jump; recovery is DFU/Apple restore. Do not describe a
"previous image" or an A/B rollback as implemented.

## 6. App/game sandbox failure

| Failure | Safe response |
|---|---|
| Permission denied | Clear message; no crash; log the requested capability. |
| Quota exceeded | Stop the app; return to home; preserve app-private data. |
| App crash/trap | Watchdog returns to home; log; do not panic the firmware. |
| Package signature invalid | Refuse install; explain trust failure. |
| App tries raw block/NOR/PMU/GPIO | Deny; log; treat as a sandbox escape attempt. |
| Uninstall with data | Confirm; remove app-private data only after confirmation. |

A sandbox failure must never become a kernel panic or a storage write outside
the app's granted roots.

## 7. Desktop transfer failure

| Failure | Safe response |
|---|---|
| Device not detected | Check cable/mode; read-only LUN vs service mode; do not force a mount. |
| HFS+ volume dirty | Stop; ask the user to repair on macOS; do not write. |
| Transfer interrupted | Keep the old file; verify the new file hash; retry from the host. |
| Conflict (newer device file) | Report; resolve by mtime/content hash; never silently overwrite. |
| Scrobble upload fails | Keep the queue offline; retry rate-limited; no listening history leaves the machine without consent. |
| Backup verification fails | Stop; do not perform a destructive action. |

## 8. Destructive-action warnings

These are hard stops, not troubleshooting suggestions:

- **Do not format, erase, restore or repartition** to fix a mount, journal,
  overlay, update or app error.
- **Do not delete or zero the journal**, catalog, bitmap, extents or volume
  header.
- **Do not auto-repair** or run a firmware "fsck".
- **Do not use `--single`.**
- **Do not write to a volume `fsck_hfs` reports dirty/damaged.**
- **Do not test writes on the only copy of the library.**
- **Do not hide an unsupported file** to make a listing look clean.
- **Do not claim a gate passed** because the device boots.

If a proposed fix is format/erase/restore/delete, stop and escalate to the owner.

## 9. Troubleshooting order

1. Identify the exact stage: partition, header, journal, B-tree, bitmap, extents,
   VFS, player, UI, USB, update, app, host.
2. Capture the exact code and message; do not paraphrase.
3. Capture the device state (Disk Mode, DFU, recovery, booted), firmware hash and
   adapter.
4. Check whether the volume is read-only by design (dirty journal/corruption).
5. Preserve logs/images before any retry.
6. Follow the safe response above; if it requires a destructive action, stop.
7. Record the result in `device-matrix.md` or `docs/log/`.

## 10. Related

- [`invariants.md`](invariants.md) — I-04–I-11
- [`hfsplus-write-engine.md`](hfsplus-write-engine.md) — journal/transaction
- [`verification.md`](verification.md) — evidence template
- [`device-matrix.md`](device-matrix.md) — recorded runs
- [`security.md`](security.md) — destructive-operation policy
- Paper §8.3, §9.4, §11.2, §16, §20, §24
