# Release and update process

> Purpose: channels, signing, staging, rollback, bootloader DFU and support
> policy. This is a stub: several owner decisions are pending.
> Status: stub — owner and TODOs below
> Owner: release lead; project owner for signing/support decisions
> Last updated: 2026-11-03
> Source: design paper §16, §18, §28.2.5, §28.4.3; `docs/validation.md`

**No release exists.** There is no firmware image, bootloader image, installer,
manifest, signing key, release channel or support window. Do not publish release
artifacts or a version claim from this repository.

## 1. Channels

| Channel | Audience | Required gates |
|---|---|---|
| Stable | General users | All P0 gates in `validation.md`; recorded device run; `fsck_hfs` zero repairs |
| Beta | Testers who accept risk | P0 safety gates that apply; known limitations listed; no hidden stress gate |
| Development | Developers/test rigs | Build/test evidence; disposable volume only for writes until phase 3 |

Channel names are design labels. The cadence is not chosen. "Regular updates"
means a release train that can support a monthly or quarterly cadence without a
new installer design, not a promise of a specific date.

## 2. Signed manifests

Design requirements; none implemented. The intended manifest fields (paper
§16/§28.2.5):

```text
magic, board, version, load address, size, SHA-256, signature
```

The bootloader verifies before jumping. A bad magic, board, version, load
address, size, hash or signature is rejected; the previous verified image stays
bootable.

**Status:** D-01 accepts Ed25519 signed manifests and D-20/ADR-0002 accepts an
offline Ed25519 root on two hardware tokens plus a certified release key, with
rotation, revocation and lost-key recovery. No release key has been generated
yet; the certificate/revocation verifier is T-053. Never commit a private key,
token, recovery phrase or signing secret.

## 3. Staging and verification

Design flow; none implemented.

1. Firmware image and manifest are written to ordinary native files under
   `/.helikon/update/` through the HFS+ transaction engine.
2. Flush/barrier, read back and verify size/hash.
3. The user explicitly chooses "install update". There is no silent install.
4. The bootloader reads the manifest/image read-only and verifies.
5. The firmware commits a boot-state record through the transaction engine.
6. On failure, the old image remains bootable; recovery offers rollback/DFU.

No writable USB mass-storage LUN. No host write that bypasses the transaction
engine. No update on a dirty/damaged volume.

## 4. Boot state and rollback

The intended boot state is a two-slot checksummed native file
`/.helikon/bootstate`, read-only from the bootloader and written only through the
transaction engine. If missing/corrupt, the bootloader falls back to the default
image and button-selected recovery.

Rollback rules:

- Keep the previous verified image until the new image passes its self-test.
- A failed boot/self-test marks the new slot failed and boots the previous slot.
- Bootloader changes are not staged on HFS+; they recover through DFU.
- The Apple NOR path and DFU recovery are preserved. `--single` is never used.
- No auto-format, no journal deletion, no metadata "repair".

**Status:** D-02 accepts the two-slot read-only boot state. The owner accepts a
single-image fail-closed model with no A/B rollback (T-050 closed); recovery is
DFU/Apple restore. D-16 (update transport) remains open.

## 5. Bootloader updates

- Bootloader changes are **DFU-only**, with their own verification.
- The installer preserves the Apple NOR path and offers Apple restore.
- `--single` is rejected and never exposed.
- A failed bootloader update must remain recoverable through DFU; do not rely on
  a half-written HFS+ file.
- Bootloader size must fit `MOVE_AREA` with measured headroom.

## 6. Support window

Not chosen. The release policy must eventually state:

- supported firmware versions and how long they receive fixes;
- security-response process and disclosure timeline;
- upgrade path from beta to stable and rollback;
- database/settings/checksum schema migration policy;
- device/adapter support status and how it is recorded.

Until the owner records this, do not promise a support window. The license
(GPL-2.0-or-later) is settled; no code of conduct is currently adopted.
Signing-key ownership is accepted (D-20); support terms are not. See D-20 and
[`adr/0002-signing-key-ownership.md`](adr/0002-signing-key-ownership.md).

## 7. Release-note requirements

Every release note must state:

- a unique user defined codename
- version, date, commit/image hash and manifest hash;
- update type: firmware only, bootloader only, or both;
- channels and supported devices/adapters (with `device-matrix.md` evidence);
- what changed and what did not;
- known limitations and unproven goals;
- `fsck_hfs`/validation results, or "not run";
- rollback/recovery instructions;
- whether the release is signed and by which certified release key ID.

No marketing claims. No battery/RAM/frame-time number without method and runs. No
"host tests passed" presented as hardware validation. No hidden unexecuted gate.

## 8. Release checklist

- [ ] `validation.md` P0 gates have evidence.
- [ ] `fsck_hfs` zero repairs after the power-cut matrix.
- [ ] Recorded device run with exact model/revision/adapter/firmware hash.
- [ ] Bad board/version/hash/size/truncated image rejected.
- [ ] Power cut during staging leaves the old image bootable.
- [ ] Boot state/rollback tested.
- [ ] Bootloader DFU path tested; `--single` rejected; Apple path intact.
- [ ] Backup/recovery rehearsal recorded.
- [ ] No secrets, disk images, backups or personal data in artifacts.
- [ ] Signing, support-window and key-ownership decisions recorded; the release
      is GPL-2.0-or-later.

## 9. Open items and completed follow-ups

- **TODO (owner/release):** generate the first offline root/release keys and
  record fingerprints and certificate validity; implement the certificate and
  revocation verifier (T-053).
- **TODO (owner):** choose update transport (D-16) and support window.
- **DONE:** license (GPL-2.0-or-later) is adopted; keep `LICENSE` and `NOTICE`
  current. No code of conduct is currently adopted (D-19 rejected).
- **TODO (release lead):** define release branches/tags and reproducible build.
- **TODO (test lead):** define the release evidence bundle and archive location.
- **TODO (docs lead):** add the release-note template when a release exists.

## 10. Related

- [`validation.md`](validation.md) — release matrix
- [`security.md`](security.md) — secrets and signing
- [`architecture.md`](architecture.md) — update/recovery flow
- [`decisions.md`](decisions.md) — D-01, D-02, D-16, D-18, D-20 (D-19 rejected)
- Paper §16, §28.2.5, §28.4.3
