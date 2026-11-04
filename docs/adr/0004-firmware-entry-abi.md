# ADR 0004 — Firmware entry ABI and boot information

> Purpose: freeze what the bootloader passes to the firmware, what CPU state the
> firmware may assume, and how the firmware handles a missing or invalid handoff.
> Status: proposed — awaiting owner acceptance
> Owner: project owner; boot lead; firmware lead
> Last updated: 2026-11-04
> Date: 2026-11-04
> Paper section: §5.3, §16; `docs/firmware-architecture.md` §3
> Related decisions: D-01, D-02, D-26, T-051, T-054, T-055

## Context

The loader test image jumps to the signed image with:

```c
uint32_t (*entry)(void) = (uint32_t (*)(void))load_address;
disable_irq();
commit_discard_idcache();
uint32_t marker = entry();
```

The test payload returns `HELO` so the loader can print `JUMP OK`. A production
firmware entry must never return. Beyond that, the handoff is undocumented: no
argument, no boot information, no statement of MMU state, stack state, register
state, watchdog state or display/storage state. The firmware therefore has to
re-read the manifest and bootstate through HFS+ before it knows what the loader
already verified. That duplicates work, obscures the trust boundary and makes
the first firmware screen slower than it needs to be.

The bootloader already owns the authoritative facts: it verified the manifest,
selected a bootstate slot, knows whether the journal was clean enough to mount,
and measured its own stages. Passing those facts across the jump is cheap. The
risk is that the firmware starts to treat a RAM structure as a trust anchor, or
that the ABI grows into an unstable in-memory interface.

## Constraints

- The bootloader is read-only. It may write DRAM, never the filesystem.
- The image is verified before the jump; the firmware cannot re-verify the
  Ed25519 signature without the public key, and it should not need to.
- The firmware must boot defensively if the handoff is absent, because a
  development image, an older loader or a future boot path may not supply it.
- The ABI must be versioned, fixed-size, little-endian and checksummed.
- The ABI must not become a second manifest. The on-disk manifest remains the
  source of truth; boot-info is a cached, non-authoritative copy of verified
  facts.
- No pointer in boot-info may point outside a range the firmware can validate.
- A change to the ABI must be detectable by both sides. Silent field reordering
  is forbidden.

## Options

| Option | Pros | Cons | Safety/data risk |
|---|---|---|---|
| A. No arguments; firmware re-reads the manifest and bootstate | No ABI; loader unchanged | Duplicate HFS+ work; firmware cannot know the loader's journal decision or timings; slower first screen; no place for loader diagnostics | Low data risk, high ambiguity: two independent interpretations of the same boot state |
| B. `r0` points to a fixed boot-info page in the reserved top of the signed window | Stable, versioned, checksummed; loader can pass verified facts and timings; no relocation | Requires a reserved page and a loader change; the struct becomes an interface to maintain | Moderate; mitigated by version/size/CRC checks and a defensive fallback |
| C. Fixed struct at an absolute address with no pointer | Same as B, one less register | The firmware must hard-code the address; harder to test on the host; no way to run without it | Moderate; less flexible than B |
| D. Pass the manifest pointer and let the firmware parse it | Reuses the existing 256-byte format | Manifest is not the whole handoff (bootstate, journal, timings); pointer lifetime/range unclear | Moderate; incomplete |

Option A is viable but leaves the firmware blind to the loader's decisions and
duplicates the most timing-sensitive I/O. Option C is unnecessarily rigid.
Option D is a subset of B and does not carry the bootstate/journal facts.

## Proposed decision

**Option B — a versioned boot-info page at `0x087ff000`, passed in `r0`.** This
is a proposal for owner acceptance, not an accepted decision.

### Register and CPU contract

| Item | Proposed contract |
|---|---|
| `r0` | Physical pointer to `struct helikon_boot_info` at `0x087ff000` |
| `r1` | `0`; reserved for a future secondary pointer |
| `r2`–`r12` | Undefined; firmware must not rely on them |
| `sp` | Undefined; firmware sets its own stacks before any C code |
| `lr` | Return address into the loader; production firmware must never use it |
| Mode | SVC mode, IRQ and FIQ disabled |
| MMU | As left by the loader; the exact state must be documented and measured before F1 |
| Caches | Cleaned/invalidated by the loader; firmware re-establishes its own policy |
| Watchdog | Undefined; firmware must disarm or re-arm it explicitly |
| Display | Whatever the loader last painted; firmware must repaint, not assume |
| Storage | The loader may have initialized ATA; firmware must not assume it and must re-init through the HAL |
| Return | Forbidden. A return is a firmware panic, not a loader recovery path |

The entry sequence must run with a valid stack, zero `.bss`, copy `.data`, set
up the exception vectors and only then call C. No C code may run before that.

### Boot-info layout

The page is 4 KiB; the first 512 bytes are defined and the rest is reserved
zero. All fields are little-endian.

| Offset | Size | Field | Rule |
|---:|---:|---|---|
| 0 | 4 | magic | `HBI1` (`0x31494248` little-endian) |
| 4 | 2 | version | `1` |
| 6 | 2 | size | `512` for this version |
| 8 | 4 | loader version | nonzero; loader build number |
| 12 | 4 | board ID | `6` = 6G; `7` = 7G |
| 16 | 4 | image version | copied from the verified manifest |
| 20 | 4 | load address | copied from the verified manifest |
| 24 | 4 | image size | copied from the verified manifest |
| 28 | 32 | image SHA-256 | copied from the verified manifest |
| 60 | 4 | bootstate sequence | `0` if no valid slot |
| 64 | 1 | bootstate state | `0` empty, `1` trying, `2` good, `3` failed |
| 65 | 1 | journal state | `0` unknown, `1` clean, `2` dirty, `3` absent |
| 66 | 2 | flags | bit 0 development image; bit 1 bootstate present; bit 2 manifest verified |
| 68 | 4 | loader stage time | milliseconds from loader start to jump; `0` if not measured |
| 72 | 4 | CRC-32 | over bytes 0–71 |
| 76 | 180 | reserved | zero |
| 256 | 256 | manifest copy | the exact 256-byte verified manifest |
| 512 | 3584 | reserved | zero |

`journal state` is the loader's observation, not a firmware permission. The
firmware must re-check the journal through the HFS+ core before its first write.
A `TRYING` bootstate is a hint to promote after self-test; the firmware must
re-read and validate the bootstate file through the transaction engine before
writing it. Boot-info is never sufficient authority for a write.

### Validation and fallback

The firmware validates, in order:

1. `r0` is inside the boot-info page and 4-byte aligned;
2. magic, version, size and CRC;
3. `board ID` matches the BSP;
4. `image size`, `load address` and `load address + image size` match the
   running image's raw load-size symbol (`__image_end - __image_start`) and the
   map in ADR-0003;
5. the manifest copy's magic, board and hash match the boot-info fields.

On failure the firmware shows `BOOT_INFO_INVALID`, continues in a read-only
diagnostic mode, and **does not write bootstate**. It may re-read the manifest
and bootstate through HFS+ to recover the facts, but it must not pretend the
handoff was valid. A production build may treat this as a hard boot failure if
the owner prefers; the first firmware keeps the diagnostic path so a bad ABI is
debuggable instead of a black screen.

If `r0` is zero (an older loader or a host test), the firmware takes the same
diagnostic path. This keeps the firmware runnable from a host harness and from
the existing loader test image.

### Why not pass more

- No filesystem handles, no open file descriptors, no block-device state: those
  are firmware-owned and must be re-established through the HAL.
- No framebuffer pointer: the display is firmware-owned; the loader's splash is
  a picture, not a display contract.
- No heap pointer: the map in ADR-0003 is fixed.
- No bootstate file path: the path is frozen in `bootloader/LOADER.md` and the
  firmware reads it through the VFS, not through a loader pointer.

## Consequences

- The loader gains a small, testable write to a reserved DRAM page and a
  versioned struct to fill. It remains read-only toward the filesystem.
- The firmware's first screen can show the verified image version and the
  loader's stage time without touching the disk. That helps F1 and T-052.
- The ABI needs its own host test: build a boot-info page in memory, run the
  firmware parser, and reject bad magic/version/size/CRC/board/range.
- The 4 KiB page interacts with the image cap in ADR-0003. A max-size image that
  overlaps the page is a build error; a later loader revision must reject it.
- The firmware still re-reads the bootstate before writing it. Boot-info is a
  cache, not a trust anchor; this is the main safety property of the design.
- A future loader revision can add fields only by bumping `version` and `size`.
  The firmware accepts a known range and ignores a newer trailing extension; it
  never guesses the meaning of an unknown field.

## Follow-up

- Owner acceptance or rejection of option B and of the boot-info page.
- Record the accepted ABI in `docs/decisions.md` as D-28.
- Loader change: fill the page after verification and before the jump; keep the
  no-args path for the test payload.
- T-054 implements the firmware parser and the host test.
- Update `bootloader/LOADER.md` with the entry ABI once accepted; the format
  document is the canonical on-disk/loader record.
- Add a bad-boot-info case to the firmware host tests and to the device
  bring-up ladder.

## References

- Design paper §5.3, §16.
- [`bootloader/LOADER.md`](../../bootloader/LOADER.md) §2–§4.
- [`bootloader/hardware/helikon_loader_hw.c`](../../bootloader/hardware/helikon_loader_hw.c).
- [`docs/adr/0003-firmware-memory-map.md`](0003-firmware-memory-map.md).
- [`docs/firmware-architecture.md`](../firmware-architecture.md) §3.
- [`docs/invariants.md`](../invariants.md) I-14, I-15.
- [`docs/tasks.md`](../tasks.md) T-054, T-055.
