# Helikon Companion — macOS app shell

> Purpose: the macOS companion, reset to a minimal shell after removing its
> speculative feature set.
> Status: `in progress` — app shell only; no data, network, device, transfer or
> installer functionality.
> Owner: host/product
> Last updated: 2026-11-06
> Source: owner reset instruction 2026-11-06; paper §28.4.2, goals 19/20

This is a Swift package for `HelikonCompanion.app`. It is a separate host product,
not part of the firmware. The current build contains a welcome screen and an
empty shell. It does not read or write a volume and does not talk to a device.

## Build and run

```sh
cd tools/desktop

# Build the app bundle in tools/desktop/dist/
./scripts/build-app.sh

# Build a universal arm64 + x86_64 app bundle
./scripts/build-app.sh --universal

# Build the drag-to-Applications DMG (universal)
./scripts/build-dmg.sh --universal

# Debug build, open immediately
./scripts/build-app.sh --debug --open

# Open directly to the empty shell, skipping the welcome screen
open "dist/Helikon Companion.app" --args --skip-splash
```

The DMG is written to `dist/Helikon-Companion-0.1.0-alpha2.dmg`; the script
verifies the ad-hoc signature, checks both architectures, verifies the DMG and
prints its SHA-256. It is not notarized.

Requirements: macOS 14 or newer, Xcode command-line tools with Swift 6. The app
has no third-party package dependencies. `scripts/make-icon.py` regenerates the
source icon and needs Pillow; a committed 1024 px PNG is enough for normal builds.

## Current state

| Area | Status | Evidence |
|---|---|---|
| Welcome screen | `in progress` | Owner banner crop; Open companion only; host screenshot reviewed |
| App shell | `in progress` | Release build and app bundle pass; empty sidebar; no navigation |
| Features | removed | Library, metadata, transfer, podcasts, audiobooks, scrobbler, install and settings code deleted |
| Tests | none | No test target after the reset; previous 21 core tests were removed with their modules |
| Device access | none | The shell does not enumerate or read a volume |
| Network | none | No network client remains |
| Installer | none | No install action exists |

There is no Developer ID, notarization, sandbox entitlement, update feed or
support matrix. The Alpha 2.0.0 DMG is ad-hoc signed only; first launch needs
the standard Gatekeeper right-click-to-open path.

### Interface

- One welcome screen built around the owner-supplied Halftone banner, cropped from
  `assets/branding/helikon-os-poster.png` into
  `Sources/HelikonCompanion/Resources/helikon-os-banner.png` (1600×1054). It is
  not redrawn, recoloured or generated.
- The shell has a brand-only sidebar and a single empty-state page. There are no
  navigation items and no feature buttons.
- No all-caps UI labels; acronyms and device-supplied text are the exception.
- The welcome screen can be reopened from the app menu.

## What was removed

- Device detection, volume inspection, library scanning and local library storage.
- Music/album/artist metadata search and artwork caching.
- Transfer planning, copy verification, backups, MP3 tag writing and the
  transfer queue.
- Podcast and audiobook search, RSS parsing, downloads and transfers.
- Last.fm authentication, scrobbling and play-history import.
- The install guide and preflight checks.
- Settings, activity log and the sample library.
- The `CompanionCore` target and all 21 host tests.

The code remains recoverable in git history; this branch is a product reset, not
a claim that those features never existed.

## Next

The next build starts from the actual Helikon-OS surface, not from the old
feature list. The firmware now has a launcher home, a read-only music library
browser and a HelikonDB catalog. The companion should add capabilities only when
the device side, protocol or file format exists and can be proven.

The first step is to write the capability inventory and decide which of those
surfaces the host can legitimately read without inventing a protocol. No feature
work is included in this reset.

## Architecture

```text
Sources/HelikonCompanion/
  App/           SwiftUI app entry, shell and splash state
  Design/        paper/ink palette, type and the two button styles
  Features/      welcome splash only
  Resources/     owner banner crop

scripts/build-app.sh   release/debug .app bundle and ad-hoc signing
scripts/make-icon.py   source icon generator
```

The app has no `CompanionCore` target and no test target after the reset.

## Safety boundary

- No volume read or write.
- No network request.
- No installer or bootloader action.
- No device database or library metadata write.
- The only runtime state is whether the welcome screen is visible.

## What not to claim

- That the app has library, transfer, metadata, podcast, audiobook, scrobble or
  install functionality.
- That the app has been run against a device in this reset.
- That the shell is a release, notarized or supported.
- That the owner banner was regenerated.

## Related

- [`docs/companion-app.md`](../../docs/companion-app.md) — reset record
- [`docs/tasks.md`](../../docs/tasks.md) — T-083
- [`docs/design-paper.md`](../../docs/design-paper.md) §28.4.2
