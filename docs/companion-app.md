# Companion app — product and design record

> Purpose: what the macOS Helikon Companion is, what exists today and which
> boundaries a future version must keep.
> Status: living; app MVP `in progress`, installer `planned`
> Owner: host/product
> Last updated: 2026-11-05
> Source: paper §28.4.2, goals 19/20, D-09, D-38, D-43, owner brief
> 2026-11-05; `tools/desktop/`

The companion is a separate host product. It is not the firmware, the
bootloader, or a replacement filesystem. It reads a mounted HFS+ volume in
place and plans writes before it makes them.

## 1. Status

| Item | Status | Evidence |
|---|---|---|
| Native macOS app shell | `in progress` | SwiftPM package and release build pass; host visual review only |
| Welcome screen | `in progress` | Owner banner crop, Open companion and Add music; host screenshot reviewed; no UI automation |
| HFS+ volume detection | `in progress` | `diskutil` metadata and volume markers; a mounted 32 MiB HFS+ test image with `.helikon` was detected as Helikon-OS, read/write; no device acceptance run |
| Library scan and host library | `in progress` | AVFoundation scan; 21 core tests pass |
| Metadata search | `in progress` | MusicBrainz and Apple clients; live API shape checked; no search matrix |
| Music transfer | `in progress` | Plan, streaming copy, SHA-256, backup-before-replace and MP3 ID3 test pass; no device run |
| Podcast subscription/download | `in progress` | RSS/Atom parser fixtures and download code; no 500-episode run |
| Audiobook search/download | `in progress` | Open Library and LibriVox clients; no real-book run |
| Last.fm scrobbling | `in progress` | offline queue and signature test; no real account session |
| Install guide | `planned` | preflight and guide only; the installer is disabled |
| Windows/Linux | `unproven` | none claimed; D-38 service mode not implemented |

The app is not a release: no notarization, no signed distribution, no device
matrix and no installer.

## 2. Product jobs

The owner brief and goal 20 ask for:

- file tagging, album descriptions and ratings;
- Last.fm scrobbling;
- easy installation;
- improved read/write capabilities;
- app, game and skin installation;
- podcast and audiobook metadata search and transfer;
- a useful music transfer mode.

This build covers the host-side parts that can exist before a signed firmware
release. Installation is a guide and preflight only. App/game/skin installation
is not implemented because the package format, sandbox and signed release do not
exist.

## 3. Screen map

| Screen | What it does | Current state |
|---|---|---|
| Welcome | Owner banner, companion introduction, Open companion and Add music | host only; opens at launch |
| Overview | Device status, library counts, attention list, recent activity, album shelf | host only |
| Library | Tracks/albums/artists, search, missing-tag queue, inspector, manual edit | host only |
| Transfer | Drag/drop, plan, conflicts, verification, backup, progress, report | host only; no device run |
| Metadata | Missing-tag queue, manual search, candidates, apply | host only; live APIs |
| Podcasts | Directory search, feed URL, subscriptions, episode download/transfer | host only; no large-library run |
| Audiobooks | Open Library/LibriVox search, shelf, download/transfer | host only |
| Scrobbler | Last.fm connection, queue, import, send | host only; no real account run |
| Install | Readiness checklist, backup action, release path | guide only; disabled |
| Settings | Network, providers, folders, conflict policy, Last.fm, privacy | host only |

### Visual direction

The interface borrows the *editorial composure* of the owner’s reference site,
not its elements: warm paper, deep ink, a serif display face, a system sans for
utility text, thin rules and restrained colour. It does not copy the site’s
layout, icon set, copy, assets or components. Helikon’s own palette from
[`ui.md`](ui.md) §5 is the source for the paper/ink/rules; the six launcher
accents are reused only as small status colours.

The app uses system fonts (New York/Charter-class serif and SF for UI) so it does
not redistribute a proprietary typeface. No album artwork is bundled; sample
covers are generated geometry. The app icon is generated from the mountain mark.

The shell is deliberately flat: one page header per screen, no second title bar
above it, and a plain named sidebar with no ordinal numbers. Meta labels and
tags use title case; uppercase is reserved for acronyms such as HFS+, MP3, API
and SHA-256, and for volume names supplied by the device. Counts and installation
steps keep their numbers because those numbers carry meaning, not navigation
decoration.

The welcome screen is a full-window paper canvas built around the owner’s
Halftone banner. The banner is a 1600×1054 crop of the photo area in
[`../assets/branding/helikon-os-poster.png`](../assets/branding/helikon-os-poster.png),
kept as an app resource at
`tools/desktop/Sources/HelikonCompanion/Resources/helikon-os-banner.png`.
Nothing is redrawn or recoloured. The screen adds only the companion wordmark,
a lead, Open companion and Add music; it can be reopened from the app menu or
Settings. `--skip-splash` is the developer shortcut to the main shell.

## 4. Transport and writes

The current path is **macOS Disk Mode**, i.e. D-09 option (a). The app writes
through the mounted HFS+ volume and macOS handles the journal. This is not the
same as the firmware’s `hfs_txn` writer and does not prove device-side
consistency.

Safety rules implemented in the host MVP:

- detect HFS+ and read-only state; refuse unsupported filesystems for planned
  device writes;
- never mount, format, restore, repartition or erase;
- plan every transfer before copying;
- copy through a temporary file in the destination directory;
- verify every copy with SHA-256;
- back up an existing destination file before replacement and verify the backup;
- show conflicts; offer skip, replace-with-backup and keep-both;
- write MP3 tags only into the copied temporary file, never into the source;
- leave the previous destination file in place until the replacement verifies;
- keep the library, artwork cache, downloads and backups in Application Support;
- keep scrobbling off until the user connects Last.fm and presses Send queue.

The service-mode transport (D-38) and a writable LUN are not implemented. A
writable LUN remains rejected by the project.

## 5. Data model

The MVP persists a JSON document:

- tracks, album annotations and artist annotations;
- podcasts and episodes;
- audiobooks and local file references;
- scrobble queue;
- settings (separate JSON);
- artwork and downloaded media files in Application Support.

The paper targets a host SQLite library. The MVP store is deliberately isolated
behind `LibraryRepository` so it can move to SQLite without rewriting the
features. The device database/index and its schema are still open (D-07); the app
does not claim to build one.

## 6. Metadata sources

| Source | Used for | Key needed | Notes |
|---|---|---|---|
| MusicBrainz | Recording, release and artist search | none | descriptive User-Agent, 1 request/s guard |
| Apple Search API | Music, podcast, artist search and artwork | none | terms go to Apple; no account |
| Open Library | Audiobook catalogue search | none | cover art by cover ID |
| LibriVox | public-domain audiobook recordings | none | downloads individual sections |
| Last.fm | scrobbling | user API key + secret | secret/session in Keychain; opt-in |

Metadata candidates are always shown for review. The app does not auto-apply a
best guess, does not scrape album reviews and does not bundle album artwork or
artist biographies.

## 7. Podcasts and audiobooks

- Podcast subscriptions are fetched by the Mac. Episode downloads happen on the
  Mac. The device never fetches or uploads anything.
- The RSS/Atom parser handles title, author, summary, artwork, enclosure,
  publication date and duration. It is tested against fixtures, not every feed
  in the wild.
- Audiobook catalogue entries can exist without a local file. LibriVox entries
  can be downloaded as multiple sections and then planned for transfer.
- A 500-episode podcast test and a large audiobook run are not done.

## 8. Installation guide boundary

The Install screen shows:

1. volume mounted;
2. HFS+ and writable;
3. verified backup;
4. signed release exists;
5. future DFU-only bootloader installation;
6. future signed firmware staging;
7. first-boot check.

The install action is disabled. The app does not run `--single`, format,
repartition, restore, erase or delete a journal. The guide becomes an installer
only after a signed release, certificate verification, staging and a restore
path exist.

## 9. Tests and evidence

Current host evidence:

```text
swift build -c release      PASS
swift test                  PASS — 21 tests
./scripts/build-app.sh      PASS — ad-hoc app bundle
mounted HFS+ test image     PASS — detected as HFS+, writable, Helikon-OS marker
```

The 21 tests cover:

- transfer planning, conflict handling, duplicate destinations, copy
  verification, backup/skip and replace-with-verified-backup,
- MP3 ID3 tag writing into the copy only,
- Last.fm signature canonicalisation,
- Rockbox log and JSON play-history import,
- RSS/Atom podcast parsing,
- library shape, merge, metadata apply, file-name parsing and destination naming.

Not run:

- macOS task matrix (import/tag/rate/describe/scrobble/install/backup);
- a real iPod transfer, device unmount, failed-transfer recovery or 100-cycle
  mount test;
- a real Last.fm account;
- 10k-track performance, 500-episode podcast or large audiobook transfer;
- notarization/sandbox;
- Windows/Linux.

## 10. Open questions

1. Should the final host library be SQLite (paper target) or a document store?
2. When D-07 settles a device database schema, which fields does the companion
   write, and through which API?
3. When D-38 service mode lands, does the macOS app use Disk Mode for media and
   service mode for metadata, or exactly one path?
4. Which tag writers are in scope beyond MP3 (M4A, FLAC, Vorbis, APE)?
5. What is the Last.fm client registration and secret-provisioning flow for end
   users?
6. Does the app become sandboxed/notarized before v1, and what file-access
   entitlements does that require?
7. How are failed transfers reported and retried without duplicating files?
8. Should podcast/audiobook sidecar state be written to the device before the
   device model (D-43/T-082) exists?

## 11. What not to claim

- That the companion is released, signed, notarized or supported.
- That installation works or that a signed firmware image exists.
- That a transfer has passed on an iPod or leaves a device database consistent.
- That the app writes native library metadata, app packages or device settings.
- That Last.fm scrobbling has been exercised with a real account.
- That Windows/Linux are supported.
- That the sample library is real music or album artwork.

## Related

- [`../tools/desktop/README.md`](../tools/desktop/README.md) — build, features and limits
- [`design-paper.md`](design-paper.md) §28.4.2 — desktop app scope
- [`goals.md`](goals.md) — goal 19/20
- [`decisions.md`](decisions.md) — D-09, D-38, D-43
- [`testing.md`](testing.md) — desktop test layer
- [`ui.md`](ui.md) — Helikon visual system
