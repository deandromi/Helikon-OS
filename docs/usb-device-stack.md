# USB device stack — research and port plan

> Purpose: define the hardware facts, Helikon additions, minimal device-only
> design, LUN mapping, porting strategy and test plan for the USB mass-storage
> stack used by Helikon Disk Mode.
> Status: living
> Owner: firmware/USB lead
> Last updated: 2026-11-11
> Source: [disk-mode.md](disk-mode.md), [ADR-0005](adr/0005-helikon-disk-mode.md),
> [firmware-architecture.md](firmware-architecture.md) §6.13, §7.7, §10.5;
> donor Rockbox checkout pinned by
> [tools/build-diagnostic-bootloader.sh](../tools/build-diagnostic-bootloader.sh)
> (`55603bd4bc2524ded900fa17feae99a223b04349`)

**This is a design and research document. No USB stack exists, no host has
enumerated Helikon-OS, no LUN has been mounted and no host write has been
performed.** The only hardware evidence is the donor Rockbox driver and the
existing Helikon ATA/HFS+ code. Read it with [disk-mode.md](disk-mode.md)
§7–§9 and [ADR-0005](adr/0005-helikon-disk-mode.md).

## 1. Scope

**In scope:** a device-only USB 2.0 high-speed stack for the S5L8702 DesignWare
OTG core (PHY, clocks, VIC/IRQ, EP0, two bulk endpoints); MSC bulk-only
transport; SCSI transparent commands; one data-volume LUN (largest HFS+
partition, bare volume); read-only in P3/T-111 and writable in P4/T-112; the
whole-disk LUN mapping designed here but scheduled for T-114; host and mock
tests.

**Out of scope:** USB host/OTG role swap, external USB audio, HID, MTP, iAP,
serial, framed service mode (T-077), vendor-specific SCSI commands, ATA
pass-through, SMART, raw NOR/PMU/GPIO access over USB, a writable LUN in the
normal firmware profile, Windows/Linux HFS+ write support, a second firmware
image, a bootloader USB path, and copying donor source into this repository.
Normal USB is read-only; only Helikon Disk Mode may expose a writable LUN
(D-54, ADR-0005, I-16).

## 2. Hardware facts confirmed from the donor

These are donor facts, not Helikon measurements. Each needs a P2 bring-up check
on the 6G.

### 2.1 Bases, IRQ and clocks

| Item | Value | Donor source | Consequence |
|---|---|---|---|
| OTG core base | `0x38400000` | `export/s5l87xx.h` (`OTGBASE`, S5L8702) | S5L8701 uses `0x38800000`; not the 6G |
| USB PHY base | `0x3C400000` | `export/s5l87xx.h` (`PHYBASE`) | PHY is outside the OTG core |
| USB IRQ | 19 (`IRQ_USB_FUNC`) | `export/s5l87xx.h` | VIC bank 0, bit 19 |
| VIC base | `0x38E00000`, bank stride `0x1000` | `export/s5l87xx.h` | `VIC0INTENABLE +0x10`, `VIC0ADDRESS +0xF00` |
| OTG clock gate | 2 (`CLOCKGATE_USBOTG`) | `export/s5l87xx.h` | `PWRCON` index 0, bit 2 |
| PHY clock gate | 35 (`CLOCKGATE_USBPHY`) | `export/s5l87xx.h` | `PWRCON` index 1, bit 3 |
| PHY interface | UTMI 16-bit (`DWC_PHYTYPE_UTMI_16`) | `target/arm/s5l8702/usb-s5l8702.c` | `GUSBCFG.PHYIF16` |
| PHY clock | `USB_DW_CLOCK 0` | `export/config/ipod6g.h` | `OPHYCLK = 0` |
| Turnaround | `USB_DW_TURNAROUND 5` | `export/config/ipod6g.h` | `GUSBCFG.TRDT(5)` |
| Endpoints | 9 available, 7 exposed (`USB_NUM_ENDPOINTS`) | `export/s5l87xx.h` | EP0–EP6; Helikon needs EP0–EP2 |
| DMA | internal DMA supported | `drivers/usb-designware.c` (`GHWCFG2` check) | donor uses DMA; Helikon may start with PIO |

### 2.2 FIFO budget

The donor uses dedicated TX FIFOs; `USB_DW_SHARED_FIFO` is not defined for the
S5L8702.

| FIFO | Words | Bytes | Use |
|---|---:|---:|---|
| RX | `0x360` | 3456 | all OUT traffic, including EP2 bulk |
| Non-periodic TX (`TX0FSIZ`) | `0x40` | 256 | EP0 IN (four 64-byte packets) |
| Dedicated TX | `0x180` × 3 | 1536 each | donor comment: IN1, IN3, IN5 |
| Total | `0x820` | 8320 | exact sum |

Helikon uses one dedicated TX FIFO for EP1 IN; two remain free for a later HID
or service endpoint. Keep the donor RX size for the first enumeration test.

### 2.3 PHY initialisation

From `target/arm/s5l8702/usb-s5l8702.c`. Offsets are from `export/s5l87xx.h`.

```text
enable clock gates USBOTG=2 and USBPHY=35
OPHYPWR  (PHY + 0x00) = 0; delay 10 us
OPHYUNK1 (PHY + 0x1C) = 1; OPHYUNK2 (PHY + 0x44) = 0xE3F
ORSTCON  (PHY + 0x08) = 1; delay 10 us; ORSTCON = 0; delay 10 us
OPHYUNK3 (PHY + 0x18) = 0x600
OPHYCLK  (PHY + 0x04) = USB_DW_CLOCK (0); delay 400 us
```

**Naming trap.** `export/s5l87xx.h` also defines `USB_PHY_BASE` with
`PHYCTRL +0x00`, `PHYPWR +0x04`, `URSTCON +0x08`, `UCLKCON +0x10`. That is a
different naming scheme. The target file above is authoritative; do not mix
the two.

### 2.4 Core initialisation

From `drivers/usb-designware.c` (`usb_dw_check_hw`, `usb_dw_init`):

1. Disable the USB IRQ, enable the OTG/PHY clocks, run the PHY sequence.
2. `DWC_PCGCCTL = 0`; read `GHWCFG2/3/4` and check endpoints, PHY type,
   internal DMA and FIFO memory.
3. `DWC_GUSBCFG = PHYIF16 | TRDT(5)`; optionally force device mode (`FDMOD`).
4. Wait for AHB idle, `DWC_GRSTCTL = CSRST`, wait for clear, wait again.
5. Program `DWC_GRXFSIZ = 0x360`, `DWC_TX0FSIZ = (0x40 << 16) | 0x360`, and
   `DWC_DIEPTXF(i) = (0x180 << 16) | (0x40 + 0x360 + 0x180 * i)` for `i=0..2`.
6. Flush FIFOs: `DWC_GRSTCTL = TXFFLSH | RXFFLSH` with `TXFNUM(0x10)`.
7. `DWC_GAHBCFG = GINT | HBSTLEN(INCR8) | DMAEN` for DMA; omit `DMAEN` for PIO.
   `DWC_DCFG = NZLSOHSK` (speed 0 = high speed; required for the status stage).
   Dedicated-FIFO DMA only: `DWC_DTHRCTL = ARPEN | RXTHRLEN(8) | RXTHREN`.
8. `DWC_DOEPMSK = STUP | XFRC`; `DWC_DIEPMSK = TOC | XFRC`. Set
   `DWC_DAINTMSK` only for EP0/EP1/EP2 IN and OUT; do not unmask all 32.
   `DWC_GINTMSK = USBRST | ENUMDNE | IEPINT | OEPINT`; add `RXFLVL` for PIO and
   `DISCINT` for cable removal; `EPMIS` only for a shared-FIFO core.
9. Reset endpoints, `DWC_DCTL = SDIS`, enable the VIC IRQ, wait 3 ms, clear
   `SDIS`, arm EP0 OUT for the first SETUP.

### 2.5 Reset, enumeration and SET_ADDRESS

Bus reset clears the address, resets endpoint state and re-arms EP0: handle
`USBRST`, then `ENUMDNE`, and clear the BOT/SCSI state. The donor writes the
new address to `DCFG` **before** acknowledging `SET_ADDRESS`, because the core
requires it for the status stage; keep that order until P2 disproves it. The
donor does not enable `DISCINT`; Helikon should enable it for Disk Mode cable
removal, with PMU VBUS polling as fallback. Keep the 3 ms soft-disconnect
delay.

The actual `GHWCFG` values, the loader's MMU/cache state and the iFlash sector
size/LBA48 support are not confirmed. Record `GHWCFG`, CP15 and the T-109
IDENTIFY words (83, 86, 100–103, 60–61, 106, 117–118) before relying on them.

## 3. What Helikon must add

### 3.1 VIC and IRQ entry

`firmware/boot/vectors.S` sends every IRQ to `helikon_panic_vector` (reason 6).
`firmware/boot/start.S` installs the table and reserves a 4 KiB IRQ stack, but
the firmware runs with IRQ/FIQ disabled. USB cannot work until this changes.

- Add `firmware/hal/6g/vic.c/.h`: init, enable/disable IRQ 19, acknowledge,
  spurious-IRQ counter. Use `VIC0INTENABLE`/`VIC0INTENCLEAR` for bit 19; read
  `VIC0ADDRESS` at `0x38E00F00` for the active source and write it back to
  acknowledge.
- Replace the IRQ vector: subtract 4 from LR, save `r0–r3`, `r12`, `lr`, call a
  C dispatcher, restore with `ldmia sp!, {r0-r3, r12, pc}^`. A normal IRQ must
  not panic.
- Add `helikon_cpu_enable_interrupts` (clear CPSR I, keep FIQ disabled) and
  call it only after VIC and USB init.
- No nested interrupts in P2. Keep the handler short; the core re-asserts the
  line if another event is pending.
- Spurious IRQ: count and return; panic after a fixed threshold.

The IRQ stack is 4096 bytes. The handler must not call ATA, HFS+, the block
layer, formatting code or any large-stack function.

### 3.2 EP0/EP1/EP2 allocation

| EP | Dir | Type | Max packet | FIFO | Purpose |
|---:|---|---|---:|---|---|
| 0 | IN/OUT | Control | 64 | NPTX `0x40` | enumeration/control |
| 1 | IN | Bulk | 512 HS / 64 FS | TX `0x180` | SCSI data-in, CSW |
| 2 | OUT | Bulk | 512 HS / 64 FS | RX `0x360` | CBW, SCSI data-out |
| 3–6 | — | — | — | two TX FIFOs free | future HID/service |

EP0 is armed with `STUPCNT(1) | PKTCNT(1) | 64` and `SNAK`; on `STUP` the
handler reads the 8-byte setup and re-arms. Configure EP1/EP2 only on
`SET_CONFIGURATION(1)`; deconfigure on 0 and on bus reset. Set
`USBAEP | EPTYP_BULK | maxpacket` and `SETD0PIDEF`; at full speed the max packet
is 64, so build the descriptor from `DSTS`, not a fixed 512.

### 3.3 Cache, DMA alignment and buffers

`firmware/hal/6g/cpu.h` says the MMU/cache state across the loader jump is
unknown; only a D-cache clean helper exists, with no invalidate helper.

- Use **PIO** for EP0 and bulk first. The DWC slave path moves packets through
  `DFIFO`, removing cache coherency from the first milestone. The donor
  supports it via `USB_DW_ARCH_SLAVE`, although the 6G build uses DMA.
- Reserve all USB buffers as static `__attribute__((aligned(32)))` arrays.
  32 bytes is the donor `CACHEALIGN_SIZE`; never allocate in the USB path.
- If DMA is enabled later, add clean (CPU→device) and invalidate (device→CPU)
  range operations. The existing `helikon_cpu_clean_dcache_range` uses CP15
  `c7, c14, 1` (clean and invalidate); confirm the exact operation.
- Do not use the donor uncached alias (`+0x40000000`) until Helikon owns an MMU
  mapping. Without one it may be invalid.
- Record CP15 control and cache bits in the P2 trace. DMA without a recorded
  cache state is a bug.

### 3.4 Main loop versus IRQ

| Context | Does | Never does |
|---|---|---|
| USB IRQ | `GINTSTS`; `USBRST`, `ENUMDNE`, `IEPINT`, `OEPINT`, `DISCINT`; one packet/PIO chunk; event flags; copy 8-byte SETUP to a static buffer | ATA, HFS+, block I/O, descriptor parsing, long loops, allocation, formatting |
| Main loop | EP0 requests, descriptors, BOT, SCSI, ATA read/write/flush, CSW, timeouts, mode state | DWC register access the IRQ owns without masking |

Use a fixed `usb_event` ring (`SETUP`, `EP1_IN_DONE`, `EP2_OUT_DONE`,
`BUS_RESET`, `DISCONNECT`, `ERROR`) plus per-endpoint flags: one producer
(IRQ), one consumer (main loop). Only one transfer per endpoint at a time; no
semaphores, threads or `malloc`. `helikon_cpu_idle()` when the ring is empty.
Every wait is bounded: EP0 5 s, BOT 10 s; on timeout reset BOT and stall bulk.

### 3.5 Power and mode entry

Disk Mode `PREPARE` checks USB power or a charged battery before enabling the
PHY; low battery refuses the mode (the PMU external-power read exists). P2 is
charge-only plus enumeration and does not negotiate charging current. The
normal profile stays read-only; a writable LUN exists only after Disk Mode
reaches `ENUMERATED` and the host owns the volume (D-54/I-16).

## 4. Minimal device-only stack design

### 4.1 Layers and file layout

```text
firmware/hal/6g/vic.c/.h        VIC, IRQ 19, spurious handling
firmware/hal/6g/usb.c/.h        PHY, clocks, DWC registers, EP0/1/2, IRQ glue
firmware/hal/6g/usb_hw.h        test seam: reg/FIFO read/write
firmware/usb/usb_private.h      event ring, endpoint state
firmware/usb/descriptors.c/.h   device/config/interface/endpoint/string
firmware/usb/core.c/.h          device state, EP0, standard requests
firmware/usb/bot.c/.h           CBW/CSW, reset/stall
firmware/usb/scsi.c/.h          opcode table, CDB parse, sense
firmware/usb/msc.c/.h           LUN mapping, read/write/flush backend
firmware/usb/disk_mode.c/.h     mode state, UI hook, ATA/block backend
tests/usb/                      host unit, mock DWC, fuzz
```

`firmware/hal/6g/usb.c` is the only file that includes the DWC register header.
`firmware/usb/*` uses the small HAL interface (`usb_ep_read`, `usb_ep_write`,
`usb_ep_arm`, `usb_ep_stall`, `usb_event_pop`) so the mock replaces the
controller without compiling `s5l8702_regs.h`.

### 4.2 Descriptors

| Descriptor | Field | Value |
|---|---|---|
| Device | `bcdUSB` | `0x0200` |
| Device | class/subclass/protocol | `0/0/0` (class at interface) |
| Device | `bMaxPacketSize0` | 64 |
| Device | VID/PID | open question; never Apple `0x05AC/0x1261` |
| Device | `bcdDevice` | `0x0100` |
| Device | strings | manufacturer 1, product 2, serial 0 (no personal serial) |
| Config | `wTotalLength` | 32 (`9 + 9 + 7 + 7`) |
| Config | interfaces | 1 |
| Config | attributes/power | `0xC0` self-powered, no remote wakeup; 500 mA (open) |
| Interface | class/subclass/protocol | `0x08/0x06/0x50` (MSC, SCSI transparent, BOT) |
| Interface | endpoints | 2 |
| EP1 IN | address/type/max | `0x81`, bulk, 512 HS / 64 FS |
| EP2 OUT | address/type/max | `0x02`, bulk, 512 HS / 64 FS |

Strings: language `0x0409`; manufacturer `"Helikon"`; product
`"Helikon Disk Mode"`; no serial by default. `GET_DESCRIPTOR` also serves the
10-byte device qualifier; `OTHER_SPEED_CONFIG` may be stalled in P2 if unused,
but macOS is happier when it exists.

### 4.3 EP0 and standard requests

EP0 is `SETUP → DATA IN` or `DATA OUT → STATUS`. `SETUP` is always 8 bytes
OUT. A new SETUP aborts the previous transfer.

| Request | Code | Behaviour |
|---|---|---|
| GET_DESCRIPTOR | `0x06` | device, config, string, qualifier; validate type/index/language; return `min(wLength, size)` |
| SET_ADDRESS | `0x05` | 0–127; write `DCFG.DAD` before status ACK (donor quirk) |
| SET_CONFIGURATION | `0x09` | 0/1; configure/deconfigure EP1/EP2; reset BOT; ACK or STALL |
| GET_CONFIGURATION | `0x08` | one byte |
| GET_STATUS | `0x00` | device self-powered/remote-wakeup; interface zero; endpoint halt bit |
| CLEAR_FEATURE | `0x01` | endpoint halt: clear stall, reset toggle; remote wakeup: ACK; interface/test mode: STALL |
| SET_FEATURE | `0x03` | endpoint halt: set stall; test mode/other: STALL |
| GET_INTERFACE | `0x0A` | alt 0 |
| SET_INTERFACE | `0x0B` | alt 0; STALL others |
| SET_DESCRIPTOR | `0x07` | STALL |
| GET_MAX_LUN | `0xFE` class | 0 in P3/P4; 1 only in expert whole-disk mode |
| BULK-ONLY RESET | `0xFF` class | clear CBW/CSW, de-stall EP1/EP2, ACK |

Validate `bmRequestType` recipient/type before dispatch. Standard requests go
to `core.c`; class interface requests to `msc.c`; vendor requests are stalled.
A short `wLength` is legal; return that many bytes. A zero-length IN request
still needs the status stage. `GET_STATUS` validates the endpoint address.
`CLEAR_FEATURE(ENDPOINT_HALT)` clears the DWC stall and sets `SETD0PIDEF` so the
host restarts at DATA0. Do not implement USB test modes in production.

### 4.4 Bulk-only transport

CBW is 31 bytes, CSW 13; multi-byte fields are little-endian on the wire.

| CBW field | Offset | Rule |
|---|---:|---|
| signature | 0 | `0x43425355`, else stall both bulk endpoints |
| tag | 4 | echo in CSW |
| data transfer length | 8 | 32-bit; cap to LUN bytes; larger is a phase error |
| flags | 12 | bit 7: 1 IN, 0 OUT; other bits zero |
| LUN | 13 | low 4 bits; reject > max LUN |
| CB length | 14 | 1–16; reject 0 or >16 |
| CDB | 15 | 16 bytes; unused ignored |

| CSW field | Offset | Rule |
|---|---:|---|
| signature | 0 | `0x53425355` |
| tag | 4 | same as CBW |
| residue | 8 | expected minus transferred |
| status | 12 | 0 good, 1 fail, 2 phase error |

`IDLE` (EP2 armed for 31 bytes) → `DATA_IN`/`DATA_OUT` → `STATUS` (EP1 sends 13
bytes) → `IDLE`. Bad signature, LUN, CB length, direction or oversized transfer
stalls EP1/EP2 and waits for Bulk-Only Reset or bus reset. A short packet ends
the data phase; the CSW reports the residue. Do not read the next CBW until
the CSW is queued. Stream 512-byte USB packets through a 64 KiB staging buffer;
never allocate a whole host transfer.

### 4.5 SCSI command dispatch

The command set is [disk-mode.md](disk-mode.md) §8. P3 is read-only; P4 adds
writes and flush.

| Opcode | Command | P3 read-only | P4 writable |
|---:|---|---|---|
| `0x00` | TEST UNIT READY | ready after ATA + LUN | same |
| `0x03` | REQUEST SENSE | fixed 18-byte sense, clear after read | same |
| `0x12` | INQUIRY | vendor "Helikon", product "Disk Mode", rev "0001", RMB=1 | same |
| `0x1A`/`0x5A` | MODE SENSE(6/10) | minimal header, write-protect set | write-protect clear |
| `0x1B` | START STOP UNIT | no-op / mark ejected | eject marks absent |
| `0x1E` | PREVENT ALLOW MEDIUM REMOVAL | track lock | same |
| `0x23` | READ FORMAT CAPACITIES | last LBA + block size | same |
| `0x25`/`0x9E` | READ CAPACITY(10/16) | last LBA + block size; 16 for >2^32 blocks | same |
| `0x28`/`0x88` | READ(10/16) | ATA read | same |
| `0x2A`/`0x8A` | WRITE(10/16) | DATA PROTECT | ATA write |
| `0x2F` | VERIFY(10) | bounds check, GOOD | same or compare later |
| `0x35`/`0x91` | SYNCHRONIZE CACHE(10/16) | GOOD | ATA flush |
| `0xA0` | REPORT LUNS | one LUN (two expert) | same |
| `0x85`/`0xA1` | ATA PASS-THROUGH | ILLEGAL REQUEST | ILLEGAL REQUEST |
| `0xC0`+ | vendor-specific | ILLEGAL REQUEST | ILLEGAL REQUEST |

Sense is fixed format (`0x70`), 18 bytes, with sense key, information (LBA)
and ASC/ASCQ:

| Condition | Key | ASC/ASCQ |
|---|---:|---|
| LBA out of range / invalid field / invalid command | `0x05` ILLEGAL REQUEST | `0x21/0x00`, `0x24/0x00`, `0x20/0x00` |
| Medium not present / becoming ready | `0x02` NOT READY | `0x3A/0x00`, `0x04/0x01` |
| Read / write error | `0x03` MEDIUM ERROR | `0x11/0x00`, `0x0C/0x00` |
| Write-protected | `0x07` DATA PROTECT | `0x27/0x00` |
| Aborted command | `0x0B` ABORTED COMMAND | `0x00/0x00` |

Keep these donor behaviours, not code: READ CAPACITY returns the **last LBA**;
MODE SENSE(10) can be an 8-byte header with no block descriptors (macOS reads
the write-protect bit in byte 3); MODE SENSE(6) page `0x3F` may need a short
block descriptor only if the host rejects the header; READ CAPACITY(16) needs
the logical-blocks-per-physical-block exponent when the USB block is >512.

### 4.6 Data path, buffering and errors

Two 64 KiB staging buffers, 32-byte aligned, in `.bss`; no allocator. The ATA
HAL accepts 64-bit LBAs and chunks at 256 sectors, but the USB 64 KiB buffer is
the binding limit: at most 128 sectors at 512 bytes, 16 at 4096. Every ATA wait
is bounded. A failed read/write produces CHECK CONDITION plus residue, never a
silent zero-fill or `GOOD`. A stalled host is timed out and BOT resets. On
eject, flush first, then report success; a failed flush is CHECK CONDITION. On
surprise removal, attempt a bounded flush (about 2 s), stop commands, go to
`VERIFY`, and report unsafe removal if the flush failed. No unbounded retry,
no ATA pass-through, SMART, NOR, PMU or raw register access over USB.

## 5. LUN mapping

### 5.1 Data-volume LUN (default)

- `offset` = byte start of the largest HFS+ partition from
  `helikon_partition_scan`; `length` = partition byte length.
- The host sees a **bare HFS+ volume** (volume header at byte 1024), not the
  APM/MBR or free space.
- Translation: `ata_byte = offset + usb_lba * usb_block_size`; the ATA HAL gets
  `ata_byte / ata_sector_size` and a sector count. Require
  `usb_block_size >= ata_sector_size` and an integer ratio; a 4Kn ATA device
  must use 4096. Otherwise reject the LUN.
- `offset` and `length` must be aligned to the USB block size. Use 4096 when
  the partition offset, length and HFS+ allocation block are all 4096-aligned;
  otherwise 512. A 4Kn ATA device always reports 4096.
- A read of LUN block 0 returns the HFS+ header area at `offset`, not disk
  block 0.

### 5.2 Whole-disk LUN (expert, T-114)

`offset = 0`, `length` = true disk capacity; the host sees the real APM/MBR and
every partition. Read-only by default; writable only as an explicit restore
mode with long-press confirmation and a backup reminder. P3/P4 must not
advertise it. This is the one path where the MBR unit is visible to the host:
do not present a 512-byte whole-disk LUN over a 4096-unit MBR. Use 4096-byte
logical blocks or synthesise a corrected 512-unit MBR (not P2–P4).

### 5.3 512 versus 4096

| Case | USB block | ATA access | Note |
|---|---:|---|---|
| 512-byte ATA, 512-aligned HFS+ | 512 | 1 sector/block | simplest, more overhead |
| 512-byte ATA, 4096-aligned HFS+ | 4096 | 8 sectors/block | preferred for 6G; matches HFS+ allocation block |
| 4096-byte ATA (4Kn) | 4096 | 1 sector/block | no bounce |
| 4096-byte ATA as 512 | not allowed | — | would need read-modify-write |

The LUN must report the same block size in READ CAPACITY(10/16), READ FORMAT
CAPACITIES and MODE SENSE; do not change it after the host reads capacity.

### 5.4 Capacity discovery

1. **Data LUN:** partition length from APM/MBR; cross-check the HFS+ header
   (`blocks × block_size` must fit inside the partition). Refuse if it does not.
2. **Whole disk:** IDENTIFY words 60–61 (LBA28) and 100–103 (LBA48), then APM
   `sbBlkCount × sbBlkSize` or MBR total sectors, then HFS+ as cross-check.
3. If none is proven, refuse the LUN and show the failure. Do not fall back to
   the current adopt-mode `ata->total_sectors = 0xffffffff` placeholder.

`helikon_block_capacity_bytes()` uses `sector_count × sector_size`; with the
adopt-mode value it reports about 2 TiB. P3/P4 must bound every LUN access by
the partition length, not that value.

### 5.5 The 6G MBR 4096-unit quirk

The 6G/iFlash MBR may store partition start/size in 4096-byte virtual sectors
while ATA reports 512-byte logical sectors.

- Donor `hfsplus_partitions()` reads the Apple_HFS MBR entry, computes
  `offset = start × 4096`, `length = blocks × 4096`, and accepts only if an
  HFS+ signature is at that offset. It does not try 512 for that entry.
- Helikon `firmware/block/partition.c` tries 512 first, then 4096, accepting
  whichever has a valid HFS+ signature. The 512-first order can theoretically
  accept a false positive; T-109 must record the actual unit.
- For the data LUN the MBR unit is irrelevant after the scan: use byte offset
  and length, and never expose the MBR.
- For the whole-disk LUN the unit is visible: a 512-byte logical disk over a
  4096-unit MBR is misread.
- The donor's `DEFAULT_VIRT_SECTOR_SIZE 4096`/`MAX_VIRT_SECTOR_SIZE 4096` match
  this quirk; they are not a general 6G sector-size claim.

### 5.6 LUN table and mode rules

| Mode | LUN 0 | LUN 1 | GET MAX LUN | REPORT LUNS |
|---|---|---|---|---|
| Normal firmware USB | read-only data volume (future T-076) | none | 0 | one |
| Disk Mode P3 | read-only data volume | none | 0 | one |
| Disk Mode P4 | writable data volume | none | 0 | one |
| Disk Mode expert T-114 | data volume | whole disk, read-only default | 1 | two |

The data LUN is always the largest HFS+ partition; `select_partition()` already
does this in `firmware/boot/firmware_main.c`. A second larger HFS+ partition is
selected automatically after the host-side migration.

## 6. Porting strategy and licensing

### 6.1 Reuse as a register reference

| Donor file | Take as facts | Do not take |
|---|---|---|
| `target/arm/s5l8702/usb-s5l8702.c` | gates, PHY sequence, IRQ 19, FIFO sizes, UTMI type, turnaround | Rockbox `usb_drv_*` API/globals |
| `drivers/usb-designware.c` | DWC offsets/bits, init order, FIFO/EP programming, EP0 setup/status, reset/flush, soft-disconnect | semaphores, threads, `logf`, `panicf`, cache macros |
| `usbstack/usb_core.c` | standard-request and descriptor behaviour | driver/thread model, global state |
| `usbstack/usb_storage.c` | CBW/CSW layout, SCSI behaviour, macOS quirks, sense codes | storage/disk API, writable-LUN policy |
| `export/usb-designware.h` | register names/bits as a checklist | copying the whole header |
| `export/s5l87xx.h` | `OTGBASE`, `PHYBASE`, `VIC_BASE`, `IRQ_USB_FUNC`, endpoint count | unrelated SoC definitions |
| `export/config/ipod6g.h` | `USB_DW_CLOCK`, `USB_DW_TURNAROUND`, `MAX_VIRT_SECTOR_SIZE`, `HAVE_LBA48` | Apple VID/PID and strings |

### 6.2 Rewrite and do not copy

Rewrite the register layer in Helikon's `HELIKON_REG32/16` style, the VIC/IRQ
entry, the main-loop event model, EP0/descriptors, BOT/SCSI/sense, the LUN
mapper, capacity discovery, branding and tests.

Do not copy `usb_core.c`, `usb_storage.c` or `usb-designware.c` bodies into
`firmware/`; do not import Rockbox `kernel.h`, `system.h`, `thread.h`,
semaphores, `core_alloc`, `logf` or `panicf`; do not import Apple's VID/PID,
strings or descriptor bytes; do not import the donor overlay/writable-LUN
policy; do not paste donor code into tests.

### 6.3 GPL provenance, NOTICE and file-map

The project is GPL-2.0-or-later (`LICENSE`, `NOTICE`). The donor Rockbox files
are also GPL-2.0-or-later. A port would be licence-compatible, but this task
deliberately copies no code.

If a later change copies or closely adapts a donor file:

1. Keep the donor copyright and add `SPDX-License-Identifier: GPL-2.0-or-later`.
2. Add a row to the donor provenance table in [file-map.md](file-map.md):
   Helikon file, donor path, donor commit `55603bd4…`, what was taken, copy or
   rewrite.
3. Add a specific `NOTICE` entry naming the donor file, authors and licence.
   The current generic Rockbox lineage credit is not enough for a direct port.
4. Have the owner review provenance before merge; licensing is settled, source
   recording is the process step.
5. Do not relicense a derived file, remove its copyright or move it to another
   licence.

This document is original prose; it cites register values, sequences and
behaviour as facts, not copied expression.

### 6.4 Can the donor driver be ported without the Rockbox kernel?

**No as a drop-in.** `usb-designware.c` depends on `kernel.h`,
`semaphore_wait/release`, `HZ`, Rockbox cache helpers and `usb_core`'s threaded
completion model. `usb_core.c`/`usb_storage.c` depend on the driver registry,
storage/disk layer, `logf` and `panicf`. **Yes as a register/behaviour
reference:** the PHY sequence, DWC programming, FIFO layout, EP0 setup/status,
reset/flush order and soft-disconnect timing can be re-expressed in a Helikon
HAL with no kernel. That is the recommended strategy: a small native rewrite,
not a port.

## 7. Host and mock test plan

### 7.1 Host unit tests

`tests/usb/`, one binary per module, C99, `-Wall -Wextra -Werror`, ASan/UBSan on
every target. No device, no donor code.

| Test | Covers |
|---|---|
| `test_descriptors` | lengths/types, `wTotalLength`, string encoding, HS/FS max packet, qualifier |
| `test_control` | GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION, GET_STATUS, CLEAR_FEATURE, class, STALL |
| `test_bot` | CBW parse, tag echo, direction, LUN/CB length, residue, CSW status, Bulk-Only Reset |
| `test_scsi` | every opcode, allocation length 0/short/long, reserved bits, sense |
| `test_lun` | offsets, 512/4096, unaligned/rejected LUN, capacity cross-check, MBR quirk |
| `test_mock_dw` | register/FIFO model, EP enable/disable, SETUP, completion, reset, disconnect, errors |

### 7.2 Fake DesignWare controller

Model the controller, not a second USB stack: register file with the same
offsets/reset values; FIFO memory with packet boundaries; EP enable/disable/
NAK/STALL; a test host that injects SETUP, CBW, OUT data and IN tokens and
observes CSWs/stalls; interrupt injection (`GINTSTS`, `DAINT`, EP bits); bus
reset, enumeration done and disconnect; short/oversized/wrong-direction/stall/
timeout errors.

The HAL needs a test seam so `firmware/usb/*` compiles against the mock:
`firmware/hal/6g/usb_hw.h` with `usb_hw_read32`, `usb_hw_write32`,
`usb_hw_fifo_read`, `usb_hw_fifo_write`, implemented by `usb.c` on target and
`tests/usb/mock_dw.c` on host. Do not compile `s5l8702_regs.h` into host tests.

### 7.3 macOS enumeration and mount

P2 (no LUN):

```sh
system_profiler SPUSBDataType
ioreg -p IOUSB -l -w 0
diskutil list
```

P3/P4 (LUN):

```sh
diskutil list
diskutil info /dev/diskXsY
diskutil unmount /dev/diskXsY
fsck_hfs -fn /dev/diskXsY
shasum -a 256 /Volumes/<volume>/<known-file>
```

Acceptance is recorded output, not "it appeared". For P2, `diskutil list` must
show **no new volume**. P3 must match the known-file hash. P4 must hash the
1 GB write/read-back and `fsck_hfs -fn` must report zero repairs.

### 7.4 100 connect/disconnect cycles

Use a switched USB hub or data-pair relay; manual plugging is the fallback. A
cycle: disconnect, wait for disappearance in `ioreg`, reconnect, wait for
`Helikon Disk Mode` and the Disk Mode `WAIT_HOST` screen. Pass: 100/100
enumerations within 5 s; no panic, hang, watchdog reset or leaked endpoint; no
host kernel error storm; one normal read still works afterwards.

### 7.5 Malformed CBW and SCSI

Feed the mock and, if a Linux host with `usbmon` is available, the device: wrong
CBW signature/tag/LUN/CB length, reserved flag bits, data length larger than
the LUN, direction opposite to the opcode; CDB reserved bits, allocation length
0, transfer length 0, LBA `2^32−1`, `2^32`, `2^48−1`, last LBA, last+1; WRITE to
a read-only LUN; ATA PASS-THROUGH; vendor opcode; short REPORT LUNS; CSW tag
mismatch; host stops mid-data-phase. Pass: STALL or CHECK CONDITION, never a
write outside the LUN, never a hang; Bulk-Only Reset returns to `IDLE`.

### 7.6 Fuzz

`fuzz_cbw` (arbitrary 31-byte CBW + fake LUN), `fuzz_scsi` (arbitrary CDB and
allocation length), `fuzz_control` (arbitrary setup packet). Seed with valid
CBWs, every opcode, boundary LBAs and the malformed cases. Run ASan/UBSan with
a time limit; a crash or timeout is a defect. Optionally replay the corpus
through the mock DWC.

## 8. Phased plan P2–P4

P2/P3/P4 map to T-110/T-111/T-112 in [tasks.md](tasks.md). T-109 is a hard
dependency for capacity/LBA48 facts; T-113 integration and T-115 hardening
follow P4. Effort is rough person-weeks (pw).

| Phase | Task | Deliverable | Depends on | Acceptance gate | Effort | Risk |
|---|---|---|---|---|---|---|
| P2 | T-110 | VIC/IRQ, PHY/core, EP0, descriptors, enumeration | this document; T-109 board facts | macOS enumerates `Helikon Disk Mode`; host/mock tests pass; 100 cycles; no LUN | 3–4 pw | High |
| P3 | T-111 | BOT, SCSI read-only, data LUN, ATA read | P2; T-109 capacity/LBA48/APM/MBR | macOS mounts largest HFS+ read-only; known-file hash; error paths | 2–3 pw | Medium-high |
| P4 | T-112 | write, flush, eject, sense, writable data LUN | P3; D-54; verified backup | 1 GB write + SHA-256; `fsck_hfs -fn` zero repairs; safe eject; surprise unplug | 2–3 pw | High |

### 8.1 Phase detail

The per-phase files are in §4.1. P2/T-110 owns the HAL, core and descriptors;
P3/T-111 adds BOT, SCSI, MSC and the data LUN; P4/T-112 extends SCSI/MSC and
adds the write/flush tests and a mock ATA write model. Each phase's acceptance
gate is the table row above; no phase starts before the previous gate is
recorded.

### 8.2 Sequence, dependencies and risk

T-109 must land before P3 can select a block size or capacity. P2 can start
earlier. P3 is deliberately read-only, proving enumeration, BOT, SCSI and LUN
translation before any host write. P4 is gated by a verified backup
([agents.md](../agents.md) §3 rule 8) and D-54/I-16. T-113 adds the Settings
entry, status screen, USB-power gate and safe exit; T-114 adds whole-disk mode
and migration; T-115 adds DMA, fuzzing, throughput and release evidence.

**Biggest risk.** The first P2 bring-up combines three unproven things: the
PHY, the VIC/IRQ path and the post-loader cache/MMU state. Any one can prevent
enumeration and there is no host-side substitute. Mitigate by bringing up VIC
with a timer or software interrupt first, then PHY, then EP0; record CP15 and
GHWCFG values in the trace; keep PIO as fallback. The second risk is P4 data
loss; read-only P3 and the backup gate are the controls.

## 9. Open questions

| # | Question | Why it matters | Who decides | Default until answered |
|---:|---|---|---|---|
| 1 | VID/PID, and does Disk Mode share it with the normal profile? | descriptor freeze, macOS identity, release quality | owner | build-time placeholder in host tests only; never Apple `0x05AC/0x1261` |
| 2 | Is EP1 IN / EP2 OUT final, or does the normal profile need HID/service endpoints? | 7-endpoint/3-TX-FIFO budget | architecture/USB lead | EP1/EP2 MSC; EP3–EP6 reserved |
| 3 | Self-powered or bus-powered, and what `bMaxPower`/charge current? | descriptor, PMU charging, low-battery policy | owner/product + PMU | self-powered `0xC0`, 500 mA, no charging negotiation in P2 |
| 4 | Does macOS mount a bare HFS+ data LUN without MBR/APM? | P3 default design | test result | try bare HFS+; fall back to a synthetic one-partition MBR |
| 5 | PIO or DMA for the first writable milestone? | cache/MMU risk, throughput, schedule | USB lead + performance target | PIO P2/P3; DMA in T-115 after cache proof |
| 6 | Can the donor driver be used without the Rockbox kernel? | porting effort and licensing | USB lead | no as-is; rewrite against the register/behaviour reference |
| 7 | Does the reference iFlash report 512/4096 sectors and LBA48? | block size, >128 GiB | T-109 probe/owner hardware | no LUN until recorded |
| 8 | Who owns the first write run and the verified backup? | P4 safety gate | owner | P4 blocked until named |

## 10. What this document does not claim

- No USB stack, VIC, IRQ handler or PHY bring-up exists in Helikon-OS.
- No host has enumerated Helikon-OS, mounted a LUN or written a byte.
- No LBA48, 4096-byte sector or >128 GiB volume is proven on the 6G.
- No macOS bare-HFS+ mount is proven.
- No donor code has been copied into this tree.
- No support claim beyond one device and one adapter after a recorded run.
- This document does not change F2/T-055 or the normal read-only USB policy; it
  is the T-111 research input for the P2–P4 line.
