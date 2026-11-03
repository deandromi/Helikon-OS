# Security — agent and project rules

> Purpose: prevent secrets, data loss and unsafe device operations.
> Status: living document
> Owner: project owner; security lead
> Last updated: 2026-11-03
> Source: design paper §19.3, §20, §28.3.9–10, §28.4.4; root `SECURITY.md`

## 1. Secrets

Never commit, paste into a prompt, or store in this repository:

- signing keys, private keys, tokens, recovery phrases or key-store exports;
- passwords, API keys, Wi-Fi credentials or Last.fm credentials;
- device serial numbers, UDIDs, personal data or user library contents;
- raw device backups, disk images, `/.rockpod-rw` contents or database dumps;
- proprietary theme/art/font files without a recorded licence.

If a secret is exposed, stop, do not rewrite history on your own, and report it
to the owner through the private channel in root `SECURITY.md`. Assume it is
compromised.

## 2. Signing keys and ownership

- No signing scheme, key, custodian, rotation, revocation or escrow policy is
  chosen (D-01, D-20).
- A release process must not depend on one person's laptop or an unpublished key.
- Prefer an offline signing ceremony and a hardware token once the owner defines
  the policy.
- A lost key must not brick a device; Apple/DFU recovery remains available.
- Never embed a private key in firmware, host tools, CI, docs or tests.

## 3. Device data and backups

- A verified backup is mandatory before any write test. "Verified" means hashed
  and read back, not "copied".
- Do not test writes on the only copy of the library.
- Do not commit device backups, disk images or `/.rockpod-rw`.
- Do not include serial numbers or personal data in logs/evidence.
- A migration must be host-side, verify-before-switch, with rollback. The
  firmware does not "repair" the donor overlay.

## 4. Destructive-operation policy

Stop and ask before any operation that could format, erase, restore, repartition,
delete the journal, zero metadata, delete app data or overwrite a newer file.

- No destructive repair as a first response.
- No `--single`.
- No writable USB mass-storage LUN.
- No auto-format/auto-repair.
- No write to a volume `fsck_hfs` reports dirty/damaged.
- If the only evidence is "it boots", do not proceed.

## 5. Network policy

- The device has no network stack and no Wi-Fi credentials. Do not add one as a
  shortcut for podcasts, scrobbling, updates or telemetry.
- Podcast fetching, scrobbling and update downloads happen on the host, opt-in.
- No listening history leaves the host without explicit consent.
- No hidden telemetry or metadata upload.
- Host tools must work with a device that has no network.

## 6. App/game trust model

These are design requirements, not implemented behaviour.

- Legacy Rockbox plugins are excluded: they expose raw hardware and cannot be
  sandboxed.
- Helikon-OS apps/games are a new sandboxed runtime with capabilities, quotas and
  a crash boundary (D-08).
- Permissions: read library, write app-private storage, audio playback, display,
  input, timed background work.
- No permission for raw block/NOR/PMU/GPIO, firmware, or network.
- Packages are signed; the install screen shows the trust decision.
- App-private data lives under `/.helikon/apps/<id>/` and is written through the
  transaction engine.
- Uninstall removes app data only after confirmation.
- A crash/trap returns to the home screen with a log; no kernel panic.
- Adversarial tests are required before third-party apps.

## 7. Firmware and update trust

These are release requirements, not implemented behaviour.

- Bootloader verifies magic, board, version, load address, size, SHA-256 and
  signature before jump.
- Firmware is staged through the transaction engine and verified before install.
- A bad image keeps the previous verified image bootable.
- Bootloader updates are DFU-only and preserve the Apple NOR path.
- No unsigned firmware or plugin execution.
- No debug bypass in a release build.

## 8. Reporting vulnerabilities

Use root [`SECURITY.md`](../SECURITY.md). Do not open a public issue with exploit
details, device keys, user data or a weaponized image. Include the affected
component, version/hash, reproduction steps at a safe level, and impact.

## 9. Agent-specific rules

- Do not run destructive shell commands against a device, volume, backup or
  donor repository.
- Do not clone/modify the donor inside this repository; use a separate read-only
  checkout.
- Do not paste secrets, logs with personal data, or device images into a prompt.
- Do not "fix" a failed test by deleting the journal, formatting, or zeroing
  metadata.
- Do not claim a security property (sandbox, signature, journal) without a test.
- Record security-relevant decisions in `decisions.md`/ADRs.

## 10. Related

- [`../SECURITY.md`](../SECURITY.md) — reporting policy
- [`invariants.md`](invariants.md) — safety invariants
- [`release.md`](release.md) — signing/update
- [`failures.md`](failures.md) — destructive-action warnings
- [`decisions.md`](decisions.md) — D-01, D-08, D-11, D-20
- Paper §19.3, §20, §28.3.9–10
