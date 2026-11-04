# Firmware architecture — a research and design paper

> Purpose: specify how the Helikon-OS firmware should be structured
> functionally, visually and for developer ergonomics before code exists.
> Status: living document — design paper; no firmware implementation exists.
> Owner: architecture lead; firmware lead
> Last updated: 2026-11-04
> Source: design paper §5–§19, §27; `docs/architecture.md`; `docs/ui.md`;
> ADR-0003; ADR-0004

**Evidence statement.** This paper describes **planned** firmware. There is no
kernel, HAL, VFS, player, UI or firmware image in the repository. Nothing here
is a device result, a performance measurement or a support claim. Values marked
"design target" are budgets to be measured later; values marked "open question"
are not decided. The only measured firmware-adjacent fact in this repository is
the loader test image's owner-reported **6.5 s** startup on one 6G with an iFlash
SD adapter, and that number belongs to the loader, not to the firmware.

**How to read this paper.** Sections 1–4 define the problem, the boot boundary
and memory. Sections 5–7 define the runtime and the modules. Section 8 is about
what it feels like to program the firmware. Section 9 is about what it feels
like to use it. Sections 10–14 cover trust, performance, testing, phases and
risks. Section 15 lists the questions that must be answered before the design is
frozen. The appendices are checklists and proposed constants, not code.

## Contents

0. Abstract
1. Scope, status and method
2. What the firmware must become
3. The boot boundary
4. Memory architecture
5. Runtime model
6. Module architecture
7. Functional flows
8. The firmware as a program
9. Aesthetic and interaction design
10. Security and trust boundaries
11. Performance and power
12. Testing and validation
13. Phased implementation F1–F5
14. Risks and mitigations
15. Open questions
16. Recommendations
17. References
A. Proposed boot-info layout
B. Proposed memory-map constants
C. Module ownership matrix
D. Firmware coding rules
E. Screen inventory
F. Error-to-screen mapping
G. First-week onboarding
H. Glossary delta
I. Design-review checklist

---

## 0. Abstract

Helikon-OS has a verified bootloader and a host-tested HFS+ core, but no
firmware. The obvious next move is to start the player: audio, UI, a file
browser. This paper argues that the obvious move is the wrong first move. The
highest-risk unknowns are not HFS+ logic, which already has 104 host tests and a
device-run writer, but the **boot environment**: DRAM layout, cache and DMA
coherency, interrupt state, ATA bring-up, panel timing, power sequencing and the
handoff contract between a verified loader and a non-returning firmware. Those
unknowns are cheap to resolve with a read-only skeleton and expensive to
discover after a player, a UI and an audio pipeline depend on them.

The paper makes five arguments.

1. **The first firmware should be a read-only bring-up skeleton.** It should
   prove the chain loader → jump → firmware → ATA read → HFS+ read → screen →
   power-off, with no filesystem writes. That skeleton is also the first real
   signed firmware image, so it unblocks the production handoff (T-051).
2. **The boot boundary must be a frozen ABI, not an accident.** The loader
   currently calls `entry(void)` and expects a test payload to return. Production
   firmware must never return. It needs a versioned, checksummed boot-info page
   that carries the facts the loader already verified, while remaining a cache
   rather than a trust anchor.
3. **The 8 MiB signed window and the 64 MiB runtime RAM are different budgets.**
   The firmware image must fit the loader's validated window; the running
   firmware needs a fixed map for stacks, framebuffer, audio and database. A
   fixed map with compile-time assertions is the smallest design that can be
   debugged on one device.
4. **A cooperative, single-address-space runtime with fixed pools is the right
   first shape.** Preemption, an MMU-protected process model and a general heap
   are all solutions to problems the first player does not have. They add
   failure modes before there is a working block driver.
5. **Developer ergonomics and visual identity are architecture.** A firmware
   that is hard to test, hard to trace and hard to extend will not reach a
   second device. A UI that is merely "functional" will not feel like a product.
   Both must be designed now, with the same rigor as the block layer.

The paper ends with a phased plan (F1–F5), acceptance criteria and a list of
questions that only the owner or the device can answer.

---

## 1. Scope, status and method

### 1.1 What this paper is

This is a **firmware-internal architecture paper**. It answers:

- where firmware starts and what it may assume;
- how memory is divided and why;
- what execution model the code runs under;
- which modules exist, what each owns and what each must not own;
- how a boot, a mount, a read, a first write, a sleep and a shutdown flow;
- how the code is built, tested, traced and kept maintainable;
- how the product looks, moves and speaks to the user;
- what must be measured before any claim is made.

It is deliberately narrower than the design paper and deeper than
`docs/architecture.md`. The design paper is the product and system source;
`architecture.md` is the system-boundary summary; this paper is the firmware
zoom-in.

### 1.2 What this paper is not

- It is not firmware code. Interface sketches are proposals, not headers.
- It is not a second design paper. If it conflicts with `design-paper.md`, the
  design paper wins and the conflict is recorded in `docs/decisions.md`.
- It is not a release plan. Phases F1–F5 are an implementation order, not a
  schedule promise.
- It is not a skin format, a database schema, an audio codec datasheet or a
  hardware reference manual.
- It is not evidence. A design target is not a measurement; a host test is not a
  device run; a link is not a boot.

### 1.3 Method

The paper uses four rules.

**Rule 1: separate invariants from choices.** Safety invariants (I-01–I-15) are
not open for convenience. A memory-map constant, a UI row height or a scheduler
shape is a choice and must name its alternatives.

**Rule 2: design for the smallest testable step.** Every section asks what can
be proven on one 6G with an iFlash SD before the next dependency is added. A
module that cannot be tested without three other unfinished modules is too big.

**Rule 3: make failure visible.** A firmware that fails silently is worse than a
firmware that refuses to boot. Every layer has an error code, a screen or a log
entry, and a safe stop.

**Rule 4: mark unknowns.** "Open question" is a valid answer. The repository has
a history of treating an unmeasured value as a fact and then building on it; the
memory map and the entry ABI are the next places where that would hurt.

### 1.4 Status labels and evidence

The project labels are defined in [`conventions.md`](conventions.md):
`implemented`, `in progress`, `planned`, `unproven`, `rejected`. This paper uses
`planned` for firmware features, `proposed` for design choices that need owner
acceptance, and `open question` for unknowns.

A design target is written as a target. A measurement names the device, adapter,
image hash and run count. A host test is labeled host-only. A device run is
recorded in [`device-matrix.md`](device-matrix.md), not inferred from a
successful link.

### 1.5 Relation to existing documents

| Document | Role | Relationship to this paper |
|---|---|---|
| [`design-paper.md`](design-paper.md) | Product and system design source | Wins on conflict; this paper zooms into firmware |
| [`architecture.md`](architecture.md) | System boundaries, boot chain, memory budget, security | Parent summary; this paper expands §4–§9 |
| [`hfsplus-write-engine.md`](hfsplus-write-engine.md) | Writer, journal and crash consistency | Canonical for the write engine; this paper only integrates it |
| [`ui.md`](ui.md) | Default typography and rendering rules | Canonical for type; this paper adds interaction and layout |
| [`../bootloader/LOADER.md`](../bootloader/LOADER.md) | Manifest, bootstate and loader flow | Canonical for the loader; ADR-0004 extends the entry contract |
| [`adr/0003-firmware-memory-map.md`](adr/0003-firmware-memory-map.md) | Memory map decision | Proposed; this paper uses it as the working assumption |
| [`adr/0004-firmware-entry-abi.md`](adr/0004-firmware-entry-abi.md) | Entry ABI decision | Proposed; this paper uses it as the working assumption |
| [`state.md`](state.md) | What is true today | Evidence status; this paper does not override it |
| [`tasks.md`](tasks.md) | Current queue | F1–F5 map to T-054–T-058 |

### 1.6 Terminology

- **Loader** — the read-only bootloader that verifies and jumps. It is not the
  firmware.
- **Firmware** — the code that runs after the jump: HAL, block, filesystem, VFS,
  player, UI, diagnostics.
- **Kernel** — in this project, the small runtime that owns boot, memory,
  interrupts, time, panic and the main loop. It is not a POSIX kernel.
- **HAL** — hardware primitives with no policy: registers, clocks, ATA, LCD,
  buttons, codec, PMU.
- **BSP** — the board description and init sequence for one target (6G first).
  The BSP selects HAL implementations and supplies constants; the shared core
  must not contain target `#ifdef`s.
- **Boot-info** — the versioned page the loader passes in `r0` (ADR-0004).
- **Fixed pool** — a statically sized region with a compile-time maximum; no
  general-purpose allocator.
- **Host harness** — a build of firmware modules on macOS with a fake block
  device, clock, display and input, used for tests and UI iteration.

---

## 2. What the firmware must become

### 2.1 Product requirements and their firmware consequences

The root README lists 25 product goals. The firmware does not need all of them
in v1, but the architecture must not make the later ones impossible. The
following table is the firmware view of the goals that shape the first three
phases.

| Product requirement | Firmware consequence | First phase |
|---|---|---|
| Native HFS+ playback library | HFS+ read core, block layer, VFS, catalog lookup | F1/F3 |
| Native HFS+ writable without overlay | Transaction engine, journal replay, catalog/bitmap/extents mutation | F2 and later |
| Browse by artist/album/song | Database cache plus catalog fallback | After F3 |
| Playback with seek and gapless | Audio buffer, decode, output, playback state machine | F5 |
| Settings, playlists, resume | Native files under `/.helikon/`, written through the transaction engine | After F2 |
| Themes and skins | Documented subset, hard caps, first-party fallback | Later |
| Apps and games | Sandbox, quotas, trust boundary | Later |
| Podcasts and car use | Host-synced content, line-out and charging first | Later |
| Desktop app and installer | Host-side; firmware exposes files and update staging | Later |
| 7G and other ports | BSP boundary, no target `#ifdef` in core | Later |
| Battery and thermals | Explicit power states, measured budgets | Continuous |

The first firmware is a **player**, not a platform. The platform hooks exist as
boundaries, not as implementations.

### 2.2 Safety invariants that shape the firmware

The numbered invariants are canonical in [`invariants.md`](invariants.md). Five
of them drive this architecture:

- **I-03/I-05: every metadata change goes through the transaction engine.** The
  firmware cannot expose a "fast path" that writes a catalog block directly,
  even for bootstate or settings.
- **I-06: no silent journal bypass.** A dirty journal mounts read-only. The
  firmware never ignores, deletes, truncates or zeroes a journal to make a boot
  succeed. Replay belongs to the write engine and must finish before the first
  write.
- **I-11: never write a volume `fsck_hfs` reports dirty or damaged.** The mount
  path must carry that state into every write decision.
- **I-14: signed firmware before jump.** The loader owns this; the firmware must
  not weaken it by loading unsigned payloads, plugins or updates.
- **I-15: one writer and a power gate.** The firmware has one writer per volume,
  serialized, and it refuses writes below the owner-defined battery/charger
  threshold (D-12).

Firmware-specific consequences are listed in §5.9 and Appendix D.

### 2.3 Hardware constraints

The 6G reference facts are in the design paper §1. The firmware-relevant subset:

| Constraint | Value | Firmware consequence |
|---|---|---|
| SoC | Samsung S5L8702, ARM926EJ-S | 32-bit ARM, no assumption of a usable FPU |
| RAM | 64 MiB | Fixed map, fixed pools, no unbounded caches |
| Display | 320×240, 16-bit colour | RGB565 framebuffer, integer blitting |
| Input | Click wheel, buttons, hold switch | Event queue, debounce, no polling in the UI renderer |
| Audio | CS42L55 codec, line-out, headphone | I2S/DMA path, integer sample handling |
| Storage | ATA/LBA48, 512-byte logical sectors, 4096-byte support | Block layer with explicit sector size, bounce buffers, flush |
| Power | PMU, battery, charging, backlight, ATA sleep | Explicit states, no implicit sleep from the backlight path |
| Boot | Apple NOR path, DFU, `MOVE_AREA` for the loader | Firmware is a separate image at `0x08000000`; `MOVE_AREA` is not its budget |
| Filesystem | HFS+ v4 native only | No FAT mount, no overlay, no HFSX |
| Network | None | No device-side scrobbling, podcast fetching or update download |

The **no-FPU assumption** is deliberate. The ARM926EJ-S class commonly ships
without a vector floating-point unit, but the 6G board must confirm it. Until
confirmed, kernel, UI and audio code use integer or fixed-point math only. That
is also the right choice for determinism and code size.

### 2.4 Phase-0 and phase-1 exit criteria

From [`roadmap.md`](roadmap.md):

- **Phase 0:** boots, loads a verified image, Apple NOR path intact, zero
  filesystem writes, volume hash unchanged. No in-bootloader recovery menu
  (D-26); native Menu+Play and DFU cover recovery.
- **Phase 1:** plays music, browses the library, survives reboot, volume hash
  unchanged; unsupported files report a reason.

F1 completes the firmware half of phase 0. F2–F5 complete phase 1. The loader
half of phase 0 is already owner-reported on one 6G, but no firmware exists.

### 2.5 Non-goals for the first firmware

The following are explicitly out of scope until the player works:

- USB mass storage or a USB service mode;
- a host-built database and on-device browsing by metadata;
- skins, apps, games and the sandbox;
- podcast sync, car protocol and the desktop app;
- update installation (staging only much later);
- 7G or any second board;
- battery-life optimization or thermal tuning;
- writable access to the music library (only `/.helikon/bootstate` in F2);
- an in-firmware "repair", format or journal-delete action.

Saying no to these is what makes F1 finishable.

---

## 3. The boot boundary

### 3.1 What the loader guarantees today

The loader is documented in [`../bootloader/LOADER.md`](../bootloader/LOADER.md).
For the firmware, the relevant guarantees are:

1. It reads `/.helikon/firmware.manifest`, `/.helikon/firmware.img` and
   `/.helikon/bootstate` through a read-only HFS+ path.
2. It verifies the Ed25519 signature **before any image byte is read**, then
   checks magic, version, board, image version, load address, size and SHA-256.
3. It reads the image directly to the signed load address.
4. It flushes/invalidates caches, disables IRQ and jumps.
5. It never writes the filesystem and never replays a dirty journal.
6. It fails closed: no jump, a recovery screen and a safe power-off.

### 3.2 What the loader does not guarantee

This is the list that makes the entry ABI necessary.

| Not guaranteed | Why it matters | Firmware action |
|---|---|---|
| A boot-info argument | The loader currently calls `entry(void)` | Proposed ADR-0004; defensive fallback until accepted |
| A valid stack | The loader's stack is its own | Set SVC/IRQ/FIQ stacks before C |
| Zeroed `.bss` | The loader does not know the firmware's sections | Startup code zeroes `.bss` |
| MMU state | Not documented | Confirm on device; establish identity map |
| Cache state | The loader cleans/invalidates, but firmware policy is its own | Re-enable with explicit policy |
| Watchdog state | Not documented | Disarm or re-arm explicitly |
| Display state | The splash is left on screen | Repaint; never assume a blank framebuffer |
| ATA state | The loader may have initialized storage | Re-init through the HAL; do not reuse loader state |
| DRAM size | The loader validates 8 MiB, not 64 MiB | Use the fixed map; verify on device |
| Bootstate authority | The loader selected a slot; the file is still the truth | Re-read before writing |
| A return path | Production firmware must not return | Panic on return |

The last row is not a joke. The test payload returns `HELO` so the loader can
print `JUMP OK`; that path must not survive into production. A firmware that
returns to the loader would re-enter a bootloader that has already given up its
stack frame, with caches in an unknown state. The entry sequence must end in
`main()` and `main()` must never return.

### 3.3 The entry ABI, in one page

ADR-0004 proposes the full layout; the firmware-facing summary is:

```text
r0 = pointer to struct helikon_boot_info at 0x087ff000
r1 = 0
IRQ/FIQ disabled, SVC mode
no valid stack, no zeroed BSS, no display/ATA state
entry at the first instruction of the image
no return
```

The boot-info page carries the verified image version, board, load address,
size, SHA-256, bootstate sequence/state, journal observation, loader stage time
and a copy of the manifest. It is versioned, fixed-size, little-endian and
CRC-32 protected. The firmware validates it and treats a bad page as
`BOOT_INFO_INVALID`, not as a reason to guess.

Two design rules keep the ABI honest:

- **Boot-info is a cache, not a trust anchor.** The loader's signature check is
  the trust anchor. The firmware may display boot-info facts and use them to
  avoid duplicate I/O, but it must re-read the bootstate file through HFS+
  before writing it.
- **The ABI is additive only.** A new field means a new version and size. The
  firmware never interprets an unknown trailing field.

### 3.4 Image layout and linker contract

The image is linked at its load address and is not relocatable. Proposed
layout:

```text
0x08000000  .text.start      first instruction: branch to _start
            .text            kernel, HAL, block, HFS+, VFS, player, UI
            .rodata          strings, font atlases, splash, tables
            .data            initialized globals
0x087ff000  boot-info page   loader-owned; not part of the image
0x08800000  .bss             zeroed globals, fixed pools, stacks (runtime-only)
```

Rules:

- The first word of the image is a branch, not a header. The manifest is the
  metadata; a second on-image header would be a second source of truth.
- The linker script defines `__image_start`, `__image_end`, `__bss_start`,
  `__bss_end`, `__stack_top` and `__arena_start`. The raw binary ends at
  `__image_end`; BSS, pools and stacks live in the kernel region at
  `0x08800000` and are zeroed/initialized at boot.
- A linker `ASSERT` fails the build if `__image_end` exceeds `0x087ff000`, or if
  `__bss_end` exceeds `0x09000000`.
- `_start` runs in assembly: set stacks, zero `.bss`, copy `.data`, install
  vectors, then call `firmware_main`.
- No C++ exceptions, RTTI, constructors or global destructors. C only for the
  first firmware; a later C++ decision would need its own review.
- No position-independent code, no dynamic linking, no relocation at boot.
- DMA buffers are aligned to the cache line and placed in a non-cacheable or
  explicitly cleaned region. The exact alignment is a BSP constant, measured on
  the device.

### 3.5 Failure before `main`

A firmware that faults before the display is initialized is invisible. The
startup path therefore has a minimal early-panic path:

1. If the serial port is available, write a one-line fault record.
2. If the display is usable, paint a solid error screen with a short code.
3. If neither is available, toggle the backlight or piezo in a documented
   pattern (only if the BSP proves it is safe).
4. Power off through the PMU, or park with interrupts disabled if power-off is
   not yet proven.

The early-panic path must not call the filesystem, allocate memory or depend on
the scheduler. It is allowed to be ugly; it must be reliable.

---

## 4. Memory architecture

### 4.1 Two budgets, not one

The most common firmware mistake is to confuse three numbers:

- **`MOVE_AREA`** — the loader's code window, `0x22020800`–`0x22040000` on the
  current diagnostic build. The firmware does not run there.
- **The signed image window** — `0x08000000`–`0x08800000`, 8 MiB. The loader
  validates the image inside it. The firmware image must fit.
- **Runtime DRAM** — 64 MiB. The firmware may use it after entry, but only
  through a fixed map that the linker and the modules agree on.

The current loader accepts images up to 8 MiB. The proposed map reserves the
top 4 KiB of that window for boot-info, so the image cap becomes 8 MiB − 4 KiB.
That is a deliberate trade: 4 KiB for a stable handoff, instead of a second
out-of-band structure.

### 4.2 The proposed map

The full table and options are in
[`adr/0003-firmware-memory-map.md`](adr/0003-firmware-memory-map.md). The working
assumption for this paper:

| Range | Size | Use |
|---|---:|---|
| `0x08000000`–`0x087fefff` | 8 MiB − 4 KiB | Verified image: code, rodata, data |
| `0x087ff000`–`0x087fffff` | 4 KiB | Boot-info page (loader writes, firmware reads) |
| `0x08800000`–`0x08ffffff` | 8 MiB | BSS, exception stacks, fixed pools, log ring |
| `0x09000000`–`0x097fffff` | 8 MiB | UI: framebuffer(s), font atlas, artwork cache |
| `0x09800000`–`0x0a7fffff` | 16 MiB | Audio buffers, codec work area, DMA descriptors |
| `0x0a800000`–`0x0b7fffff` | 16 MiB | Database mapped pages and index cache |
| `0x0b800000`–`0x0bffffff` | 8 MiB | Reserve: diagnostics, crash record, future growth |

These are **design targets** derived from the 64 MiB budget in
`docs/architecture.md` §6, not measurements. The split is deliberately
conservative: audio gets the lower end of its 16–24 MiB target, database gets
16 MiB for mapped pages (not a full library copy), and 8 MiB stays unassigned.

### 4.3 Why a fixed map

A fixed map has four practical benefits:

1. **The linker can check it.** A build that overflows the image or a pool fails
   before it reaches the device.
2. **DMA is debuggable.** A driver knows its buffer region and alignment; there
   is no "where did the allocator put it this time" failure.
3. **The host harness can mirror it.** A fake block device and a fake display
   can use the same addresses or a host-side shadow, so layout bugs reproduce.
4. **A port is a BSP change.** The 7G changes constants, not the core.

The cost is fragmentation and the risk of a wrong constant. Both are handled by
measurement and a reserve region, not by a general allocator. If a module needs
more memory, it gets a budget review; it does not silently take from reserve.

### 4.4 Stacks and exception modes

The ARM926 has several exception modes. The first firmware uses:

| Mode | Use | Proposed initial size |
|---|---|---:|
| SVC | Kernel and main loop | 8 KiB |
| IRQ | Interrupt top halves | 4 KiB |
| FIQ | Not used initially; reserved | 1 KiB |
| ABT | Fault handler only | 1 KiB |
| UND | Fault handler only | 1 KiB |
| SYS | Not used initially | 0 |

The sizes are placeholders to be measured with a high-water pattern in F1. Each
stack gets a guard word; a guard hit is a panic, not a silent corruption. The
kernel must not run deep recursion on the SVC stack; HFS+ B-tree traversal is
bounded by the tree depth and the existing code's recursion must be audited
before it is used in an interrupt or a small-stack context.

### 4.5 Caches, MMU and DMA

The ARM926 has separate instruction and data caches. The exact 6G cache
geometry and the loader's MMU state are **open questions** until measured. The
design rules are independent of the answers:

- The firmware starts with the MMU in a known state (identity map, or off) and
  enables caches only after the map is established.
- DMA buffers are either in a non-cacheable region or explicitly cleaned before
  a device read and invalidated after a device write. "It worked on the host"
  is not evidence of coherency.
- No cacheable and non-cacheable aliases of the same physical page.
- ATA transfers use a bounce buffer when the caller's buffer is not aligned or
  not DMA-safe.
- A flush/barrier is a real hardware operation with an error return. A failed
  flush is a failed transaction, not a warning.
- Interrupt handlers do not touch cacheable buffers that a DMA engine may be
  writing without the documented clean/invalidate sequence.

The loader already calls `commit_discard_idcache()` before the jump. That is a
starting point, not a firmware cache policy. F1 must print the cache/MMU state
it observes and record it in the device matrix.

### 4.6 Sections and zeroing

`_start` performs, in order:

1. mask interrupts and set the mode stacks;
2. invalidate caches and the branch predictor as required by the BSP;
3. zero `.bss` from `__bss_start` to `__bss_end`;
4. copy `.data` from its load address to its run address (if they differ);
5. install the exception vectors;
6. call `firmware_main(&boot_info)`.

The loader does not do this for the firmware. A firmware that assumes a zeroed
`.bss` will work in a host build and fail on the device, which is the worst kind
of bug.

### 4.7 Budget enforcement

Three layers keep the map honest:

- **Compile time:** `static_assert` for every pool, stack and cache size; linker
  `ASSERT`s for the image end and the BSS end.
- **Link time:** a size report in the build log with the image end, BSS end,
  headroom and a fail on overflow.
- **Run time:** watermarks and high-water counters for stacks and pools; a
  `NO_SPACE` or `BUFFER` error before an overrun; a panic if a guard is hit.

A module that cannot state its maximum is not ready to merge.

### 4.8 Open questions

- What is the exact MMU and cache state at the jump?
- Is the full 64 MiB DRAM contiguous and usable at `0x08800000` onward?
- Where does the LCD controller expect its framebuffer, and what alignment?
- Which region is safe for DMA on the 6G?
- What are the real stack high-water marks?
- Does the loader's window validation cover the boot-info page if it is placed
  above `0x08800000`? (The proposed map avoids this by keeping it inside.)
- Is there a usable watchdog, and who owns it across the jump?

---

## 5. Runtime model

### 5.1 Single core, one address space

The 6G has one ARM926EJ-S core. The first firmware runs with one address space
and no user mode. The MMU, if enabled, is used for caching, permissions on
read-only regions and a guard page or two — not for process isolation. There are
no processes, no threads and no `fork`.

This is a deliberate reduction. A microkernel, a preemptive scheduler and an
MMU-protected app runtime are all defensible for a mature product, but they add
failure modes before the first block driver works. The design paper's security
model puts apps in a sandbox later (D-08); that sandbox can be built on top of a
stable single-address-space core or replace part of it, but it must not shape
the first player.

The one address space has a rule: **no module writes outside its own pools.**
The fixed map and the guard words are the enforcement. A future sandbox that
needs isolation will need a separate address space and a real review; it is not
a flag on the existing core.

### 5.2 Execution contexts

There are four contexts, in priority order:

1. **Exception handlers** — reset, undefined instruction, prefetch/data abort,
   IRQ, FIQ. They run on the exception stacks. They do the minimum: acknowledge
   the source, record a timestamp/counter, queue deferred work and return.
2. **Deferred work** — a small queue drained by the main loop. ATA completion,
   DMA completion, button events and timer expiry are queued here. Deferred work
   may block on I/O only if the main loop is allowed to block; the first player
   keeps blocking calls out of the UI path.
3. **Main loop** — the cooperative scheduler. It drains deferred work, runs the
   current screen's frame, advances the player state machine, services the
   filesystem and sleeps when idle.
4. **Boot/panic** — a single-threaded path before the main loop and after a
   fatal fault. It does not use the scheduler.

There is no preemption between main-loop tasks. A long filesystem operation can
stall the UI unless it is chunked or moved to a worker context. The first
firmware accepts that trade; F3 must measure the worst-case catalog scan and
chunk it if the UI stutters.

### 5.3 Why cooperative first

A cooperative loop is easier to reason about:

- no locks between main-loop tasks;
- no priority inversion;
- no context-switch code to port;
- deterministic host tests: a fake clock and a scripted input sequence produce
  the same frames;
- a smaller code and stack budget.

The cost is that one blocking driver can freeze the UI. The mitigation is a
discipline, not a scheduler: drivers expose non-blocking start/poll/finish
operations, and the main loop never calls a "wait forever" function. A blocking
call is allowed in boot and in a dedicated worker, not in the render path.

If measurements show the discipline is not enough — for example, ATA reads
regularly take longer than a frame — the next step is a small preemptive
scheduler or a dedicated I/O thread, not a general-purpose RTOS. That decision
needs its own ADR and a measurement.

### 5.4 Interrupt discipline

Rules:

- Handlers do not call the filesystem, the UI, the player or the allocator.
- A handler may read/write a register, set a flag, push a fixed-size event and
  return.
- Critical sections are short and explicit. The first firmware uses
  `irq_save()`/`irq_restore()` around shared counters and queues, not a global
  "interrupts off for the whole operation".
- Every wait has a timeout. A driver that can wait forever is a hang, not a
  driver.
- The interrupt priority/controller configuration is a BSP detail. The core sees
  an interrupt number and a handler registration.
- A spurious interrupt is counted and ignored; it must not panic the device.
- FIQ is reserved and unused in F1. A later audio path may use it, but only with
  a measured latency need.

### 5.5 Time

The firmware needs three clocks:

- **Monotonic tick** — for timeouts, frame pacing and profiling. The BSP
  provides a free-running counter or a timer interrupt; the resolution and drift
  are measured, not assumed.
- **Wall clock** — from the RTC, for file timestamps and the clock screen. It may
  be wrong; the UI must let the user set it and must not silently write the
  volume on every tick.
- **Playback clock** — derived from the audio DMA position, not from the UI tick.
  Seeking and progress display use the audio clock; the UI tick only schedules
  repaints.

Time is represented as a 64-bit count of ticks or milliseconds at the boundaries
where wraparound matters. A 32-bit millisecond counter wraps in about 49 days;
the firmware must either use 64-bit arithmetic or handle wrap explicitly. This
is the kind of bug that appears after a long test, not in the first hour.

### 5.6 Memory allocation and ownership

There is no `malloc`. There are three allocation mechanisms:

1. **Static pools** — fixed-size arrays of structs, with a free list and a
   high-water counter. Used for VFS handles, block requests, UI nodes and player
   buffers.
2. **Arena** — a boot-time bump allocator over a reserved region, used for
   one-time tables (font atlas, database index, catalog cache) that live for the
   session. No free; a reset is only allowed at a defined lifecycle point.
3. **Stack** — small, fixed, measured. No large buffers on the stack.

Ownership rules:

- The caller owns the buffer it passes in; the callee borrows it for the call
  unless the interface says otherwise.
- No function returns a pointer to a hidden static buffer unless the API names
  the buffer and its lifetime.
- A handle is an index plus a generation counter, not a raw pointer. A stale
  handle returns `INVALID_HANDLE`, not a wild pointer.
- A module that allocates from a pool returns `NO_SPACE`/`BUSY`; it never grows
  the pool at runtime.
- Free lists are LIFO or FIFO by design, not by accident. A pool with
  fragmentation risk gets a fixed-size object, not variable-size blocks.

### 5.7 Error model

Every fallible function returns an `int` error code from a single enum. There is
no `errno`, no exception, no global "last error" and no boolean that hides a
cause. The error taxonomy in [`failures.md`](failures.md) is the vocabulary:
`NOT_HFS`, `JOURNAL_DIRTY`, `CORRUPT`, `NO_SPACE`, `READ_ONLY`, `UNSUPPORTED`,
`NAME_INVALID`, `DB_STALE`, `IO`, plus the VFS lookup errors.

Rules:

- `0` is success; negative values are errors. Do not mix positive success codes
  with negative errors in the same function.
- A function that can partially succeed must report how far it got, or must be
  transactional. The writer is transactional; the UI is not.
- Errors propagate upward until a layer can either handle them or turn them into
  a user-visible state. They are never swallowed.
- A flush error is always an error. A short read is an error. A timeout is an
  error, even if a retry later succeeds.
- The UI maps errors to plain language and an action; it does not print a raw
  negative number alone.

### 5.8 Logging and tracing

A freestanding firmware has no `printf` by default. The first firmware needs a
small, bounded trace facility:

- a RAM ring of fixed-size records: timestamp, level, module, event code, two
  arguments;
- a compile-time level (`ERROR`, `WARN`, `INFO`, `TRACE`) with `TRACE` off in
  release;
- optional serial output in development builds;
- optional on-screen "last stage" code for bring-up;
- no file writes on a failing filesystem; the log ring is RAM-only until the
  filesystem is healthy and the user explicitly exports.

The ring must be safe to write from a panic path and from an interrupt. That
means a lock-free single-writer ring or a very short critical section, not a
`printf` that calls the filesystem.

### 5.9 Panic, faults and watchdog

A panic is a controlled stop:

1. disable interrupts;
2. record the fault type, PC, LR, SP, mode and a short reason in a reserved RAM
   record;
3. paint a panic screen if the display works;
4. write one line to serial if available;
5. power off through the PMU, or park with interrupts disabled if power-off is
   not proven.

A panic must not attempt a filesystem write, a journal replay or a "repair". It
must not reboot in a loop. If a watchdog is available, the panic path either
feeds it once for long enough to paint the screen or disables it deliberately;
it must not leave the device in a reboot loop that hides the fault.

The watchdog policy is an open question. A watchdog that resets during a normal
long ATA operation is worse than no watchdog; a watchdog that never fires during
a hung UI is useless. The first firmware should instrument the main loop and the
longest operations before arming one.

### 5.10 Main loop and power

The main loop is a priority-ordered set of tasks, not a free-for-all:

```text
loop:
  drain deferred work (interrupt completions, input, timers)
  service the active screen (input -> focus -> action)
  advance the player state machine (buffer, decode, output)
  service background work (artwork decode, database page, log flush)
  if nothing to do: enter the idle power state
```

The idle state must be explicit. The design paper's power model is:

```text
ACTIVE -> IDLE -> STORAGE_GATED -> STORAGE_DEEP -> SYSTEM_SLEEP -> OFF
```

Every transition names its wake source and restore steps. The first firmware
implements `ACTIVE -> IDLE -> OFF` and leaves storage gating for later. It must
not silently sleep from the backlight path or cache PMU state without
invalidation.

### 5.11 Firmware invariants

These are derived from the canonical list and apply to every firmware module:

- **F-I1:** `firmware_main` never returns. A return is a panic.
- **F-I2:** No filesystem write before the mount path has established
  read-write state and the journal is clean or replayed by the write engine.
- **F-I3:** The first firmware write is the bootstate promotion in F2, through
  the transaction engine, after re-reading the file. Boot-info is not authority.
- **F-I4:** No module outside the HAL touches ATA, PMU, LCD, GPIO or codec
  registers.
- **F-I5:** No allocation outside a fixed pool or the boot arena. No `malloc`.
- **F-I6:** Every wait has a timeout; every flush error propagates.
- **F-I7:** A panic, fault or watchdog stop never writes the filesystem.
- **F-I8:** A dirty or invalid journal mounts read-only; no UI action can
  override that.
- **F-I9:** The UI never blocks on storage in the render path.
- **F-I10:** A failed skin, font or artwork load falls back to the first-party UI
  and reports the failure; it never leaves a blank screen.

### 5.12 Open questions

- Is a preemptive scheduler needed for audio, or is DMA + a cooperative loop
  enough?
- What is the worst-case ATA read latency on the 6G/iFlash SD?
- Is there a usable RTC wake or alarm?
- What is the watchdog behaviour across the loader jump?
- How much stack does the deepest HFS+ path actually use?
- Do we need a separate worker stack for the writer, or can it run on the main
  stack with a bounded dirty set?

---

## 6. Module architecture

### 6.1 Layer map

```text
+--------------------------------------------------------------+
| UI / screens / input / renderer                              |
+--------------------------------------------------------------+
| Player / playback state / audio pipeline                     |
+--------------------------------------------------------------+
| VFS / handles / paths / capabilities                         |
+--------------------------------------------------------------+
| HFS+ read core        | HFS+ write engine + journal + txn   |
+------------------------+-------------------------------------+
| Block device / partition scanner / flush / power             |
+--------------------------------------------------------------+
| Board HAL: clock, PMU, ATA, LCD, buttons, codec, ADC/RTC     |
+--------------------------------------------------------------+
| BSP 6G: pins, constants, init sequence, memory map           |
+--------------------------------------------------------------+
```

Dependencies point downward. A lower layer never includes a higher layer's
header. The UI does not know what a B-tree is; the HFS+ core does not know what a
screen is; the HAL does not know what a file is.

### 6.2 The dependency rule

| Layer | May call | Must not call |
|---|---|---|
| UI | player, VFS, diagnostics | HFS+, block, HAL registers |
| Player | VFS, audio HAL, diagnostics | HFS+, block, UI |
| VFS | HFS+ read/write, diagnostics | HAL registers, UI |
| HFS+ | block, lib, diagnostics | VFS, player, UI |
| Block | HAL block primitives | HFS+, VFS, UI |
| HAL | BSP constants, registers | HFS+, VFS, UI, player |
| BSP | HAL implementations | any core module |

A violation is a review blocker, not a style preference. It is also a test
seam: the HFS+ core can be built on the host with a fake block device because it
never reaches for a register.

### 6.3 The HAL boundary

The HAL exposes **operations**, not global state:

```c
struct block_device {
    int (*read)(void *ctx, uint64_t lba, unsigned int count, void *buf);
    int (*write)(void *ctx, uint64_t lba, unsigned int count, const void *buf);
    int (*flush)(void *ctx);
    int (*power)(void *ctx, enum power_state state);
    struct device_info info;
};
```

The same pattern applies to display, input, audio, PMU, RTC and backlight:

- an opaque context;
- init/deinit;
- a small set of operations with explicit errors;
- no exposed register addresses;
- no policy (no "sleep after 30 s", no "dim at 20%", no "retry three times");
- no hidden global that a second instance would corrupt.

The HAL may use a singleton internally because the hardware is a singleton, but
the API does not require one. That keeps the host harness possible.

### 6.4 The BSP

The BSP is the only place that knows it is a 6G:

- memory-map constants (ADR-0003);
- pin muxing, clock/PLL setup, power rails;
- LCD panel timings and framebuffer address;
- ATA controller base and DMA constraints;
- button/click-wheel mapping;
- codec routing and I2S configuration;
- PMU/charger/backlight sequences;
- board ID and capability flags.

The shared core sees a `struct board` with function pointers and constants. A 7G
port is a new BSP, not a new `#ifdef` in the VFS. The donor code is the source
for the register sequences (paper §6, verdict K/M); the BSP wraps them, it does
not copy the donor's policy layers.

### 6.5 Block and partition layer

Responsibilities:

- sector read/write with explicit 512/4096-byte handling;
- bounce buffers for unaligned or non-DMA-safe callers;
- flush/barrier with error propagation;
- power state transitions for the storage device;
- partition scan (APM first, then the documented alternatives);
- device info: logical/physical sector size, capacity, read-only flag.

It does not own:

- paths, files, caches or HFS+ semantics;
- retry policy beyond the documented one;
- a write path for the bootloader (the loader has its own read-only adapter).

The first firmware only needs read and flush. Write is added in F2 with the
transaction engine, not before.

### 6.6 HFS+ read core integration

The existing `firmware/hfs/` core is the asset. It is freestanding-friendly: it
uses `stdint`/`stddef` and `memcpy`/`memset`, not `malloc`, `stdio` or an OS. The
integration work is therefore small:

- provide the `hfs_read_fn` callback over the block device;
- provide `memcpy`/`memset`/`memcmp` (and any other libgcc helpers) in
  `firmware/lib/`;
- keep the existing error taxonomy; do not wrap it in a second one;
- add a compile-time check that no hosted header or symbol slips in;
- keep the host tests as the regression suite.

The core stays read-only in F1. The write engine is linked only when F2 needs
it, and even then only through the transaction API.

### 6.7 Write engine and transaction integration

The write engine (`hfs_txn`, `hfs_journal`, bitmap, B-tree, catalog, extents) is
already host-tested and has one outer-volume device run. The firmware
integration rules are:

- one writer per volume, serialized by the VFS;
- a bounded dirty set; `NO_SPACE` before any change if it does not fit;
- data before metadata; flush before and after commit;
- replay before the first write, never after a read-only mount;
- no direct block write outside `hfs_txn`;
- the bootstate promotion is a normal transaction, not a special case.

F2 is deliberately the first firmware write: it is small, reversible and
observable by the loader. It proves the write path without touching the music
library.

### 6.8 VFS

The VFS is a thin, capability-oriented layer:

- lookup, iterate, open, read, seek, close;
- create, write, rename, delete (later, through the transaction engine);
- handles with generation counters;
- paths with length checks and no traversal;
- capabilities: `READ`, `WRITE`, `DIRECTORY`, `SEEKABLE`, `RESOURCE_FORK`,
  `SYMLINK`;
- `WRITE` means a native HFS+ transaction, not an overlay write.

It does not own:

- HFS+ key ordering, B-trees or extents;
- hardware registers;
- UI policy or playback state;
- a second error taxonomy.

The VFS is where the "no overlay" promise becomes visible: there is one
namespace, and a non-HFS+ volume returns `NOT_HFS`.

### 6.9 Caches

The first firmware needs four caches, each with a hard maximum:

| Cache | Purpose | Proposed budget | Eviction |
|---|---|---:|---|
| B-tree node cache | Catalog/extents nodes | 512 KiB | LRU, dirty-node pinning |
| Name/path cache | Recent lookups | 128 KiB | LRU |
| File read cache | Sequential playback/artwork | 1 MiB | Read-ahead window, drop on seek |
| Artwork cache | Decoded album art | 2 MiB | LRU, one screen's worth |

These are **design targets**, not measurements. D-05 is still open; the numbers
must be measured on the device and recorded before they are called budgets.
Every cache exposes hit/miss counters, a high-water mark and a `flush` for
transaction boundaries. A dirty B-tree node is pinned until commit; it cannot be
evicted by a UI artwork decode.

### 6.10 Database

The database is a cache, not the source of truth (D-07 is open). The first
firmware should not build one. F3 can browse the catalog directly; the database
arrives when a 10k-track catalog makes direct browsing too slow. The boundary is
defined now:

- immutable, host-built first;
- read-only mapped pages, not a full copy;
- a stale entry falls back to a catalog scan and reports `DB_STALE`;
- the database never owns playback state or file data.

### 6.11 Player and audio pipeline

The player is a state machine:

```text
IDLE -> OPENING -> BUFFERING -> PLAYING -> PAUSED -> SEEKING -> DRAINING -> IDLE
```

It owns:

- the track handle and read-ahead;
- decode buffers and codec state;
- the output DMA queue;
- volume, pause, seek and next/previous;
- the playback clock;
- gap handling between tracks.

It does not own:

- HFS+ parsing or metadata writes;
- UI drawing;
- raw codec register access (that is the audio HAL);
- "audiophile" claims without measurement.

F5 starts with one codec and one output path. The design paper's audio budget is
16–24 MiB; the first implementation should use the smallest buffer that survives
storage sleep and seek, then measure.

### 6.12 UI and input

The UI is retained-mode and declarative:

```text
screen -> widgets -> layout -> renderer
input  -> focus  -> action
```

It owns:

- the screen stack and transitions;
- focus and selection;
- the renderer and font atlas;
- the first-party theme;
- error, empty and loading states;
- a bounded skin subset later.

It does not own:

- storage policy, filesystem writes or playback decisions;
- raw block/PMU/GPIO access;
- an unbounded image cache;
- a second font family.

The renderer is integer-only. It blits RGB565 and 8-bit alpha masks; it does not
rasterize a font per frame. The first-party UI must remain usable if a skin,
font or artwork file fails.

### 6.13 USB

USB is not in F1–F5. The boundary is defined so it does not leak into the core:

- read-only mass storage by default;
- no writable LUN;
- descriptors and strings in RAM, not in the filesystem;
- an optional framed service mode later, with its own protocol and trust model;
- no network stack on the device.

### 6.14 Diagnostics

Diagnostics are a first-class module, not `printf` scattered through the code:

- a bounded event ring;
- counters for block I/O, cache hits/misses, transactions, UI frames and audio
  underruns;
- a boot-stage record that survives into the UI;
- a crash record in reserved RAM;
- an export path only when the filesystem is healthy and the user asks.

The diagnostics module must be safe to call from a panic path and from an
interrupt. It must not write the filesystem by itself.

### 6.15 Update and bootstate

The firmware's update role is small in the first phases:

- read the bootstate file through the VFS;
- promote a `TRYING` slot to `GOOD` after self-test, through the transaction
  engine (F2);
- never write bootstate before the mount is read-write and the journal is clean;
- never stage or install an update before the update transport (D-16) and the
  signed manifest flow are designed.

The loader owns verification. The firmware owns the post-boot health decision.
The two communicate through the bootstate file, not through a shared mutable
structure.

### 6.16 Module ownership

Appendix C lists every module with its owner, inputs, outputs and review gate.
The short version:

| Area | Owner | First deliverable |
|---|---|---|
| Boot/entry | boot lead | F1 `_start`, linker, panic |
| HAL/BSP | hardware lead | F1 clock/PMU/LCD/ATA read |
| Block | storage lead | F1 read + flush |
| HFS+ read | HFS+ lead | F1 mount + read one file |
| Write engine | HFS+ lead | F2 bootstate transaction |
| VFS | VFS lead | F3 lookup/iterate/read |
| Player | audio lead | F5 one-codec playback |
| UI | UI lead | F4 renderer + screens |
| Diagnostics | diagnostics lead | F1 ring + stage codes |
| Build/test | test lead | host harness + image build |

### 6.17 What is deliberately not a module

- No overlay layer, no namespace merge, no `/.rockpod-rw`.
- No FAT adapter in the firmware.
- No plugin loader, no Rockbox binary compatibility.
- No full WPS engine.
- No general-purpose allocator.
- No shell, no scripting language, no network stack.
- No "repair" module. Recovery is read-only diagnosis, DFU or Apple restore.

---

## 7. Functional flows

### 7.1 Cold boot

The first firmware boot is a sequence of visible checkpoints, not a black box.
Each checkpoint has a screen code, a log record and a timeout.

| Stage | What happens | Visible result | Failure |
|---:|---|---|---|
| 0 | Loader verifies and jumps | Splash stays on screen | Loader recovery screen |
| 1 | `_start`: stacks, BSS, vectors | Splash, then firmware banner | Early panic |
| 2 | Boot-info validation | Banner shows image version | `BOOT_INFO_INVALID` diagnostic mode |
| 3 | Clock/PMU/backlight init | Backlight stable | Panic with code |
| 4 | LCD init and first framebuffer | Firmware splash/first frame | Panic with code |
| 5 | ATA init and identify | "Storage OK" | `IO` screen |
| 6 | Partition scan | "Volume found" | `NOT_HFS` screen |
| 7 | HFS+ probe and catalog open | "HFS+ OK" | `JOURNAL_DIRTY`/`CORRUPT` screen |
| 8 | Read one known file | "Read OK" | `IO`/`CORRUPT` screen |
| 9 | Input init and button test | "Input OK" | Panic or diagnostic |
| 10 | Idle / hand to UI | First screen | Panic |

F1 stops at stage 8/9 and powers off. F2 adds the bootstate write between 8 and
10. F3 adds the browser. The stage codes are stable; a user photograph of the
screen is enough to identify where a boot stopped.

The loader's 6.5 s includes its own splash, HFS+ lookup and verification. The
firmware's boot time is measured from the first firmware instruction to the
first interactive frame. They are separate numbers and must not be added
silently.

### 7.2 Mount

The mount path is a decision tree, not a retry loop:

```text
block device ready?
  no  -> IO; show storage error; power off or retry once
  yes -> scan partitions
          no HFS+ candidate -> NOT_HFS; read-only; explain
          candidate found   -> read primary volume header
                                invalid -> read alternate
                                           invalid -> CORRUPT; read-only
                                valid   -> inspect journal
                                           dirty/invalid -> JOURNAL_DIRTY; read-only
                                           clean/absent  -> validate catalog/extents
                                                            invalid -> CORRUPT; read-only
                                                            valid   -> mount read-write
```

A read-only mount is a successful mount with a capability restriction, not a
failure. The UI must say why writes are unavailable and must not offer a
"repair" button that bypasses the rule.

### 7.3 Read a track

The read path is the first end-to-end product path:

```text
UI selects a track
  -> VFS lookup path
  -> HFS+ catalog lookup (indexed descent + leaf scan)
  -> file record: size, extents, flags
  -> read-ahead: allocate a buffer from the file cache
  -> block read: one or more extents, bounce if needed
  -> player buffer: enqueue
  -> decoder: PCM frames
  -> audio HAL: I2S/DMA
  -> UI: playback state and clock
```

Failure points and user-visible results:

| Failure | Result |
|---|---|
| Path not found | `NOT_FOUND`; skip/remove from the list |
| File is a directory | `IS_DIR`; UI bug, log it |
| Unsupported object (resource fork, symlink) | `UNSUPPORTED`; list it, do not hide it |
| Extents overflow missing | `CORRUPT`; stop playback, read-only mode |
| Short read / ATA error | `IO`; retry once, then stop and report |
| Decoder rejects the file | `UNSUPPORTED`; show the codec/format reason |
| Buffer underrun | `BUFFER`; pause, refill, resume if the gap is acceptable |

The player must never spin on a bad file. A skipped track is better than a
frozen UI.

### 7.4 The first write: bootstate promotion

F2 is the first firmware write and the most carefully bounded one:

1. Mount read-write only if the journal is clean or the write engine has
   replayed it.
2. Re-read `/.helikon/bootstate` through the VFS. Do not trust boot-info.
3. Validate the two slots: magic, version, sequence, checksum, board, image
   version, load address, size and hash.
4. Find the slot that matches the running image. If the state is `TRYING` and
   the firmware self-test passed, stage a `GOOD` slot with a higher sequence.
5. Commit through `hfs_txn`; flush; read back; verify the checksum.
6. If any step fails, leave the old state untouched. A failed promotion is not a
   reason to write again with a different slot.

The loader then sees `GOOD` on the next boot. This closes the D-02 loop without
A/B rollback: there is one image, and a failed or mismatched state fails closed
to recovery.

The bootstate write is also the template for every later settings write:
re-read, validate, stage, commit, flush, read back, verify.

### 7.5 Sleep, wake and power

The first firmware implements a conservative subset:

```text
ACTIVE -> IDLE -> OFF
```

- `ACTIVE`: CPU running, display on, audio active or UI interactive.
- `IDLE`: display dimmed/off after a timeout, CPU in a low-power loop, audio
  paused or stopped, storage still powered.
- `OFF`: PMU power-off after a clean shutdown; no filesystem write in the power
  path.

Storage gating and system sleep are later work. When they arrive, each transition
must name:

- the wake source (button, click wheel, RTC, charger, USB);
- the state that must be saved before the transition;
- the restore order (clock, PMU, ATA, LCD, codec);
- the timeout and the failure path.

The design paper explicitly rejects implicit sleep from the backlight path and
cached PMU state without invalidation. The firmware follows that.

### 7.6 Shutdown

A clean shutdown:

1. stop playback and drain the audio DMA;
2. flush the current transaction, if any;
3. flush the block device and check the error;
4. blank the display or show a short "power off" frame;
5. power off through the PMU.

A shutdown must not start a new write. If a flush fails, show the error and do
not pretend the shutdown was clean. A power-off during a transaction is a crash,
not a shutdown; the journal handles it on the next boot.

### 7.7 USB (later)

Not in F1–F5. The future flow is read-only mass storage:

- enumerate as a read-only LUN;
- expose the HFS+ volume without a write path;
- no host write, no format, no repair;
- an optional service mode with a framed protocol and its own trust review.

### 7.8 Update staging (later)

The firmware does not install updates in F1–F5. The future flow:

1. host stages `firmware.img` and `firmware.manifest` under `/.helikon/update/`;
2. firmware verifies size/hash, flushes and reads back;
3. the user explicitly chooses install;
4. the loader verifies and jumps;
5. bootstate records the result.

No silent update, no writable USB, no unsigned image.

### 7.9 Failure flows

| Failure | Detection | User-visible | Firmware action |
|---|---|---|---|
| No storage | ATA init/identify timeout | "Storage not found" | Retry once, then power off |
| Not HFS+ | Volume signature | "HFS+ only" | Read-only, explain, no format |
| Dirty journal | Journal header/state | "Journal needs replay" | Read-only; write engine may replay later |
| Corrupt catalog | B-tree validation | "Catalog damaged" | Read-only, log, no repair |
| Missing manifest | Loader, before firmware | Loader recovery | Firmware never runs |
| Bad signature | Loader | Loader recovery | Firmware never runs |
| Bad boot-info | Firmware validation | Diagnostic mode | Read-only, no bootstate write |
| Bootstate mismatch | Firmware validation | "Boot state mismatch" | No write, recovery guidance |
| Audio underrun | Player | Short gap or pause | Refill, resume; log counter |
| UI frame overrun | Frame counter | Visible stutter | Log; optimize or reduce work |
| Panic | Fault handler | Panic screen | Record, power off, no write |

### 7.10 Boot-time budget

The loader's 6.5 s is measured. The firmware budget is not. A proposed
breakdown, to be measured in F1:

| Segment | Target | Notes |
|---|---:|---|
| `_start` + BSS + vectors | < 10 ms | Pure CPU |
| Clock/PMU/backlight | < 50 ms | Donor sequences |
| LCD init + first frame | < 100 ms | Panel timing |
| ATA init + identify | < 250 ms | Measure on iFlash SD |
| Partition scan + HFS+ probe | < 500 ms | Depends on catalog size |
| Total to first interactive frame | < 1 s | Design target, not a promise |

The numbers are targets chosen to leave room for the loader's 6.5 s. They are
not a claim that the firmware will boot in under a second. T-052 tracks the
loader time separately.

---

## 8. The firmware as a program

This section is about the people who will write and debug the firmware. The
design paper says the project is measured in months to years; a codebase that is
unpleasant to work in will not survive that. Ergonomics is not polish.

### 8.1 Repository layout

Proposed firmware tree:

```text
firmware/
  boot/            _start, vectors, linker script, boot-info parser, panic
  lib/             freestanding memcpy/memset, endian, CRC, ring buffer, fixed pools
  hal/6g/          clock, PMU, ATA, LCD, input, codec, ADC/RTC, piezo
  board/6g/        BSP constants, init sequence, memory map, board description
  block/           block_device, partition scan, bounce buffers, flush
  hfs/             existing HFS+ read/write core (do not duplicate)
  vfs/             lookup, handles, capabilities, paths
  db/              immutable database reader (later)
  player/          playback state, buffer, decode, output
  ui/              screens, widgets, layout, renderer, theme
  input/           click wheel/button events, debounce, focus
  usb/             read-only LUN (later)
  diag/            event ring, counters, crash record, export
  update/          staging and bootstate promotion (later)
```

The existing `firmware/hfs/` stays where it is in F1. Moving it would create a
large diff with no functional gain; a later split can happen when the write
engine gets its own directory.

### 8.2 Build system

Proposed commands:

```sh
make -C firmware BOARD=6g            # ELF + raw image + manifest
make -C firmware BOARD=6g size       # size report + assertions
make -C firmware BOARD=6g test       # host tests for freestanding modules
make -C firmware BOARD=6g image      # signed test image (dev key)
make -C firmware BOARD=6g clean
```

The build must:

- use the same pinned ARM toolchain as the loader (`arm-elf-eabi-gcc` 9.5.0 in
  the current Rockbox toolchain);
- compile with `-ffreestanding`, no hosted headers, and a linker script per
  board;
- fail on any warning in project code;
- print the image end, BSS end, headroom and SHA-256;
- produce a raw binary and a manifest, not just an ELF;
- keep the dev private key under `build/`, never in Git;
- support a host build of the core modules with a fake HAL.

A reproducible build is a later requirement. The first step is a build that
prints enough information to reproduce a failure: compiler version, flags,
source commit, image hash and map file.

### 8.3 Toolchain

The loader already depends on the Rockbox `arm-elf-eabi` toolchain. The firmware
should use the same compiler family for the first target to avoid a second
toolchain variable. The trade-offs:

- the toolchain is old, but proven on this hardware;
- a newer `arm-none-eabi` may generate better code, but changes codegen and
  library behaviour;
- a toolchain change is a measured decision, not a drive-by upgrade.

The build records the compiler version and the exact flags. A firmware bug that
only reproduces with one compiler is a toolchain bug until proven otherwise.

### 8.4 C dialect and style

- C99, no compiler extensions unless they are isolated and documented.
- 4-space indentation, 80-column soft limit, braces on their own line for
  functions (match the existing HFS+ code).
- `snake_case` for functions and variables, `UPPER_CASE` for macros, `struct
  lower_case` for types.
- Fixed-width types from `<stdint.h>`; no `int` for on-disk or register values.
- `const` on input pointers; `static` for file-local functions; no global
  mutable state unless it is a singleton with a documented reset.
- No VLAs, no `alloca`, no recursion without a documented depth bound.
- No floating point in kernel, UI or audio paths until the FPU question is
  settled. Use fixed-point with explicit scaling.
- No 64-bit division in hot paths unless measured; libgcc calls are not free.
- Every public function has a comment that states ownership, error codes and
  whether it may block.
- Every file starts with the SPDX identifier used by the project.

### 8.5 Headers and includes

- A header includes what it uses; it does not rely on the including file.
- No header includes a HAL register header except the HAL implementation.
- Public headers expose opaque structs and init/deinit functions; private
  structs stay in the `.c` file.
- One module, one public header. A second header is a sign the module is doing
  two things.
- No circular includes. If A needs B and B needs A, the boundary is wrong.

### 8.6 Interface design rules

- Pass an explicit context pointer, not a global. The host harness depends on it.
- Return an error code; do not encode failure in a sentinel pointer or a
  negative size.
- Take a length with every buffer. No `strcpy`, no unbounded `strlen` on
  untrusted data.
- Make blocking explicit: `_try`, `_poll`, `_wait` suffixes or a documented
  "may block" comment.
- Do not expose internal structs for the caller to poke. Provide an operation.
- A callback gets a `void *ctx` and a value; it does not get a pointer to the
  callee's private state.
- Timeouts are arguments or documented constants, not hidden globals.

### 8.7 Ownership and lifetime

- The caller owns its buffers; the callee borrows.
- A handle is an index + generation, not a pointer.
- A module that returns a pointer documents who frees it and when.
- No function stores a caller pointer beyond the call unless the API says so.
- Pool objects are returned exactly once; a double free is a panic in debug and
  a counter in release.
- A screen owns its widgets; a widget does not own the screen.
- The player owns its buffers; the VFS owns file handles; the UI owns no file
  data beyond the current screen.

### 8.8 Error handling patterns

Preferred shape:

```c
int rc = module_do(&ctx, args);
if (rc != 0)
    return rc;
```

For cleanup:

```c
int rc = 0;
resource = acquire();
if (!resource) {
    rc = ERR_NO_SPACE;
    goto out;
}
rc = use(resource);
out:
    release(resource);
    return rc;
```

Rules:

- Do not use `goto` to jump backward; forward cleanup is fine.
- Do not log and return the same error at every layer; log once where the
  context is known.
- Do not convert an error to success because a retry succeeded; report the
  retry in diagnostics.
- Do not panic on a recoverable user error (bad file, unsupported codec).
- Panic on an invariant violation (pool corruption, guard hit, impossible
  state).

### 8.9 Freestanding libc

The firmware provides a small, tested subset:

- `memcpy`, `memmove`, `memset`, `memcmp`;
- `strlen`, `strcmp`, `strncmp` (bounded variants preferred);
- endian conversion helpers;
- CRC-32 and SHA-256 only where needed (the loader has its own);
- fixed-point helpers;
- a ring buffer and a pool allocator.

No `printf`, `malloc`, `free`, `qsort`, `time`, `errno`, `stdio` or locale. A
`snprintf`-like formatter may be added for the UI/diagnostics, but it must be
bounded and tested against truncation.

The host build links the same `firmware/lib/` implementations where possible, so
a host test exercises the real `memcpy`/ring/pool code, not the host libc.

### 8.10 Static analysis and warnings

Minimum gate:

- `-Wall -Wextra -Werror -Wconversion -Wshadow -Wundef -Wpointer-arith`;
- `-Wcast-align` where it does not produce noise;
- `-fstack-usage` for stack reports;
- `cppcheck` or `clang --analyze` on the host build, if available;
- ASan/UBSan on the host build of core modules;
- a symbol check that no hosted function (`malloc`, `printf`, `open`) is linked
  into the firmware image.

The existing HFS+ tests already run with sanitizers. The firmware build should
reuse that discipline, not replace it with "it boots".

### 8.11 Host harness

The host harness is the most valuable ergonomic investment. It builds the same
core modules for macOS with:

- a fake block device backed by a disk image or an in-memory fixture;
- a fake clock that advances deterministically;
- a fake display that writes RGB565 frames to a PNG or a memory buffer;
- a scripted input queue (wheel detents, buttons);
- a trace log of block I/O, VFS calls, player state and UI frames.

It enables:

- UI iteration without flashing the device;
- regression tests for screens and focus;
- reproducible boot sequences;
- performance profiling of the core without ATA noise;
- fuzzing the parser and the VFS.

The harness is not a device. It cannot prove panel timing, audio quality or
power. It is a fast loop, not evidence.

### 8.12 Fixtures and record/replay

Two fixtures pay for themselves immediately:

1. **A tiny HFS+ image** with a known catalog, a few files and a journal, used by
   the boot read path. The existing tests already build such images.
2. **A recorded I/O trace** from a real device boot: block reads, timings,
   errors. Replaying it on the host reproduces a slow or failing boot without
   the device.

Record/replay also helps the loader/firmware boundary: a trace of the loader's
last reads can be replayed against a firmware build to see whether the firmware
asks for the same blocks or duplicates work.

### 8.13 Debugging on device

Ordered by usefulness:

1. **Serial**: the donor has a serial driver; if the 6G test unit exposes it,
   this is the fastest signal. Do not assume every unit does.
2. **On-screen stage code**: a large, stable code that survives a hang.
3. **Backlight/piezo pattern**: only if the BSP proves it is safe and does not
   interfere with power.
4. **RAM log ring**: readable through a debug screen or a host export after a
   controlled reboot.
5. **PMU power-off**: a clean end state, not a debug channel.

The firmware must not depend on a debugger. A device that only works under JTAG
is not a device result.

### 8.14 Onboarding

A new firmware contributor should be able to do this in the first week:

1. build the host HFS+ tests and run them;
2. build the firmware image and inspect the map/size report;
3. run the host harness and see a fake boot screen;
4. read one file through the fake block device;
5. change a screen label and see it in a host frame;
6. flash the F1 skeleton and see the banner on the device;
7. read the panic screen and the trace ring;
8. find the module they will own in Appendix C.

If any step requires a chat explanation, the docs or the harness are incomplete.

### 8.15 Review checklist

Every firmware PR:

- [ ] No write outside `hfs_txn`; no journal bypass.
- [ ] No `malloc`, hosted header or floating point in a kernel/UI path.
- [ ] No raw register access outside the HAL.
- [ ] Every wait has a timeout; every flush error propagates.
- [ ] Buffers have lengths; names have bounds; paths reject traversal.
- [ ] New pool/cache has a compile-time maximum and a high-water counter.
- [ ] Error paths return a specific code and have a test or a screen.
- [ ] Host tests pass; sanitizer clean where applicable.
- [ ] Image size and stack report reviewed.
- [ ] Docs updated (`state`, `tasks`, module doc, this paper if the design
      changed).

### 8.16 Documentation in code

- File header: purpose, owner, assumptions, error codes.
- Public function: what it does, ownership, blocking, errors.
- A non-obvious hardware sequence: a comment with the register name and the
  donor/paper reference, not a paraphrase of the datasheet.
- A "temporary" workaround: an issue/task ID and a removal condition. No
  unnamed temporary code.
- A magic number: a named constant and a source.

### 8.17 No target `#ifdef` sprawl

The shared core must not contain `#ifdef TARGET_6G`. The BSP provides:

- a `struct board` with function pointers and constants;
- memory-map constants;
- a board ID;
- capability flags (has FM radio, has Bluetooth, etc.).

A port adds a BSP directory, not branches in the VFS. If a feature is genuinely
optional, it is a runtime capability check, not a compile-time target branch.

### 8.18 Performance budgets

Every hot path has a budget and a counter:

| Path | Budget | Measurement |
|---|---|---|
| UI selection redraw | One frame, no input backlog | Frame trace |
| Screen transition | Short enough to feel instant | Host trace + device photo |
| Track open | < 100 ms to first audio buffer | Device trace |
| Seek | < 250 ms to audio | Device trace |
| Catalog lookup | < 10 ms for a warm cache | Counter |
| Bootstate write | < 100 ms including flush | Device trace |
| Idle wake | No periodic wake without a reason | Power measurement |

The numbers are targets. The instrument is the point: a regression is visible
before a user complains.

### 8.19 Anti-patterns

- A global mutable state that the host harness cannot reset.
- A blocking call in the render path.
- A "temporary" direct block write to fix a filesystem bug.
- A retry loop with no timeout.
- A cache with no maximum.
- A UI that redraws the whole screen for one character.
- A driver that assumes the loader initialized the hardware.
- A target `#ifdef` in shared code.
- A test that only passes on the host because the fake device is too nice.
- A design doc that says "should" without an owner or a number.

---

## 9. Aesthetic and interaction design

### 9.1 Design stance

The iPod is a quiet object. The firmware should feel like a well-made
instrument: immediate, legible, calm, and free of decoration that costs a frame
or a glance. The design stance is:

- **content first** — the music, the album, the track, the time;
- **one hand** — the click wheel is the primary input; the screen is 320×240;
- **no waiting without feedback** — a spinner, a progress row or a stage code;
- **no dead ends** — every error offers a next step or a safe power-off;
- **no fake depth** — no gradients, shadows or animated backgrounds that pretend
  to be a modern phone;
- **no tiny text** — 11 px is the floor, and it must be checked on the panel.

This is not minimalism for its own sake. It is a constraint that makes the UI
fast, readable in sunlight and consistent across skins.

### 9.2 Typography

The canonical table is [`ui.md`](ui.md). The firmware implements it exactly:

| Element | Typeface | Size |
|---|---|---|
| Main menu | IBM Plex Sans Regular (400) | 14 px |
| Selected item | IBM Plex Sans Medium (500) | 14 px |
| Screen titles | IBM Plex Sans Semibold (600) | 17 px |
| Artist / album | IBM Plex Sans Regular | 12 px |
| System information | IBM Plex Sans Regular | 11 px |
| Helikon-OS logo | Manrope Bold (700) | variable |

Rules:

- One UI family. No second font without an owner decision.
- Grayscale anti-aliasing; precomputed masks/atlas; no per-frame rasterization.
- A 1-bit fallback that is readable if the font fails to load.
- The selected row is Medium at the same size, not a larger size.
- The wordmark is not body text and does not appear on every screen.
- No clipping, overlap or horizontal scrolling at 11 px.
- A large-text option remains an accessibility requirement.

The host mockups in `assets/ui/` illustrate the scale; they are not a device
panel result.

### 9.3 Layout grid

Proposed provisional grid for 320×240:

```text
+--------------------------------------------------+
| 12 px margin                                     |
|  title row (17 px semibold)                      |
|  content rows (24 px pitch, 14 px text)          |
|  ...                                             |
|  footer/status (11 px)                           |
| 12 px margin                                     |
+--------------------------------------------------+
```

- Side margins: 12 px.
- Row pitch: 24 px; text baseline centered in the row.
- Title area: 28–32 px including spacing.
- Footer/status: 16–20 px, only when it carries information.
- List rows show one line of primary text; a second line (artist/album) uses
  12 px and only when the row is tall enough.
- The selected row uses a filled or outlined treatment with sufficient contrast;
  the exact treatment is a theme decision.
- No row is shorter than the 14 px text plus 4 px padding.

These values are provisional, derived from the host mockup. They must be checked
on the 6G panel before they are called final.

### 9.4 Colour and contrast

The first-party UI is neutral grayscale for text and chrome. Colour is reserved
for state:

| Colour | Value | Use |
|---|---|---|
| Off-white | `#EFEDE5` | Splash/base background, light theme surface |
| Ink | near-black, exact value open | Primary text on off-white |
| Neutral grey | `#6B665C` | Secondary text, disabled, neutral HFS+ state |
| Green | `#00BF63` | Success, HFS+ OK, healthy state |
| Red | `#FE3131` | Failure, destructive confirmation, HFS+ error |

The green/red values are the owner's bootloader colours. Extending them to the
firmware UI is a proposal, not a settled palette. Colour must never be the only
signal: pair it with an icon, a label or a shape. A user with a monochrome
perception or a washed-out panel must still understand the state.

Contrast targets are measured on the panel, not assumed from sRGB. The host
mockup is a design aid, not a calibration.

### 9.5 Iconography

- Simple geometric glyphs, one stroke weight, drawn on the pixel grid.
- 16×16 or 20×20 nominal sizes; no pseudo-3D, no photographic thumbnails as
  icons.
- Icons carry meaning, not decoration. A play triangle, a pause bar, a battery,
  a folder, a track, a warning.
- The same icon set is used in the first-party UI and the recovery screens.
- A missing icon renders a labelled placeholder, not a blank square.
- No proprietary icon font or copied artwork.

### 9.6 Motion

Motion is feedback, not entertainment:

- A wheel detent changes the selection on the next frame. No easing curve that
  makes the UI feel laggy.
- A screen transition is short and optional; a fade or slide must not block
  input.
- A progress indicator appears only after a threshold (for example, a slow
  catalog scan), not for every operation.
- No animated wallpaper, parallax, bouncing icons or idle animation.
- The splash-to-first-frame transition should not flash: keep the background
  colour and typography continuous.

Exact durations are open questions. The design target is "no input backlog and
no perceived wait without feedback", measured on the device.

### 9.7 Focus, selection and the click wheel

- One focus owner per screen. The focus ring/row is never ambiguous.
- The wheel moves one row per detent at normal speed; acceleration is a
  documented curve, not an accident.
- Centre button activates; Menu goes back; Play/Pause is global where it makes
  sense; Forward/Back seek or skip by context.
- Hold switches the input off; the screen says so.
- A long list uses a scrollbar or a position indicator, not a shrinking font.
- The selected row stays at a stable screen position while scrolling; the list
  moves under it.
- No double-tap, no hidden gesture, no modal that traps the user.

### 9.8 Now playing

The now-playing screen is the product's centre:

```text
+--------------------------------------------------+
|  Title (17 px semibold, truncated with ellipsis) |
|  Artist — Album (12 px)                          |
|  Progress bar + elapsed / remaining (11 px)      |
|  Play state, volume, battery, shuffle/repeat     |
+--------------------------------------------------+
```

- Title first, then artist/album; do not invert the hierarchy.
- Truncate with an ellipsis at a word boundary where possible; never shrink the
  font to fit.
- Progress uses the playback clock, not the UI tick.
- Album art is optional and bounded; a missing image shows a neutral
  placeholder, not a broken icon.
- No visualizer by default. A visualizer costs CPU and battery and is not part
  of the first-party identity; it can be a skin feature later.
- Volume changes show a transient overlay, not a permanent row.

### 9.9 Error and recovery screens

An error screen has four parts:

1. **What happened** — plain language, no raw code alone.
2. **Why** — one sentence, specific (`JOURNAL_DIRTY`, `NOT_HFS`, `IO`).
3. **What to do** — a next step: retry, connect to macOS, use DFU, power off.
4. **What not to do** — "do not format", "do not delete the journal" where it
   matters.

Rules:

- No dead end. Every screen has a safe exit.
- No "repair" button that formats, erases or deletes the journal.
- The error code is visible but secondary; the explanation is primary.
- The same error has the same wording across the loader and the firmware where
  possible.
- A read-only mount is explained as a protection, not a failure of the device.
- A panic screen shows a short code and a power-off instruction; it does not
  dump a register file at the user.

Appendix F maps the canonical errors to screens.

### 9.10 Boot splash continuity

The loader splash is the owner's green/red artwork with `VERSION ALPHA`. The
firmware's first frame should continue it, not fight it:

- same off-white background (`#EFEDE5`);
- same wordmark and type scale where text appears;
- the HFS+ label is already green/red from the loader; the firmware does not
  repaint a contradictory state;
- the first firmware frame may show a thin progress row or a stage code, but not
  a second logo;
- the transition from splash to UI should be a content change, not a flash.

The final splash is implemented and device-checked; the firmware first frame is
not. This section is a design intent, not a claim.

### 9.11 Settings and system information

Settings are ordinary native files under `/.helikon/`, written through the
transaction engine. The UI reads them through the VFS, not from a hidden blob.

First settings:

- playback: shuffle, repeat, volume limit, gapless;
- display: brightness, backlight timeout, large text, high contrast;
- system: clock, battery display, storage info, firmware version, loader
  version, image hash (short);
- about: licences, build date, board ID, HFS+ status.

Rules:

- A setting change writes only when the user confirms or the value is stable;
  no write per wheel detent.
- A failed write keeps the old value and tells the user.
- System info shows the verified image version and bootstate state; it does not
  invent a "secure boot" badge.
- No developer menu in a release build unless it is explicitly enabled.

### 9.12 Accessibility

- Large-text mode: a second type scale, not a pinch-zoom.
- High-contrast mode: stronger foreground/background, not colour inversion that
  breaks the splash.
- 11 px is the minimum; a larger default may be needed on the 6G panel.
- All state is conveyed by text or shape as well as colour.
- The click wheel is the primary input; no feature requires a multi-touch
  gesture or a precise pointer.
- A screen reader is not planned for v1; do not claim one.

### 9.13 Empty, loading and error states

Every list and screen defines:

- **empty** — "No tracks", "No albums", "No playlists", with a next step;
- **loading** — a bounded spinner or progress row after a threshold;
- **error** — the four-part screen above;
- **partial** — "Some files could not be read" with a count and a way to see
  them;
- **unsupported** — the file is listed with a reason, not hidden.

A blank screen is a bug.

### 9.14 Theming boundary

The first-party UI is the default and must survive a failed skin. Skins are a
documented Rockbox WPS subset later (D-10/D-15), with:

- a parsed-node cap;
- an image-cache cap;
- a frame-work cap;
- a fallback to the default theme on any parse/render failure;
- a visible "skin unsupported" report, not a silent blank.

The firmware does not ship a full WPS engine and does not bundle proprietary
themes or artwork. The first-party UI is not a skin; it is the reference.

### 9.15 Aesthetic anti-goals

- No gradients, drop shadows, bevels or glass effects.
- No animated backgrounds or idle motion.
- No font smaller than 11 px.
- No colour-only status.
- No icon-only navigation without a label where meaning matters.
- No "modern" hamburger menus or nested settings mazes.
- No fake album art or placeholder that looks like a broken image.
- No splash that promises a feature the firmware does not have.
- No theme that can make the first-party UI unusable.

### 9.16 Open design questions

- Exact ink colour and selected-row treatment.
- Whether the first-party theme is light-only or has a dark variant.
- Row pitch and title height on the real panel.
- Icon set and stroke weight.
- Motion durations and whether transitions are worth the frame cost.
- Large-text scale and whether 11 px is readable outdoors.
- Album-art size and cache budget.
- Whether the battery indicator shows a percentage or only a glyph.
- How the loader splash hands off to the first firmware frame.

---

## 10. Security and trust boundaries

### 10.1 The trust chain

```text
Apple NOR boot ROM
  -> loader (verified by the Apple/DFU path, read-only)
     -> Ed25519 signature over the manifest
     -> SHA-256 over the image
     -> boot-info (cache of verified facts)
        -> firmware (trusts the loader, not boot-info)
           -> HFS+ transaction engine
           -> bootstate file (the durable record)
```

The chain has one root: the loader's signature verification. The firmware does
not re-verify the image; it cannot, because it does not carry the public key and
the image is already executing. It also does not need to. What it must not do is
treat a RAM structure or a file it has not validated as an authority.

### 10.2 Trust boundaries

| Boundary | Trusted side | Untrusted side | Rule |
|---|---|---|---|
| Loader → firmware | Loader | Boot-info page | Validate magic/version/size/CRC/board/range; boot-info is a cache |
| Firmware → volume | HFS+ core | On-disk bytes | Every header/node/name is bounds-checked |
| VFS → UI | VFS | UI | UI cannot request a raw block write |
| Firmware → apps (later) | Kernel | App package | Capabilities, quotas, signed packages (D-08) |
| Firmware → USB host | Firmware | Host | Read-only LUN; no host write |
| Firmware → update | Loader | Staged files | Manifest + hash + signature before install |
| Diagnostics → user | Firmware | Log export | No secrets, no personal data, bounded ring |

### 10.3 Bootstate is the durable record

The loader reads bootstate; the firmware writes it. The two do not share a
mutable structure. The firmware's write is a normal transaction:

- re-read the file;
- validate both slots and checksums;
- choose the slot that matches the running image;
- stage a higher sequence with `GOOD` only after self-test;
- commit, flush, read back, verify.

A bootstate that does not match the manifest is a fail-closed condition: no
jump, recovery screen. There is no A/B rollback; the owner accepted single-image
fail-closed (T-050). The firmware must not invent a second image path or a
"previous good" slot that the loader does not implement.

### 10.4 No raw hardware above the HAL

The UI, player, VFS and HFS+ core never touch ATA, PMU, LCD, GPIO or codec
registers. This is a security rule as well as an architecture rule: a malicious
skin or a buggy app cannot toggle a power rail, brick the panel or write a raw
sector. The HAL is the only code with register access, and it is not reachable
from a skin or an app.

### 10.5 USB

The future USB path is read-only by default. A writable mass-storage LUN would
let a host bypass the transaction engine and corrupt the volume; it is rejected.
A service mode, if added, is a framed protocol with its own trust review and no
raw block access.

### 10.6 Update staging

No unsigned image, no silent install, no update from a host without an explicit
user action. The loader verifies before the jump. The firmware's only update
role in the first phases is bootstate promotion. The update transport (D-16) and
the release-key certificate (T-053) are still open.

### 10.7 Logs, privacy and secrets

- No secrets on the device: no private keys, no tokens, no Wi-Fi credentials.
- No personal data in logs by default: no full paths if a basename is enough, no
  serial numbers, no library names in an exported log unless the user opts in.
- The log ring is bounded and cleared on a documented action.
- A crash record contains registers and a stage code, not file contents.
- The device has no network; scrobbling and podcast fetching are host-side and
  opt-in.

### 10.8 Threat model

| Threat | Impact | Mitigation | Residual risk |
|---|---|---|---|
| Corrupt/malicious firmware image | Code execution | Ed25519 + SHA-256 before jump | Loader/key compromise |
| Malicious HFS+ image | Parser crash, OOB | Bounds checks, fuzzing, read-only fallback | Unknown parser bug |
| Dirty journal after power loss | Corruption | Replay before write, read-only otherwise | Replay bug |
| Bad USB host | Volume corruption | Read-only LUN | None for writes |
| Malicious skin (later) | UI crash, memory exhaustion | Caps, parser isolation, fallback | Renderer bug |
| Malicious app (later) | Data theft, hardware abuse | Sandbox, capabilities, quotas | Sandbox escape |
| Stolen device | Library disclosure | No encryption planned; do not claim | Physical access |
| Lost signing key | No new firmware | Offline root, two tokens, rotation | DFU/Apple restore |

The last row is honest: Helikon-OS does not plan full-disk encryption in v1. Do
not claim it.

---

## 11. Performance and power

### 11.1 Performance philosophy

Performance is a budget, not a benchmark. Every subsystem has a target, a
counter and a measurement method. A number without a device, image hash and run
count is not a result.

The first firmware is not optimized. It is instrumented. The order is:
correctness, observability, then speed.

### 11.2 Boot time

Two numbers:

- **Loader time:** owner-reported 6.5 s on one 6G/iFlash SD. T-052 tracks a
  later reduction to 2–3 s. Not a firmware budget.
- **Firmware time:** from the first firmware instruction to the first
  interactive frame. Not measured. The proposed target is under 1 s to the
  first interactive frame, with the breakdown in §7.10.

The firmware must record its own stage timestamps in the boot-info/trace ring so
a slow boot can be attributed without a stopwatch.

### 11.3 UI frame budget

The UI must not accumulate input backlog. A wheel detent should produce a
visible selection change on the next frame. The exact frame time depends on the
panel refresh and the renderer; both are open questions.

Rules:

- Redraw the dirty region, not the whole screen, for a selection change.
- Cache the font atlas and static chrome.
- Decode artwork off the render path or from a cache; never block on a file read
  in the middle of a frame.
- Cap the widgets per screen and the image cache.
- Measure frame time on the device, not only on the host.

A 30 fps interactive target is a reasonable starting point for a 320×240 panel,
but it is a target, not a measured panel fact.

### 11.4 Audio

The audio path is the hardest real-time budget:

- the DMA buffer must survive the worst-case storage latency;
- decode must keep ahead of playback;
- a seek must refill without a click or a long gap;
- an underrun is a counter and a pause, not a crash.

The buffer size is a trade between RAM and skip resistance. The architecture
budget is 16–24 MiB; F5 starts at the low end and measures. No gapless or
"audiophile" claim before the measurement.

### 11.5 Storage

- Sequential read is the playback path; random read is the browse path.
- ATA PIO/DMA mode and 512/4096-byte handling are BSP facts, not assumptions.
- The block layer counts operations, bytes, retries and errors.
- A flush is expensive; batch transactions, but never skip a required flush.
- The write engine's dirty set is bounded; a large operation may need multiple
  transactions, and the UI must show progress.

### 11.6 Power

Power is a product feature, not a late optimization. The first firmware
implements a conservative state machine and measures:

- active current with display on and audio playing;
- idle current with display off and audio stopped;
- off current;
- wake latency from each source.

D-12 is open: there is no battery target or baseline yet. Do not publish a
battery gain. The donor's owner-reported gain is not Helikon-OS evidence.

### 11.7 Measurement method

Every performance/power claim records:

- device model, adapter/card, battery state;
- image hash and build commit;
- test file/media and volume;
- warm/cold state;
- run count and range (not a single best run);
- the counter/tool used.

A host benchmark is labeled host-only. A device number without this metadata is
`unproven`.

---

## 12. Testing and validation

### 12.1 The firmware test pyramid

```text
                 device runs (few, expensive)
              perceptual/panel/audio checks
           power-cut and crash injection
        host integration with fake HAL
     host unit + model + property tests
        static analysis + fuzzing
```

The pyramid is inverted from the usual web analogy: device runs are scarce, so
the lower layers must be strong. The existing HFS+ suite (104 tests) and
bootloader suite (31 tests) are the model.

### 12.2 Host unit tests

- HFS+ read/write, journal, transaction, bitmap, B-tree, Unicode: existing.
- Boot-info parser: magic/version/size/CRC/board/range.
- VFS: path rules, handles, capabilities, traversal rejection.
- Player: state machine, buffer accounting, seek/underrun.
- UI: layout, focus, truncation, fallback font.
- Diagnostics: ring wrap, panic path, counter overflow.

The host build uses the same `firmware/lib/` and core modules; it does not
reimplement them.

### 12.3 Model and property tests

- B-tree insert/delete at depth 3 with 60 records (existing).
- Catalog index descent with shared leaves (existing).
- Bitmap allocation/free invariants.
- Transaction dirty-set bounds and `NO_SPACE`.
- Bootstate sequence/checksum selection.
- Ring buffer wrap and overwrite.

A model test compares the implementation against a simple reference model, not
against itself.

### 12.4 Fuzzing

Targets, in order:

1. HFS+ catalog/B-tree/extents parsers (existing mutation fuzz; expand).
2. Manifest and bootstate parsers.
3. Boot-info parser.
4. VFS path/name handling.
5. Skin parser (later).
6. Database index (later).

Fuzzing runs on the host with ASan/UBSan. A crash is a bug; a hang is a bug; a
"correct" result on a malformed image is only correct if the error code is the
documented one.

### 12.5 Host harness

The harness described in §8.11 is a test layer:

- deterministic clock and input;
- fake block device with recorded errors;
- frame capture for UI regression;
- trace assertions (e.g., "bootstate is written once, after a clean mount").

It is not a device and cannot validate panel, audio or power.

### 12.6 Device bring-up ladder

Each rung is a recorded device run with a screen photo or a trace:

1. Banner on screen, power-off.
2. ATA identify, "Storage OK".
3. Partition scan, "Volume found".
4. HFS+ probe, "HFS+ OK".
5. Read one known file, "Read OK".
6. Input events, "Input OK".
7. Bootstate write, loader sees `GOOD`.
8. Browser list and selection.
9. First UI screen with real typography.
10. First audio playback.
11. Seek, pause, resume, track change.
12. Sleep/wake, 100 cycles (later).

A rung is not skipped because the host test passes.

### 12.7 Crash and power-cut tests

The writer already has 280 host cut points. The firmware adds:

- power cut during bootstate promotion (F2);
- power cut during a settings write (later);
- power cut during audio buffer refill (no metadata write expected);
- dirty journal on boot (read-only, no replay by the read path);
- replay by the write engine, then `fsck_hfs` zero repairs.

Every cut records the image hash, operation, cut point, journal state,
`fsck_hfs` output and the recovered tree.

### 12.8 UI and audio perceptual tests

- Panel photos at normal and high brightness; check clipping, contrast and
  colour.
- 11 px readability at arm's length.
- Selected row visible in monochrome and in bright light.
- No flicker on selection or transition.
- Audio: no clicks at start/stop/seek; channel balance; volume steps; line-out
  and headphone.
- A "golden frame" set for the host harness, not a substitute for the panel.

### 12.9 Evidence format

A firmware device result records:

```text
task: T-054 / F1
image: <sha256>, <bytes>, <commit>
device: 6G, adapter/card, battery state
boot: loader <sha>, bootstate <state>
stages: 0..8 pass/fail, screen photo
fsck: command, exit code, output
volume hash before/after (if no writes expected)
notes: what was not tested
```

### 12.10 Release gates

A firmware release is not in scope yet. When it is, the gates are the design
paper's scorecard: signed manifest, device matrix, `fsck_hfs` zero repairs, no
unexecuted stress gate, no hidden limitation. This paper does not lower them.

---

## 13. Phased implementation F1–F5

### 13.1 Phase table

| Phase | Task | Deliverable | Exit criteria | Depends on |
|---|---|---|---|---|
| F1 | T-054 | Boot skeleton + signed image | Banner, ATA read, HFS+ probe, read one file, PASS, power-off; no writes; `fsck_hfs` clean | ADR-0003/0004 accepted (or fallback), loader test image |
| F2 | T-055 | First firmware write | Bootstate `TRYING`→`GOOD` through `hfs_txn`, flush, readback, loader sees `GOOD`; `fsck_hfs` clean | F1, clean journal, D-02 |
| F3 | T-056 | Read-only VFS + browser | Browse a real library, open a track, list unsupported files with reasons, no writes except bootstate | F1, F2 optional |
| F4 | T-057 | UI renderer + screens | Typography, layout, now playing, settings, error screens; panel check | F3, T-042, `ui.md` |
| F5 | T-058 | Audio playback | Play, pause, seek, next/previous, volume, line-out/headphone; underrun handling | F3, audio HAL |

### 13.2 F1 — the read-only skeleton

**Goal:** prove the chain on the device with the smallest possible firmware.

Scope:

- `firmware/boot/`: `_start`, linker script, vectors, boot-info parser, panic.
- `firmware/hal/6g/`: clock, PMU, backlight, LCD, ATA read, buttons (minimal).
- `firmware/block/`: read-only block device, partition scan.
- `firmware/hfs/`: existing read core, `hfs_read_fn` adapter.
- `firmware/diag/`: stage codes, trace ring.
- A signed test image with a development flag.

Out of scope: writes, VFS, player, UI beyond a text/stage screen, USB, database.

Exit evidence:

- one 6G run: banner with image version, `Storage OK`, `Volume found`,
  `HFS+ OK`, `Read OK`, `PASS`, power-off;
- screen photo and trace ring;
- `fsck_hfs -fn` clean (no writes expected);
- image hash, size, headroom and map report;
- boot time per stage.

**Must not:** write the filesystem, replay the journal, load an unsigned image,
claim a firmware product.

### 13.3 F2 — the first write

**Goal:** prove the firmware can write one native file through the transaction
engine, safely.

Scope:

- mount read-write only after a clean journal;
- read bootstate through the VFS;
- validate both slots;
- promote `TRYING` to `GOOD` after self-test;
- commit/flush/readback/verify;
- show the result and power off.

Out of scope: any other file, settings, library writes, update install.

Exit evidence:

- bootstate readback matches the expected slot and checksum;
- next loader boot sees `GOOD`;
- `fsck_hfs -fn` clean;
- a power cut during the promotion recovers to a consistent state (host first,
  device when the rig exists);
- no other file changed (volume diff).

**Must not:** trust boot-info as authority, write before a clean mount, retry a
failed promotion with a different slot, touch the journal directly.

### 13.4 F3 — read-only VFS and browser

**Goal:** make the library visible and navigable.

Scope:

- VFS lookup/iterate/open/read/seek/close;
- handles with generation counters;
- a simple list screen: artists/albums/tracks or a folder browser first;
- unsupported objects listed with reasons;
- error screens for `NOT_HFS`, `JOURNAL_DIRTY`, `CORRUPT`, `IO`.

Out of scope: database, metadata indexing, skins, playback.

Exit evidence:

- browse a real library on the 6G;
- open a file and read its first bytes through the VFS;
- a malformed/unsupported fixture shows the documented error;
- no writes except bootstate;
- UI stays responsive on a large directory (measured).

**Must not:** scan the whole library into RAM, hide unsupported files, or write
metadata to "fix" a name.

### 13.5 F4 — the first-party UI

**Goal:** make the product feel like Helikon-OS.

Scope:

- renderer: RGB565, font atlas, grayscale AA, 1-bit fallback;
- typography from `ui.md`;
- layout grid, list, now playing, settings, system info, error screens;
- focus/selection, click-wheel acceleration, hold;
- screen transitions and progress feedback;
- large-text/high-contrast hooks.

Out of scope: skins, apps, podcast/car screens, animated art.

Exit evidence:

- host frames for every screen;
- panel photos at normal/high brightness;
- no clipping at 11 px;
- selection visible in monochrome;
- frame time measured on the device;
- a failed font/artwork load falls back and reports.

**Must not:** block the render path on storage, rasterize fonts per frame, or
let a skin make the default UI unusable.

### 13.6 F5 — audio

**Goal:** play music from the native library.

Scope:

- one codec (the first supported format, chosen by the audio lead);
- buffer/read-ahead, decode, I2S/DMA output;
- play, pause, seek, next/previous, volume;
- line-out and headphone;
- underrun/error handling;
- playback clock and now-playing updates.

Out of scope: gapless across formats, ReplayGain, EQ, visualizer, Bluetooth.

Exit evidence:

- plays a known file on the 6G;
- seek and track change without a click or a hang;
- line-out and headphone levels sane;
- battery/current measured for the run;
- no metadata writes during playback;
- `fsck_hfs -fn` clean.

**Must not:** claim audiophile quality, gapless or battery gain without
measurement.

### 13.7 Ordering rules

- F1 before F2. A write path on an unproven block layer is a data-loss bug
  waiting for a device.
- F2 before F3's writes. The browser is read-only until bootstate promotion
  works.
- F3 before F4. The UI needs real data and real error states.
- F3 before F5. The player needs the VFS.
- F4 and F5 may overlap only if they do not share the same device testing time;
  the writer/player budgets come first (roadmap goal sequencing rule).
- No phase starts with "we will fix it in the next phase" for a safety
  invariant.

### 13.8 What each phase must not do

| Phase | Must not |
|---|---|
| F1 | Write the filesystem, replay the journal, load unsigned code |
| F2 | Write anything but bootstate, trust boot-info, skip the clean-journal gate |
| F3 | Build a database, hide unsupported files, write metadata |
| F4 | Let a skin/theme break the default UI, block on I/O in render |
| F5 | Write metadata during playback, claim unmeasured audio/battery results |

---

## 14. Risks and mitigations

### 14.1 Technical risks

| Risk | Likelihood | Impact | Mitigation | Early signal |
|---|---|---|---|---|
| Boot environment differs from the host assumption (MMU/cache/stack) | High | F1 hangs or faults | Print state, start with caches/MMU conservative, measured ladder | Banner missing or immediate fault |
| ATA init/DMA unreliable | Medium | No storage, corrupt reads | Donor sequences, bounce buffers, timeouts, read-only first | `Storage OK` missing |
| Memory map overlaps framebuffer/loader | Medium | Corruption | ADR-0003, assertions, boot-time map print | Guard hit or garbage frame |
| Audio codec undocumented | Medium | F5 slips | Donor `audio-6g.c`/`cscodec-6g.c`, one codec, measure | No sound or wrong routing |
| Panel timing/colour wrong | Medium | UI unreadable | Donor LCD init, panel photos, contrast test | Clipping/flicker |
| Write path bug corrupts library | Low but severe | Data loss | F2 writes only bootstate, txn, backup, fsck | fsck repair |
| Boot-info ABI drift | Medium | Firmware misreads state | Version/size/CRC, host test, additive only | `BOOT_INFO_INVALID` |
| Toolchain aging | Medium | Build breaks | Pin version, record flags, avoid new language features | CI/host build failure |
| Single-device evidence mistaken for support | High | False claim | Device matrix, one-device wording | Release note overreach |

### 14.2 Process risks

- **Scope creep:** the paper's non-goals list is the defense. A USB or skin task
  before F1 is a schedule bug.
- **Docs drift:** `state.md`, `tasks.md` and `handoff.md` are updated every
  session; a design change updates this paper and the ADRs.
- **Hidden temporary code:** every workaround gets a task ID and a removal
  condition.
- **Unmeasured claims:** a number without method/device/hash is `unproven`.
- **Owner dependency:** D-05, D-07, D-12, D-16, T-053 and the ADRs need owner
  decisions; the firmware can proceed on F1 without them, but not on F5/F2
  policy.

### 14.3 Aesthetic/product risks

- **Splash/UI discontinuity:** the loader's final splash is warm off-white with
  green/red; the firmware must continue it, not switch to a generic dark UI.
- **Too much chrome:** a 320×240 screen punishes decoration. Content first.
- **Tiny text:** 11 px is a floor, not a target; test on the panel.
- **Colour-only status:** inaccessible and fragile on a washed-out panel.
- **Skin-first identity:** the first-party UI must be the reference; skins are
  optional.
- **Fake claims:** no "studio quality", no "lossless" unless the codec path
  supports it and it is measured.

### 14.4 Unknowns that could invalidate the design

- The loader's MMU/cache state may force a different entry sequence.
- The 6G DRAM may not be usable as one contiguous 64 MiB region.
- The panel may not support the assumed framebuffer format/refresh.
- The codec may require a closed blob or an undocumented init sequence.
- The ATA controller may have DMA constraints that make the bounce-buffer
  design invalid.
- The device may not expose serial, making early debug slower.
- A 7G port may invalidate a 6G-specific BSP assumption; the core boundary is
  designed to absorb that, but it is unproven.

---

## 15. Open questions

These are not rhetorical. Each needs an owner, a method and a recorded answer.

**Boot and memory**

1. What are the exact MMU, cache and watchdog states at the loader jump?
2. Is the full 64 MiB DRAM contiguous and usable above `0x08800000`?
3. Where does the LCD controller expect its framebuffer, and what alignment?
4. Which DRAM region is safe for DMA?
5. What are the real stack high-water marks for boot, HFS+, UI and audio?
6. Should the boot-info page stay at `0x087ff000`, or move above the signed
   window after the loader mapping is confirmed?
7. Is a 4 KiB image cap acceptable, or should the loader validate a larger
   window?

**Runtime**

8. Is a cooperative loop sufficient for audio, or is a worker/preemptive context
   needed?
9. What is the worst-case ATA read latency on the target adapter?
10. Is there a usable RTC alarm or wake source?
11. Does the device need a watchdog, and who owns it across the jump?
12. How are 64-bit time values represented at the boundaries?

**Filesystem and writes**

13. What is the cache budget for B-tree nodes, names and file data (D-05)?
14. Does the first player need a database, or is a catalog browser enough (D-07)?
15. What is the exact bootstate promotion sequence for a `TRYING` slot?
16. How does the firmware detect "self-test passed" without a user action?
17. What is the battery/charger threshold for writes (D-12)?

**Product and UI**

18. What is the exact ink colour and selected-row treatment?
19. Is there a dark theme, and does it fit the splash identity?
20. What are the final row pitch, title height and margins on the panel?
21. Which icon set and stroke weight?
22. What motion durations are worth the frame cost?
23. Is 11 px readable outdoors, or is 12 px the real floor?
24. How does the loader splash hand off to the first firmware frame?
25. What does the battery indicator show: glyph, percentage, or both?

**Audio**

26. Which codec/container is first?
27. What buffer size survives storage sleep and seek?
28. Is gapless a v1 requirement or a later goal?
29. What are the measured line-out and headphone levels?
30. Is the codec init sequence fully documented, or does it need donor code?

**Release and trust**

31. When is the release-key certificate verifier (T-053) required for firmware?
32. Does the firmware need to display the signing key ID/certificate state?
33. What is the update transport (D-16) and does it change the bootstate flow?

---

## 16. Recommendations

In order:

1. **Accept or reject ADR-0003 and ADR-0004.** The memory map and entry ABI are
   the two decisions that block a clean F1. If the owner rejects the boot-info
   page, use the no-args fallback and keep the full 8 MiB window.
2. **Build F1 before anything else.** A read-only skeleton with a signed image,
   banner, ATA read and HFS+ read is worth more than a player that cannot boot
   reliably.
3. **Keep F1 write-free.** The first write is F2's bootstate promotion, through
   the transaction engine. Do not add a "small" settings write earlier.
4. **Invest in the host harness early.** A fake block device, deterministic
   clock and frame capture will pay for themselves before F3.
5. **Freeze the error taxonomy and the boot-stage codes.** They are the shared
   language between the loader, the firmware, the UI and the docs.
6. **Do not start UI or audio before F1.** They are the fun parts; they are also
   the parts that hide a broken block layer.
7. **Measure before claiming.** Boot time, frame time, audio latency, battery
   current and volume hashes all need method, device and image hash.
8. **Keep the first-party UI quiet.** The product identity is legibility and
   speed, not decoration. The splash is the reference.
9. **Update the docs every session.** `state.md`, `tasks.md`, `handoff.md` and,
   when the design changes, this paper and the ADRs.
10. **Leave the library alone.** Until F2 is proven, the only write target is
    `/.helikon/bootstate` on a disposable or fully backed-up volume.

---

## 17. References

- [`design-paper.md`](design-paper.md) — product and system design source.
- [`architecture.md`](architecture.md) — boot chain, layers, memory, security.
- [`hfsplus-write-engine.md`](hfsplus-write-engine.md) — writer, journal, crash
  consistency.
- [`ui.md`](ui.md) — default typography and rendering rules.
- [`../bootloader/LOADER.md`](../bootloader/LOADER.md) — manifest, bootstate and
  loader flow.
- [`../bootloader/LOADER_TEST.md`](../bootloader/LOADER_TEST.md) — loader test
  image runbook.
- [`../bootloader/hardware/README.md`](../bootloader/hardware/README.md) — donor
  hardware integration.
- [`adr/0002-signing-key-ownership.md`](adr/0002-signing-key-ownership.md) —
  signing policy.
- [`adr/0003-firmware-memory-map.md`](adr/0003-firmware-memory-map.md) — memory
  map proposal.
- [`adr/0004-firmware-entry-abi.md`](adr/0004-firmware-entry-abi.md) — entry ABI
  proposal.
- [`invariants.md`](invariants.md) — safety invariants.
- [`failures.md`](failures.md) — error taxonomy and safe responses.
- [`testing.md`](testing.md) — test layers and commands.
- [`roadmap.md`](roadmap.md) — phases 0–12.
- [`tasks.md`](tasks.md) — current queue, including T-054–T-058.
- [`state.md`](state.md) — what is true today.

---

## Appendix A — Proposed boot-info layout

See ADR-0004 for the full table. The firmware-facing fields:

```text
magic HBI1 | version 1 | size 512 | loader version | board ID
image version | load address | image size | image SHA-256
bootstate sequence/state | journal state | flags | loader stage ms | CRC-32
manifest copy (256 bytes)
```

Rules: little-endian, fixed size, CRC over bytes 0–71, additive versioning,
boot-info is a cache not an authority, invalid page → diagnostic mode, no
bootstate write.

## Appendix B — Proposed memory-map constants

```c
#define HELIKON_IMAGE_BASE      0x08000000u
#define HELIKON_IMAGE_MAX       0x007ff000u  /* 8 MiB - 4 KiB */
#define HELIKON_BOOT_INFO_BASE  0x087ff000u
#define HELIKON_BOOT_INFO_SIZE  0x00001000u
#define HELIKON_KERNEL_BASE     0x08800000u
#define HELIKON_KERNEL_SIZE     0x00800000u
#define HELIKON_UI_BASE         0x09000000u
#define HELIKON_UI_SIZE         0x00800000u
#define HELIKON_AUDIO_BASE      0x09800000u
#define HELIKON_AUDIO_SIZE      0x01000000u
#define HELIKON_DB_BASE         0x0a800000u
#define HELIKON_DB_SIZE         0x01000000u
#define HELIKON_RESERVE_BASE    0x0b800000u
#define HELIKON_RESERVE_SIZE    0x00800000u
```

These are proposals for the 6G BSP, not measured facts. A different board changes
the header, not the core.

## Appendix C — Module ownership matrix

| Module | Owner | Inputs | Outputs | Review gate |
|---|---|---|---|---|
| `firmware/boot` | boot lead | Loader jump, boot-info | `firmware_main`, panic | Entry ABI test, fault test |
| `firmware/lib` | firmware lead | — | mem/string/CRC/pool/ring | Host unit tests, no hosted symbols |
| `firmware/hal/6g` | hardware lead | BSP constants | Block/display/input/audio ops | Register sequences, timeout tests |
| `firmware/board/6g` | hardware lead | Board facts | `struct board`, memory map | Board audit, map assertions |
| `firmware/block` | storage lead | HAL block ops | `block_device`, partition scan | Read/flush/error injection |
| `firmware/hfs` | HFS+ lead | Block read/write | Mount, catalog, extents, txn | Existing 104 tests + fuzz |
| `firmware/vfs` | VFS lead | HFS+ ops | Handles, paths, capabilities | Path/capability tests |
| `firmware/player` | audio lead | VFS, audio HAL | Playback state, PCM | State-machine tests, device audio |
| `firmware/ui` | UI lead | VFS/player, input | Frames, focus | Host frames, panel photos |
| `firmware/input` | UI/hardware | HAL input | Events, wheel curve | Debounce/acceleration tests |
| `firmware/diag` | diagnostics lead | All modules | Ring, counters, crash | Wrap/panic tests |
| `firmware/usb` | USB lead | Block/VFS | Read-only LUN | Host enumeration, no writes |
| `firmware/update` | release lead | VFS/loader | Staged update, bootstate | Signed manifest, rollback gap |

## Appendix D — Firmware coding rules

- SPDX header on every file.
- C99, no VLAs, no `alloca`, no `malloc`, no `printf` in core.
- Fixed-width types; explicit endian conversion.
- `const` inputs; explicit lengths; bounded strings.
- One public header per module; opaque structs.
- Errors are negative `int` codes; no `errno`.
- Every wait has a timeout; every flush error propagates.
- No raw registers outside the HAL.
- No target `#ifdef` in shared code.
- No recursion without a depth bound.
- No floating point until the FPU question is settled.
- Every pool/cache has a compile-time maximum and a high-water counter.
- Every workaround has a task ID and a removal condition.
- Every write goes through `hfs_txn`.
- A panic never writes the filesystem.

## Appendix E — Screen inventory

| Screen | Purpose | First phase |
|---|---|---|
| Boot/stage | Bring-up banner and stage codes | F1 |
| Home | Main menu: Music, Settings, Now Playing, System | F3 |
| Music | Artists/Albums/Songs or folder browser | F3 |
| Album | Track list | F3/F4 |
| Now playing | Title, artist/album, progress, state | F4 |
| Settings | Playback, display, system | F4 |
| System info | Firmware/loader version, image hash, storage, HFS+ state | F4 |
| Error/recovery | Four-part error screen | F1/F4 |
| Panic | Fault code and power-off | F1 |
| Read-only notice | Why writes are unavailable | F2/F4 |
| Update (later) | Staged update and explicit install | Later |
| USB (later) | Read-only LUN state | Later |

## Appendix F — Error-to-screen mapping

| Error | Screen title | Body | Action |
|---|---|---|---|
| `NOT_HFS` | "HFS+ only" | "This volume is not native HFS+ v4." | Power off; connect to macOS; do not format |
| `JOURNAL_DIRTY` | "Journal needs attention" | "The volume was not shut down cleanly." | Read-only; let the write engine replay or use macOS |
| `CORRUPT` | "Catalog damaged" | "The filesystem structure did not validate." | Read-only; export log; macOS repair |
| `NO_SPACE` | "Not enough space" | "The operation needs more room than the transaction allows." | Cancel; free space; retry |
| `READ_ONLY` | "Read-only mode" | "Writes are disabled to protect the volume." | Keep playing; explain why |
| `UNSUPPORTED` | "Unsupported file" | "This file uses a feature Helikon-OS does not read." | Skip; list it; do not hide |
| `NAME_INVALID` | "Name not allowed" | "The name is too long or uses a reserved character." | Edit the name |
| `DB_STALE` | "Library index is stale" | "The database does not match the catalog." | Rescan; fall back to catalog |
| `IO` | "Storage error" | "The storage device did not respond correctly." | Retry once; power off; check adapter |
| `BOOT_INFO_INVALID` | "Boot handoff invalid" | "The loader information did not validate." | Diagnostic mode; no bootstate write |
| `PANIC` | "Firmware stopped" | "A fatal internal error occurred." | Record code; power off |

## Appendix G — First-week onboarding

1. Read `agents.md`, `state.md`, `tasks.md`, this paper.
2. Run `make -C tests/hfsplus test` and `make -C tests/bootloader test`.
3. Build the firmware image; read the map/size report.
4. Run the host harness boot; capture a frame.
5. Read a file through the fake block device.
6. Change a UI label; see the host frame change.
7. Flash F1; photograph the banner and stage codes.
8. Read the trace ring and the panic screen.
9. Pick a module from Appendix C and review its tests.

## Appendix H — Glossary delta

| Term | Meaning |
|---|---|
| **Boot-info** | Versioned page passed by the loader in `r0`; a cache of verified facts, not a trust anchor. |
| **Entry ABI** | The register/CPU-state contract at the jump from loader to firmware. |
| **Firmware skeleton** | The read-only F1 image: boot, HAL, block read, HFS+ read, screen, power-off. |
| **Freestanding** | A build with no hosted C library or OS; only the firmware's own `lib/`. |
| **Fixed pool** | A statically sized object pool with a compile-time maximum; no general heap. |
| **Arena** | A bump allocator over a reserved region; no free until a lifecycle reset. |
| **Cooperative loop** | A main loop that runs tasks to completion; no preemption between them. |
| **BSP** | Board support package: constants and init for one target. |
| **Framebuffer** | The RGB565 memory the LCD controller scans out. |
| **Panic** | A controlled fatal stop: record, paint, power off; never write the filesystem. |
| **Watchdog** | A hardware timer that resets the device if the firmware stops feeding it; policy open. |

## Appendix I — Design-review checklist

- [ ] Does the change respect the layer dependency rule?
- [ ] Does it add a write outside `hfs_txn`? (Must be no.)
- [ ] Does it add a cache/pool without a maximum?
- [ ] Does it add a blocking call to the render path?
- [ ] Does it touch raw registers outside the HAL?
- [ ] Does it add a target `#ifdef` to shared code?
- [ ] Does it trust boot-info for a write decision? (Must be no.)
- [ ] Does it handle a dirty journal by refusing writes?
- [ ] Does it have a host test, and a device rung if it touches hardware?
- [ ] Does it update `state.md`, `tasks.md`, `handoff.md` and this paper?
- [ ] Is every new number labeled target or measured?
- [ ] Is the UI still usable if the new asset/skin fails?
- [ ] Is the error screen four-part and free of a dead end?

---

*End of paper. This is a design document. It is not evidence that any firmware
exists, boots, plays, writes or supports a device.*
