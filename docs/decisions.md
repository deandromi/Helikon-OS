# Decisions — settled choices and open decisions

> Purpose: why the design is what it is, and what is still undecided.
> Status: living document
> Owner: project owner
> Last updated: 2026-11-04
> Source: design paper §18, §25, §28; Helikon-OS README

This file is the index and short-form record. Use `docs/adr/` only for a
decision too large to explain here. Do not invent an owner decision. Mark
unresolved items `proposed`; only the owner can mark them `accepted` or
`rejected`.

Decision status: `proposed`, `accepted`, `rejected`, `superseded`.

## Settled baseline

These are not open design choices; they come from the owner-authored README and
the paper's revision-2 decision. They are recorded here so an agent does not
re-litigate them.

| ID | Decision | Status | Source |
|---|---|---|---|
| B-01 | Helikon-OS is design-stage; the donor V3.2 overlay release is a separate product. | accepted | README, paper §1 scope note |
| B-02 | Native HFS+ is writable; no namespace overlay, no `/.rockpod-rw`, no FAT mount. | accepted | README goal 12, paper §9.1, §28.2.1 |
| B-03 | 6G is the reference target; 7G is the next named port after a board audit; other devices are ports. | accepted | README goal 24, paper §1, §28.4.5 |
| B-04 | Bootloader is read-only; `--single` is forbidden; no destructive repair. | accepted | paper §1, §5.3, §21 |
| B-05 | `fsck_hfs` zero repairs is a release gate. | accepted | paper §9.4, §24, §28.6 |

## Decision index

| ID | Title | Status | Owner | Paper |
|---|---|---|---|---|
| D-01 | Boot image verification | accepted | owner | §25.1, §5.3 |
| D-02 | Boot state and fallback | accepted | owner | §25.2, §5.3 |
| D-03 | Journal strategy | accepted | owner | §25.3, §9.3 |
| D-04 | Journal ownership | accepted | owner | §25.4, §9.3 |
| D-05 | B-tree cache and transaction limits | proposed | owner | §25.5, §19.2 |
| D-06 | Unicode policy | accepted | owner | §25.6, §10.4 |
| D-07 | Database location | proposed | owner | §25.7, §12.2 |
| D-08 | App/game runtime | proposed | owner | §25.8, §28.3.9–10 |
| D-09 | Desktop app transport | proposed | owner | §25.9, §28.4.2 |
| D-10 | Skin compatibility scope | proposed | owner | §25.10, §28.3.4 |
| D-11 | Anti-corruption scope | proposed | owner | §25.11, §28.2.2 |
| D-12 | Battery target | proposed | owner | §25.12, §28.3.1 |
| D-13 | 7G and port scope | proposed | owner | §25.13, §28.4.5 |
| D-14 | Car protocol | proposed | owner | §25.14, §28.3.7 |
| D-15 | Theme format | proposed | owner | §25.15, §14.2 |
| D-16 | Update transport | proposed | owner | §25.16, §16.2 |
| D-17 | Product identity | accepted | owner | §25.17, Appendix D |
| D-18 | Licensing | accepted | owner/legal | §18, §28.4.4 |
| D-19 | Code of conduct | rejected | owner | §18, §28.4.4 |
| D-20 | Signing-key ownership | proposed | owner | §25.1, §16, §28.2.5 |
| D-21 | First diagnostic bootloader scope | accepted | owner | §5, §24, §27 |
| D-22 | Separate write diagnostic scope | accepted | owner | §5, §24, §27; D-21 |
| D-23 | Default UI typography | accepted | owner | §14, §28.3.4; D-10, D-15 |
| D-24 | UI text/icon rendering | accepted | owner | §14, §28.3.3; D-23 |
| D-25 | Outer-volume write diagnostic scope | accepted | owner | §5, §24, §27; D-03, D-04, D-22 |

The paper's §25 lists 17 decisions. D-18, D-19 and D-20 are added from §18 and
§28.4.4. D-18 is accepted; D-19 was adopted and then rejected when the owner
deleted `CODE_OF_CONDUCT.md` on 2026-11-03; D-20 remains open. D-21 records the
first diagnostic bootloader scope. D-01/D-02 are accepted for the
firmware-loading bootloader. D-22 records the separate inner-image write
diagnostic; D-25 records the separate outer-volume diagnostic and its backup
gate. See C-08.

## D-01 — Boot image verification

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §25.1, §5.3; D-20.
- **Context:** the donor bootloader trusts a fixed-path image. Helikon-OS needs a
  manifest with magic, board ID, version, load address, size, SHA-256 and a
  signature before jumping.
- **Decision:** use an **Ed25519-signed manifest**. The bootloader validates the
  magic, manifest version, board ID, image version, load address, size and
  policy flags, verifies the Ed25519 signature with a key ID **before reading
  any image byte**, then reads the image and verifies its SHA-256. The manifest
  and boot-state formats are fixed in
  [`../bootloader/LOADER.md`](../bootloader/LOADER.md). A hash-only manifest is
  development-only and is not release-eligible. Key custody, rotation,
  revocation and lost-key recovery remain D-20/ADR-0002.
- **Consequences:** no unsigned firmware is loaded. The bootloader carries only
  public-key material; no private key is on the device or in Git. A lost key
  must not brick the device; Apple/DFU recovery remains available. Release
  signing stays blocked until D-20 is accepted.
- **Follow-up:** host manifest/signature/boot-state core and real Ed25519
  verification are implemented; the loader test image builds and passes host
  gates. Device integration is T-049; no device run is recorded. Link D-20.

## D-02 — Boot state and fallback

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §25.2, §5.3.
- **Context:** the bootloader needs to know whether the last firmware boot
  passed, and which image to load after a failure.
- **Decision:** use a two-slot checksummed native file `/.helikon/bootstate`,
  read-only from the bootloader and written only through the HFS+ transaction
  engine. Each slot records magic/version/sequence/board/image version/load
  address/size/image SHA-256/state and a slot SHA-256. The bootloader selects the
  highest valid `GOOD` slot; a newest `FAILED` slot falls back to the previous
  `GOOD` slot. Missing or corrupt state falls back to the default image and
  button-selected recovery. The bootloader never writes the state file.
- **Consequences:** the state file cannot be a bootloader write path. A failed
  boot must not boot the failed image; the previous verified image must remain
  recoverable and become bootable once C-10 is resolved. Recovery is explicit
  and visible; no silent repair or fallback to an unsigned image.
- **Known implementation limit:** the frozen format has one image path, not a
  per-slot path. The loader validates the selected `GOOD` slot against that
  image; a previous slot naming a different image fails closed. Real A/B
  fallback needs two fixed image paths or an update rollback writer. See C-10.
- **Follow-up:** parser/selector implemented; test missing/corrupt/board-mismatch
  and failed-newest cases. Define the A/B path or rollback writer, who may clear
  a `FAILED` slot, and how recovery is displayed.

## D-03 — Journal strategy

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.3, §9.3.
- **Context:** HFS+ volumes are normally journaled. Writing a journaled volume
  while ignoring the journal is unsafe.
- **Options:** (a) journaled HFS+ writes for release; (b) non-journaled writes
  only on a volume formatted without a journal, with journaled volumes mounted
  read-only.
- **Decision:** **journaled HFS+ writes are the required write path** for every
  writable volume. A non-journaled writer is not the product path; if built for
  bring-up, it is a disposable-volume prototype only, journaled volumes mount
  read-only under it, and it must never touch a real library. Release and any
  user-owned volume use the journaled transaction engine.
- **Consequences:** no write release without the journaled path. The journaled
  engine is the critical path; the non-journaled prototype is optional and
  disposable-only. Do not disable journaling to make a write fit.
- **Follow-up:** freeze the writer interface (T-002), then the journal record,
  commit, barrier and replay design (T-009). The non-journaled prototype, if any,
  is gated by T-004 and is labelled prototype-only.

## D-04 — Journal ownership

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.4, §9.3.
- **Context:** after a crash, a dirty journal must be replayed. Either the device
  replays it, or it refuses writes and requires macOS to replay it.
- **Options:** (a) Helikon-OS owns replay and completes it before the first
  write; (b) read-only mount, refuse writes, require macOS Disk Utility/fsck to
  replay; (c) hybrid: device replay only for a known-clean journal format.
- **Decision:** **Helikon-OS owns replay.** A dirty or invalid journal mounts
  read-only; the write engine replays committed records and discards incomplete
  ones before the first write. Replay is idempotent and bounded. The journal is
  never deleted, truncated, zeroed or ignored. macOS-assisted replay is not the
  primary write path.
- **Consequences:** the device must implement replay, sequence/checksum
  validation and a dirty-journal recovery screen. A journal that cannot be
  replayed stays read-only and is preserved for diagnosis. Do not silently mix
  device-owned and macOS-owned replay.
- **Follow-up:** define the `JOURNAL_DIRTY`/replay screen and recovery
  instructions; test idempotent replay at every cut point (T-009/T-010).

## D-05 — B-tree cache and transaction limits

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.5, §19.2.
- **Context:** the writer needs a B-tree node cache, a name cache and a dirty-set
  budget inside 64 MiB without starving audio/UI.
- **Options:** fixed static budgets; high/low watermarks; one cache per volume;
  a transaction cap derived from worst-case node splits; a smaller cache with
  more I/O.
- **Decision:** unresolved. The paper gives a 2–4 MiB write-engine/journal
  budget as a design target, not a measured allocation.
- **Consequences:** every cache needs a maximum, hit/miss counters and an
  eviction policy. A transaction that cannot fit fails with `NO_SPACE` before
  changing anything.
- **Follow-up:** measure on device; do not publish a budget as a measurement.

## D-06 — Unicode policy

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.6, §10.4.
- **Context:** HFS+ stores decomposed UTF-16 and uses Apple's case-folding
  comparison. The donor's exact-byte lookup is a read-only behaviour, not a
  writable policy.
- **Options:** (a) implement Apple `FastUnicodeCompare` and decompose names
  before insertion; (b) exact-byte plus explicit refusal of names that could
  collide after normalization; (c) a limited normalization table with a
  documented rejection path.
- **Decision:** **Option (a).** Catalog key order, lookup, create, delete and
  duplicate detection use Apple's `FastUnicodeCompare`. The folding and
  normalization tables are generated from Unicode 2.1.9 data plus the HFS+
  rules: canonical decompositions are applied before folding, U+2000–U+2FFF,
  U+F900–U+FAFF and U+2F800–U+2FAFF stay composed, Hangul syllables are
  decomposed algorithmically, combining marks are reordered by canonical
  combining class, format controls and U+FEFF are ignored, and U+0000 folds to
  the non-sentinel value 0xFFFF. The tables are generated by
  `tools/gen-hfs-unicode-table.py`, not copied from Apple's APSL table or from
  a GPL-2.0-only source. Names are normalized before key construction, so
  composed and decomposed input address the same key. A name that compares
  equal after folding is a duplicate (`HFS_ERR_BUSY`); NUL, unpaired
  surrogates, empty-equivalent names and results over 255 units are rejected.
- **Consequences:** comparator and normalization are implemented and
  host-tested, including a local disposable image whose names were created by
  macOS. The broader macOS name corpus, the `:`/`/` mapping and `fsck_hfs`
  ordering checks remain open. D-10 still owns how display names are shown.
- **Follow-up:** broaden the macOS-created corpus (case, combining marks,
  Hangul, CJK compatibility, emoji, length boundaries), decide the `:`/`/`
  mapping, and run `fsck_hfs -n` after normalized-name catalog mutations. Link
  the display-name policy to D-10. `fsck_hfs` remains the external oracle.

## D-07 — Database location

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.7, §12.2.
- **Context:** the README asks for improved database building on iPods. A
  full in-RAM rebuild is not planned; the paper proposes a host builder first
  and an incremental on-device builder later.
- **Options:** (a) host-built immutable database only; (b) host builder first,
  incremental on-device builder in v1.x; (c) on-device full rebuild in v1.
- **Decision:** unresolved. The paper recommends (b); it is not an owner
  acceptance.
- **Consequences:** do not claim on-device building in v1. The database is a
  cache, not the source of truth; a stale entry falls back to a catalog scan and
  reports `DB_STALE`.
- **Follow-up:** choose schema ownership, migration policy and evidence for the
  10k-track/power-cut tests.

## D-08 — App/game runtime

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.8, §28.3.9–10.
- **Context:** the README asks for custom games and applications. The paper
  excludes legacy Rockbox plugin binaries and proposes a sandboxed runtime.
- **Options:** WebAssembly interpreter (no JIT); a small bytecode VM; a native
  signed-code sandbox (higher risk). SDK language and signing model are also
  open.
- **Decision:** unresolved. No runtime is selected. Rockbox plugin/game binary
  compatibility is explicitly not promised.
- **Consequences:** the sandbox needs capabilities, quotas, a crash boundary and
  a signature/trust model before third-party code. It is P2 and cannot delay
  P0 writer work.
- **Follow-up:** choose runtime and SDK; define the trust screen and package
  format; test escape/crash isolation.

## D-09 — Desktop app transport

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.9, §28.4.2.
- **Context:** macOS can write HFS+ in Disk Mode. Windows/Linux cannot reliably
  write HFS+ without a bundled driver. A Helikon-OS USB service mode could expose
  framed file operations through the device's own VFS/transaction engine.
- **Options:** (a) macOS Disk Mode only; (b) optional vendor-specific USB
  service mode for cross-platform; (c) a writable mass-storage LUN (rejected by
  the paper).
- **Decision:** unresolved. (a) is the first path; (b) is the cross-platform
  path but expands the USB stack; (c) is rejected.
- **Consequences:** no Windows/Linux write claim until (b) is tested. A service
  mode is not a raw block device and must never bypass the transaction engine.
- **Follow-up:** define the protocol boundary and threat model; keep read-only
  LUN as the fallback.

## D-10 — Skin compatibility scope

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.10, §28.3.4.
- **Context:** the README asks for Rockbox skins. A full WPS engine is large and
  the assets have varied licences. The paper proposes a documented subset.
- **Options:** (a) full WPS engine and bundled themes; (b) documented subset with
  a 20-skin corpus, visible unsupported-tag placeholders and hard caps;
  (c) no skin compatibility, first-party themes only.
- **Decision:** unresolved. The paper recommends (b) as P1/v1.x and rejects (a).
  Full WPS compatibility is not promised.
- **Consequences:** do not copy proprietary themes/art/fonts. A malformed or
  pathological skin must be rejected without crashing; the default UI remains.
- **Follow-up:** publish the supported tag list and corpus licence status.

## D-11 — Anti-corruption scope

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.11, §28.2.2.
- **Context:** HFS+ journals metadata but does not checksum file data. The README
  asks for active anti-corruption.
- **Options:** checksum sidecar (SHA-256 or BLAKE3), covered file classes,
  verify-on-open, charging/idle scrub cadence, user opt-out, quarantine policy.
- **Decision:** unresolved. The paper proposes metadata journaling plus a
  sidecar checksum index, verify-on-open for critical files, a charging/idle
  scrub and quarantine; no self-healing claim.
- **Consequences:** scrub must not wear the SD card unnecessarily or delay
  playback. A mismatch is reported, not silently rewritten.
- **Follow-up:** choose algorithm/coverage/cadence; define the quarantine and
  restore workflow.

## D-12 — Battery target

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.12, §28.3.1.
- **Context:** the README asks for improved battery life over Rockbox. That is a
  comparative measurement, not a design slogan.
- **Options:** exact percentage target (the paper uses "at least 10%" as an
  example goal), baseline build/commit, media, volume, backlight, output,
  number of runs and measurement rig.
- **Decision:** unresolved. The paper explicitly says the target is a goal, not
  a promise, and is withdrawn if the measurement does not support it.
- **Consequences:** no battery claim in release notes without a recorded
  baseline and three runs. Do not use the donor's owner-reported ~2-hour gain as
  Helikon-OS evidence.
- **Follow-up:** define baseline and rig before phase 11; publish mean and range.

## D-13 — 7G and port scope

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.13, §28.4.5.
- **Context:** the README asks for a wide range of iPods and maybe other players.
  The paper says 6G first, 7G next after a board audit, other devices are ports.
- **Options:** 6G-only v1; 6G + 7G; additional S5L8702 iPods; other MP3 players.
- **Decision:** unresolved beyond the 6G reference and 7G next-named port. No
  7G support claim before the board audit.
- **Consequences:** a port needs a BSP, target description, hardware, maintainer
  and per-target matrix. No target `#ifdef` in the shared core.
- **Follow-up:** record the 7G audit checklist and the first port candidate.

## D-14 — Car protocol

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.14, §28.3.7.
- **Context:** car use is mostly line-out, charging and a few controls. Tier 2 is
  a subset of the 30-pin accessory protocol, with legal and reverse-engineering
  risk.
- **Options:** Tier 1 only (line-out/charging/resume); Tier 2 control subset;
  full Apple accessory certification (not claimed).
- **Decision:** unresolved. Tier 1 must not depend on Tier 2. No Apple
  certification claim.
- **Consequences:** unknown accessories fall back to line-out/charging, never a
  hang. The receiver matrix records model, cable, charging, resume, controls and
  audio quality.
- **Follow-up:** define the legal review and the 100-ignition-cycle test.

## D-15 — Theme format

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.15, §14.2.
- **Context:** the new UI needs one theme format for colours, fonts, spacing and
  backgrounds. The Rockbox WPS subset is a compatibility path, not the design
  centre.
- **Options:** minimal declarative format; retained-mode layout format; reuse a
  Rockbox theme subset as the native format.
- **Decision:** unresolved. The paper defers a new native theme format until the
  core UI is stable.
- **Consequences:** do not make the WPS subset the native theme engine. Keep
  assets installed, not compiled in, and respect licences.
- **Follow-up:** decide after the first-party UI is stable; link to D-10.

## D-16 — Update transport

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.16, §16.2.
- **Context:** firmware can be staged as native files under `/.helikon/update/`
  over USB read-only + macOS, or installed only through DFU.
- **Options:** (a) USB read-only + macOS staging, then bootloader verification;
  (b) DFU-only; (c) a writable USB LUN (rejected).
- **Decision:** unresolved. The paper describes (a) as the update flow and keeps
  bootloader changes DFU-only. It is not owner-accepted.
- **Consequences:** no writable mass-storage LUN. Staging is a transaction;
  verification happens before install and before jump. Bad images keep the old
  image bootable.
- **Follow-up:** choose transport and support window; link to D-01/D-20.

## D-17 — Product identity

- **Status:** accepted. **Name clearance:** open. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.17, Appendix D.
- **Context:** the working name is Helikon-OS; the paper records it as the chosen
  name and lists a clearance checklist.
- **Options:** retain Helikon-OS; choose another name; use a component codename.
- **Decision:** Helikon-OS is the product name. Trademark, domain, package and
  repository-name clearance is still open.
- **Consequences:** use Helikon-OS consistently for the project, bootloader,
  firmware, UI, docs and host tools. Do not inherit "Rockpod", "Rockbox" or
  "HFS+" as the public product identity; use them only for lineage/attribution.
- **Follow-up:** complete the clearance checklist before public branding or
  release assets. The owner will supply the final logo; the mountain reference
  image is in `assets/logo/` with source/licence open.

## D-18 — Licensing

- **Status:** accepted. **Owner:** owner/legal. **Date:** 2026-11-03. **Paper:** §18, §28.4.4.
- **Context:** the README asks for an open-source, community-friendly project.
  The paper says firmware/bootloader should be GPL-compatible with Rockbox
  attribution, host tools under a compatible license, and docs under a
  permissive documentation license. The donor README states that Rockbox and the
  donor project are **GPL-2.0-or-later**.
- **Options:** GPL-2.0-or-later (match Rockbox/donor); GPL-3.0-or-later;
  permissive MIT/Apache for new code; split code/documentation licenses.
- **Decision:** the repository is licensed under **GPL-2.0-or-later**, matching
  Rockbox and the donor. `LICENSE` contains the GNU GPL version 2 text;
  `NOTICE` states the "or later" option and upstream attribution. The
  documentation is also GPL-2.0-or-later for now, rather than the paper's
  suggested permissive documentation license, to keep one simple repository
  license.
- **Consequences:** distribution and derivative works must comply with
  GPL-2.0-or-later, including source availability and attribution. The project
  is now licensed for reuse under those terms. Do not copy third-party assets
  whose licenses are incompatible. Any future split license for docs/tools is a
  new owner decision.
- **Follow-up:** add SPDX headers (`GPL-2.0-or-later`) to source files when code
  exists; keep donor/Rockbox attribution; obtain legal review before a release;
  update `CONTRIBUTING.md` and release notes.

## D-19 — Code of conduct

- **Status:** rejected. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §18, §28.4.4.
- **Context:** the paper lists a code of conduct and security policy as
  community infrastructure. Contributor Covenant 2.1 was added, then the owner
  deleted `CODE_OF_CONDUCT.md` on GitHub on 2026-11-03.
- **Options:** adopt a standard code; write a project-specific code; defer until
  community growth; no code.
- **Decision:** no code of conduct is currently adopted. The Contributor
  Covenant text was removed by the owner and must not be re-added without a new
  owner decision.
- **Consequences:** there is no enforcement text or conduct contact. Be civil;
  report serious security-sensitive issues through `SECURITY.md`. Do not link to
  a non-existent `CODE_OF_CONDUCT.md`.
- **Follow-up:** the owner may choose a different text later; if so, record a new
  decision, add the file deliberately and update the links.

## D-20 — Signing-key ownership

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.1, §16, §28.2.5.
- **Context:** signed manifests and release integrity require key custody,
  rotation, revocation and recovery. The paper warns against a release process
  that depends on one person's laptop or an unpublished key.
- **Options:** owner-held key; project maintainer roles; hardware token; offline
  signing ceremony; key escrow/recovery; separate bootloader and firmware keys.
- **Decision:** unresolved. No key, custodian, rotation or revocation policy is
  chosen.
- **Consequences:** no release signing until this is settled. Never commit a
  private key, token, recovery phrase or signing secret. A lost key must not
  brick devices; DFU/Apple recovery remains the fallback.
- **Follow-up:** define roles, storage, ceremony, rotation, revocation and
  emergency recovery; link to D-01 and `release.md`. A proposed option set is in
  [`adr/0002-signing-key-ownership.md`](adr/0002-signing-key-ownership.md).

## D-21 — First diagnostic bootloader scope

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §5, §24, §27.
- **Context:** the owner wants a first flashable diagnostic image before the
  full firmware-loading bootloader exists. The host read core, diagnostic core,
  320×240 splash and donor-derived S5L8702/ATA/LCD glue are now integrated.
- **Options:** read-only diagnostic only; diagnostic plus firmware loading;
  signed manifest from day one; manual hash verification; write diagnostic on a
  disposable volume.
- **Decision:** the first image is **read-only diagnostic only**. It uses
  **manual SHA-256 verification**, performs **no HFS+ write test** and reports
  `HFS+ write: NOT IMPLEMENTED`. It waits for a button, then powers off. It does
  not load firmware. Backup scope is the current `.rockbox` build and
  `/.rockpod-rw`; music backup is deliberately omitted by the owner. The owner
  accepts that a failed flash may require Apple restore and can erase music.
- **Consequences:** D-01/D-02 are now accepted for the later firmware-loading
  bootloader; D-20 remains open. This diagnostic image is not a release and has
  no signed manifest. It requires a recorded image hash, Thumb/`MOVE_AREA`
  checks and a recovery rehearsal. No HFS+ writes are allowed.
- **Follow-up:** the image was built on 2026-11-03: 67,144 bytes, SHA-256
  `280ed7aee5dcf38d1a30470704f91740b299004692c2aebbb21d41744c05f43c`,
  `_moveend` `0x22030e40`, 61,888 bytes below `MOVE_AREA`. The first build
  (`bd0ebc92…`) booted and showed the splash, but the report formatter and root
  read probe failed; both are fixed in the current image. The fixed image is
  still unproven; record the reflash in
  [`device-matrix.md`](device-matrix.md) using
  [`../bootloader/DFU.md`](../bootloader/DFU.md). The separate inner-image write
  diagnostic is D-22 and [`../bootloader/WRITE_DIAG.md`](../bootloader/WRITE_DIAG.md).


## D-22 — Separate write diagnostic scope

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §5, §24, §27; D-21.
- **Context:** the read-only diagnostic cannot exercise the native writer. The
  owner asked for a read/write diagnostic while preserving the rule that the
  production bootloader and the library volume are never written.
- **Decision:** build a **separate DFU-only write diagnostic**. It keeps the
  splash and the outer HFS+ volume read-only, finds a preallocated root-level
  `helikon-test.img` (16 MiB, journaled HFS+), maps only that file's data-fork
  extents, and runs create folder/file, append, readback, rename, truncate and
  delete through the real writer inside the inner image. It rejects a dirty
  outer journal, a missing/small/non-journaled inner image, and an extent map
  that overlaps outer metadata. It reports PASS/FAIL and leaves the inner image
  for macOS `fsck_hfs`. The production bootloader remains read-only; this image
  does not load firmware, does not write the outer filesystem and is never
  installed with `--single`.
- **Consequences:** the write image contains `ata_write_sectors` and is
  experimental. The outer file's data blocks change; outer metadata does not.
  The test image is disposable and must be created by the user. No device or
  support claim is allowed until a recorded run and an external `fsck_hfs`
  pass. D-25 adds a separate outer-volume diagnostic; it does not relax this
  image's inner-only rule.
- **Follow-up:** run it on the owner's 6G with a verified backup, record the
  report and extracted-image fsck in `docs/device-matrix.md`; if it fails,
  preserve the image and stop. The production bootloader stays read-only.
- **Evidence:** first owner-reported 6G run, 2026-11-03: every screen stage
  PASS; extracted inner image SHA-256
  `156060075eef60c463f421ce459c2138a00e1c43de44da802b77ce89c79ca0e8`;
  `fsck_hfs -fn` exit 0; no `diag` folder; outer `diskutil verifyVolume` exit 0.
  One run only; not a library-write, power-cut or support claim.


## D-23 — Default UI typography

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §14, §28.3.4; D-10, D-15.
- **Context:** the first-party UI needs one hierarchy and one UI family. The
  owner supplied the default element/typeface/size table on 2026-11-03.
- **Decision:** the default theme uses IBM Plex Sans as the only UI family:
  main menu 14 px Regular, selected item 14 px Medium, screen titles 17 px
  Semibold, artist/album 12 px Regular, system information 11 px Regular.
  Manrope Bold is the Helikon-OS logo/wordmark only, with a variable
  font/layout-dependent size. The selected item changes weight, not size. 11 px
  is the default minimum; no clipping or horizontal scrolling is allowed.
- **Consequences:** the table is recorded in [`ui.md`](ui.md), not in the design
  paper. Font binaries need their exact OFL licence, source and subset hash
  before they are committed. Line height, colours, margins, selection
  background and large-text behaviour are still open; do not treat them as
  settled. Skins may not make the first-party UI unusable (D-10/D-15).
- **Follow-up:** verify the IBM Plex Sans and Manrope releases and licences,
  build a 320×240 host mockup, check 17/14/12/11 px for clipping and contrast,
  then implement the renderer. Record a device-panel result in
  [`device-matrix.md`](device-matrix.md) before any support claim.
- **Evidence:** owner-supplied table, 2026-11-03.

## D-24 — UI text and icon rendering

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §14, §28.3.3; D-23.
- **Context:** the typography table (D-23) defines sizes and weights. The 6G
  panel needs readable text at 11–17 px; the owner requires anti-aliasing.
- **Decision:** UI text and icons use grayscale anti-aliasing. Glyph masks or a
  glyph atlas are precomputed; the renderer must not rasterize a full font per
  frame. A readable 1-bit bitmap fallback remains when a font fails to load.
  Anti-aliased output is checked on the 6G panel for contrast, clipping and
  memory use. Subpixel rendering, gamma and hinting policy remain open.
- **Consequences:** font assets need masks/atlases and a measured cache budget;
  the host mockup and device panel check must include anti-aliased glyphs, not
  only 1-bit text. The bootloader splash is separate; its 1-bit/grayscale path
  is T-045.
- **Follow-up:** implement in the renderer (T-042), include in the 320×240 mockup
  (T-041), and record a 6G panel result before a UI support claim.
- **Evidence:** owner direction, 2026-11-04.

## D-25 — Outer-volume write diagnostic scope

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §5, §24, §27; D-03, D-04, D-22.
- **Context:** the inner-image diagnostic cannot exercise the outer journal,
  catalog, bitmap or volume header. The owner wants the first real outer-volume
  write on their own 6G after a full backup.
- **Decision:** build a **separate DFU-only outer-volume write diagnostic**. It
  requires a root-level `helikon-outer-ok.txt` with magic
  `HELIKON-OUTER-WRITE-OK`, a clean valid journal, native HFS+ v4, an inline
  catalog/allocation fork and no pre-existing `/helikon-outer-test`. It writes
  only that folder's marker and test files, then deletes them and the folder.
  It never replays a dirty journal, never deletes an existing folder
  automatically, never touches another root entry and never loads firmware.
  The full-backup and restore-rehearsal gate is in
  [`outer-volume-test.md`](outer-volume-test.md); the runbook is
  [`../bootloader/OUTER_DIAG.md`](../bootloader/OUTER_DIAG.md).
- **Consequences:** this image writes outer metadata (journal, catalog, bitmap,
  volume header) and is higher risk than D-22. It is not the production
  bootloader and is never installed with `--single`. The first device run
  requires T-048 and a recorded `fsck_hfs` and backup diff.
- **Evidence:** owner direction, 2026-11-04; host test passes, no device run.

## Conflicts and source separation

These are recorded so an agent does not silently pick a side. A conflict does
not authorize a product change.

### C-01 — Donor V3.2 safety model vs Helikon-OS native writes

- **What:** the donor README/`agents.md` say native HFS+ metadata is immutable
  and writes live in `/.rockpod-rw`. Helikon-OS requires native HFS+ writes and
  forbids the overlay.
- **Why it is not a bug:** they are different products with different write
  models. The donor is overlay-based; Helikon-OS is native.
- **Recorded resolution:** product boundary B-01/B-02. Donor files are
  read-only reference material. Never copy donor safety rules into Helikon-OS or
  use donor overlay tests as Helikon-OS evidence.
- **Affected docs:** `state.md`, `architecture.md`, `hfsplus-write-engine.md`,
  `device-matrix.md`.

### C-02 — README skins goal vs documented subset

- **What:** the README asks for "the ability to use Rockbox skins on Helikon-OS";
  the paper limits this to a documented WPS subset and explicitly does not
  promise full compatibility.
- **Status:** open owner confirmation (scope, not safety). D-10 records the
  options. Until confirmed, documentation must say "documented subset", never
  "full Rockbox skin compatibility".
- **Affected docs:** `goals.md`, `faq.md`, `architecture.md`, `validation.md`.

### C-03 — README database goal vs host-first sequencing

- **What:** the README asks for improved database building on iPods; the paper
  puts a host-built database first and an incremental on-device builder in
  v1.x. D-07 is unresolved.
- **Status:** open. Do not claim on-device database building in v1.
- **Affected docs:** `goals.md`, `roadmap.md`, `validation.md`.

### C-04 — README DAC goal vs no external USB DAC on 6G

- **What:** the README asks for improved DAC support; the paper interprets this
  as using the internal CS42L55 path correctly and excludes external USB DACs on
  the 6G because it has no USB host port.
- **Status:** interpretation recorded. Not a contradiction of hardware fact.
  External USB DAC support is not promised on the 6G.
- **Affected docs:** `goals.md`, `faq.md`, `validation.md`.

### C-05 — README games/apps vs no Rockbox binary compatibility

- **What:** the README asks for custom games/applications; the paper excludes
  legacy Rockbox plugins and promises a new sandboxed runtime, not binary
  compatibility.
- **Status:** interpretation recorded. D-08 is open. Do not promise Rockbox
  plugin/game compatibility.
- **Affected docs:** `goals.md`, `faq.md`, `architecture.md`.

### C-06 — README wide device support vs 6G/7G-first roadmap

- **What:** the README asks for a wide range of iPods and maybe other players;
  the paper says 6G first, 7G next, other devices are ports and P2.
- **Status:** roadmap sequencing recorded. No v1 claim for other devices.
- **Affected docs:** `goals.md`, `roadmap.md`, `device-matrix.md`.

### C-07 — README updates/community vs open signing policy

- **What:** the README asks for regular updates and a community-friendly
  project; the paper requires signed manifests. Licensing is settled (D-18);
  the code of conduct was deleted by the owner (D-19 rejected); signing-key
  ownership is still open (D-20).
- **Status:** D-18 accepted; D-19 rejected; D-20 open. Do not publish release
  artifacts or signing claims until D-20 is accepted.
- **Affected docs:** `release.md`, `security.md`, `CONTRIBUTING.md`,
  `SECURITY.md`.

### C-08 — Paper §25 has 17 decisions; this file has 20

- **What:** the paper's §25 lists 17 open decisions. The documentation task
  requires licensing, code of conduct and signing-key ownership as well.
- **Resolution:** D-01–D-17 mirror §25; D-18–D-20 are added from §18 and
  §28.4.4. D-18 is accepted; D-19 was rejected after the owner deleted the code
  of conduct; D-20 remains `proposed`. This is a documentation reconciliation,
  not a new product decision.
- **Affected docs:** this file, `agents.md`, `goals.md`.

### C-09 — Paper suggests permissive docs license; repository uses GPL-2.0-or-later

- **What:** the paper suggests documentation under a permissive documentation
  license. D-18 chooses GPL-2.0-or-later for the whole repository, including
  docs, to match Rockbox/donor compatibility and keep one license.
- **Status:** resolved by the owner-authorized D-18 choice; a separate
  permissive docs license would be a new owner decision.
- **Affected docs:** `LICENSE`, `NOTICE`, `CONTRIBUTING.md`, `docs/README.md`.

### C-10 — D-02 says previous GOOD, but the frozen format has one image path

- **What:** D-02 selects the previous `GOOD` boot-state slot after a failed
  newest slot, but `LOADER.md` defines only `/.helikon/firmware.img` and
  `/.helikon/firmware.manifest`. A previous slot cannot name a different image
  file, so the bootloader can validate metadata but cannot boot a previous image.
- **Status:** open design gap. The loader fails closed when the selected slot
  does not match the current manifest/image. Do not claim working A/B fallback.
  Resolve with two fixed image paths or an update rollback writer.
- **Affected docs:** `bootloader/LOADER.md`, `bootloader/LOADER_TEST.md`,
  `docs/state.md`, `docs/tasks.md`.

## Adding a decision

1. Add an index row and an entry with ID, title, status, owner, date and paper
   section.
2. State context, options, decision (or "unresolved"), consequences and
   follow-up.
3. If unresolved, use `proposed`; do not describe a recommendation as accepted.
4. Link affected docs and update `state.md` if the decision changes a blocker.
5. Use `docs/adr/` only when the entry would exceed a short-form record.
