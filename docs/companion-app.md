# Companion app — reset record

> Purpose: record the 2026-11-06 reset of the macOS companion to a minimal shell
> and the boundary for rebuilding it against the actual Helikon-OS surface.
> Status: living; app shell `in progress`, all previous features removed
> Owner: host/product
> Last updated: 2026-11-06
> Source: owner reset instruction 2026-11-06; paper §28.4.2, goals 19/20
> 2026-11-05; `tools/desktop/`

The companion is a separate host product. It is not the firmware, the
bootloader, or a replacement filesystem. It currently has no device, library,
network or installer functionality.

## 1. Status

| Item | Status | Evidence |
|---|---|---|
| Native macOS shell | `in progress` | SwiftPM release build and app bundle pass; host screenshot reviewed |
| Welcome screen | `in progress` | Owner banner crop, one Open companion action; host screenshot reviewed |
| Sidebar navigation | removed | Brand and footer only; no sections, numbers or actions |
| Library, metadata, transfer | removed | Source and tests deleted; recoverable only from git history |
| Podcasts, audiobooks, scrobbler | removed | Source and tests deleted |
| Installer / settings | removed | No install or settings surface exists |
| Device access | none | The shell does not enumerate or read a volume |
| Network access | none | No network client remains |
| Test target | none | The previous 21 host tests were removed with their modules |

The app is not a release: no notarization, no signed distribution, no device
matrix and no installer.

## 2. What the reset removed

- HFS+ volume detection, device markers, capacity and access state.
- Library scanning, tag reading and the JSON library store.
- MusicBrainz/Apple metadata search, artwork caching, ratings, notes and bios.
- Transfer planning, copy verification, backups, MP3 tag writing and conflict
  handling.
- Podcast search, RSS/Atom parsing, episode download and transfer.
- Audiobook search through Open Library and LibriVox, download and transfer.
- Last.fm authentication, scrobbling, play-history import and Keychain storage.
- Install guide/preflight, settings, activity log and the sample library.
- The `CompanionCore` target and all 21 host tests.

The deleted code remains in git history. This branch is a deliberate product
reset, not a claim that those features never existed.

## 3. What remains

### Shell

A single paper/ink window with a brand-only sidebar and an empty-state page. The
sidebar has no navigation items. The only shell action is reopening the welcome
screen from the app menu.

### Welcome screen

The full-window welcome screen uses the owner-supplied Halftone banner: a
1600×1054 crop of the photo area in
[`../assets/branding/helikon-os-poster.png`](../assets/branding/helikon-os-poster.png),
kept at
`tools/desktop/Sources/HelikonCompanion/Resources/helikon-os-banner.png`.
Nothing is redrawn or recoloured. It has one action, **Open companion**.
`--skip-splash` remains the developer shortcut to the shell.

### Visual direction

The shell keeps the editorial paper/ink language: warm paper, deep ink, a serif
display face, system utility text, thin rules and restrained colour. There are
no all-caps UI labels; acronyms and device-supplied text are the exception.

## 4. Why the reset

The removed feature set was built around a planned product, not the current
firmware. Helikon-OS now has a launcher home, a read-only music library browser
and a HelikonDB catalog, but the companion has no proven protocol or file contract
for them yet. Keeping speculative library, transfer, podcast, audiobook,
scrobbler and installer screens made the app look more capable than the device.

The companion will be rebuilt from capabilities that exist and can be tested:
- read-only inspection where the firmware exposes a real surface;
- a host builder or reader only when its schema is frozen;
- transfer or installation only after the device-side transaction path and
  release artifacts exist;
- no network dependency for core work.

## 5. Safety boundary

- No volume read or write.
- No network request.
- No installer or bootloader action.
- No device database or library metadata write.
- The only runtime state is whether the welcome screen is visible.

## 6. Evidence

```text
swift build            PASS
swift build -c release PASS
scripts/build-app.sh   PASS — app bundle and owner banner present
plutil -lint           PASS
host screenshots       welcome screen and empty shell reviewed
```

Not run:
- no test target exists after the reset;
- no device run;
- no macOS task matrix;
- no notarization, sandbox or release validation.

## 7. Next

1. Write a capability inventory against the current firmware surface: launcher,
   read-only library, HelikonDB, diagnostics and the documented device modes.
2. Choose one honest first host capability, with acceptance evidence, rather
   than restoring the removed feature list.
3. Add a small test target with the first rebuilt module.
4. Keep the splash and shell as the visual base.

## 8. What not to claim

- That the app has library, metadata, transfer, podcast, audiobook, scrobble or
  install functionality.
- That the app has been run against a device since the reset.
- That the app shell is a release or a support claim.
- That the owner poster or banner was regenerated.

## Related

- [`../tools/desktop/README.md`](../tools/desktop/README.md) — build and current limits
- [`tasks.md`](tasks.md) — T-083
- [`design-paper.md`](design-paper.md) §28.4.2
- [`goals.md`](goals.md) — goal 19/20
- [`state.md`](state.md) — current phase
