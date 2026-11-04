# ADR 0003 — Firmware memory map and image layout

> Purpose: freeze where the verified firmware image, boot information, stacks,
> framebuffer, audio buffers and database cache live in the 6G's 64 MiB DRAM.
> Status: accepted — owner decision recorded 2026-11-04
> Owner: project owner; architecture lead
> Last updated: 2026-11-04
> Date: 2026-11-04
> Paper section: §19.2, §27; `docs/firmware-architecture.md` §3–§4
> Related decisions: D-01, D-02, D-05, D-12, D-28, T-051, T-054, T-059

## Context

The bootloader validates a firmware image inside an **8 MiB window**:
`0x08000000`–`0x08800000`. The manifest records a load address in
`0x08000000`–`0x087fffff` and an image size of 1–8 MiB; the loader requires
`load address + image size` to stay inside that window. The 6G has **64 MiB** of
DRAM. These are two different budgets and the repository has never written down
how the firmware uses the memory above the signed window.

The first firmware milestone (F1/T-054) needs a linker script, a stack, a
framebuffer and an ATA/DMA buffer. The later phases need 16–24 MiB for audio,
4–8 MiB for database pages and 3–5 MiB for UI, per the design targets in
`docs/architecture.md` §6. Without a map, every module invents its own address
and the first cache/DMA bug becomes a memory-corruption bug that looks like an
HFS+ bug.

The bootloader is not affected by this map. Its `MOVE_AREA` budget is a separate
constraint and remains defined by the donor bootloader.

## Constraints

- The loader copies the image to the **signed load address**; the image is
  linked at that address and is not relocatable.
- The loader verifies the image before the first firmware instruction executes.
- The firmware must not assume that the loader zeroed `.bss`, set up a stack, or
  left the MMU/caches in a documented state. The entry sequence must establish
  its own state.
- The firmware must be able to run with no boot-info (defensive fallback), even
  if the production ABI always supplies it.
- No module may allocate outside its budget. A transaction or cache that cannot
  fit fails with `NO_SPACE` before changing anything (I-15).
- The map must be expressible in a linker script, a C header and the build's
  static assertions. It must not depend on runtime discovery for the first
  target.
- A later 7G port may change the constants in its BSP, but the shared core must
  not contain target `#ifdef`s.

## Options

| Option | Pros | Cons | Safety/data risk |
|---|---|---|---|
| A. Keep everything inside the 8 MiB signed window | Simplest; one validated range; no loader change | Audio (16–24 MiB) and database (4–8 MiB) do not fit; forces early paging or a smaller product | Low now, high later: a forced redesign after audio/UI exist |
| B. Fixed 64 MiB map with a reserved boot-info page and fixed pools | Simple linker; predictable DMA/cache behaviour; easy to assert budgets; no allocator | Wastes memory if a module is undersized; a wrong constant is a hard fault, not a graceful failure | Moderate; mitigated by compile-time assertions and a reserve region |
| C. Runtime-discovered memory with a relocatable heap | Uses the whole device; one image for several RAM sizes | Requires relocation, a heap, and a boot-time memory probe; harder to debug; cache/DMA aliasing risk | High for the first firmware; not justified by one known target |
| D. Defer the map until audio/UI exist | No decision now | F1 cannot write a linker script or a boot-info page; modules invent addresses | High: rework and hidden corruption |

Option A is rejected for the phase-1 player because the documented audio budget
alone exceeds the window. Option C is rejected for F1: it adds a relocator and a
general allocator before there is a single working driver. Option D is rejected
because it blocks the first firmware milestone.

## Decision

**Option B — a fixed map, with the boot-info page reserved at the top of the
signed window.** Accepted 2026-11-04.

The map is a set of constants in the 6G BSP, mirrored in a linker script and a
`static_assert` header:

| Range | Size | Use | Notes |
|---|---:|---|---|
| `0x08000000`–`0x087fefff` | 8 MiB − 4 KiB | Verified firmware image | `.text`, `.rodata`, `.data`; linker fails above the cap |
| `0x087ff000`–`0x087fffff` | 4 KiB | Boot-info page | Written by the loader before the jump; read-only to the firmware after entry; accepted in ADR-0004 |
| `0x08800000`–`0x08ffffff` | 8 MiB | BSS, exception stacks, fixed pools, log ring | Starts immediately above the signed window; zeroed/initialized at boot |
| `0x09000000`–`0x097fffff` | 8 MiB | UI: framebuffer(s), font atlas, artwork cache | Framebuffer address and alignment are BSP-provided, not yet fixed |
| `0x09800000`–`0x0affffff` | 24 MiB | Audio buffers, codec work area, DMA descriptors | Upper end of the 16–24 MiB design target; measured in F5 |
| `0x0b000000`–`0x0b7fffff` | 8 MiB | Database mapped pages and index cache | Top of the 4–8 MiB design target; read-only mapping, not a full library copy; D-07 still open |
| `0x0b800000`–`0x0bffffff` | 8 MiB | Reserve: diagnostics, crash record, future growth | Not assigned to a feature without a new budget entry |

**Implementation note (2026-11-04).** The loader test image keeps its own BSS,
framebuffer and LCD DMA scratch at `0x08800000`–`0x08884200`. F2 reserves
`0x08800000`–`0x0888ffff` and starts the firmware BSS at `0x08890000`. The
kernel region remains `0x08800000`–`0x09000000`; this is a sub-allocation, not a
new region. F1's 229 KB BSS stopped below the active LCD DMA buffers; F2's
601 KB BSS reached them and blanked the display after the jump. The linker
asserts `__bss_start >= 0x08890000`. Do not move it back without first making
the loader quiesce its display controller before the jump.

Audio gets the **24 MiB** upper end of its 16–24 MiB design target and the
database gets **8 MiB**, the top of its 4–8 MiB target. Audio is the real-time
budget that is hardest to recover at runtime; the database is a read-only cache
with a catalog fallback. The two regions plus the reserve fill the 64 MiB
exactly. The reserve is for diagnostics and temporary overrun, not a second
database.

The image cap is `0x087ff000 − 0x08000000 = 8,384,512` bytes
(8 MiB − 4 KiB). The current manifest validator still accepts up to 8 MiB, so
the build must enforce the smaller cap now and a later loader revision should
reject an image that overlaps the boot-info page. Until that revision, an image
larger than the cap is a build error, not a runtime hope.

The boot-info page is inside the validated window because the loader is known to
map that range. Placing it above `0x08800000` would require confirming that the
loader's mapping covers the extra page; that is an unnecessary unknown for the
first firmware. The cost is 4 KiB of image space, which the F1 skeleton will not
approach.

## Consequences

- F1 can write a linker script, a startup file and a boot-info parser without
  waiting for a memory probe.
- Audio and database budgets are reserved before the features exist. If a
  measurement shows the split is wrong, the map changes before those features
  are written, not after.
- The build gains three hard checks: image size, pool sizes and section
  placement. A violation fails the build, not the device.
- The firmware cannot use the last 4 KiB of the signed window. The manifest
  image size and the boot-info page must be kept consistent in the loader,
  build script and documentation; otherwise a maximum-size image overwrites the
  page.
- A future 7G BSP may use different constants. The shared core only sees
  `struct board_memory` and the `HELIKON_*` constants from its BSP header; no
  target `#ifdef` is allowed in `firmware/boot`, `firmware/vfs` or
  `firmware/player`.
- The reserve region is not a license to grow caches silently. A module that
  needs reserve memory needs a budget update and a measurement.

## Follow-up

- Accepted 2026-11-04; D-27 records the decision.
- T-054 implements the linker script, `static_assert`s and the boot-info page
  reader. F1 must print the map constants and the image end address on the
  bring-up screen.
- Before F5, measure the audio buffer need on the device; do not publish the
  24 MiB split as a measurement.
- Before any max-size image is signed, update the loader to reject
  `load address + image size > 0x087ff000`.
- If a later measurement rejects the reserved page, fall back to the no-args
  entry ABI in ADR-0004 option A and keep the full 8 MiB window; that is a new
  decision, not a silent map change.

## References

- Design paper §19.2 (memory budget), §27 (assembly order).
- [`docs/architecture.md`](../architecture.md) §4, §6.
- [`docs/firmware-architecture.md`](../firmware-architecture.md) §3–§4.
- [`bootloader/LOADER.md`](../../bootloader/LOADER.md) §2.
- [`docs/invariants.md`](../invariants.md) I-14, I-15.
- [`docs/decisions.md`](../decisions.md) D-27, D-28.
- [`docs/tasks.md`](../tasks.md) T-054, T-059.
