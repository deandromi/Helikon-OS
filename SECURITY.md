# Security policy

> Purpose: private vulnerability reporting and safe-disclosure rules.
> Status: living document; no supported release
> Owner: project owner
> Last updated: 2026-11-03
> Source: `docs/security.md`, design paper §19.3/§28.4.4

## Supported versions

**None.** Helikon-OS has no firmware release, bootloader image, installer or
supported version. Do not report a Helikon-OS version as supported.

The donor V3.2 release of `hfsplus-rockboxed-ipod` is a separate product. Report
donor issues to that repository, not here.

## Reporting a vulnerability

Please report privately. Do not open a public issue with exploit details, device
keys, user data, a weaponized image or a step-by-step that could endanger a
device.

1. Use GitHub's private vulnerability reporting/security advisory flow for this
   repository if it is enabled.
2. If it is not available, open a minimal public issue asking the owner for a
   private channel; do not include details, logs with personal data, or a proof
   of concept.
3. Include: affected component/design, version or commit, impact, safe
   reproduction level, and any suggested fix.

The owner will acknowledge and coordinate disclosure. No bounty or response-time
promise is made at this stage.

## What not to publish

- Signing keys, tokens, recovery phrases or key-store exports.
- Device backups, disk images, `/.rockpod-rw` contents or database dumps.
- Serial numbers, UDIDs, Wi-Fi credentials or personal data.
- Proprietary themes/art/fonts or unlicensed assets.
- A destructive proof of concept that could erase a user's library.

## Signing keys

No signing scheme or key owner is chosen (D-01, D-20). Never commit a private key
or embed one in firmware, host tools, CI, docs or tests. A lost key must not brick
a device; Apple/DFU recovery remains available. See
[`docs/security.md`](docs/security.md) and [`docs/release.md`](docs/release.md).

## Scope

In scope: the Helikon-OS design, documentation, future firmware/bootloader, HFS+
writer/journal, update path, sandbox and host tools.

Out of scope for this repository: the donor project, third-party themes/assets,
and vulnerabilities in hardware/Apple firmware that Helikon-OS cannot fix.

## Safe-harbour

Do not test on a device you cannot recover, on an only-copy library, or with
`--single`. A report that requires destructive testing must use a disposable
volume and a verified backup.

## Related

- [`docs/security.md`](docs/security.md) — agent/project security rules
- [`docs/failures.md`](docs/failures.md) — destructive-action warnings
- [`docs/decisions.md`](docs/decisions.md) — D-01, D-08, D-11, D-20
