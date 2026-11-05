# Helikon Companion — macOS desktop app

> Purpose: the macOS-first companion for a native-HFS+ iPod or a Helikon-OS
> development volume.
> Status: `in progress` — host MVP builds and its core tests pass; no installer,
> no signed firmware, no device acceptance run.
> Owner: host/product
> Last updated: 2026-11-05
> Source: paper §28.4.2, goals 19/20, D-09, D-43; owner brief 2026-11-05

This is a Swift package for `HelikonCompanion.app`. It is a separate host product,
not part of the firmware. It reads a mounted HFS+ volume in place, plans every
write before copying, and uses macOS HFS+ support for the actual host-side copy.
It does not expose a writable USB LUN, raw block access or a second filesystem.

## Build and run

```sh
cd tools/desktop

# Core tests (21 tests: transfer, scrobble, podcast RSS, library, ID3)
swift test

# Release app bundle in tools/desktop/dist/
./scripts/build-app.sh

# Debug build, open immediately
./scripts/build-app.sh --debug --open

# Developer preview with the fictional sample library
open "dist/Helikon Companion.app" --args --demo --section library
```

Requirements: macOS 14 or newer, Xcode command-line tools with Swift 6. The app
has no third-party package dependencies. `scripts/make-icon.py` regenerates the
source icon and needs Pillow; a committed 1024 px PNG is enough for normal builds.

## Status

| Area | Status | Evidence |
|---|---|---|
| App shell and visual system | `in progress` | `swift build -c release` succeeds; host screenshots reviewed after removing the duplicate title bar, numbered navigation and all-caps labels; no UI test suite |
| HFS+ volume detection | `in progress` | `DeviceDetector` read `diskutil -plist` metadata; a mounted 32 MiB HFS+ test image with `.helikon` was detected as Helikon-OS, read/write; no physical acceptance run |
| Library read/scan | `in progress` | AVFoundation tag scan and JSON library store; 21 core tests pass; large-library timing not measured |
| Metadata search | `in progress` | MusicBrainz recording/release/artist and Apple Search API clients; network integration exercised against live endpoints, but no recorded search matrix |
| Music transfer | `in progress` | plan, streaming copy, SHA-256 verification, backup-before-replace and MP3 ID3 copy test pass; no device transfer run |
| Podcast subscription/download | `in progress` | iTunes directory search, RSS/Atom parser test and download code; no 500-episode test |
| Audiobook search/download | `in progress` | Open Library and LibriVox clients; catalogue and public-domain download code; no large-book test |
| Last.fm scrobbling | `in progress` | offline queue, signature test, token/session/scrobble client; no real Last.fm account or scrobble run |
| Installation guide | `planned` | preflight and checklist only; installer intentionally disabled |
| Windows/Linux | `rejected` for now | not claimed; service mode is not implemented |

The app is not a release. There is no code signing identity, notarization,
sandbox entitlement, update feed or support matrix.

### Interface

- One page header per screen; the old second title bar was removed.
- The sidebar is a plain named list with no ordinal numbers.
- Labels and tags are title case. Uppercase is limited to acronyms such as HFS+,
  MP3, API and SHA-256, plus a volume name supplied by the device.
- Counts and install steps keep their numbers because those numbers are data or
  sequence, not navigation decoration.

## What the app does

### Device desk

- Finds mounted HFS+ volumes through `diskutil`, identifies `/.helikon`,
  `iPod_Control` and `.rockbox`, and shows format, bus, capacity and access mode.
- Never mounts, repairs, formats or partitions a volume. A FAT or read-only
  volume is reported, not converted.
- Supports local staging when no volume is mounted, clearly labelled as such.

### Library and metadata

- Scans a folder or device read-only with AVFoundation. Missing tags are derived
  from file names and shown as a queue.
- Searches MusicBrainz and the Apple Search API by track, album or artist.
  Candidates show source, match score and the fields they would fill.
- Applying a candidate stores the result in the companion library, caches the
  artwork locally, and marks the source. It does not silently rewrite the audio
  file.
- Manual tag editing is available in the inspector. Ratings, album notes and
  artist biographies are kept in the host library.
- The transfer planner indexes selected music and reports how many files still
  need tags before the copy.

### Music transfer

- Plans a copy with source, destination, relative path, size and conflicts.
- Copies through a temporary file in the destination folder and verifies the
  result with SHA-256.
- Backups every replaced file into Application Support and verifies the backup
  before the destination changes.
- Writes edited tags into MP3 **copies** only. MP4, FLAC, Vorbis and other tag
  writers are not implemented; their companion metadata stays in the host
  library.
- Supports skip, replace-with-backup and keep-both conflict policies.

### Podcasts and audiobooks

- Podcast directory search through Apple; RSS/Atom subscriptions are parsed and
  refreshed in the app.
- Episodes are downloaded on the Mac and then planned for transfer. The device is
  never asked to fetch a feed or upload listening history.
- Audiobook catalogue search uses Open Library; public-domain recordings use
  LibriVox. Remote files are downloaded to the Mac first.
- Tested with RSS fixtures, not with a 500-episode library or a real device.

### Scrobbler

- Listening history is an offline queue in the companion library.
- Last.fm is opt-in and needs a user-supplied API key/secret. The secret and
  session key are stored in the macOS Keychain, never in the repository.
- Imports Rockbox `.scrobbler.log` and JSON play-history files; reads
  `.scrobbler.log` or `.helikon/playhistory.json` from a mounted volume without
  writing to it.
- Sends only when the user presses **Send queue**; no background timer.

### Install guide

- Shows the release path, a device preflight and a verified-backup action.
- The install button is absent because no signed firmware or production
  bootloader exists. The app does not run `--single`, format, erase, restore,
  repartition or delete a journal.

## Safety boundary

- A write is only a file copy inside the destination folder chosen by the user.
- No writable USB mass-storage LUN; no raw block device, NOR, PMU or GPIO access.
- A newer file is never silently overwritten: conflicts are shown and a replace
  is backed up and verified first.
- A failed copy leaves the destination's previous file in place until the
  replacement is verified.
- No metadata is written to the device database yet. That needs the Helikon-OS
  database schema and a separate decision.
- The app never repairs a filesystem. If macOS reports a dirty or damaged volume,
  stop and use the documented backup/DFU path.

## Architecture

```text
Sources/CompanionCore/        testable models and services
  Models.swift               library, device, podcast, audiobook, transfer, metadata
  DeviceDetector.swift       mounted HFS+ volume inspection
  LibraryScanner.swift       AVFoundation tag read
  LibraryRepository.swift    JSON library store and merge/apply operations
  MetadataService.swift      MusicBrainz + Apple search and artwork cache
  PodcastService.swift       Apple directory, RSS parser, episode download
  AudiobookService.swift     Open Library + LibriVox search/download
  LastFMClient.swift         signature, auth, scrobble, Keychain, log import
  TransferService.swift      plan, streamed copy, verification, backup
  ID3TagWriter.swift         ID3v2.3 writer for MP3 copies
  Support.swift              install guide, sample library

Sources/HelikonCompanion/    SwiftUI app
  App/                       app entry, app model, root/sidebar
  Design/                    palette, type, components
  Features/                  overview, library, transfer, metadata, etc.

Tests/CompanionCoreTests/    21 XCTest cases
scripts/build-app.sh         release/debug .app bundle and ad-hoc signing
scripts/make-icon.py         source icon generator
```

The store is a JSON document for the MVP. The paper's target is a host SQLite
database; moving the repository behind a protocol is the next data-layer step.
The app has no database builder yet.

## Network and privacy

The app has no telemetry and no account. With metadata lookups enabled, it sends
only the search text the user typed to MusicBrainz and/or Apple. Podcast feeds and
episode downloads are explicit actions. Last.fm is off until a session is
connected.

All network work is host-side:
- MusicBrainz: `musicbrainz.org`, with a descriptive User-Agent and a 1 request/s
  guard.
- Apple: `itunes.apple.com` for music, podcast and artist search.
- Open Library: `openlibrary.org`; LibriVox: `librivox.org`.
- Last.fm: `ws.audioscrobbler.com` only after the user supplies credentials and
  presses an action.

No API secret, Last.fm session key or device serial is stored in the repository.
The app does not log serial numbers or library contents.

## Known limitations

- No installer, firmware download or bootloader flow.
- No Helikon-OS device database/tag index build; the device database schema is
  still open (D-07).
- No writable-LUN or service-mode transport; macOS Disk Mode is the only path
  used here.
- No library-wide conflict matrix, 10k-track timing, power-cut or failed-device
  run.
- No M4A/FLAC/Vorbis tag writing; only MP3 copies are updated.
- No app sandbox or notarized distribution. The current bundle is ad-hoc signed.
- Last.fm credentials have not been exercised with a real account.
- Podcast and audiobook results depend on third-party APIs; their terms and
  availability are not a Helikon-OS guarantee.

## What not to claim

- That Helikon-OS has a desktop app release.
- That the installer works or that a signed firmware image exists.
- That a transfer has been validated on an iPod.
- That the app builds a device database or writes native library metadata.
- That Windows/Linux are supported.
- That a metadata match is correct without the user reviewing it.

## Related

- [`docs/companion-app.md`](../../docs/companion-app.md) — product and design record
- [`docs/design-paper.md`](../../docs/design-paper.md) §28.4.2 — desktop app scope
- [`docs/goals.md`](../../docs/goals.md) — goal 20
- [`docs/decisions.md`](../../docs/decisions.md) — D-09, D-38, D-43
- [`docs/testing.md`](../../docs/testing.md) — desktop test layer
