# Helikon Disk Mode — writable USB mass storage and LBA48

> Purpose: define the Helikon Disk Mode, the writable-LUN exception, the LBA48
> path and the capacity plan beyond the Apple 6G 128 GiB limit.
> Status: living design
> Owner: firmware/USB lead; owner decision D-54
> Last updated: 2026-11-11
> Source: D-54, [ADR-0005](adr/0005-helikon-disk-mode.md), design paper §16.2,
> §21, §28.4.2; [`firmware-architecture.md`](firmware-architecture.md);
> [`hfsplus-write-engine.md`](hfsplus-write-engine.md);
> [`device-matrix.md`](device-matrix.md)

This document is the canonical plan for Helikon Disk Mode. It does not claim
that any part of it exists. The only implementation evidence today is the
existing read-only ATA/HFS+ firmware and the separate diagnostic images; there
is no USB stack, no LUN, no probe run and no Disk Mode device result.

## 1. Scope

Helikon Disk Mode is a dedicated, explicit, exclusive firmware mode that exposes
the iPod's storage to a USB host as a mass-storage LUN.

In scope:

- USB mass-storage class (MSC), bulk-only transport (BOT), SCSI transparent
  command set;
- one writable **data-volume LUN** by default, starting at the largest HFS+
  partition;
- an expert **whole-disk LUN** for backup, restore and partition migration,
  read-only by default;
- ATA/LBA48 read, write and flush with a runtime capability probe;
- safe eject, flush, journal check and reboot on exit;
- a minimal status screen and a settings entry;
- a host-side evidence and capacity-migration workflow.

Out of scope for the first implementation:

- a writable LUN in the normal firmware USB profile;
- a second firmware image, a new bootloader path or a bootloader write;
- a filesystem format, erase, repair or repartition action in firmware;
- HFS+ writes by the firmware while the host owns the LUN;
- CE-ATA support on the original 160 GB 6G drive;
- 7G hardware;
- Windows/Linux HFS+ write support;
- a framed service-mode protocol (T-077 remains separate);
- background sync while the player UI is running.

## 2. Why this exists

Apple's 6G firmware uses LBA28 addressing for the internal disk. The practical
limit is 2^28 × 512 bytes = **128 GiB**, even when a larger iFlash SD card is
installed. The 7G uses LBA48. The 6G ATA controller and the Rockbox `ipod6g`
port support LBA48 (`HAVE_LBA48`), so the 128 GiB limit is a firmware policy
limit, not a proven hardware limit.

Helikon-OS already needs a write path that does not depend on Apple Disk Mode.
The project also needs to read and write volumes larger than 128 GiB if the
adapter and card support it. Helikon Disk Mode provides both:

1. a USB mass-storage write path owned by Helikon firmware, not Apple firmware;
2. an LBA48 ATA path that can address the full card;
3. a host-side capacity plan when Apple created only a 128 GiB partition.

## 3. The D-54 exception

D-09 and D-38 rejected a writable mass-storage LUN in the normal USB profile.
That rejection remains true. D-54 adds one narrow exception:

| Mode | LUN | Writer | Status |
|---|---|---|---|
| Normal firmware USB | read-only | nobody | unchanged |
| Helikon Disk Mode — Data | writable, largest HFS+ partition | host (macOS HFS+) | D-54 exception |
| Helikon Disk Mode — Whole Disk | read-only by default; expert writable | host | D-54 exception |
| Helikon Service Mode | no LUN; framed file operations | firmware via `hfs_txn` | T-077, future |

The exception is safe only because of the following rules:

- the mode is entered by an explicit user action, never silently;
- the firmware does not mount, read or write HFS+ while the host owns the LUN;
- the bootloader remains read-only and is never part of Disk Mode;
- normal USB remains read-only;
- the host is told to eject; the firmware flushes and checks the journal before
  it reboots;
- a writable whole-disk LUN is not the default and requires an explicit
  confirmation;
- the first device write test requires a verified backup and a disposable or
  fully backed-up volume.

This is the same ownership model as Apple Disk Mode: the host filesystem is the
only writer. The firmware transaction engine is not bypassed; it is not in the
write path because the firmware is not mounted. The optional post-eject journal
replay is a firmware write and goes through `hfs_txn`.

See [ADR-0005](adr/0005-helikon-disk-mode.md) and D-54 in
[`decisions.md`](decisions.md).

## 4. Hardware and firmware facts

| Item | Fact | Consequence |
|---|---|---|
| SoC | Samsung S5L8702, ARM926EJ-S, 64 MiB DRAM | fixed buffers, no allocator in the USB path |
| Storage | ATA PIO today; LBA48 probe exists; DMA exists in the donor loader, not in the firmware | LBA48 and DMA are separate work items |
| 6G Apple limit | LBA28, 128 GiB | LBA48 is required for a larger usable volume |
| USB core | Synopsys DesignWare OTG device, `OTGBASE 0x38400000`, PHY `0x3C400000`, IRQ 19, 7 endpoints, UTMI 16-bit | donor `usb-designware.c` / `usb-s5l8702.c` is the reference |
| USB clocks | `CLOCKGATE_USBOTG` 2, `CLOCKGATE_USBPHY` 35, `USB_DW_CLOCK 0`, `USB_DW_TURNAROUND 5` | PHY sequence must be copied from the donor, not invented |
| Interrupts | firmware currently runs with IRQs disabled; the IRQ vector panics; VIC at `0x38E00000` is not implemented | VIC/IRQ is a prerequisite, not a detail |
| Partition map | 6G/iFlash MBR may store start/size in 4096-byte virtual sectors; APM parser needs verification | the data-volume LUN avoids the MBR entirely |
| Partition policy | `select_partition()` already chooses the largest HFS+ partition | a second larger HFS+ partition is selected automatically |
| HFS+ writer | not needed while the host owns the LUN; needed only for optional post-eject replay | Disk Mode is independent of the writer |

### 4.1 Known ATA gaps

The current `firmware/hal/6g/ata.c` has these gaps; T-109 must prove or fix them
before a writable LUN is built:

1. `ata_adopt_read()` sets `total_sectors = 0xffffffff` and `lba48 = 1` without
   reading the real IDENTIFY capacity.
2. `ata_parse_geometry()` handles the IDENTIFY LBA48 words but not CE-ATA.
3. `helikon_ata_flush()` chooses `FLUSH CACHE` instead of `FLUSH CACHE EXT` in
   adopt mode because `identify_valid` is zero.
4. The LBA28 write path uses `WRITE SECTORS` (0x30) while the read path uses
   `READ MULTIPLE` (0xC4); the write path is inconsistent.
5. There is no DMA path in the firmware HAL.
6. The APM parser in `firmware/block/partition.c` reads the entry count from
   block 0 offset 4, which is Apple's `sbBlkCount`; the count is in the first
   `PM` record's `pmMapBlkCnt`. The current fixture hides this.

## 5. Mode and state machine

```text
NORMAL
  -> user selects Settings > Disk Mode
PREPARE
  stop audio/DMA, close HFS+, flush ATA, check USB power,
  scan partitions, choose LUN, initialise USB
  -> WAIT_HOST
WAIT_HOST
  wait for enumeration; 60 s timeout -> REBOOT
  -> ENUMERATED
ENUMERATED
  host configured the interface; LUN ready
  -> ACTIVE
ACTIVE
  READ/WRITE/SYNCHRONIZE CACHE; activity indicator
  -> EJECTED on host eject / START STOP UNIT
  -> VERIFY on cable removal
EJECTED
  flush, optional journal check/replay
  -> REBOOT or POWER_OFF
VERIFY
  bounded flush; journal status
  -> REBOOT/POWER_OFF or ERROR
ERROR
  show code; MENU = reboot; Apple Disk Mode/DFU = recovery
```

Rules:

- No exit while `ACTIVE`. The host must eject first.
- No HFS+ handle, VFS handle or player buffer survives `PREPARE`.
- `PREPARE` checks USB power or a charged battery; a low battery refuses the
  mode.
- `EJECTED` and `VERIFY` flush before any reboot.
- A dirty or invalid journal is reported; the optional replay is bounded and
  idempotent and uses the existing write engine.
- On any exit, reboot rather than resume the UI, so the firmware re-reads the
  host-written volume.

## 6. LUN layout

### 6.1 Data-volume LUN (default)

- `offset` = start of the largest HFS+ partition;
- `length` = partition length;
- the host sees a bare HFS+ volume (superfloppy), not the MBR/APM;
- translation: `ata_lba = partition_offset + usb_lba × (usb_block_size / ata_sector_size)`;
- no host access to the partition map, the Apple partition or the free space;
- block size: 4096 when the partition offset and length are 4096-aligned and the
  HFS+ block size is 4096; otherwise 512. ATA 4Kn always reports 4096.

This is the default because it is the smallest attack surface and avoids the
6G 4096-unit MBR quirk. It still allows normal Finder/companion writes to the
music volume.

### 6.2 Whole-disk LUN (expert)

- `offset = 0`, `length` = full disk capacity;
- block size = detected unit (512 or 4096);
- host sees the real APM/MBR and all partitions;
- read-only by default;
- writable only as an explicit restore mode with a long-press confirmation and
  a backup reminder;
- used for block-level backup, restore and partition migration.

### 6.3 Capacity discovery

The LUN must not be exposed with a guessed capacity. In order:

1. IDENTIFY words 60–61 and 100–103;
2. APM `sbBlkCount × sbBlkSize` or the MBR total sector count;
3. HFS+ `blocks × block_size` as a cross-check;
4. if none is proven, refuse the LUN and show the exact failure.

## 7. USB device stack

Proposed modules; names may change in T-110:

```text
firmware/hal/6g/vic.c/.h          VIC, IRQ 19, spurious-IRQ handling
firmware/hal/6g/usb.c/.h          DesignWare device core, PHY, clocks, EP0/1/2
firmware/usb/descriptors.c        device/config/interface/endpoint/string
firmware/usb/core.c               standard requests, SET_ADDRESS/CONFIGURATION
firmware/usb/bot.c/.h             bulk-only transport, CBW/CSW, stall/reset
firmware/usb/scsi.c/.h            SCSI commands and sense data
firmware/usb/msc.c                MSC class, LUN, read/write/flush
firmware/usb/disk_mode.c/.h       mode state, UI, ATA backend, eject/exit
```

The hardware layer is a Helikon implementation with the donor
`usb-designware.c` and `usb-s5l8702.c` as the register-level reference. No
Rockbox `kernel.h`, `semaphore`, `thread` or `malloc`. The IRQ moves packets;
the main loop performs ATA and SCSI work.

Descriptor baseline:

| Item | Value |
|---|---|
| USB version | 0x0200 (high speed) |
| Device class | 0 (class at interface) |
| EP0 max packet | 64 |
| Interface | class 0x08, subclass 0x06, protocol 0x50 |
| Endpoints | one bulk IN, one bulk OUT, max packet 512 |
| Strings | manufacturer "Helikon", product "Helikon Disk Mode" |
| Serial | avoid; do not expose a personal serial number |
| VID/PID | distinct from Apple; exact ID to be allocated in T-110 |
| INQUIRY | vendor "Helikon", product "Disk Mode", removable medium |

## 8. SCSI command set

| Command | Opcode | Behaviour |
|---|---|---|
| TEST UNIT READY | 0x00 | ready only after ATA and LUN are ready |
| REQUEST SENSE | 0x03 | fixed sense data |
| INQUIRY | 0x12 | vendor/product/revision, RMB=1 |
| MODE SENSE(6/10) | 0x1A/0x5A | write-protect bit, caching page |
| START STOP UNIT | 0x1B | eject/load; mark ejected |
| PREVENT ALLOW MEDIUM REMOVAL | 0x1E | lock/unlock |
| READ FORMAT CAPACITIES | 0x23 | capacity |
| READ CAPACITY(10/16) | 0x25/0x9E | 16-byte form for >2^32 blocks |
| READ(10/16) | 0x28/0x88 | ATA read |
| WRITE(10/16) | 0x2A/0x8A | ATA write |
| SYNCHRONIZE CACHE(10/16) | 0x35/0x91 | ATA flush |
| REPORT LUNS | 0xA0 | one LUN |
| VERIFY(10) | 0x2F | optional read/compare or GOOD |
| ATA PASS-THROUGH | 0x85/0xA1 | reject |
| vendor-specific | 0xC0+ | reject unless defined later |

GET MAX LUN and Bulk-Only Reset are handled as control requests.

## 9. Write, flush and error policy

- Stream writes in bounded chunks (64 KiB buffer, double-buffered); do not
  buffer a whole host transfer.
- ATA write completion means the data is in the device cache, not necessarily
  on media. Honour `SYNCHRONIZE CACHE` with `FLUSH CACHE EXT` (LBA48) or
  `FLUSH CACHE`.
- On eject: flush first, then report safe to disconnect.
- On surprise removal: attempt a bounded flush (for example 2 s), then power
  down ATA. Report unsafe removal if the flush fails.
- Errors: CHECK CONDITION with `MEDIUM ERROR`, `WRITE ERROR`,
  `ILLEGAL REQUEST` or `LBA OUT OF RANGE`. Never silently drop data.
- Every ATA wait is bounded. No retry loop without a timeout.
- No ATA pass-through, SMART, NOR, PMU or raw register command over USB.

## 10. LBA48 plan

### 10.1 Build and runtime

- Add `HELIKON_HAVE_LBA48` for the 6G build, mirroring Rockbox `HAVE_LBA48`.
- The runtime probe still decides; the build flag only enables the path.
- If LBA48 is not supported, cap at 2^28 sectors and show `LBA28 LIMIT`.

### 10.2 Detection

- IDENTIFY word 83 bit 10: LBA48 supported.
- IDENTIFY word 86 bit 10: LBA48 enabled.
- IDENTIFY words 100–103: 48-bit capacity.
- IDENTIFY words 60–61: 28-bit capacity.
- IDENTIFY word 106 and 117–118: logical/physical sector size.
- CE-ATA: Rockbox treats CE-ATA as LBA48; Helikon does not implement CE-ATA
  yet and must reject it explicitly rather than misparse it.

### 10.3 Command path

- When LBA48 is proven, use `READ/WRITE MULTIPLE EXT` for every LBA, including
  below 2^28. One code path, no boundary bug.
- Otherwise use `READ/WRITE MULTIPLE`; do not use `WRITE SECTORS` for a
  multi-sector write.
- Use 64-bit LBA through the block layer, LUN and SCSI READ/WRITE(16).
- Use `FLUSH CACHE EXT` when LBA48 and word 83 bit 13 are set; otherwise
  `FLUSH CACHE`.
- Chunk at 256 sectors per ATA command, 64 KiB per USB buffer.

### 10.4 Boundary tests

Test at LBA `0`, `1`, `2^28−2`, `2^28−1`, `2^28`, `2^28+1`, `2^32−1`,
`2^32` and the last LBA. Test counts `1`, `255`, `256`, `257`, `65535`,
`65536`. Test 512-byte and 4096-byte sectors. Read back and compare SHA-256.
Run `fsck_hfs -fn` after the HFS+ tests.

## 11. Capacity beyond 128 GiB

LBA48 makes the extra sectors addressable; it does not make them part of the
existing HFS+ volume. Apple may have created a 128 GiB partition on a larger
card. Three migration options:

| Option | What | Pro | Con | Recommendation |
|---|---|---|---|---|
| A | create a second larger HFS+ partition in the free space | Apple partition intact; Helikon selects the largest HFS+ partition | two libraries; Apple does not see the new partition | first choice |
| B | grow the existing HFS+ partition | one library | partition-map and HFS+ resize risk; Apple may refuse | expert only, after A |
| C | repartition the whole card and restore | clean layout | destructive; Apple restore may recreate 128 GiB | only with backup and explicit consent |

The firmware never formats, erases, repartitions or repairs. The migration is a
host-side tool with a verified backup, `fsck_hfs` and an explicit confirmation.
The whole-disk LUN is the transport; it is not the migration policy.

## 12. UI

One status screen, no player UI:

```text
HELIKON DISK MODE
Volume: IPOD
Capacity: 238.4 GB   LBA48: YES
Free: 182.1 GB       Block: 4096
USB: CONNECTED — READ/WRITE
[activity]

Eject from computer before disconnecting.
MENU: reboot after eject.
```

- 1 Hz refresh during transfers.
- `READ ONLY` for the read-only modes.
- `JOURNAL DIRTY — host may replay` when applicable.
- `CONNECT USB POWER` and refuse the mode on low power.
- Error screen with a code and the recovery path.
- Entry from the launcher Settings tile; a boot gesture is future work and must
  not conflict with Apple Menu+Play or DFU Menu+Select.

## 13. Test and evidence matrix

| Layer | Test | Oracle | Evidence |
|---|---|---|---|
| Host unit | SCSI parser, CBW/CSW, descriptors, LUN mapping, LBA48 encoding | unit tests + sanitizers | test count/output |
| Host mock | enumeration, GET_DESCRIPTOR, SET_CONFIGURATION, BOT, stall/reset | mock controller | test output |
| P0 host | Apple Disk Mode descriptors, diskutil, partition map, fsck | read-only captures | report + hashes |
| P1 probe | IDENTIFY/LBA48/APM/MBR/HFS geometry | probe screen + photo | device-matrix row |
| P2 | macOS enumerates "Helikon Disk Mode" | system_profiler/ioreg | output |
| P3 | read-only mount and file read | diskutil, fsck | output |
| P4 | 1 GB write + read-back | SHA-256, fsck | hashes |
| P5 | LBA48 boundary and last LBA | read-back compare | test log |
| P5 | >128 GiB HFS+ volume | last file readable | fsck + hash |
| P6 | 100 connect/disconnect | no hang/corruption | matrix |
| P6 | surprise unplug during write | fsck, journal replay | output |
| P6 | Apple Disk Mode fallback | mount + fsck | output |
| P6 | Helikon boot after host write | library visible, track plays | photo/output |
| P9 | malformed SCSI/BOT fuzz | no crash, no out-of-range write | fuzz log |
| Host | Linux read-only, Windows enumeration | lsusb/dmesg/diskutil | output |
| Perf | PIO vs DMA, 1 GB copy | MB/s, CPU, battery | measurement |

Required hardware for the full matrix: the reference 6G/iFlash SD, a card larger
than 128 GiB, a disposable card or fully backed-up volume, a macOS host and
optionally a Linux host with `usbmon` for packet capture and fuzzing.

## 14. Phases and tasks

| Phase | Task | Deliverable | Gate |
|---|---|---|---|
| P0 | T-108 | host evidence runbook and collector | read-only captures from Apple Disk Mode |
| P1 | T-109 | read-only ATA/LBA48/APM probe image | one 6G run, photo and values |
| P2 | T-110 | USB device core and enumeration | macOS sees the device |
| P3 | T-111 | MSC read-only data LUN | macOS mounts and reads |
| P4 | T-112 | MSC writable data LUN with flush/eject | 1 GB write, SHA-256, fsck |
| P5 | T-109/T-112 | LBA48 boundary and 4K sectors | boundary writes/read-back |
| P6 | T-113 | integration, UI, safe exit, journal check | end-to-end device run |
| P7 | T-114 | whole-disk expert mode and capacity migration | backed-up migration + fsck |
| P8 | T-115 | DMA, throughput, fuzz, cycles, power-cut | measured and hardened |
| P9 | T-115 | release evidence | one device/adapter, no support claim |

Task IDs are allocated in [`tasks.md`](tasks.md). P0 and P1 are the only
authorized first steps; P2+ starts only after the probe evidence is recorded.

## 15. Open questions

1. Does the owner's iFlash bridge pass LBA48 and report the full card capacity?
2. Is a card larger than 128 GiB available for the boundary test?
3. Does the 6G use APM or MBR, and in which unit size?
4. Does macOS mount a bare HFS+ data LUN without a partition map?
5. Which VID/PID is allocated for Helikon Disk Mode?
6. Is the reference device the iFlash SD 6G only, or also the original CE-ATA
   160 GB 6G?
7. Is a dedicated mode enough, or is background service-mode sync required?
8. Who owns the first device run and the verified backup?

## 16. What this document does not claim

- No USB stack exists.
- No LBA48 write exists.
- No >128 GiB volume is proven.
- No Disk Mode device run exists.
- No Windows/Linux HFS+ write path exists.
- No support claim beyond one device and one adapter.
- No change to F2/T-055; Disk Mode is a separate line.
