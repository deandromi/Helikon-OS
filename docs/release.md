# Release and update process

> Purpose: channels, signing, staging, rollback, bootloader DFU and support
> policy. Several owner decisions (support window, update transport, release
> key) are still pending.
> Status: Alpha 2.0.0 development release published; no stable or beta release.
> Owner: release lead; project owner for signing/support decisions
> Last updated: 2026-11-11
> Source: design paper §16, §18, §28.2.5, §28.4.3; `docs/validation.md`;
> `docs/disk-mode.md`

**Alpha 2.0.0 “Launchers and Interfaces”** is published as a normal GitHub
release at tag `alpha-2.0.0`
([GitHub](https://github.com/deandromi/Helikon-OS/releases/tag/alpha-2.0.0)).
It is a **development release**, not a support release: it ships engineering
firmware images, the loader test image and a companion shell preview. The owner
reports 6G testing is complete; the detailed per-image results were not attached
to the repository at cut time. The firmware uses a build-local Ed25519 test key,
not a release key. There is no CI, reproducible release build, signed update
path, installer or support window.

Do not describe Alpha 2.0.0 as stable, supported or production firmware. Do not
claim a release signature, notarization, A/B rollback, audio playback, a working
companion or a device support matrix. The full notes are archived in
[`releases/alpha-2.0.0/RELEASE_NOTES.md`](releases/alpha-2.0.0/RELEASE_NOTES.md).

## 0. Alpha 2.0.0 — Launchers and Interfaces

| Field | Value |
|---|---|
| Release | `Alpha 2.0.0 — Launchers and Interfaces` |
| Tag | `alpha-2.0.0` |
| Date | 2026-11-06 |
| Type | Normal GitHub release; development alpha |
| Update type | Firmware + bootloader + companion preview; manual copy and DFU |
| Firmware | Launcher v10 `b3218ccd…`; Music hub v12 `de6491a9…` |
| Bootloader | `7f13e7e5…` (106,184 bytes); last device-tested remains `6bc6927c…` |
| Companion | `Helikon-Companion-0.1.0-alpha2.dmg`; universal, ad-hoc signed, not notarized |
| Signing | Build-local Ed25519 test key `0x48454c01`; no release key |
| Device evidence | Owner reports 6G testing complete; per-image details not attached; older 6G/iFlash SD runs recorded |
| Not included | Firmware-owned filesystem write, audio playback, VFS, storage-backed library, settings, USB, update client, installer, app runtime, working companion |
| Open evidence | Per-image timings/photos/`fsck_hfs`, other adapters/devices, release key |

The assets are one matched set: loader, DFU installer, uninstaller, two
firmware zips, companion DMG, test public key, quick start, release notes,
license, notice and `SHA256SUMS`. The build-local test key is not a release key.
Use each firmware zip as one set; never mix its image, manifest and bootstate
with another set. Full hashes and install steps are in the release notes and
[`QUICKSTART.md`](releases/alpha-2.0.0/QUICKSTART.md).

## 0.1 Alpha 1.0.1 — Bootloader Only

| Field | Value |
|---|---|
| Release | `Alpha 1.0.1 Bootloader Only - A Quick Start` |
| Tag | `alpha-1.0.1` |
| Date | 2026-11-04 |
| Type | Normal GitHub release; bootloader test image only |
| Image | `bootloader-ipod6g-loader-test.ipod` |
| Image SHA-256 | `6bc6927cb282f7c01b34b07020419171adb9fa61e6ffe1180156b7579b0b2388` |
| Tested | `6bc6927c…`: one 6G/iFlash SD, owner-reported pass, 6.5 s startup, `fsck_hfs -fn` clean |
| Not included | Firmware image, kernel, release key, A/B rollback, recovery menu |
| Open evidence | Repeat/power-cut matrix; other adapters/devices; production firmware handoff and release key |

The Alpha 1.0.1 assets are the tested loader-only set: bootloader,
`firmware.manifest`, 12-byte `firmware.img`, `bootstate`, public test key, DFU
installer, uninstaller and `QUICKSTART.md`.

## 1. Channels

| Channel | Audience | Required gates |
|---|---|---|
| Stable | General users | All P0 gates in `validation.md`; recorded device run; `fsck_hfs` zero repairs |
| Beta | Testers who accept risk | P0 safety gates that apply; known limitations listed; no hidden stress gate |
| Development | Developers/test rigs | Build/test evidence; disposable volume only for writes until phase 3 |

Channel names are design labels. The cadence is not chosen. "Regular updates"
means a release train that can support a monthly or quarterly cadence without a
new installer design, not a promise of a specific date. Alpha 2.0.0 is a
development-channel release with storage-free UI images. It does not include a
firmware-owned filesystem write, a library write or a settings write.

## 2. Signed manifests

Design requirements; the host loader and bootloader test image implement the
manifest fields, but no release key exists. The intended manifest fields (paper
§16/§28.2.5):

```text
magic, board, version, load address, size, SHA-256, signature
```

The bootloader verifies before jumping. A bad magic, board, version, load
address, size, hash or signature is rejected and the loader fails closed to
recovery; there is no A/B previous-image fallback (T-050).

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

No writable USB mass-storage LUN in the normal firmware profile. Helikon Disk
Mode is the only exception: the host owns the volume, the firmware does not mount
HFS+, and the loader still verifies every staged image. No silent install and no
update on a dirty/damaged volume. See [disk-mode.md](disk-mode.md) and D-54.

## 4. Boot state and rollback

The boot state is a two-slot checksummed native file `/.helikon/bootstate`,
read-only from the bootloader and written only through the transaction engine.
If missing/corrupt, the bootloader can fall back to the single installed
verified image and button/DFU recovery.

Owner decision (2026-11-04): **single-image fail-closed, no A/B rollback.** A
failed or mismatched slot shows recovery and does not jump. Recovery is
DFU/Apple restore. Do not claim a previous-image boot.
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

This checklist is the **stable-release** gate. A development alpha may ship with
unexecuted gates only when the release notes disclose every one of them and the
release is not described as supported. Alpha 2.0.0 does that.

## 9. Open items and completed follow-ups

- **TODO (owner/release):** generate the first offline root/release keys and
  record fingerprints and certificate validity; implement the certificate and
  revocation verifier (T-053).
- **TODO (owner):** choose update transport (D-16) and support window.
- **DONE:** license (GPL-2.0-or-later) is adopted; keep `LICENSE` and `NOTICE`
  current. No code of conduct is currently adopted (D-19 rejected).
- **DONE (release lead):** Alpha 2.0.0 uses a short-lived `release/alpha-2.0.0`
  branch, an annotated tag from `main`, a clean release worktree and a draft
  GitHub release verified before publish. A reproducible build is still open.
- **TODO (test lead):** define the release evidence bundle and archive location.
- **DONE (docs lead):** release notes are archived under
  [`releases/`](releases/README.md); use the Alpha 2.0.0 notes as the template.
- **TODO (release lead):** attach the per-image 6G run details (screen photos,
  timings and `fsck_hfs`) for the launcher and Music hub images when the owner
  provides them.

## 10. Related

- [`releases/README.md`](releases/README.md) — archived release notes
- [`releases/alpha-2.0.0/RELEASE_NOTES.md`](releases/alpha-2.0.0/RELEASE_NOTES.md) — current release
- [`validation.md`](validation.md) — release matrix
- [`security.md`](security.md) — secrets and signing
- [`architecture.md`](architecture.md) — update/recovery flow
- [`decisions.md`](decisions.md) — D-01, D-02, D-16, D-18, D-20 (D-19 rejected)
- Paper §16, §28.2.5, §28.4.3
