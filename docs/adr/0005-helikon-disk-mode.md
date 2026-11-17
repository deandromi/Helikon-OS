# ADR-0005 — Helikon Disk Mode: an exclusive writable USB LUN

> Status: accepted
> Date: 2026-11-11
> Owner: project owner
> Supersedes: the blanket writable-LUN rejection in D-09 and D-38, for the
> explicit Helikon Disk Mode only
> Related: D-54, [`disk-mode.md`](../disk-mode.md),
> [`architecture.md`](../architecture.md), [`invariants.md`](../invariants.md)

## Context

The normal Helikon-OS USB profile is read-only. D-09 and D-38 rejected a
writable mass-storage LUN because a host write path would bypass the firmware
HFS+ transaction engine and its crash-consistency model.

The 6G Apple firmware uses LBA28 and is limited to 128 GiB. The owner needs a
Helikon-owned way to write to the iPod without Apple Disk Mode, and the project
needs LBA48 support to go beyond that limit. A read-only LUN plus a future
service mode does not provide a bulk host write path on macOS today.

The key distinction is ownership:

- a firmware-owned write is a write while the firmware has mounted HFS+; it must
  go through `hfs_txn`;
- a host-owned write is a write while the firmware has **not** mounted HFS+ and
  the host filesystem is the only writer. This is exactly how Apple Disk Mode
  works.

A writable LUN is unsafe only when both writers can run at the same time. An
exclusive mode removes that condition.

## Decision

Helikon-OS adds a dedicated, explicit, exclusive **Helikon Disk Mode** with a
writable USB mass-storage LUN.

1. **Normal firmware USB remains read-only.** D-09/D-38 are unchanged for the
   normal profile: read-only LUN, HID, charge-only, optional service mode.
2. **Disk Mode is a separate mode**, entered only by an explicit user action
   (Settings > Disk Mode, later an optional boot gesture). It does not run the
   player, the VFS or an HFS+ mount.
3. **The default LUN is the data volume**: the largest HFS+ partition, exposed
   as a bare volume. The host does not see the partition map.
4. **The whole-disk LUN is an expert mode**, read-only by default and writable
   only as an explicit restore mode with a confirmation and a backup reminder.
5. **The firmware never mounts HFS+ while the host owns the LUN.** It quiesces
   audio, DMA, VFS and caches before enumeration and reboots after eject.
6. **Safe eject is mandatory.** The firmware flushes ATA before reporting
   success, reports unsafe removal if the flush fails, and checks the journal
   after the host releases the volume.
7. **The bootloader is unchanged and read-only.** Disk Mode is a firmware mode,
   not a second image path and not a bootloader write path.
8. **LBA48 is runtime-probed.** The build enables the code path, but the device
   must prove LBA48 and the real capacity before a LUN is exposed.
9. **The first write test requires a verified backup** and a disposable or
   fully backed-up volume.
10. **The migration beyond 128 GiB is host-side.** The first choice is a second
    larger HFS+ partition in the free space; growing the existing partition is
    expert-only. The firmware never formats, erases or repartitions.

## Alternatives

| Option | Why not |
|---|---|
| Keep read-only LUN and wait for service mode | Does not give macOS a bulk write path now and does not address LBA48 capacity. |
| Writable LUN in the normal firmware | Two writers and no explicit user consent; the original D-09/D-38 objection applies. |
| Writable whole-disk LUN only | Too dangerous as a default; a single mistake can destroy the partition map. |
| A separate Disk Mode firmware image | The loader has one image path (D-02/T-050); a second image would reopen a rejected A/B design. |
| A bootloader USB mode | The bootloader is read-only (B-04); adding writes there violates the rescue boundary. |

## Consequences

Positive:

- macOS can write to the iPod without Apple Disk Mode and without an HFS+ driver
  in the companion.
- LBA48 can address cards larger than 128 GiB.
- The normal USB profile stays read-only; the old safety rule remains true for
  normal operation.
- Disk Mode does not depend on the HFS+ writer, so it can proceed while F2/T-055
  is blocked.
- The whole-disk mode gives a backup/restore path and enables a host-side
  capacity migration.

Negative:

- The firmware needs a USB device stack, VIC/IRQ handling and a new test surface.
- Host writes bypass the firmware transaction engine by design. The safety story
  becomes "exclusive mode + host journal + flush/eject + fsck", not "every
  firmware metadata change is transactional".
- A host can corrupt the volume or write a bad partition map in whole-disk mode.
  The default data-volume LUN and the read-only whole-disk default limit this.
- The 6G iFlash path must prove LBA48; if it does not, the >128 GiB goal is
  blocked on this hardware.
- Apple firmware may not mount a volume that was grown beyond 128 GiB. The safe
  second-partition option keeps the Apple partition intact.

## Safety invariants

- Normal firmware USB never exposes a writable LUN.
- Disk Mode never mounts HFS+ while the host owns the LUN.
- Disk Mode never writes the bootloader, NOR, PMU or partition map by itself.
- Disk Mode flushes before it reports eject success.
- Disk Mode reboots before the firmware mounts the host-written volume again.
- A dirty or invalid journal is reported; replay is bounded, idempotent and goes
  through `hfs_txn`.
- A verified backup is required before the first write test.

See I-16 in [`invariants.md`](../invariants.md).

## Implementation

The canonical plan is [`disk-mode.md`](../disk-mode.md). The first tasks are:

- T-108: P0 host evidence in Apple Disk Mode;
- T-109: read-only ATA/LBA48/APM probe image;
- T-110: USB device core and enumeration;
- T-111: read-only MSC LUN;
- T-112: writable MSC LUN with flush/eject;
- T-113: integration, UI and safe exit;
- T-114: whole-disk mode and capacity migration;
- T-115: DMA, throughput, fuzz and release evidence.

No code is implemented by this ADR.
