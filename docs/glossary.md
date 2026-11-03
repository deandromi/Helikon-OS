# Glossary — plain-language terms

> Purpose: stable definitions so docs and agents use the same words.
> Status: living document
> Owner: docs lead
> Last updated: 2026-11-03
> Source: design paper Appendix F, plus project-specific terms

| Term | Plain-language meaning |
|---|---|
| **APM** | Apple Partition Map, an old partition-table format the scanner can read to find the HFS+ volume. |
| **App runtime** | The sandboxed environment that runs Helikon-OS apps and games. |
| **B-tree** | A balanced tree data structure. HFS+ uses one for the catalog and one for extents overflow. |
| **Block device** | A device that reads/writes fixed-size sectors or blocks. |
| **Bootloader** | The small program that runs first, finds/verifies the firmware and starts it. |
| **Boot state** | A small native file that tells the bootloader which image last booted and whether it passed its check. |
| **BSS** | Zero-initialised global memory in a firmware image. |
| **Car link / AAP** | The 30-pin accessory path; AAP is Apple's accessory protocol. Tier 2 is a subset, not a certification claim. |
| **Case folding** | Apple's rule for treating names that differ only by case as the same catalog key. |
| **Catalog** | The HFS+ database of files, folders and names. |
| **Catalog node** | One fixed-size block of the catalog B-tree. |
| **Checksum** | A value used to detect corruption. |
| **Checksum sidecar** | A separate index of file hashes, because HFS+ does not checksum file data. |
| **CNID** | Catalog node ID. HFS+'s permanent number for a file or folder; the root is 2. |
| **Commit** | The moment a transaction is durable and visible. |
| **Container (donor)** | The preallocated HFS+ file that held the donor overlay (`/.rockpod-rw`). Helikon-OS does not use it as a write path. |
| **Compaction (donor)** | Copying live donor overlay records into a fresh area. Donor migration concept only. |
| **Cross-compiler** | A compiler that runs on the host (e.g. macOS) but produces ARM code. |
| **DFU** | Device Firmware Upgrade. The USB recovery/install mode. Bootloader changes use DFU only. |
| **Dircache** | A legacy Rockbox directory cache. Helikon-OS does not use it. |
| **DRAM** | The iPod's main memory; 64 MiB on the 6G reference. |
| **Extent** | A contiguous run of disk blocks belonging to a file. |
| **Extents overflow** | Extra extent records for files with more than eight inline runs. |
| **FAT** | A simpler filesystem used by some iPods. Helikon-OS does not mount it; a FAT volume returns `NOT_HFS`. |
| **GPL-2.0-or-later** | The project license: GNU GPL version 2, or any later version. Matches Rockbox and the donor. |
| **Firmware** | The operating system and drivers that run the player. |
| **Fuzzing** | Feeding random/malformed input to find crashes and parser bugs. |
| **HAL** | Hardware abstraction layer. A clean interface over registers and chips. |
| **HFS+** | Apple's hierarchical file system used by the iPod. Helikon-OS targets HFS+ v4. |
| **HFSX** | A case-sensitive HFS+ variant; not supported by Helikon-OS. |
| **Host test** | A test that runs on the Mac/host, not the iPod. |
| **Journal** | A log of pending metadata changes that makes HFS+ writes crash-consistent. |
| **Journal replay** | Re-applying committed journal records after an interrupted write. |
| **Journaled volume** | An HFS+ volume with a journal. Helikon-OS must replay or refuse writes. |
| **LUN** | A USB mass-storage logical unit. Helikon-OS exposes it read-only. |
| **Manifest** | A small file describing a firmware image: magic, board, version, load address, size and hashes. |
| **MOVE_AREA** | The memory window the bootloader must fit into. |
| **Native metadata** | Apple's catalog, bitmap, extents tree, volume header and journal. |
| **Normalization** | Converting Unicode names to a canonical form. HFS+ stores decomposed names. |
| **Overlay (donor)** | The old `/.rockpod-rw` container. Helikon-OS uses it only as migration material. |
| **Podcast / RSS** | A feed of episodes fetched by the desktop app and synced to the device. The device has no network. |
| **Porting layer** | The per-device code (BSP/HAL hooks) that adapts the shared firmware to one board. |
| **Roonserver** | A rich music-browsing interface used as inspiration, not a dependency. |
| **Sandbox** | A restricted execution environment with explicit permissions and quotas. |
| **Scrobble** | Sending a played track to Last.fm. Done by the desktop app, opt-in, never by the device. |
| **Scrub** | A background read-and-verify pass over files to detect corruption. It detects/quarantines, not repairs. |
| **Skin / viewport** | A visual layout and the rectangular regions inside it. Helikon-OS plans a documented Rockbox WPS subset, not the full engine. |
| **Superblock (donor)** | The header that described the donor overlay's layout. Migration material only. |
| **Tagcache** | A legacy Rockbox metadata database. Helikon-OS replaces it with an immutable database. |
| **Toolchain** | Compiler, linker, assembler and related build tools. |
| **Transaction** | A group of metadata changes that becomes visible together or not at all. |
| **Unicode** | The character encoding behind HFS+ names; the writer needs Apple's case-folding and a normalization policy. |
| **VFS** | Virtual filesystem. The common file interface used by applications. |
| **WASM** | WebAssembly, a portable bytecode proposed as a sandboxed app format. The runtime choice is open. |
| **WPS** | Rockbox's text-based "While Playing Screen" skin format. Helikon-OS plans a documented subset. |
| **`fsck_hfs`** | macOS's HFS+ consistency checker. Helikon-OS treats it as the external oracle; zero repairs is a release gate. |
| **iFlash** | A family of ATA bridge adapters for SD/CF cards. They are not all equivalent; record exact models. |
| **`/.helikon/`** | Planned native directory for settings, boot state, logs, updates, app data and checksums. Written through the transaction engine. |
| **`/.rockpod-rw`** | Donor overlay container. Not part of Helikon-OS; never delete or reformat it during migration. |
| **`NOT_HFS`** | Error for a non-HFS+ volume; no FAT fallback. |
| **`JOURNAL_DIRTY`** | Error/status for a volume whose journal needs replay or repair; mount read-only. |
| **`fsck` / repair** | An external macOS action on a repaired/backed-up volume. Helikon-OS does not auto-repair. |

## Terms to avoid

- "Full Rockbox compatibility" — say "documented WPS subset".
- "Rockbox plugins work" — say "legacy plugin binaries are unsupported; new sandboxed apps are planned".
- "FAT support" — say "HFS+ only; `NOT_HFS` for FAT".
- "Self-healing" — say "detect, quarantine and report; restore from backup".
- "Working firmware" — say "design-stage".
- "Supported device" — say "recorded device run" or "planned port".
