# Invariants — what must never break

> Purpose: numbered safety invariants. Each one is testable or inspectable.
> Status: living document
> Owner: project owner; every contributor
> Last updated: 2026-11-03
> Source: design paper §1, §5, §8–§10, §16, §21, §24, §28

These are not style preferences. A change that violates one is rejected, even if
it makes a demo work. "Verify" describes the evidence that would prove the
invariant; no Helikon-OS implementation exists yet.

## I-01 — Native HFS+ is writable; no overlay, no FAT

**Statement.** The data partition is native HFS+ v4 and is written through the
HFS+ engine. There is no `/.rockpod-rw`, no namespace overlay, no shadow
namespace, no hidden container and no FAT filesystem mount. A non-HFS+ volume
returns `NOT_HFS`.

**Why it matters.** The product promise is that the iPod owns ordinary HFS+
files that macOS can read. An overlay hides writes; a FAT mount is a second
filesystem and a second set of bugs.

**How to verify.** Inspect the firmware for FAT mount/write symbols and overlay
loaders. Mount a FAT32 test volume and confirm `NOT_HFS`, not a mount attempt or
crash. Create a file on a disposable HFS+ volume and confirm macOS sees the same
file after unmount.

**What would violate it.** A live `/.rockpod-rw` path, a namespace merge, a FAT
driver, a hidden container, or a second write path.

**Paper:** §4, §9.1, §10.2, §28.2.1.

## I-02 — The bootloader is read-only

**Statement.** The bootloader reads native HFS+, verifies a firmware manifest,
reads boot state and loads an image. It links no filesystem write code and never
writes the filesystem.

**Why it matters.** A write path before the OS bypasses the transaction engine
and can corrupt a volume before recovery is available. The paper's rescue loader
is deliberately not a small OS.

**How to verify.** Inspect the bootloader image/map for write symbols and HFS+
write modules. Run a boot cycle and compare a whole-volume hash outside the
bootloader/firmware files. Confirm no journal or metadata change.

**What would violate it.** Bootloader code that creates, renames, deletes,
truncates, writes boot state, replays the journal, or "repairs" metadata.

**Paper:** §5.3, §19.1, §21 (item 5).

## I-03 — Every metadata change goes through a transaction

**Statement.** Catalog records, allocation bitmap, extents/overflow, volume
header, alternate header and journal changes are grouped into a validated
transaction. One writer is serialized at the filesystem boundary.

**Why it matters.** A torn catalog or bitmap update loses files. The journal is
what makes a multi-block metadata change recoverable.

**How to verify.** Code review: every mutation API takes a transaction handle;
no direct catalog/bitmap/extents writes outside the engine. Fault injection at
each boundary leaves either the old or the new consistent state.

**What would violate it.** A "quick" direct metadata write, a write path that
skips the journal, a second writer, or an application writing raw blocks.

**Paper:** §9.2, §9.4, §9.6, §19.1.

## I-04 — `fsck_hfs` reports zero repairs after crash tests

**Statement.** After every crash and power-cut test, macOS `fsck_hfs` reports
zero repairs and the volume mounts. A single catalog or bitmap inconsistency
blocks release.

**Why it matters.** `fsck_hfs` is the external oracle. If it wants to repair
anything, the writer is wrong, even if the device still boots.

**How to verify.** For each operation and cut point: copy the disk image, boot
and mount, run `fsck_hfs -n` (or the recorded read-only equivalent), capture
the full output, and compare the recovered tree with the committed state.

**What would violate it.** Any required repair, a "mostly clean" result, a
skipped crash point, or a release note that hides an unexecuted gate.

**Paper:** §9.4, §24, §26.7, §28.6.

## I-05 — Dirty or invalid journal means read-only

**Statement.** A journaled volume with an invalid journal mounts read-only. A
valid dirty journal is replayed only by the write engine, before the first
write. A read-only mount never modifies the journal.

**Why it matters.** Ignoring a dirty journal and writing anyway can destroy
committed metadata. Replay is a write operation and belongs to the writer.

**How to verify.** Mount a copied dirty-journal image read-only and hash the
journal before/after. Confirm no modification and a `JOURNAL_DIRTY` status.
With the write engine, cut power before/during replay and confirm idempotent
recovery.

**What would violate it.** A mount path that writes the journal, a read-only
path that "fixes" it, or a write that starts before replay completes.

**Paper:** §8.3, §9.3, §9.4.

## I-06 — No silent journal bypass

**Statement.** Never disable, delete, truncate, ignore or pretend a dirty journal
is clean to make a write fit. If the journal cannot be used, the volume is
read-only.

**Why it matters.** The journal is the crash-consistency mechanism. Bypassing it
turns a recoverable interruption into metadata corruption.

**How to verify.** Inspect all mount/write paths for "journal off", "ignore
journal", "delete journal" and "mark clean" operations. Test a volume with a
damaged journal and confirm a clear read-only error.

**What would violate it.** A hidden fallback, a settings flag that disables
journaling on a real library, or a "repair" that zeroes the journal header.

**Paper:** §9.3, §21 (items 2, 8), §28.2.1.

## I-07 — No destructive repair

**Statement.** No format, erase, restore, repartition, journal deletion or
metadata zeroing as a software repair. A failed transaction fails closed and
preserves the last consistent state; repair is a macOS/user action from a
backup.

**Why it matters.** An embedded repairer can turn a recoverable inconsistency
into permanent data loss. The paper treats `fsck_hfs` as an oracle, not a
feature to reimplement.

**How to verify.** Search the firmware and host tools for destructive fallbacks.
Every error path must return a specific code and a next action, not "format".

**What would violate it.** An auto-format, auto-erase, auto-repair, journal
delete, or a support script whose first step is "restore".

**Paper:** §9.4, §21 (items 4, 8, 21), §26.11.

## I-08 — Never install a bootloader with `--single`

**Statement.** `--single` is never exposed or used. It destroys the Apple NOR
boot path. The dual-boot installer preserves Apple firmware and recovery.

**Why it matters.** Losing the Apple path can make the device unrecoverable
without hardware intervention.

**How to verify.** Inspect the installer's argument parsing and help text.
Confirm `--single` is rejected and the Apple path remains bootable after a
recorded install/uninstall test.

**What would violate it.** A hidden `--single`, a "single boot" shortcut, or an
installer that removes the Apple selector.

**Paper:** §5.1, §5.2, §16.2, §21 (item 6).

## I-09 — Verified backup before write tests

**Statement.** No write test runs on the only copy of the library. A backup is
made, hashed, and read back before any test that can modify the volume.

**Why it matters.** A writer bug can destroy the library. A backup that was
never read back is not a verified backup.

**How to verify.** Record the backup path, size, SHA-256, read-back result,
`fsck_hfs -n` of the source and the recovery plan. The device test cannot start
without this record.

**What would violate it.** "It's probably backed up", a backup on the same
device, or a write test justified by "it boots".

**Paper:** §22, §24, §26.4, §26.11, Appendix H.2.

## I-10 — No support claim without a recorded device run

**Statement.** A model, adapter, receiver, skin or feature is not "supported"
until a recorded run in `device-matrix.md` includes the exact device, revision,
adapter, firmware hash, test, result and evidence.

**Why it matters.** Host tests and a successful link are not hardware
validation. The donor's history shows adapter and power differences matter.

**How to verify.** Every support claim links to a matrix row with evidence. A
claim without a row is removed or marked `unproven`.

**What would violate it.** "Works on 6G" from a host test, a 7G claim before a
board audit, or a battery claim without measurements.

**Paper:** §1, §24, §28.4.5, §28.6.

## I-11 — Never write a volume `fsck_hfs` reports dirty or damaged

**Statement.** If `fsck_hfs` reports the volume dirty or damaged, Helikon-OS
mounts read-only and asks the user to repair it on macOS first.

**Why it matters.** Writing a damaged volume can make the damage worse and
invalidates the oracle.

**How to verify.** Mount a known-damaged test image; confirm `READ_ONLY` or
`CORRUPT`, no write attempt and a clear next action. Check the journal/volume
state before every write transaction.

**What would violate it.** A write path that ignores the dirty flag, or a
"repair on mount" feature.

**Paper:** §1, §9.4, §21 (item 21), §26.11.

## I-12 — Unsupported objects are listed, not hidden

**Statement.** Hardlinks, symlinks, compressed files, resource forks and other
unsupported objects appear in directory listings and return a specific
`UNSUPPORTED` reason. They are never silently omitted.

**Why it matters.** Silent skipping makes real files look missing and hides
format bugs. The donor's old behaviour caused large plugin data files to
disappear.

**How to verify.** Create fixtures for each unsupported type. Confirm `readdir`
returns the entry and `open`/`stat` returns `UNSUPPORTED` with a reason. Confirm
the UI displays the reason.

**What would violate it.** A filter that drops entries, a generic "not found",
or a silent skip in the catalog iterator.

**Paper:** §8.3, §10.2, §10.4, §21 (item 9).

## I-13 — Metadata parity and Apple key ordering

**Statement.** The writer preserves BSD info, Finder info, flags and timestamps;
allocates CNIDs monotonically without reusing live IDs; keeps the volume
header's next-CNID/free-block counts consistent; and uses Apple's
`FastUnicodeCompare` plus a documented normalization policy for catalog order,
lookup, create, rename and duplicate detection.

**Why it matters.** macOS and `fsck_hfs` will disagree with the device about
where a name belongs if key ordering or metadata is wrong. That is catalog
corruption, not a cosmetic difference.

**How to verify.** Differential tests against macOS-created trees: names,
ordering, CNID uniqueness/monotonicity, BSD/Finder info, timestamps, extents and
`fsck_hfs` output. Test composed/decomposed names and case variants.

**What would violate it.** Exact-byte lookup as the write policy, a zeroed
metadata field, CNID reuse, or a different name comparison from macOS.

**Paper:** §9.2, §10.4, §28.2.2.

## I-14 — Signed firmware before jump

**Statement.** The bootloader verifies magic, board ID, version, load address,
size, SHA-256 and signature before jumping to a firmware image. Unsigned or
mismatched images are rejected; the previous verified image remains bootable.

**Why it matters.** An unsigned image is a trust and bricking risk. The update
path must fail safe.

**How to verify.** Test bad magic, wrong board, wrong version, truncated image,
bad hash, bad signature and a power cut during staging. Record the rejection and
the fallback image.

**What would violate it.** A "debug bypass", a fixed-address jump without
verification, or a bootloader that accepts an image because it is present.

**Paper:** §5.3, §16.2, §19.3, §28.2.5.

## I-15 — One writer and a power gate

**Statement.** At most one write transaction is active per volume. Writes are
refused below the battery/charger threshold defined by the owner (D-12), and a
transaction that cannot fit its dirty set fails with `NO_SPACE` before changing
anything.

**Why it matters.** Concurrent writers and low-battery cuts are avoidable
corruption sources. Bounded dirty sets keep the 64 MiB budget honest.

**How to verify.** Concurrency tests, allocation-failure tests, low-battery
injection, and a check that `NO_SPACE` leaves the volume unchanged. Record the
threshold and measurement method.

**What would violate it.** Two writers, an unbounded transaction, or a write
that starts on a low battery "to finish quickly".

**Paper:** §9.4, §19.2, §20, §28.3.2.
