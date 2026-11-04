# ADR 0002 — Signing-key ownership and release signing

> Purpose: decide who holds the keys that sign Helikon-OS bootloader and firmware
> manifests.
> Status: accepted — owner decision recorded 2026-11-04
> Owner: project owner
> Last updated: 2026-11-04
> Date: 2026-11-04
> Paper section: §16, §25.1, §28.2.5
> Related decisions: D-01, D-16, D-20

## Context

The bootloader must verify a firmware manifest before jumping: magic, board ID,
version, load address, size, SHA-256 and signature (D-01). Release signing needs
key custody, rotation, revocation and recovery (D-20). The paper warns against a
release process that depends on one laptop or an unpublished key. The project is
owner-led, so the scheme must be usable by one owner without making a lost key
unrecoverable or a stolen laptop a firmware-signing oracle.

No private key, token, recovery phrase or signing secret may be committed.

## Options

| Option | Pros | Cons | Safety/data risk |
|---|---|---|---|
| A. Offline root + separate release key | Root can rotate/revoke release keys; release signing can live on an offline release machine or CI with a delegated key; smaller blast radius | Two key roles and a signing ceremony; bootloader must carry root + release public keys or a signed release-key certificate | Moderate complexity; lost root still requires DFU/Apple recovery |
| B. Single offline owner key for bootloader and firmware | Simplest; one custody path | Every release needs the root token; compromise/rotation affects both layers | High blast radius |
| C. HSM/cloud signing service | Auditable, remote access, rotation | Network dependency, legal/custody review, recurring cost, more attack surface | Operational and supply-chain risk |
| D. Hash-only development manifests | Easy while no release exists | No authenticity; unacceptable for a release or a support claim | Rejected for release |
| E. Defer until release | No key to lose now | Blocks D-01/D-20 and any signed release; risks a rushed ceremony later | Keeps P0 open |

## Decision

Option **A** is accepted.

- **Root key:** Ed25519, generated offline, stored on two hardware tokens held by
  the project owner in separate locations. It is used only for a key ceremony,
  release-key certification, rotation or revocation — not for every release.
- **Release key:** Ed25519, generated on an offline release machine and certified
  by the root key in a signed release-key record. It signs firmware and
  bootloader manifests. The certificate records a key ID and a bounded validity
  period; the default is 12 months, shortened at the owner's discretion.
- **Bootloader trust:** the release bootloader carries the root public key and a
  release-key certificate, plus explicit key IDs and a minimum accepted version.
  The current loader test image uses a direct, build-local test-key table and is
  **not release-eligible**; the certificate verifier is still implementation
  work.
- **Rotation:** the root certifies a new release key. The new bootloader image
  carries the new certificate and is installed through DFU. Old and new release
  key IDs may overlap for one release cycle.
- **Revocation:** the root publishes a revocation record; the next bootloader
  update removes the revoked key ID. A manifest signed by a revoked or expired
  key fails closed. No device-side key write is required.
- **Recovery:** a lost release key is replaced by a root-signed certificate. A
  lost root key means no new signed firmware can be produced, but the device is
  not bricked: DFU and Apple restore remain available. The two root tokens are
  the primary loss protection; there is no third-party key escrow.
- **Custody:** private keys and tokens never enter Git, CI secrets, chat or
  unrecorded backups. Public keys, key IDs and SHA-256 fingerprints are recorded
  in the repository and release notes.

The owner is the root custodian. The release signer is the owner or a person
named in the release record; the release record names the key ID and certificate
fingerprint used.

## Consequences

- D-20 is accepted and T-006 is closed as a decision task.
- D-01's manifest key ID can select a certified release key; the certificate and
  revocation verifier are implementation work, tracked separately.
- Release signing remains blocked until a real signed release image and the
  certificate/verification path exist. The build-local test key is not a release
  key.
- The bootloader needs tests for bad signature, wrong key ID, expired/revoked
  release key, rotation overlap and rollback.
- A lost root key does not brick a device, but it can stop future signed updates.
  DFU/Apple restore is the recovery path.

## Follow-up

- Implement the release-key certificate, revocation record and bootloader
  verifier (T-053).
- Record the public-key fingerprints and the first release-key validity period
  when the first release key is generated.
- Add certificate/rotation/revocation tests before any signed firmware release.
- Link this accepted ADR from `docs/decisions.md` D-01/D-20, `docs/release.md`
  and `docs/security.md`.

## References

- Design paper §16, §25.1, §28.2.5.
- `docs/decisions.md` D-01, D-16, D-20.
- `docs/release.md`, `docs/security.md`.
- `docs/invariants.md` I-10 (unsigned firmware is not booted).
