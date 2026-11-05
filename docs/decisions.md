# Decisions — settled choices and open decisions

> Purpose: why the design is what it is, and what is still undecided.
> Status: living document
> Owner: project owner
> Last updated: 2026-11-05
> Source: design paper §18, §25, §28; Helikon-OS README; Rockbox iPod
> Classic manual `0d5c2b0604-261105`; [`competitive-roadmap.md`](competitive-roadmap.md);
> [`audio-formats.md`](audio-formats.md)

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
| D-20 | Signing-key ownership | accepted | owner | §25.1, §16, §28.2.5 |
| D-21 | First diagnostic bootloader scope | accepted | owner | §5, §24, §27 |
| D-22 | Separate write diagnostic scope | accepted | owner | §5, §24, §27; D-21 |
| D-23 | Default UI typography | accepted | owner | §14, §28.3.4; D-10, D-15 |
| D-24 | UI text/icon rendering | accepted | owner | §14, §28.3.3; D-23 |
| D-25 | Outer-volume write diagnostic scope | accepted | owner | §5, §24, §27; D-03, D-04, D-22 |
| D-26 | Bootloader recovery/OF selection | accepted | owner | §5, §16, §21 |
| D-27 | Firmware memory map and image layout | accepted | owner | §19.2, §27; ADR-0003 |
| D-28 | Firmware entry ABI and boot information | accepted | owner | §5.3, §16; ADR-0004 |
| D-29 | Competitive parity and differentiation policy | proposed | owner | §23, §28.4.6; competitive roadmap |
| D-30 | Install, dual boot and uninstall model | proposed | owner | §26.7, §28.4.3; D-26 |
| D-31 | Update and recovery model | proposed | owner | §16, §28.2.5; D-01, D-02 |
| D-32 | Filesystem object semantics | proposed | owner | §10.3–§10.4; D-06 |
| D-33 | Library index architecture | proposed | owner | §12, §28.4.1; D-07 |
| D-34 | Codec and container tiers | proposed | owner | §13, Appendix B of the Rockbox manual |
| D-35 | Sound-processing scope | proposed | owner | §6, §13; goal 16 |
| D-36 | Theme package and trust model | proposed | owner | §14, §28.3.4; D-10, D-15 |
| D-37 | Accessibility and localization | proposed | owner | §14, §28.3.3; goal 2 |
| D-38 | USB profile and service mode | proposed | owner | §16.2, §28.4.2; D-09 |
| D-39 | Anti-corruption algorithm and coverage | proposed | owner | §9.4, §28.2.2; D-11 |
| D-40 | Performance budgets and measurement | proposed | owner | §19.2, §24, §28.6 |
| D-41 | Power and thermal policy | proposed | owner | §19, §28.3.1; D-12 |
| D-42 | Adapter compatibility policy | proposed | owner | §7, §28.2.4; goal 6 |
| D-43 | Podcast and audiobook model | proposed | owner | §28.3.6; goal 19 |
| D-44 | App/game runtime and package format | proposed | owner | §25.8, §28.3.9–10; D-08 |
| D-45 | Migration and coexistence | proposed | owner | §22, §28.4.3; goal 10 |
| D-46 | Release, support and governance | proposed | owner | §18, §28.4.4; D-18, D-20 |
| D-47 | Porting contract and hardware matrix | proposed | owner | §28.4.5; D-13 |
| D-48 | Privacy, telemetry and network policy | proposed | owner | §19.3, §28.3.9 |
| D-49 | Decoder provenance and licensing | proposed | owner | §13, §18; audio-formats |
| D-50 | Audio output ceiling and resampling | proposed | owner | §13, §28.3.8; audio-formats |
| D-51 | Audio metadata, artwork and gapless policy | proposed | owner | §9.4, §28.3.8; D-06; audio-formats |

The paper's §25 lists 17 decisions. D-18, D-19, D-20 and D-26 are added from §18,
§28.4.4 and the bootloader recovery policy. D-18 and D-20 are accepted; D-19 was
adopted and then rejected when the owner deleted `CODE_OF_CONDUCT.md` on
2026-11-03. D-21 records the first diagnostic bootloader scope. D-01/D-02 are
accepted for the firmware-loading bootloader. D-22 records the separate
inner-image write diagnostic; D-25 records the separate outer-volume diagnostic
and its backup gate. D-27/D-28 record the firmware memory map and entry ABI.
See C-08.

**D-29–D-51 are proposed, not accepted.** They were added on 2026-11-05 as the
decision layer for the Rockbox competitive review in
[`competitive-roadmap.md`](competitive-roadmap.md) and the audio-format review
in [`audio-formats.md`](audio-formats.md). They do not change D-01–D-28, the 25
README goals, the phase gates or any implementation status. Only the owner can
accept, reject or narrow one of them.

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
  signing stays blocked until the release-key certificate verifier (T-053), a
  real signed firmware image (T-051) and a release record exist.
- **Follow-up:** host manifest/signature/boot-state core, real Ed25519 and the
  loader test image are implemented; the test image is owner-reported working.
  Production handoff is T-051. D-20 is accepted; link ADR-0002.

## D-02 — Boot state and fallback

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §25.2, §5.3.
- **Context:** the bootloader needs to know whether the last firmware boot
  passed, and which image to load after a failure.
- **Decision:** use a two-slot checksummed native file `/.helikon/bootstate`,
  read-only from the bootloader and written only through the HFS+ transaction
  engine. Each slot records magic/version/sequence/board/image version/load
  address/size/image SHA-256/state and a slot SHA-256. The bootloader selects the
  highest valid `GOOD` slot that matches the installed manifest/image. A newest
  `FAILED` slot, a mismatched selected slot, or corrupt state that cannot be
  defaulted produces a recovery screen and **no jump**. Missing or corrupt state
  may fall back to the single installed verified image and button/DFU recovery.
  The bootloader never writes the state file.
- **Owner resolution, 2026-11-04:** the single-image, fail-closed model is
  accepted. The frozen format has one manifest/image path, not per-slot paths.
  There is no A/B rollback and no previous-image boot. Recovery is DFU/Apple
  restore; the owner tested the fail-closed behaviour. No A/B image-path work is
  required.
- **Consequences:** the state file cannot be a bootloader write path. A failed
  boot must not boot the failed image; it fails closed rather than silently
  falling back to an unsigned or mismatched image. This closes C-10 and T-050 as
  an explicit no-A/B decision.
- **Follow-up:** parser/selector and fail-closed tests are implemented; keep
  missing/corrupt/board-mismatch and failed-newest cases covered. No A/B path or
  rollback writer is planned.

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

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §25.1, §16, §28.2.5.
- **Context:** signed manifests and release integrity require key custody,
  rotation, revocation and recovery. The paper warns against a release process
  that depends on one person's laptop or an unpublished key.
- **Decision:** **ADR-0002 option A is accepted.** An offline Ed25519 root key,
  held by the project owner on two hardware tokens in separate locations, only
  certifies release keys, rotates them and revokes them. A separate Ed25519
  release key signs firmware and bootloader manifests under a root-signed
  release-key certificate with a bounded validity period (default 12 months).
  The bootloader trust table carries the root and certified release key IDs. A
  lost release key is replaced by a root-signed certificate; a lost root key
  stops new signed updates but does not brick devices, because DFU/Apple restore
  remains. No private key or token enters Git, CI secrets, chat or unrecorded
  backups. The full policy is in
  [`adr/0002-signing-key-ownership.md`](adr/0002-signing-key-ownership.md).
- **Consequences:** D-20 is settled; T-006 closes. The current loader test image
  uses a build-local test key table and is not release-eligible. The
  release-key certificate/revocation verifier and a real signed release image
  are still implementation work; release signing is not claimed yet.
- **Follow-up:** implement the certificate, revocation and rotation verifier
  (T-053); record the first release-key fingerprints and validity period; add
  bad-signature/wrong-key/expired/revoked/rotation tests.

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
- **Consequences:** D-01/D-02/D-20 are accepted for the later firmware-loading
  bootloader. This diagnostic image is not a release and has no signed
  manifest. It requires a recorded image hash, Thumb/`MOVE_AREA` checks and a
  recovery rehearsal. No HFS+ writes are allowed.
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

## D-26 — Bootloader recovery/OF selection

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §5, §16, §21.
- **Context:** the earlier architecture sketch included an Apple/Helikon-OS/
  recovery/USB menu in the rescue bootloader. The owner notes that an iPod
  already enters OF/disk mode through the native Menu+Play startup gesture and
  that DFU/Apple restore covers bootloader recovery.
- **Options:** in-bootloader menu; native button gestures plus DFU; button-only
  recovery with no bootloader UI.
- **Decision:** **no in-bootloader Apple/OF/recovery menu.** The bootloader shows
  its splash/status, verifies firmware and jumps; OF/disk mode and recovery stay
  on the iPod's native Menu+Play and DFU/Apple restore paths. The bootloader
  does not claim or replace those paths.
- **Consequences:** T-051 is production firmware handoff only; the recovery menu
  is removed from its scope and from the architecture boot chain. Documentation
  and runbooks point to the native gestures and DFU.
- **Follow-up:** keep the native recovery instructions in `bootloader/DFU.md`
  and `docs/outer-volume-test.md`; do not add a bootloader menu without a new
  owner decision.

## D-27 — Firmware memory map and image layout

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §19.2, §27; ADR-0003.
- **Context:** the loader validates an 8 MiB image window, but the 6G has 64 MiB
  of DRAM and no firmware memory map was recorded. F1 needs a linker script,
  stacks, framebuffer, ATA buffers and a boot-info page; F5 needs audio memory.
- **Options:** keep everything inside 8 MiB; a fixed 64 MiB map with a reserved
  boot-info page; a runtime-discovered relocatable heap; defer the map.
- **Decision:** **a fixed 64 MiB map.** Image in `0x08000000`–`0x087fefff`
  (8 MiB − 4 KiB), boot-info page at `0x087ff000` (4 KiB), BSS/stacks/pools at
  `0x08800000` (8 MiB), UI at `0x09000000` (8 MiB), audio at `0x09800000`
  (24 MiB), database at `0x0b000000` (8 MiB) and reserve at `0x0b800000`
  (8 MiB). The image cap is 8 MiB − 4 KiB. Audio takes the upper end of its
  16–24 MiB target; the database takes the top of its 4–8 MiB target.
- **Consequences:** the loader must later reject an image that overlaps the
  boot-info page. Audio and database budgets are reserved before those features
  exist. A measurement may change the split before F5, but only through a new
  decision.
- **Follow-up:** T-054 implements the linker script and assertions; T-059 fills
  the boot-info page in the loader; measure the audio buffer need before F5.
- **Evidence:** design-paper memory budget and accepted ADR-0003; no device
  measurement yet.

## D-28 — Firmware entry ABI and boot information

- **Status:** accepted. **Owner:** owner. **Date:** 2026-11-04. **Paper:** §5.3, §16; ADR-0004.
- **Context:** the loader calls `entry(void)` with IRQ disabled and caches
  cleaned, expects a test payload to return, and passes no boot information. A
  production firmware must never return and needs the verified image facts and
  bootstate/journal observations without re-reading HFS+.
- **Options:** no arguments and re-read everything; a versioned boot-info page
  in `r0`; a fixed struct at an absolute address; pass only the manifest
  pointer.
- **Decision:** **a versioned, CRC-32 protected boot-info page at
  `0x087ff000`, passed in `r0`**, with `r1 = 0` and a no-return contract.
  Boot-info is a cache of verified facts, not a trust anchor; the firmware must
  re-read bootstate before writing it. The firmware parser and host test can
  land before the loader fill (T-054), so F1 is not blocked.
- **Consequences:** the loader gains a small DRAM write before the jump. The
  firmware gains a parser, a host test and a defensive `BOOT_INFO_INVALID` path.
  A bad or missing page must not lead to a bootstate write.
- **Follow-up:** T-054 implements the parser; T-059 fills the page in the
  loader; the accepted ABI is recorded in `bootloader/LOADER.md` §4.
- **Evidence:** accepted ADR-0004; current loader source and test payload
  behaviour; no device run of a boot-info page yet.

## D-29 — Competitive parity and differentiation policy

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §23, §28.4.6; [`competitive-roadmap.md`](competitive-roadmap.md).
- **Context:** the owner wants Helikon-OS to be a strong alternative to Rockbox
  in every aspect and better in several. Rockbox has a much larger codec,
  plugin, theme and device catalogue. “Every aspect” is unbounded unless it is
  defined, and a feature count is not a product.
- **Options:** (a) match every Rockbox feature before v1; (b) define a finite
  P0/P1 parity set, a measured differentiation set and an explicit decline list;
  (c) ship only first-party features and make no parity promise.
- **Proposed direction:** option (b). Every competitive row gets `match`,
  `exceed`, `differ` or `decline`, a horizon and an evidence pointer. “Strong
  alternative” means the v1 threshold in `competitive-roadmap.md` §8.1 passes;
  “better” means an `exceed` row has a baseline and a recorded metric. No
  “better than Rockbox” release claim without that evidence.
- **Consequences:** missing plugins or codecs are not automatically regressions;
  the declines in `competitive-roadmap.md` §7 are part of the product. A new
  Rockbox release does not automatically add scope. The matrix is reviewed at
  each phase gate.
- **Follow-up:** T-060 (scorecard), T-061 (differential harness), T-090
  (usability) and T-091 (privacy review). The owner must accept or narrow the
  v1 threshold before it becomes a release gate.

## D-30 — Install, dual boot and uninstall model

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §26.7, §28.4.3; D-26; [`competitive-roadmap.md`](competitive-roadmap.md) §5.1.
- **Context:** Rockbox on the Classic requires FAT32; HFS+ iPods are unsupported
  and the manual links a conversion guide. Rockbox installation uses Rockbox
  Utility or a manual `mks5lboot` DFU flow and needs administrator/root for the
  bootloader. Helikon-OS is native HFS+ and already refuses FAT.
- **Options:** (a) macOS-first installer over USB Disk Mode with signed native
  files, a DFU-only bootloader step, backup/verify and one-click restore;
  (b) a cross-platform service-mode installer; (c) require a FAT32 conversion
  like Rockbox (rejected by B-02).
- **Proposed direction:** option (a) first, option (b) later, option (c)
  rejected. The installer detects the model, capacity, adapter, filesystem and
  journal state; refuses an unclean or non-HFS+ volume; backs up and reads back;
  installs the bootloader only through DFU; installs firmware as signed files;
  runs a first-boot self-test; and offers a tested restore to Apple firmware.
  The Apple NOR path and dual boot are preserved. The user must be able to boot
  the original firmware through a documented gesture and to uninstall without
  erasing the music.
- **Consequences:** macOS is the first install platform; Windows/Linux wait for
  D-38. FAT32 iPods are not supported and need an explicit backup/conversion
  explanation. The installer is a destructive-adjacent tool and needs the same
  hard stops as a device write.
- **Follow-up:** T-062 (installer), T-063 (dual boot/uninstall), T-083 (desktop
  app). The owner confirms whether booting Apple firmware is a v1 requirement or
  a documented recovery path.

## D-31 — Update and recovery model

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §16, §28.2.5; D-01, D-02; [`competitive-roadmap.md`](competitive-roadmap.md) §5.1.
- **Context:** Rockbox updates by unzipping a build over the existing install;
  ROLO can load another `.ipod` file. No signature is documented. Helikon-OS has
  a signed manifest and a single-image fail-closed bootstate (D-01/D-02), but no
  update client or recovery-copy flow.
- **Options:** (a) one-image transactional update with a signed manifest, a
  preflight, a `TRYING`/`GOOD` bootstate and a manual recovery copy; (b) A/B
  slots (rejected by D-02); (c) DFU-only firmware updates (rejected for a
  regular release train).
- **Proposed direction:** option (a). Firmware is staged under
  `/.helikon/update/`, verified before install and again before jump, committed
  through the transaction engine, and marked `TRYING` until the first
  successful boot. A recovery copy may be kept under `/.helikon/recovery/` for a
  manual restore, but it is not an automatic A/B boot path. Bootloader changes
  remain DFU-only. Stable, beta and development channels are signed.
- **Consequences:** there is no automatic rollback; recovery is manual or
  DFU/Apple restore. A power cut during staging must leave the old image
  bootable. The update client must never write around the transaction engine or
  `--single`.
- **Follow-up:** T-064 (update client), T-086 (release train), T-053
  (certificate/revocation). The owner confirms the recovery-copy retention
  policy and support window.

## D-32 — Filesystem object semantics

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §10.3–§10.4; D-06, B-02.
- **Context:** HFS+ can contain regular files, folders, symlinks, hard links,
  resource forks, extended attributes and macOS private metadata. Rockbox on the
  Classic is FAT32 and does not expose these HFS+ objects. A Helikon-OS VFS that
  pretends every path is a regular file will fail on a real Mac-created volume.
- **Options:** (a) full POSIX-like read/write for every object class;
  (b) regular files and folders read/write in v1, safe read/preserve for the
  rest, creation deferred; (c) regular files and folders only, with everything
  else hidden (rejected: hides real data).
- **Proposed direction:** option (b). V1 creates and mutates regular files and
  folders through the transaction engine. Symlinks and hard links are resolved
  safely on read and preserved on copy/move where the operation permits;
  creating them is deferred until the writer has a tested path. Resource forks
  and xattrs are preserved, not silently dropped; exposing them to apps requires
  a capability. macOS private metadata is skipped or preserved, never treated as
  user music. A name or object that cannot be represented is reported
  `UNSUPPORTED`, not hidden.
- **Consequences:** the VFS has a capability model, not a raw block API. Some
  macOS-created objects are read-only until a later decision. Copy semantics
  must say whether forks/xattrs follow the data.
- **Follow-up:** T-065 (VFS conformance), T-085 (migration). The owner confirms
  the symlink/hard-link creation scope.

## D-33 — Library index architecture

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §12, §28.4.1; D-07; [`competitive-roadmap.md`](competitive-roadmap.md) §5.3.
- **Context:** Rockbox builds a database on the device, can load it to RAM, and
  has a 1,800-byte metadata budget with a 500-byte per-item limit on the Classic
  (manual Appendix B.2.3). D-07 is unresolved between a host builder and an
  on-device builder. A 10k-track library needs a bounded, resumable index.
- **Options:** (a) SQLite on device; (b) a compact host-built immutable
  generation plus an on-device delta log; (c) a flat tagcache rebuild on every
  change.
- **Proposed direction:** option (b). The host builder owns the full scan,
  normalization, artwork references and optional descriptions. The device owns a
  versioned delta log for adds, deletes, renames, ratings and play state. The
  index uses a string pool and typed columns, is validated on mount and is
  swapped atomically. The runtime keeps a bounded page cache, not the whole
  library in RAM. A stale or corrupt generation falls back to a catalog scan and
  reports `DB_STALE`; it never blocks playback.
- **Consequences:** two builders must agree; schema migration is a release
  concern; the host app becomes part of the library path. The database remains a
  cache, not the source of truth.
- **Follow-up:** T-066 (host builder), T-067 (device builder), T-083 (desktop
  app). D-07 still decides the v1/v1.x sequencing and the device-only path.

## D-34 — Codec and container tiers

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §13, §28.3.8; Rockbox manual Appendix B; [`audio-formats.md`](audio-formats.md).
- **Context:** Rockbox documents 36 audio format families for the iPod Classic
  in Appendix B.1 and Opus elsewhere in the manual, for 37 total. The owner
  requires Helikon-OS to support at least all of them, and more only where the
  6G hardware supports it. Matching them
  all at v1 would consume CPU, code size, battery and test time and would create
  a “supports everything” claim the evidence cannot carry.
- **Options:** (a) support only a small v1 subset and never claim parity;
  (b) stage the work: Tier 1 core at v1, full Rockbox audio parity at v1.x, and
  a measured Tier 3 for anything beyond Rockbox; (c) attempt every family at
  v1.
- **Proposed direction:** option (b). **Tier 1 (v1, eight families):** MP3,
  AAC-LC, ALAC, FLAC, WAV, AIFF, Ogg Vorbis and Opus. **Tier 2 (v1.x, hard
  parity requirement):** every remaining audio family in Rockbox manual
  Appendix B.1 plus Opus — AC3, ADX, AAC-HE/HEv2, MP1/MP2, Musepack, ATRAC3,
  RealAudio Cook, Speex, VOX, WMA Standard/Professional, DSD, APE, AU/SND,
  Shorten, TTA, W64, WavPack, Atari, MMF, GBS, AY, HES, KSS, SGC, VGM/VGZ, MOD,
  NSF/NSFE, SAP, SID and SPC. **Tier 3 (beyond Rockbox, measurement-gated):**
  24-bit/44.1 or 24-bit/48 PCM output, 24-bit lossless without a 16-bit downmix,
  chained Ogg, CAF, WMA Lossless, WMA Voice, MIDI and other tracker aliases only
  after T-098 and T-102. Video and recording are not audio formats and are out
  of scope. A codec licence and patent review precedes release; no “plays
  everything” or hi-res claim without the corpus.
- **Consequences:** the v1 matrix says exactly what plays; the v1.x release
  notes must list any missing Tier 2 family as a release gap. APE above
  `-c1000` is not real-time on the 6G in Rockbox’s own 2014 data and must be
  labelled best-effort. DSD and >48 kHz sources are decoded and converted to
  PCM; native DSD or native 24/96 output is not claimed. Codec modules obey the
  size budgets in §28.2.6 and the 512 KiB codec workspace.
- **Follow-up:** T-068 (Tier 1), T-069 (Tier 2 parity), T-096–T-102. The owner
  confirms the Tier 1 list, the Tier 2 parity requirement and the licence
  boundary. The detailed inventory is in [`audio-formats.md`](audio-formats.md).

## D-35 — Sound-processing scope

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §13, §28.3.8; Rockbox manual §6.
- **Context:** Rockbox has a parametric EQ, crossfeed, dither, timestretch, Haas
  surround, perceptual bass, auditory-fatigue reduction and a compressor. All of
  them cost CPU, battery and test time; some are subtle and easy to get wrong.
- **Options:** (a) match the full DSP suite at v1; (b) core controls at v1 and
  advanced effects at v1.x, off by default and measured; (c) no DSP beyond
  volume.
- **Proposed direction:** option (b). **Core v1:** volume limit, balance, a
  bounded EQ, ReplayGain (track/album, prevent clipping, pre-amp), crossfade,
  gapless and headphone-unplug pause. **Advanced v1.x:** crossfeed, compressor,
  timestretch/pitch, dither/noise shaping, Haas, perceptual bass and
  auditory-fatigue reduction. Every effect has a measured CPU and battery cost,
  a bypass default and a documented latency. No effect may raise the output
  above the volume limit or clip silently.
- **Consequences:** v1 does not claim DSP parity. Presets and `.cfg` import are
  v1.x. The audio path needs a fixed-point/float policy and a clipping test.
- **Follow-up:** T-070 (core DSP), T-071 (advanced DSP), T-092 (DAC
  measurements).

## D-36 — Theme package and trust model

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §14, §28.3.4; D-10, D-15.
- **Context:** Rockbox themes are text files with assets and fonts; a theme can
  change visual settings and load many images. Helikon-OS needs a native theme
  format and a Rockbox WPS subset, but must not import arbitrary behaviour or
  unbounded assets.
- **Options:** (a) executable theme packages; (b) data-only declarative packages
  with caps and an optional signature; (c) no theme support.
- **Proposed direction:** option (b). A theme package contains a manifest, a
  declarative layout, fonts and images under `/.helikon/themes/<id>/`. It cannot
  execute code, read arbitrary files or change storage settings. The parser has
  caps on nodes, images, fonts and per-frame work; malformed packages are
  rejected with a reason. WPS compatibility is a separate, documented subset
  that maps tags to Helikon-OS values and shows unsupported tags as visible
  placeholders. The first-party UI remains usable if a theme fails.
- **Consequences:** some Rockbox themes will not load or will render differently.
  Theme assets need a licence review and are installed, not compiled in. A theme
  cannot make the device unsafe.
- **Follow-up:** T-072 (native theme), T-073 (WPS subset and 20-skin corpus).

## D-37 — Accessibility and localization

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §14, §28.3.3; goal 2; Rockbox manual §8.10–§8.11.
- **Context:** Rockbox has voice menus, talk clips, language files and a
  blind-users FAQ reference. Helikon-OS has a typography scale but no voice,
  large-text, high-contrast or localization pipeline. Accessibility is part of
  “cleaner interfaces”, not a post-release extra.
- **Options:** (a) voice support only; (b) voice plus visual accessibility plus
  localization; (c) defer all three.
- **Proposed direction:** option (b), starting in v1.x. **Voice:** spoken menus,
  file/folder talk clips, error announcements and a volume that follows the
  safe limit. **Visual:** large-text mode, high-contrast mode, reduced motion
  and a focus ring that does not rely on colour alone. **Localization:** UTF-8
  translation files, plural and RTL rules, font fallback and a documented
  contributor workflow. English is the first complete locale; no language is
  claimed until a native reviewer checks it.
- **Consequences:** voice files need a TTS licence and a generation workflow;
  large text changes layout and row height; translations add release QA.
- **Follow-up:** T-074 (accessibility), T-075 (localization), T-090 (usability).

## D-38 — USB profile and service mode

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §16.2, §28.4.2; D-09; [`competitive-roadmap.md`](competitive-roadmap.md) §5.6.
- **Context:** Rockbox exposes a writable mass-storage LUN and USB HID/keypad
  modes. D-09 rejects a writable LUN for Helikon-OS because it bypasses the VFS
  and transaction engine. Windows/Linux still need a write path.
- **Options:** (a) read-only LUN only; (b) read-only LUN plus HID plus
  charge-only plus an authenticated framed service mode; (c) writable LUN
  (rejected).
- **Proposed direction:** option (b). The default is a read-only LUN with
  descriptors for model, firmware, filesystem and health. HID provides media
  keys and a keyboard mode. Charge-only is available without enumeration as
  storage. The service mode is a framed file-operation protocol that runs
  through the VFS and transaction engine; it never exposes raw block writes, NOR
  or PMU access. It requires an explicit user opt-in and an authentication
  method to be decided. macOS uses Disk Mode first; Windows/Linux use the
  service mode only after it is tested.
- **Consequences:** the USB stack grows and needs a threat model, fuzzing and a
  recovery path. No Windows/Linux write claim until the service mode passes.
- **Follow-up:** T-076 (composite USB), T-077 (service mode), T-091 (security
  review).

## D-39 — Anti-corruption algorithm and coverage

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §9.4, §28.2.2; D-11.
- **Context:** HFS+ journals metadata but does not checksum file data. Rockbox
  documents no data checksum or scrub. The project has a SHA-256 implementation
  in the bootloader and a write engine; the checksum index design exists but the
  algorithm, coverage and cadence are open.
- **Options:** (a) SHA-256 everywhere; (b) BLAKE3 everywhere; (c) no data
  checksums.
- **Proposed direction:** option (a) for v1, with BLAKE3 reconsidered if a
  measured benchmark shows a meaningful device win. The sidecar lives under
  `/.helikon/checksums`, keyed by CNID, path, size and modification time, with
  two generations and its own checksum. Coverage is tiered: critical files
  (settings, database, playlists, firmware, recovery and the index itself) are
  verified on open; user media is hashed on import, on device write close and on
  desktop transfer; a full scrub runs only while charging and idle. A mismatch is
  `MISMATCH` or `UNREADABLE`; it is quarantined and reported, never rewritten or
  deleted. There is no self-healing claim.
- **Consequences:** hashing costs CPU and I/O; the index consumes space; a
  corrupt index is rebuilt, not trusted. Scrub must not wear the card or delay
  playback.
- **Follow-up:** T-078 (anti-corruption v1). The owner confirms the coverage
  tiers and whether the user may disable scrub.

## D-40 — Performance budgets and measurement

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §19.2, §24, §28.6.
- **Context:** “cleaner”, “faster” and “better RAM” are not claims until they
  have budgets and a measurement method. The loader currently starts in about
  6.5 s; the project targets 2–3 s. The UI has a 30 fps/200 ms goal but no
  recorded trace.
- **Options:** (a) publish aspirational numbers; (b) publish target budgets and
  a measurement method, then replace targets with measured values; (c) no
  numbers until release.
- **Proposed direction:** option (b). Initial **targets, not measurements**:
  loader to firmware handoff ≤2.5 s; firmware to first usable UI ≤3 s; list
  scroll 30 fps; screen open ≤200 ms; track skip ≤150 ms; seek ≤200 ms;
  database query ≤50 ms on 10k tracks; write path measured separately from raw
  device throughput; RAM within the §28.3.2 budget; battery compared in three
  runs. Every number is recorded with image hash, device and adapter.
- **Consequences:** a benchmark harness and a release scorecard are required.
  A missed target is reported, not hidden. Targets can change only by a new
  decision.
- **Follow-up:** T-079 (harness), T-080 (power), T-052 (startup), T-092 (audio).

## D-41 — Power and thermal policy

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §19, §28.3.1; D-12; Rockbox manual §13.5.
- **Context:** Rockbox documents backlight, display-off, accessory power,
  line-out, anti-skip, ReplayGain, codec and DSP settings that affect battery.
  Helikon-OS has a design budget but no measured baseline. Battery claims are
  comparative and easy to overstate.
- **Options:** (a) a fixed power policy with no user controls; (b) explicit power
  states plus measured defaults and advanced controls; (c) copy Rockbox’s
  settings.
- **Proposed direction:** option (b). States: `ACTIVE`, `IDLE`,
  `STORAGE_GATED`, `STORAGE_DEEP`, `SYSTEM_SLEEP`, `OFF`, each with a measured
  entry/exit cost. Aggressive disk sleep during playback, charge-aware scrub and
  index work, thermal throttling, battery calibration and a safe shutdown
  threshold. The public claim is a three-run comparison on the same device,
  media, volume, backlight and output; a target such as 10% longer playback is a
  goal, not a promise. If the measurement fails, the claim is withdrawn.
- **Consequences:** responsiveness and battery trade off; the user may override
  some defaults. Thermal and charging behavior need device tests.
- **Follow-up:** T-080 (power manager and baseline), T-079 (harness).

## D-42 — Adapter compatibility policy

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §7, §28.2.4; goal 6; Rockbox manual §2.1.2.
- **Context:** iFlash adapters are ATA bridges with different sector sizes,
  power-management support and error behaviour. Rockbox documents SD quirks and
  older corruption bugs. Helikon-OS has one outer-volume run and no adapter
  matrix.
- **Options:** (a) treat every ATA device as identical; (b) an identify-data
  quirks table with conservative defaults and a measured matrix; (c) support
  only the owner’s SD adapter.
- **Proposed direction:** option (b). The block layer records identify model,
  firmware, capacity, logical/physical sector size, DMA/PIO mode and flush
  behaviour. Unknown devices use a slow-but-safe path. Quirks stay in the block
  layer, never in HFS+. A known-bad list is published. The matrix covers HDD,
  iFlash SD, iFlash CF and SSD with boot, play, write, sleep/wake and 100-cycle
  resume.
- **Consequences:** more block-layer code and test devices; unknown adapters may
  be slower. No adapter-specific filesystem hacks.
- **Follow-up:** T-081 (quirks and matrix), T-079 (benchmarks).

## D-43 — Podcast and audiobook model

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §28.3.6; goal 19.
- **Context:** the device has no network. Rockbox’s Classic manual has no
  dedicated podcast workflow; it has file playback, resume and speed/pitch
  tools. Helikon-OS has a desktop app in the plan. Podcast state must survive a
  power cut and sync round-trips.
- **Options:** (a) device fetches feeds (rejected: no network); (b) host
  subscribes/downloads and the device plays with a local episode model;
  (c) file playback only.
- **Proposed direction:** option (b). The desktop app subscribes to RSS/Atom,
  downloads episodes, reads show notes, chapters and artwork, and syncs a
  bounded set. The device shows show/episode lists, played state, per-episode
  resume, speed, skip intervals, chapters and show notes. A sidecar JSON under
  `/.helikon/podcasts/` carries state. The device never fetches a feed and never
  uploads listening history.
- **Consequences:** the desktop app is required for subscription; a 500-episode
  library is the test; sync conflicts need a rule.
- **Follow-up:** T-082 (podcast/audiobook), T-083 (desktop app).

## D-44 — App/game runtime and package format

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §25.8, §28.3.9–10; D-08.
- **Context:** Rockbox plugins and games are native binaries in the same system.
  Helikon-OS excludes that model. The runtime is a P2 feature and cannot delay
  writer work.
- **Options:** (a) WebAssembly interpreter without JIT; (b) a small bytecode VM;
  (c) a native signed-code sandbox.
- **Proposed direction:** option (a) as the preferred candidate, subject to a
  measured prototype; option (b) if WASM cannot meet the memory/frame budget;
  option (c) rejected for the first runtime. A package contains a manifest,
  signature, code, assets and requested capabilities. Permissions cover display,
  input, audio, app-private storage and read-only library access; no raw block,
  NOR, PMU, GPIO or network. Quotas cover memory, CPU slice, audio channels and
  save data. A trap or watchdog returns to the home screen. The SDK has a host
  simulator, examples and a capability reference.
- **Consequences:** no Rockbox plugin/game binary compatibility; a smaller
  ecosystem at launch; the runtime is optional and can be built out.
- **Follow-up:** T-084 (runtime and SDK). The owner must accept the runtime
  before implementation.

## D-45 — Migration and coexistence

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §22, §28.4.3; goal 10.
- **Context:** users arrive with an Apple-managed library, a Rockbox install, a
  donor overlay or a mix. Helikon-OS must not destroy the old state to prove it
  is native. The donor overlay is migration material, not a runtime format.
- **Options:** (a) erase and start clean; (b) host-side import with verify and
  rollback; (c) read the old formats on device.
- **Proposed direction:** option (b). The host importer reads the source
  read-only, maps playlists, ratings, settings and database state where the
  semantics are known, writes native HFS+ files through the transaction engine,
  verifies counts/hashes, and keeps the source untouched until the user
  confirms. `.rockbox`, Apple’s database and the donor overlay are preserved as
  files, not mounted. Unsupported state is reported, not guessed. A rollback
  restores the pre-migration tree.
- **Consequences:** some ratings/settings cannot map exactly; the importer is a
  separate product path; the device never reads the old container at runtime.
- **Follow-up:** T-085 (migration), T-083 (desktop app).

## D-46 — Release, support and governance

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §18, §28.4.4; D-18, D-20.
- **Context:** Rockbox has release, current and daily builds and a long community
  history. Helikon-OS has one bootloader-only Alpha tag, no CI, no reproducible
  release and no support window. “Regular updates” is a README goal, not a
  mechanism.
- **Options:** (a) ad-hoc releases from the owner’s machine; (b) a signed,
  reproducible train with stable/beta/development channels, an LTS line and a
  published support window; (c) no formal releases until v2.
- **Proposed direction:** option (b). Every release has a signed manifest,
  source tag, build recipe, hashes, size/RAM report, changelog and known-issues
  list. Bootloader changes are DFU-only. A security response process and a
  deprecation policy are published. Governance stays owner-led until a
  contributor process exists; decisions and evidence remain public. No telemetry
  is used to measure adoption.
- **Consequences:** release engineering is real work; key custody and CI become
  project infrastructure. A missed support window is reported.
- **Follow-up:** T-086 (release train), T-053 (certificate), T-091 (security).

## D-47 — Porting contract and hardware matrix

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §28.4.5; D-13.
- **Context:** the README asks for a wide range of iPods and maybe other players.
  Rockbox supports several iPod models; Helikon-OS has one 6G and no 7G board
  audit. A port without hardware, documentation and a maintainer is a support
  claim waiting to fail.
- **Options:** (a) add targets by `#ifdef` as hardware appears; (b) a board
  description plus a HAL contract, a per-target matrix and a named maintainer;
  (c) 6G only forever.
- **Proposed direction:** option (b). A port provides a board description (SoC,
  RAM, panel, codec, storage, boot ROM/DFU, button map, `MOVE_AREA`), a BSP
  implementation and a maintainer with hardware. The shared core has no target
  `#ifdef`; differences live behind the HAL. The 7G port waits for owner
  hardware and a board audit. No target is `supported` without a recorded
  matrix.
- **Consequences:** ports are slower to add but testable; 7G remains deferred;
  other players are exploratory.
- **Follow-up:** T-087 (HAL contract and 7G readiness), T-081 (adapter matrix).

## D-48 — Privacy, telemetry and network policy

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §19.3, §28.3.9.
- **Context:** a local music player does not need a network stack. The desktop
  app may scrobble or fetch podcasts, and support needs logs. A vague privacy
  policy becomes an accidental data path.
- **Options:** (a) optional telemetry and cloud sync; (b) no network or telemetry
  on device, host-side opt-in features, local logs only; (c) no logs and no
  diagnostics.
- **Proposed direction:** option (b). The firmware has no network stack and
  sends no telemetry. Play history, ratings and crash logs stay on the device or
  the user’s computer. The desktop app performs network work only for explicit
  user actions (podcast download, opt-in scrobbling) and shows what will be
  sent. Scrobbling is off by default. Support logs are exported manually and
  reviewed for personal data before sharing. No unique device identifier is
  published.
- **Consequences:** support relies on user-provided evidence; there is no
  adoption metric. The service mode must not become a covert network path.
- **Follow-up:** T-091 (privacy and security review), T-083 (desktop app).

## D-49 — Decoder provenance and licensing

- **Status:** proposed. **Owner:** owner/legal. **Date:** 2026-11-05. **Paper:** §13, §18; [`audio-formats.md`](audio-formats.md).
- **Context:** Helikon-OS is GPL-2.0-or-later. Rockbox is GPL-2.0-or-later and
  has working decoders for every format in its manual. Some upstream libraries
  are BSD, LGPL or GPL; some codecs have patent or distribution questions. A
  decoder copied without provenance is a licence and maintenance risk.
- **Options:** (a) port Rockbox decoders with attribution; (b) use maintained
  upstream libraries where they exist; (c) write every decoder from scratch.
- **Proposed direction:** option (b) first for maintained, licence-compatible
  libraries; option (a) where no suitable library exists or the Rockbox decoder
  is smaller/better for the 6G; option (c) only for a small container or PCM
  parser. Every decoder gets an entry in the provenance matrix: name, version,
  source URL/commit, licence, modifications, patent note, owner and test
  corpus. GPL obligations and Rockbox attribution are preserved. No proprietary
  blob, no code with an incompatible licence, no decoder without a test file.
- **Consequences:** the firmware may contain a mix of GPL and BSD/LGPL
  components; the licence file and source offer must list them. A decoder
  upgrade is a normal maintenance task. A patent-sensitive codec may be
  disabled in a release build.
- **Follow-up:** T-096 (provenance matrix), T-068/T-069 (decoders), T-091
  (release/security review). The owner/legal reviewer confirms the licence and
  patent boundary before a release.

## D-50 — Audio output ceiling and resampling

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §13, §28.3.8; [`audio-formats.md`](audio-formats.md) §2.
- **Context:** the 6G uses a Cirrus Logic CS42L55, a 24-bit, 48 kHz-class
  stereo codec. Apple documents the stock audio format list but not the internal
  output width. Rockbox plays high-resolution files by downmixing/downsampling
  in places. The owner wants “more than Rockbox if the iPod supports it”.
- **Options:** (a) fixed 16-bit/44.1 output; (b) 16-bit/44.1 or 48 with native
  rate selection; (c) attempt 24-bit/48; (d) attempt 24/96.
- **Proposed direction:** measure before claiming. **Default:** native 44.1 or
  48 kHz, 16-bit until T-098 proves a clean 24-bit path. **If T-098 passes:**
  24-bit/44.1 and 24-bit/48 output for 24-bit lossless sources, with a
  documented dither policy for 16-bit. **Above 48 kHz:** decode and resample to
  48 or 44.1 with a documented polyphase filter; never truncate. **DSD:**
  convert to PCM with a documented low-pass filter; no native DSD claim.
  **24/96 or 24/192:** compatibility playback through the resampler, not
  “hi-res output”. Non-stock overclocking is not a supported path.
- **Consequences:** no “24/96” or “hi-res” marketing claim on the stock 6G.
  The UI shows source and output rate/bit depth separately. The resampler costs
  CPU and battery and must be measured. A 24-bit path that adds noise is
  rejected.
- **Follow-up:** T-098 (ceiling measurement), T-101 (CPU/battery), T-102
  (Tier 3 candidates), T-092 (line-out measurements).

## D-51 — Audio metadata, artwork and gapless policy

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-05. **Paper:** §9.4, §28.3.8; D-06, D-33; [`audio-formats.md`](audio-formats.md) §5.5.
- **Context:** Rockbox supports ID3, APE, Vorbis, MP4, ASF and codec-specific
  tags, but the Classic has a 1,800-byte total / 500-byte item limit, only one
  tag type per extension, no multi-value tags and no normalization. Album art
  is limited to certain JPEG/BMP cases. Helikon-OS has a host-built index and
  HFS+ Unicode normalization, so it can do better.
- **Options:** (a) match Rockbox’s tag limits; (b) support the same tag families
  with larger bounded values, multi-value fields and normalized names;
  (c) support only ID3.
- **Proposed direction:** option (b). Support every tag family Rockbox lists
  (ID3v1/v2.2/2.3/2.4, APE v1/v2, Vorbis, MP4, ASF and codec-specific), plus
  multi-value fields, Unicode normalization, ReplayGain 1.0/2.0, cuesheets,
  chapters and embedded/external JPEG/PNG/BMP art. The host index carries the
  full values; the decoder uses a bounded working set. Missing/malformed tags
  return `TAG_MISSING`/`TAG_INVALID` and fall back to the filename. Artwork is
  decoded to a bounded thumbnail; a huge/malformed image never stops playback.
  Gapless uses encoder delay/padding where the format provides it; otherwise it
  reports `GAPLESS_UNKNOWN`.
- **Consequences:** metadata parsers need fuzzing; multi-value tags need a
  stable index schema; artwork needs a separate bounded cache. The Rockbox
  byte caps are not copied. Power-cut resume must remain transactional.
- **Follow-up:** T-099 (metadata/artwork), T-100 (gapless/seek/resume), T-067
  (index). The owner confirms the multi-value and artwork storage policy.

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
  ownership was open and is now accepted (D-20, 2026-11-04).
- **Status:** D-18 and D-20 accepted; D-19 rejected. The D-20 policy is defined;
  do not publish release artifacts or signing claims until the certificate
  verifier, a real signed image and a release record exist.
- **Affected docs:** `release.md`, `security.md`, `CONTRIBUTING.md`,
  `SECURITY.md`.

### C-08 — Paper §25 has 17 decisions; this file has 51

- **What:** the paper's §25 lists 17 open decisions. The documentation task
  requires licensing, code of conduct and signing-key ownership as well.
- **Resolution:** D-01–D-17 mirror §25; D-18–D-20 are added from §18 and
  §28.4.4; D-21–D-26 record the diagnostic, splash, outer-volume and bootloader
  recovery decisions; D-27–D-28 record the firmware memory map and entry ABI;
  D-29–D-48 are the proposed competitive-horizon decisions added 2026-11-05;
  D-49–D-51 are the audio-decoder, output-ceiling and metadata decisions added
  in the same session. D-18 and D-20 are accepted; D-19 was rejected after the
  owner deleted the code of conduct; D-29–D-51 are `proposed`. This is a
  documentation reconciliation, not a new product decision.
- **Affected docs:** this file, `agents.md`, `goals.md`,
  `competitive-roadmap.md`.

### C-09 — Paper suggests permissive docs license; repository uses GPL-2.0-or-later

- **What:** the paper suggests documentation under a permissive documentation
  license. D-18 chooses GPL-2.0-or-later for the whole repository, including
  docs, to match Rockbox/donor compatibility and keep one license.
- **Status:** resolved by the owner-authorized D-18 choice; a separate
  permissive docs license would be a new owner decision.
- **Affected docs:** `LICENSE`, `NOTICE`, `CONTRIBUTING.md`, `docs/README.md`.

### C-10 — D-02 previous-GOOD wording vs one image path

- **What:** D-02 originally said the bootloader selects the previous `GOOD` slot
  after a failed newest slot, but `LOADER.md` defines only one manifest/image
  path. A previous slot cannot name a different image file.
- **Resolution (owner, 2026-11-04):** the single-image, fail-closed model is
  accepted. There is no A/B fallback. A failed or mismatched slot produces a
  recovery screen and no jump; recovery is through DFU/Apple restore. T-050 is
  closed as an explicit no-A/B decision.
- **Affected docs:** `bootloader/LOADER.md`, `bootloader/LOADER_TEST.md`,
  `docs/state.md`, `docs/tasks.md`, `docs/architecture.md`.

### C-11 — “Strong alternative in every aspect” vs explicit declines

- **What:** the owner wants Helikon-OS to be a strong alternative in every
  aspect and better in several. A literal reading would require every Rockbox
  codec, plugin, theme tag, language and device. The project also has an
  explicit decline list (FAT32, full WPS, native plugins, video, chiptune,
  writable USB LUN, self-healing and others).
- **Why it is not a bug:** “every aspect” is a product ambition, not an
  unbounded feature list. Matching every Rockbox plugin would conflict with the
  lightweight, safety and sandbox goals. The competitive matrix defines the
  finite P0/P1 use cases; the declines are deliberate tradeoffs, not forgotten
  work.
- **Status:** open owner confirmation. D-29 is proposed; the v1 threshold and
  the decline list are not release gates until the owner accepts or narrows
  them. Until then, documentation may say “competitive roadmap” but must not
  say “matches Rockbox” or “better than Rockbox”.
- **Affected docs:** `competitive-roadmap.md`, `roadmap.md`, `tasks.md`,
  `goals.md`, `faq.md`, `validation.md`.

## Adding a decision

1. Add an index row and an entry with ID, title, status, owner, date and paper
   section.
2. State context, options, decision (or "unresolved"), consequences and
   follow-up.
3. If unresolved, use `proposed`; do not describe a recommendation as accepted.
4. Link affected docs and update `state.md` if the decision changes a blocker.
5. Use `docs/adr/` only when the entry would exceed a short-form record.
