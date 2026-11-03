# Decisions — settled choices and open decisions

> Purpose: why the design is what it is, and what is still undecided.
> Status: living document
> Owner: project owner
> Last updated: 2026-11-03
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
| D-01 | Boot image verification | proposed | owner | §25.1, §5.3 |
| D-02 | Boot state and fallback | proposed | owner | §25.2, §5.3 |
| D-03 | Journal strategy | proposed | owner | §25.3, §9.3 |
| D-04 | Journal ownership | proposed | owner | §25.4, §9.3 |
| D-05 | B-tree cache and transaction limits | proposed | owner | §25.5, §19.2 |
| D-06 | Unicode policy | proposed | owner | §25.6, §10.4 |
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
| D-18 | Licensing | proposed | owner/legal | §18, §28.4.4 |
| D-19 | Code of conduct | proposed | owner | §18, §28.4.4 |
| D-20 | Signing-key ownership | proposed | owner | §25.1, §16, §28.2.5 |

The paper's §25 lists 17 decisions. D-18, D-19 and D-20 are added from §18 and
§28.4.4 because the task requires them and the paper leaves them open. See C-08.

## D-01 — Boot image verification

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.1, §5.3.
- **Context:** the donor bootloader trusts a fixed-path image. Helikon-OS wants a
  manifest with magic, board ID, version, load address, size, SHA-256 and a
  signature before jumping.
- **Options:** (a) Ed25519/ECDSA-style signed manifest with a project key;
  (b) hash-only integrity for development; (c) hardware-backed key; (d) no
  signature, relying on the update channel.
- **Decision:** unresolved. The paper requires signature verification for the
  new bootloader; the algorithm, key storage, rotation and lost-key recovery are
  not chosen.
- **Consequences:** no release firmware until this is settled. A lost key must
  not brick a device; the Apple/DFU recovery path must remain available.
- **Follow-up:** owner chooses scheme and recovery model; link to D-20.

## D-02 — Boot state and fallback

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.2, §5.3.
- **Context:** the bootloader needs to know whether the last firmware boot
  passed, and which image to load after a failure.
- **Options:** (a) two-slot checksummed `/.helikon/bootstate` native file, read
  only by the bootloader; (b) button-only recovery with no state file;
  (c) a NOR variable or hidden container.
- **Decision:** unresolved. The paper prefers a two-slot native file read
  read-only by the bootloader and written only through the HFS+ transaction
  engine. It is not accepted.
- **Consequences:** the state file must never be a bootloader write path. If it
  is missing/corrupt, the bootloader falls back to the default image and
  button-selected recovery.
- **Follow-up:** define who may clear the state and how recovery is displayed.

## D-03 — Journal strategy

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.3, §9.3.
- **Context:** HFS+ volumes are normally journaled. Writing a journaled volume
  while ignoring the journal is unsafe.
- **Options:** (a) journaled HFS+ writes for release: append records with
  sequence numbers/checksums, commit header, apply metadata, replay incomplete
  transactions, mark clean; (b) non-journaled writes only on a volume formatted
  without a journal, with journaled volumes mounted read-only.
- **Decision:** unresolved. The paper recommends (a) for any volume with music
  the user cares about and allows (b) only as a disposable-volume prototype.
- **Consequences:** no write release without a journaled path, unless the owner
  explicitly narrows the product to a read-only player. A non-journaled writer
  must be labelled prototype-only and must never touch a real library.
- **Follow-up:** decide before phase 2 starts. This is a critical-path blocker.

## D-04 — Journal ownership

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.4, §9.3.
- **Context:** after a crash, a dirty journal must be replayed. Either the device
  replays it, or it refuses writes and requires macOS to replay it.
- **Options:** (a) Helikon-OS owns replay and completes it before the first
  write; (b) read-only mount, refuse writes, require macOS Disk Utility/fsck to
  replay; (c) hybrid: device replay only for a known-clean journal format.
- **Decision:** unresolved. The paper says (a) is required for a standalone
  player; (b) is a safer first prototype.
- **Consequences:** (b) makes Helikon-OS a read-only player with a macOS-assisted
  write mode. (a) is a core writer requirement. Do not silently mix them.
- **Follow-up:** decide with D-03 before phase 2; define the `JOURNAL_DIRTY`
  screen and recovery instructions.

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

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §25.6, §10.4.
- **Context:** HFS+ stores decomposed UTF-16 and uses Apple's case-folding
  comparison. The donor's exact-byte lookup is a read-only behaviour, not a
  writable policy.
- **Options:** (a) implement Apple `FastUnicodeCompare` and decompose names
  before insertion; (b) exact-byte plus explicit refusal of names that could
  collide after normalization; (c) a limited normalization table with a
  documented rejection path.
- **Decision:** unresolved. The writer must reproduce Apple's catalog key order
  and test composed/decomposed and case variants against macOS-created names.
- **Consequences:** a wrong policy creates duplicate keys or failed lookups that
  `fsck_hfs` may flag. The policy must be documented and tested, not inferred.
- **Follow-up:** choose table source/licence and fixture corpus; link to D-10
  for display names.

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
  release assets.

## D-18 — Licensing

- **Status:** proposed. **Owner:** owner/legal. **Date:** 2026-11-03. **Paper:** §18, §28.4.4.
- **Context:** the README asks for an open-source, community-friendly project.
  The paper says firmware/bootloader should be GPL-compatible with Rockbox
  attribution, host tools under a compatible license, and docs under a
  permissive documentation license.
- **Options:** GPL-compatible firmware license; permissive host-tool license;
  documentation license; contribution terms; trademark policy.
- **Decision:** unresolved. No license text has been chosen.
- **Consequences:** do not add `LICENSE`; do not assume a license for
  contributions; do not copy donor code or assets until compatibility is
  reviewed. The repository is public but not yet licensed for reuse.
- **Follow-up:** owner/legal chooses and records the license; update
  `CONTRIBUTING.md` and release notes.

## D-19 — Code of conduct

- **Status:** proposed. **Owner:** owner. **Date:** 2026-11-03. **Paper:** §18, §28.4.4.
- **Context:** the paper lists a code of conduct and security policy as
  community infrastructure. No text has been chosen.
- **Options:** adopt a standard code; write a project-specific code; defer until
  community growth.
- **Decision:** unresolved. Do not add `CODE_OF_CONDUCT.md` until the owner
  chooses the text and enforcement contact.
- **Consequences:** contribution and moderation expectations are not yet
  published. `CONTRIBUTING.md` must not imply a code that does not exist.
- **Follow-up:** owner chooses text/contact; link from `CONTRIBUTING.md` and
  `SECURITY.md`.

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
  emergency recovery; link to D-01 and `release.md`.

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

### C-07 — README updates/community vs open license/signing policy

- **What:** the README asks for regular updates and a community-friendly
  project; the paper requires signed manifests, but licensing and signing-key
  ownership are unresolved.
- **Status:** open D-18 and D-20. Do not add a license or release artifacts.
- **Affected docs:** `release.md`, `security.md`, `CONTRIBUTING.md`,
  `SECURITY.md`.

### C-08 — Paper §25 has 17 decisions; this file has 20

- **What:** the paper's §25 lists 17 open decisions. The documentation task
  requires licensing, code of conduct and signing-key ownership as well.
- **Resolution:** D-01–D-17 mirror §25; D-18–D-20 are added from §18 and
  §28.4.4 and marked proposed. This is a documentation reconciliation, not a new
  product decision.
- **Affected docs:** this file, `agents.md`, `goals.md`.

## Adding a decision

1. Add an index row and an entry with ID, title, status, owner, date and paper
   section.
2. State context, options, decision (or "unresolved"), consequences and
   follow-up.
3. If unresolved, use `proposed`; do not describe a recommendation as accepted.
4. Link affected docs and update `state.md` if the decision changes a blocker.
5. Use `docs/adr/` only when the entry would exceed a short-form record.
