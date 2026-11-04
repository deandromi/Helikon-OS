# ADR 0002 — Signing-key ownership and release signing

> Purpose: decide who holds the keys that sign Helikon-OS bootloader and firmware
> manifests.
> Status: proposed — no owner decision recorded
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

## Recommendation

Option **A**:

- **Root key:** Ed25519, generated offline, stored on two hardware tokens held by
  the owner in separate locations. Used only for a key ceremony, rotation or
  revocation, not for every release.
- **Release key:** Ed25519, generated on an offline release machine, certified by
  the root key in a small signed release-key manifest. Used to sign firmware and
  bootloader manifests for a bounded validity period.
- **Bootloader:** carries the root public key and accepts a release-key
  certificate plus a firmware manifest. Key IDs and a minimum-version field make
  rotation and revocation explicit.
- **Recovery:** a lost release key is replaced by a new root-signed certificate.
  A lost root key means no new signed firmware, but Apple/DFU recovery remains;
  the device is not bricked by the key loss.
- **No key in Git, CI secrets, chat or backups without an explicit custody
  record.** Public keys and their SHA-256 fingerprints are recorded in the repo.

## Decision

Unresolved — no owner decision recorded. The recommendation above is not
accepted until the owner chooses an option and records custody, rotation,
revocation and recovery.

## Consequences

- D-01 can then freeze the manifest fields and key storage.
- D-16 can choose the update transport; D-20 closes when the owner accepts.
- The bootloader needs a public-key/certificate verifier and tests for bad
  signature, wrong key ID, expired/revoked release key and rollback.
- The release process needs an offline ceremony checklist and a public-key
  fingerprint record.
- This ADR does not promise a release date or a hardware security module.

## Follow-up

- Owner chooses A/B/C/E and names the key custodian(s).
- Record the public-key fingerprints and the release-key validity policy.
- Add verification tests before any signed firmware image.
- Link the accepted ADR from `docs/decisions.md` D-01/D-20 and `docs/release.md`.

## References

- Design paper §16, §25.1, §28.2.5.
- `docs/decisions.md` D-01, D-16, D-20.
- `docs/release.md`, `docs/security.md`.
- `docs/invariants.md` I-10 (unsigned firmware is not booted).
