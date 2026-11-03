# Architecture — how the system fits together

> Purpose: boot chain, layers, module boundaries, memory budget, security and
> recovery. This is a design, not an implementation.
> Status: living document
> Owner: architecture lead
> Last updated: 2026-11-03
> Source: design paper §5–§19, §21, §24; `docs/hfsplus-write-engine.md`

**A host HFS+ read core and a read-only diagnostic bootloader core exist.**
The flashable bootloader, kernel, block layer, VFS, player, UI and writer below
are planned. Nothing in this file is evidence
that Helikon-OS boots, mounts, plays, writes or updates.

## 1. Boot chain

```text
Power-on
  |
  +--> Apple NOR boot ROM
         |
         +--> Apple firmware (untouched)
         |
         +--> Helikon-OS rescue bootloader (NOR)
                - native HFS+ read-only
                - manifest magic/board/version/load/size/SHA-256/signature
                - boot-state read only
                - Apple / Helikon-OS / recovery / USB menu
                - load validated image to DRAM
                       |
                       v
                Helikon-OS firmware
                  board HAL
                    block device
                      HFS+ read core
                      HFS+ write engine
                        catalog B-tree mutation
                        allocation bitmap
                        extents + overflow
                        journal / transaction commit
                      VFS
                        database (host-built first)
                        player/audio
                        UI
                          skin compatibility (documented subset)
                        app/game sandbox (optional)
                        USB read-only / optional service mode
                        diagnostics
```

The bootloader is a **rescue loader**, not a small OS. It has no overlay parser,
no write path, no plugins, no audio, no database and no shell. It preserves the
Apple NOR path and DFU recovery. `--single` is never used.

## 2. Layers and responsibilities

| Layer | Owns | Must not own |
|---|---|---|
| Board HAL | Registers, clocks, PMU, ATA, LCD, input, codec, ADC/RTC/piezo | Filesystems, UI, product policy |
| Block | Sector I/O, 512/4096 translation, flush, power state, error propagation | Paths, files, caches, filesystem policy |
| HFS+ read core | Volume/alternate header validation, journal detection, B-tree parse, catalog read, extents read, Unicode decode | Writes, UI, boot policy |
| HFS+ write engine | Catalog/bitmap/extents mutation, CNID allocation, data allocation, metadata parity | Journal format, UI, boot |
| Journal/transaction | Log append, commit, barrier, replay, abort, checksum/sequence validation | Product policy, UI |
| VFS | Lookup, iterate, handles, capabilities, path/name rules | Hardware registers, journal internals |
| Database | Immutable metadata/index, validation, stale-entry fallback | Playback state, catalog mutation |
| Player | Buffer, decode, output, gapless/seek, volume safety | Filesystem internals |
| UI | Screens, focus, input mapping, rendering, error states | Storage policy, raw block access |
| Skin compatibility | Documented WPS subset parse/scene mapping, caps/fallback | Raw storage, playback policy, full WPS engine |
| App/game sandbox | Runtime, capabilities, quotas, crash boundary, package trust | Raw block/NOR/PMU/GPIO access |
| USB | Read-only LUN, descriptors, optional framed service mode | Filesystem writes, raw block writes |
| Diagnostics | Events, counters, bounded logs, recovery guidance | Product policy, auto-repair |

There is no overlay layer, no namespace merge and no second write path. Every
metadata mutation goes through the HFS+ write engine and the journal/transaction
layer. A VFS `WRITE` capability means a native HFS+ transaction, not an overlay
write and not a direct sector write.

## 3. Data flow

### Read path

```text
UI/player/database -> VFS lookup/open/read -> HFS+ read core
  -> block device -> ATA/HAL -> storage
```

The reader validates volume headers, B-tree nodes, extents and names. It never
modifies the journal. A dirty journal returns `JOURNAL_DIRTY` and mounts
read-only. Unsupported objects are listed with `UNSUPPORTED`, not hidden.

### Write path

```text
VFS create/write/rename/delete -> HFS+ write engine
  -> transaction dirty set (catalog, bitmap, extents, volume header)
  -> journal records + checksum/sequence
  -> flush/barrier
  -> commit journal header
  -> apply metadata blocks
  -> flush/barrier
  -> mark clean / acknowledge
```

Data is written before the metadata that exposes it. A failed transaction aborts
and preserves the last consistent state. A transaction that cannot fit its dirty
set returns `NO_SPACE` before changing anything. One writer is serialized per
volume.

### Mount path

```text
block device -> partition scanner -> HFS+ candidate
  -> primary volume header; alternate header fallback
  -> journal presence/state
  -> clean? mount read-write : mount read-only (JOURNAL_DIRTY/CORRUPT)
  -> write engine replays a valid dirty journal before first write
```

A non-HFS+ candidate returns `NOT_HFS`; there is no FAT fallback. A read-only
mount never writes the journal. A volume that `fsck_hfs` reports dirty/damaged is
not written.

## 4. Module boundaries and interfaces

The paper proposes a block device interface:

```c
struct block_device {
    int  (*read)(void *ctx, uint64_t lba, unsigned int count, void *buf);
    int  (*write)(void *ctx, uint64_t lba, unsigned int count, const void *buf);
    int  (*flush)(void *ctx);
    int  (*power)(void *ctx, enum power_state state);
    struct device_info info; /* logical/physical sector size, capacity, flags */
};
```

The VFS exposes capabilities, not format leaks:

```text
READ, WRITE, DIRECTORY, SEEKABLE, RESOURCE_FORK, SYMLINK
```

`WRITE` means a native transaction. `RESOURCE_FORK` and `SYMLINK` are optional
until a writer exists; a file with one is reported `UNSUPPORTED`, never hidden.

Errors are specific at the boundary: `NOT_FOUND`, `NOT_DIR`, `IS_DIR`,
`EXISTS`, `UNSUPPORTED`, `IO`, `NO_SPACE`, `READ_ONLY`, `CORRUPT`,
`JOURNAL_DIRTY`, `NAME_INVALID`, `DB_STALE`.

## 5. Power model

The paper's explicit states:

```text
ACTIVE -> IDLE -> STORAGE_GATED -> STORAGE_DEEP -> SYSTEM_SLEEP -> OFF
```

Every transition names its wake source and required restore steps. A write is
durable only after a successful flush/barrier; a flush error is returned to the
caller. No implicit sleep from the backlight path, no periodic early-boot
callback, no cached PMU/backlight state without invalidation. Writes are refused
below the owner-defined battery/charger threshold (D-12).

## 6. Memory budget

64 MiB is a hard budget. These are **design targets** from paper §19.2/§28.3.2,
not measurements:

| Area | Budget | Notes |
|---|---:|---|
| Kernel, HAL, block, HFS+ read | 3–5 MiB | Code, stacks, bounded node/name caches |
| HFS+ write engine and journal | 2–4 MiB | One transaction dirty set, log buffer |
| Audio buffers and codecs | 16–24 MiB | Sized for disk sleep and gapless; measured |
| Database mapped pages | 4–8 MiB | Read-only mapping, not a full copy |
| UI, fonts, artwork | 3–5 MiB | One screen, one theme, bounded image cache |
| App/game sandbox (optional) | 4–8 MiB | Quota, not reserved when unused |
| Diagnostics and recovery | 2–3 MiB | RAM ring, crash dump |
| Reserve | 4 MiB | Fragmentation and emergency allocation |

Every cache has a maximum, hit/miss counters and an eviction policy. No unbounded
`core_alloc`. A transaction that cannot fit fails with `NO_SPACE` before it
changes anything.

## 7. Security model

- Signed firmware manifest; verify magic, board, version, load address, size,
  SHA-256 and signature before jump.
- Boot-state file is read-only from the bootloader and checksummed; the firmware
  writes it only through the HFS+ transaction engine.
- Read-only USB by default; no writable mass-storage LUN.
- Metadata only through the transaction engine, with ordering and checksums.
- Journal replay is bounded and idempotent; any journal/checksum failure mounts
  read-only.
- Data checksums and scrub detect corruption without silent repair.
- Path names are length-checked; no traversal.
- Legacy plugins are excluded; apps/games run only in the sandbox with
  capabilities, quotas and a crash boundary.
- DFU installer verifies the image and refuses `--single`.
- No secrets on the device; no network stack on the device.
- No auto-format, no journal deletion, no metadata "repair" as a first response.

## 8. Update and recovery flow

1. Firmware image + manifest is staged under `/.helikon/update/` through the
   HFS+ transaction engine.
2. Staging files are flushed, read back and verified (size/hash).
3. The user explicitly chooses "install update".
4. The bootloader reads the manifest/image read-only, verifies signature, board,
   version, load address and size.
5. On success, the bootloader loads the image and the firmware commits a
   boot-state record through the transaction engine.
6. On failure, the previous verified image remains bootable; recovery offers
   rollback and DFU instructions.
7. Bootloader changes are DFU-only with their own verification. They are never
   staged on HFS+ and never use `--single`.

There is no auto-update, no silent install and no writable USB path.

## 9. Database, player and UI boundaries

- The database is an immutable, host-built cache first; an incremental
  on-device builder is v1.x and open (D-07). It is never the source of truth.
  A stale/missing entry falls back to a catalog scan and reports `DB_STALE`.
- The player owns buffering, decode, output and playback state; it does not parse
  HFS+ or write metadata directly.
- The UI is retained-mode and declarative: `screen -> widgets -> layout ->
  renderer`, `input -> focus -> action`. One default theme; optional skin subset
  with hard caps and fallback.
- Settings, playlists, resume state and logs are ordinary native files under
  `/.helikon/`, written through the transaction engine. Diagnostics never cause
  a write on a failing filesystem; logs stay in a bounded RAM ring.

## 10. What this architecture rejects

- Namespace overlay, `/.rockpod-rw`, shadow namespace or hidden container.
- FAT mount or FAT write path.
- Bootloader filesystem writes.
- Non-transactional metadata writes.
- Journal bypass/deletion/zeroing.
- Auto-repair, format/erase/restore as a firmware action.
- Writable USB mass storage.
- Full Rockbox plugin tree or raw-hardware plugin API.
- Full WPS engine or bundled proprietary themes.
- Unsigned firmware or plugins.
- Periodic early-boot power callbacks / cached PMU state without invalidation.
- Unmeasured overclocking, undervolting or charging changes.

## 11. Related

- [`hfsplus-write-engine.md`](hfsplus-write-engine.md) — writer deep dive
- [`file-map.md`](file-map.md) — owning files/areas
- [`invariants.md`](invariants.md) — safety rules
- [`failures.md`](failures.md) — error handling
- [`decisions.md`](decisions.md) — open choices
- Paper §19 (proposed greenfield architecture), §20 (risks), §21 (exclusions)
