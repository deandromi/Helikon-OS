# The Companion Link — metadata, synchronization and offline media delivery between Helikon-OS and the macOS Companion

> Purpose: a research and design paper for the host–device contract between
> Helikon-OS on an iPod Classic and the Helikon-OS Companion for macOS.
> Status: living — research paper; design only, not implemented and not a
> support claim.
> Owner: host/product research
> Last updated: 2026-11-06
> Source: session-start prompt; design paper §12, §25, §28.3.5–6, §28.4.1–2;
> D-07, D-09, D-33, D-38, D-43, D-48, D-53; [`helikondb.md`](helikondb.md);
> [`companion-app.md`](companion-app.md); external sources in §19.
> Base commit: `c576420` (`main`, 2026-11-06).

**Scope.** This paper studies how the iPod-side Helikon-OS firmware and the
macOS-side Helikon Companion should exchange music, artwork, editorial
metadata, podcast subscriptions, audiobook content, playback state, playlists
and device reports. It proposes a data model, source policy, file contract,
sync state machine, podcast lifecycle, failure model and test plan. It does not
change firmware, the host app, the frozen HelikonDB v1 format or any accepted
decision.

**Evidence rule.** The repository has a host-tested HelikonDB v1 builder and
reader, a host-tested HFS+ writer, diagnostic bootloader images and an
owner-reported 6G firmware run. It has no production VFS, no audio playback, no
USB service mode and no released companion app. Everything below that is not
already recorded in [`state.md`](state.md) or [`helikondb.md`](helikondb.md) is
`proposed`, `planned` or an `open question`. Design statements use the present
tense for readability ("the app builds…"); they describe the proposed contract,
not existing code. A design target is not a measurement. No part of this paper
is device evidence.

**Reading order.** §2–4 set the constraints, ownership model and end-to-end
flow. §5–7 cover metadata sources, the host database and the device catalog.
§8–10 cover transfer, podcasts and audiobooks. §11–13 cover the remaining
Companion jobs, security and failures. §14–17 cover scale, tests, the roadmap
and open decisions. The appendices contain the DDL, a worked example and the
proposed binary layouts.

---

## 1. Abstract

The iPod is an offline appliance. It has no network stack, no accounts and no
way to subscribe to a podcast, fetch a missing album cover or ask a metadata
service who played bass on a record. The Mac is the opposite: it has the
network, the user's accounts, the library, the keyboard, the backup disk and
enough memory to hold a relational database. The correct division of labour is
therefore not "the iPod is a small Mac". It is a **curator–consumer split**:

- the **Mac Companion** discovers, fetches, normalizes, licenses, edits,
  validates, backs up and builds;
- the **iPod** consumes immutable, bounded, prefetched artifacts, plays them
  offline and writes only a small append-only journal of what happened;
- the **host is the source of truth** for library metadata, editorial content,
  subscriptions, playlists and artwork choices;
- the **device is the source of truth** for playback events, resume positions,
  played/unplayed state and ratings made on the device;
- **nothing is written to the device that the host has not planned, hashed and
  verified**, and no device write bypasses the device's HFS+ transaction engine
  when that engine is the writer.

The paper proposes five concrete contracts:

1. **Host library database.** A local SQLite database in the Companion's
   Application Support directory stores tracks, albums, artists, credits,
   reviews, artwork, provenance, podcast feeds and episodes, sync history and
   conflicts. It is a normal, migratable, exportable host database. It is not
   the device database.
2. **Device catalog.** HelikonDB v1 remains the frozen, immutable music index:
   fixed 88/48/48-byte track/album/artist records, a deduplicated string pool,
   four precomputed index arrays and 96×96 RGB565 artwork. Long editorial text,
   credits, larger artwork, podcast episodes and audiobook chapters live in
   generation-bound sidecar packs, not in the v1 records. This avoids changing
   a tested format while giving the UI real data to show.
3. **Artifact and transfer contract.** The host builds a generation, writes
   temporary files, flushes, verifies SHA-256 and CRC, and atomically renames
   the catalog last. A generation-bound sidecar can never be mixed with a
   different catalog. Adds write media first and the catalog last; deletes
   remove the catalog reference first and the media second. Every sync is
   planned, idempotent, resumable and reported.
4. **Podcast lifecycle.** The Companion subscribes to RSS 2.0/Atom feeds,
   resolves a canonical feed identity, fetches conditionally, downloads
   episodes with a resumable queue, parses iTunes and Podcasting 2.0 metadata,
   stores chapters and transcripts, and exports a selected set to the device.
   The device never fetches a feed and never uploads listening history. Feed
   state and episode state are merged by stable identity and timestamp.
5. **Metadata source policy.** Embedded tags and user edits come first.
   MusicBrainz, the Cover Art Archive, AcoustID, Discogs, TheAudioDB,
   fanart.tv, Wikidata, Wikipedia and Wikimedia Commons are used only within
   their terms and with provenance. Album art is copyrighted by its owners even
   when it is hosted by a free archive; the app does not bundle or redistribute
   it. Album reviews are user-authored unless a source is explicitly licensed.
   AllMusic/TiVo/Gracenote/Roon editorial data and streaming-service catalogues
   are not scraped.

The paper closes with a failure model, a scale budget, a test matrix and a
phased implementation plan that starts with read-only inspection and ends with
an optional cross-platform service mode.

### 1.1 Method and limits

The research method was:

1. Read the repository contract and current truth: `agents.md`, `state.md`,
   `tasks.md`, `decisions.md`, the design paper, `helikondb.md`,
   `companion-app.md`, `ui.md`, `audio-formats.md` and `security.md`.
2. Compare the existing HDB v1 record layout against the data the album/artist
   and podcast screens need, and identify what must not change.
3. Review the public documentation of the candidate metadata, artwork and
   podcast sources (MusicBrainz, Cover Art Archive, AcoustID, Discogs,
   TheAudioDB, fanart.tv, Apple, Deezer, Last.fm, Podcast Index, Open Library,
   LibriVox/Internet Archive, Wikidata, Wikipedia and Wikimedia Commons) and
   record their limits and licence constraints.
4. Design the host database, device sidecars, sync state machine, podcast
   lifecycle and failure model against the device budget and the project's
   safety rules.
5. Check the result against the existing tests, the open decisions and the
   "what not to claim" list.

Limits: no code was written for this paper; no provider account was used; no
device transfer, podcast sync or app task matrix was run; and the licence
notes are an engineering summary, not legal advice. The provider limits and
terms were checked on 2026-11-06 and can change.

---

## 2. The baseline this paper has to respect

### 2.1 The device

| Constraint | Consequence for this design |
|---|---|
| iPod Classic 6G is the reference target; 7G is a future port after a board audit | The host contract targets one panel, one button map and one 64 MiB memory budget. Ports may change sizes, not semantics. |
| 64 MiB RAM and no desktop-class virtual-memory budget | No SQLite on the device. The catalog is read lazily from a file with fixed-size records. No full-library scan or full-tree load. |
| 320×240 panel with a restrained colour palette | 96×96 RGB565 thumbnails are enough for lists and album sleeves. A larger image is decoded only for the visible page. |
| Native HFS+ journaled volume; no overlay, no FAT, no writable LUN | The device writer is the only writer that can promise transactional consistency. macOS Disk Mode writes are a host path with a different safety story and must be treated as such. |
| USB 2.0 device, no network | The host does all network work. USB is for files, manifests and, later, a framed service mode. |
| Battery and storage are finite | Sync must be incremental, resumable and bounded. Podcast retention and artwork caches need hard caps. |
| The launcher reads **zero** database bytes at boot | The catalog is opened only when Music/Podcasts/Audiobooks is opened. Do not add a boot-time manifest read or a full CRC scan. |
| The device journal is small and append-only | Playback state is a stream of small events, not a rewrite of the catalog. |

The current firmware surface is narrower than the product vision. F1 reads
HFS+ and reports; F2 promotes bootstate through `hfs_txn`; the HDB reader and
the embedded-catalog Music hub/wall are host-tested; the VFS, playback, USB and
production handoff are not implemented. This paper is written against the
**intended** surface but uses the **current** surface as its dependency gate.

### 2.2 The current device catalog: HelikonDB v1

[`helikondb.md`](helikondb.md) freezes HDB v1. The relevant facts:

| Section | Size | What it carries | What it cannot carry |
|---|---:|---|---|
| Header | 128 B | magic `HLDB`, version 1, flags, generation, counts, section offsets, CRC-32 | no section directory for future data |
| Track | 88 B | path, title, artist id, album id, genre, composer, sort keys, duration, size, bitrate, sample rate, added time, artwork id, filetype, track/disc/year, rating, flags | no content hash, no MBID, no multiple artists, no per-track notes |
| Album | 48 B | title, album artist id, sort title, genre, artwork id, year, track count, first track, disc count, added time, rating, flags | no description, no review, no credits, no release MBID/barcode/label |
| Artist | 48 B | name, sort name, biography string, profile artwork id, album count, first album, track count, added time, rating, flags | no long biography source/licence, no facts, no aliases |
| Playlist | 24 B | name, sort name, entry array offset/count | static only; smart playlists are precomputed by the host |
| Strings | variable | `u16` length + UTF-8, deduplicated | max 65,535 bytes per string; no separate licence/attribution section |
| Indexes | 16 B + arrays | title, recent, genre, album title | no full-text search; no runtime sort |
| Artwork | 32 B + blobs | kind, owner, width/height, format RGB565, blob offset/bytes/CRC | one 96×96 image per owner in v1 |

The tested builder is `tools/hdb_build.py`; the firmware reader is
`firmware/db/hdb.c`. The builder already supports optional track ratings, album
ratings, artist biographies and artist profile pictures through overlays. The
functional embedded-catalog image (`de6491a9…`) proves the reader, the Music
hub/wall and the album/artist pages on the host loader, not on a production
VFS.

HDB v1 has two important properties that this paper preserves:

- **It is derived, not authoritative.** A missing or stale entry is a cache
  problem, never a reason to delete or repair the underlying file.
- **It is immutable at runtime.** Ratings and play state go to
  `/.helikon/play.journal` (planned T-067), not back into the catalog.

### 2.3 The host app

The Companion is a separate product. At the base commit, `docs/companion-app.md`
records the 2026-11-06 reset to a minimal shell. The previous MVP — volume
detector, library scan, MusicBrainz/Apple metadata search, transfer planning,
podcast/audiobook clients, Last.fm queue and 21 host tests — was removed; the
code remains in git history. The shell now has a welcome screen and a brand-only
sidebar with no device, library, network or installer functionality. This paper
deliberately does **not** depend on the removed code. It treats the Companion as
a product to be rebuilt against the device surface, one capability at a time,
with tests.

The app's current technical envelope is useful: macOS 14 or newer, Swift 6,
SwiftPM, no third-party package dependencies, no test target after the reset,
no notarization and no device run. The paper does not assume a web view, a
helper daemon, a writable USB LUN or an always-on network connection.

### 2.4 Decisions that constrain the design

| Decision | Status | Effect on this paper |
|---|---|---|
| D-07 — database location | accepted | One immutable host-built `/.helikon/music.hdb`; read-only lazy device reader; small runtime journal; no device scan for a normal build. |
| D-33 — library index architecture | accepted | Fixed HDB records, string pool, precomputed indexes, separate artwork table. No SQLite, tagcache or full on-device build. |
| D-53 — music catalog ownership | accepted | HelikonDB is the only music catalog. No iTunesDB, libgpod, tagcache or Apple-format writes. |
| D-09 — desktop transport | proposed | macOS Disk Mode is the first path. A vendor-specific USB service mode is the cross-platform path. A writable LUN is rejected. |
| D-38 — USB profile and service mode | proposed | Read-only LUN, HID and charge-only profiles; an authenticated framed service mode is a later project. |
| D-43 — podcast/audiobook model | proposed | Host subscribes/downloads; device plays a local episode model; sidecar state under `/.helikon/podcasts/`; no device network. |
| D-48 — privacy/network policy | proposed | No device network or telemetry; host network only for explicit user actions; scrobbling off by default. |
| D-02 — boot state (T-050 resolved) | accepted | Single-image fail-closed; no A/B rollback; recovery is DFU/Apple restore. Install work must not promise rollback. |

Any implementation that changes these requires an owner decision. This paper
records proposed decisions in §17 but does not silently accept them.

---

## 3. Design principles

The following principles are ordered. When two conflict, the lower number wins.

### P1 — Offline device, online host

The device never initiates a network request. It has no Wi-Fi credentials, no
HTTP client, no feed parser and no account token. The host performs every
network operation and records what it fetched, when, from where and under which
licence.

**Corollary:** a device feature that needs a network request is not a device
feature. It is a host feature with a device projection.

### P2 — One authority per field

Every field has exactly one primary writer and an explicit conflict rule. A
track title is primarily the user's tag; a play position is primarily the
device; a podcast subscription is primarily the host. Two writers cannot both
be "right" without a rule, and "last write wins" is not a rule until the
timestamps, clocks and sources are defined.

### P3 — Immutable generations plus append-only state

Catalog data is built into a new generation and switched atomically. Runtime
state is appended to a journal. The device never rewrites the catalog in place
during playback, and the host never mutates a device artifact in place while it
is being read.

### P4 — Plan, then write; verify, then commit

Every sync is a plan of typed operations with stable operation IDs, byte
counts, source hashes and destination paths. The writer never discovers work
mid-copy. After writing, the host re-reads and hashes the result before the
new catalog is allowed to point at it.

### P5 — Metadata is untrusted input

Tags, feeds, XML, artwork and provider JSON are untrusted. Parsers are bounded,
images are decoded with pixel and byte caps, paths are sanitized, XML external
entities are disabled, and no metadata value is ever interpreted as code or as
a path escape.

### P6 — Derived data is disposable; user data is precious

The HDB, sidecars, thumbnails, provider caches and transcripts are derived and
can be rebuilt. User tags, user reviews, ratings, playlists, playback history,
podcast state and backups are precious. A failed derived build must leave the
previous generation usable. A failed user-data write must stop the sync.

### P7 — Provenance and licence travel with the data

A biography without a source is a rumour. A cover without a licence is a legal
problem. Every external field records its provider, provider record ID,
retrieval time, confidence, licence and attribution. The device can show a
short "Source and licence" line on the Notes/Credits page. Data whose licence
is unknown is not transferred by default.

### P8 — Bounded resources at every layer

The host may use a relational database and a large cache; the device may not.
HDB records are fixed-size. Text has per-record caps. Artwork has per-image and
total caps. The device decodes one visible image at a time. A 10,000-track
library must not become a 10,000-object in-memory graph on either side.

### P9 — Idempotency and resumability

A sync that is interrupted at any point can be re-run without duplicating files
or losing state. Operation IDs and content hashes make retries safe. Temp files
are never presented as final files. A corrupt journal tail is discarded, not
"repaired" into the catalog.

### P10 — No hidden network, no telemetry, no lock-in

The app does not phone home on launch. It shows what it will send before it
sends it. The user can export the host database, playlists, metadata and state
in documented formats. No unique device identifier is published. Logs are local
and redacted.

---

## 4. Data taxonomy and ownership

### 4.1 Classes of data

| Class | Examples | Primary writer | Device role | Sync direction | Loss impact |
|---|---|---|---|---|---|
| Source media | MP3, M4A, ALAC, FLAC, WAV, AIFF, Ogg, Opus | user / label | read-only playback | host → device | high — the user's music |
| Embedded tags | title, artist, album, track/disc, year, genre, composer, ReplayGain, embedded art | user / label | read-only display | host → device | medium — re-rippable but tedious |
| User editorial | notes, review, album description, credits corrections, rating, playlist membership | user | read-only display | host → device | high — cannot be re-downloaded |
| External editorial | artist biography, album notes, credits, provider facts | provider | read-only display | host → device, if licensed | low — refetchable but may change |
| Artwork | front/back cover, booklet, artist portrait, artist banner, podcast cover, audiobook cover | user / provider | read-only display | host → device, if licensed | low–medium |
| Playback events | play count, skip, resume position, played/unplayed, speed, last played | device | write journal | device → host | high — listening history |
| Device ratings | track/album rating changed on device | device (or host) | write journal | device → host, host → device | medium |
| Podcast subscription | feed URL, canonical identity, refresh policy, retention policy | host | read-only | host → device | medium — re-subscribable |
| Podcast episode state | position, played, speed, skip intervals | device | write journal | device → host | high — resume/played state |
| Playlists | static and smart playlists, order, membership | host | read-only | host → device | high — user curation |
| Device manifest | model, firmware, capabilities, free space, journal state | device / installer | generated | device → host | low — regenerable |
| Checksums and backups | per-file SHA-256, sync reports, backup manifests | host | read-only / ignored | host → device / host storage | high — recovery evidence |
| Caches and indexes | HDB, sidecars, thumbnails, provider cache, FTS | host | read-only | host → device (projection) | none — derived |

### 4.2 Field ownership

This table is the contract. "Primary" means the value the device should show
when no conflict exists. "Conflict rule" applies when both sides changed since
the last common snapshot.

| Field | Primary | Secondary | Conflict rule |
|---|---|---|---|
| Track title | user tag / user edit | provider | user edit wins; if both user edits, latest `updated_at` wins and the other is kept as a conflict row |
| Artist name | user tag / user edit | MusicBrainz/Discogs | user edit wins; provider alias never silently replaces the display name |
| Album title | user tag / user edit | MusicBrainz/Discogs | user edit wins; release title vs album title is a separate field |
| Album artist | user tag / user edit | MusicBrainz | user edit wins; "Various Artists" is explicit, not inferred from a blank tag |
| Track/disc number | user tag | MusicBrainz/Discogs | user wins; provider fills only a missing value |
| Year / release date | user tag | MusicBrainz/Discogs | user wins; provider may add a full date without replacing a user year |
| Genre | user tag | provider tags | user wins; multiple genres are allowed in the host, the device shows the primary |
| Composer | user tag | MusicBrainz work/recording | user wins; classical works keep composer separate from performer |
| Rating | user | device | latest timestamp wins; equal timestamps keep the higher value and create a conflict note |
| Review / notes | user | licensed provider (opt-in) | user always wins; provider text is never merged into user text |
| Album description | user / licensed provider | Wikipedia (CC BY-SA, opt-in) | user wins; provider text carries attribution and is replaceable as a whole |
| Credits | user correction | MusicBrainz/Discogs | user-pinned role wins; provider roles are additive, not destructive |
| Artist biography | user / licensed provider | Wikipedia (CC BY-SA), TheAudioDB (terms) | user wins; provider text carries source/revision/licence |
| Artist image | user | Wikimedia Commons / TheAudioDB / fanart.tv | user wins; provider image needs a licence record |
| Cover art | embedded / user | Cover Art Archive, Discogs, provider | user wins; provider art is opt-in and never bundled |
| Play position | device | host | latest device timestamp wins; host never moves a position backwards on an equal timestamp |
| Play count | device | host | sum of device events since last merge; host edits are explicit adjustments |
| Played / unplayed | device | host | latest timestamp wins; host can override with an explicit "mark unplayed" event |
| Podcast subscription | host | none | host wins; device cannot subscribe |
| Podcast feed refresh | host | none | host-only |
| Podcast episode position | device | host | latest timestamp wins; equal timestamps keep the larger position |
| Podcast speed/skip | device | host | latest timestamp wins; per-episode, with per-show defaults |
| Playlist membership | host | device (future) | host wins in v1; device edits are a future feature and must produce a conflict, not a silent merge |
| Device health/manifest | device | host | device-generated; host never invents a value |

### 4.3 Identity layers

A file is not an album, and an album is not a release. The host stores three
identities separately:

1. **File identity:** volume-rooted path (NFD, leading `/`), size, mtime, and a SHA-256 of
   the file content (computed lazily). This is how the sync planner knows
   whether a file changed.
2. **Recording identity:** AcoustID, MusicBrainz recording MBID, ISRC, or a
   content fingerprint. This is how the matcher knows that two files are the
   same performance.
3. **Release identity:** MusicBrainz release/release-group MBID, Discogs release
   ID, barcode, catalog number, label, country and format. This is how the UI
   knows which cover, track list and credits belong together.

HDB v1's `uid` is a 64-bit FNV-1a hash of the device path. That is a **path
identity**, not a content identity. It is stable while the path is stable and
changes on rename. This paper keeps it for v1 compatibility and adds a content
hash and recording identity in the host database and the sidecar. The host must
be able to answer: "this path disappeared and a new path appeared; is it the
same recording?" before it declares a track deleted.

### 4.4 The two loops

The whole system is two loops that meet at the device catalog:

```text
Library loop (host, online, slow)
  files + tags → scanner → host SQLite → matcher/providers
  → provenance → user review → HDB/sidecar builder → sync planner → device

State loop (device, offline, fast)
  playback / pause / skip / resume → play.journal or podcasts/state.journal
  → host import → host SQLite → next library build
```

The library loop is allowed to be slow, network-bound and user-driven. The
state loop must be fast, local and crash-safe. Neither loop writes into the
other's authoritative store: the host never edits the device catalog in place,
and the device never edits the host database.

---

## 5. Where metadata should come from

### 5.1 The source hierarchy

The order below is deliberate. A lower number is more authoritative for the
user's library, but not necessarily more complete.

1. **The user's files and embedded tags.** The user already owns this data.
   It is offline, immediate and often correct. Never replace it silently.
2. **The user's explicit edits and pins.** A review the user wrote, a cover the
   user scanned, a genre the user corrected. These are first-class data, not
   cache.
3. **MusicBrainz core data and the Cover Art Archive.** CC0 core data, stable
   MBIDs, release/recording/artist relationships, credits and release events.
   Cover art is separately copyrighted; see §5.5.
4. **AcoustID/Chromaprint.** Fingerprint matching for files whose tags are
   missing, wrong or ambiguous. This is the strongest "same recording" signal
   available without a commercial fingerprint service.
5. **Discogs.** Physical-release variants, barcodes, catalog numbers, labels,
   pressing details and credits that MusicBrainz may not have. Requires
   authentication for images and has its own rate limits and terms.
6. **TheAudioDB and fanart.tv.** Artist biographies, portraits, backgrounds,
   logos and banners. Terms and attribution must be checked before shipping;
   these are enrichment sources, not the core catalog.
7. **Wikidata and Wikipedia.** Wikidata for CC0 facts (formed, origin, members,
   official website, identifiers); Wikipedia for a short biography under
   CC BY-SA with attribution and revision. Wikimedia Commons for freely
   licensed artist photographs with per-file licence metadata.
8. **Apple iTunes Search / Apple Podcasts lookup.** Directory identity,
   podcast discovery and store metadata. Apple's terms limit artwork use to
   promotional contexts; it is not a general-purpose cover source.
9. **Deezer public API.** Optional identification and 30-second preview
   metadata. It is not a source of permanent library artwork unless the terms
   are explicitly cleared.
10. **Podcast feeds themselves.** The feed is the primary source for show and
    episode metadata. Podcast Index and Apple Podcasts are directories and
    discovery aids, not replacements for the feed.
11. **Open Library, LibriVox and the Internet Archive.** Audiobook catalog
    metadata, public-domain recordings and section lists.
12. **Last.fm.** Scrobbling and user listening history only. It is not an
    editorial metadata source for the library.

**Concrete endpoints.** These are the canonical starting points; every client
must still read the provider's current documentation and terms.

| Provider | Base / example endpoint | Used for |
|---|---|---|
| MusicBrainz | `https://musicbrainz.org/ws/2/` | artist, release, recording, work, relationship lookup/search |
| Cover Art Archive | `https://coverartarchive.org/release/{mbid}/front-500` | front cover; `/back-500` and image IDs for other art |
| AcoustID | `https://api.acoustid.org/v2/lookup` | fingerprint → recording MBID |
| Discogs | `https://api.discogs.com/` | release variants, barcodes, labels, credits, images (auth) |
| TheAudioDB | `https://theaudiodb.com/api/v1/json/{key}/` | artist/album bios and artwork (terms) |
| fanart.tv | `https://webservice.fanart.tv/v3/` | artist backgrounds, thumbs, logos (terms) |
| Apple iTunes Search | `https://itunes.apple.com/search`, `/lookup` | music/podcast directory identity and artwork (promotional terms) |
| Deezer | `https://api.deezer.com/` | optional identification/preview metadata |
| Podcast Index | `https://api.podcastindex.org/api/1.0/` | podcast/episode discovery and lookup |
| Open Library | `https://openlibrary.org/search.json`, `https://covers.openlibrary.org/b/id/{id}-L.jpg` | audiobook work/edition metadata and covers |
| Internet Archive | `https://archive.org/metadata/{identifier}` | LibriVox item files and section metadata |
| Wikidata | `https://www.wikidata.org/w/api.php` | CC0 facts (formed, origin, members, website) |
| Wikipedia | `https://en.wikipedia.org/api/rest_v1/page/summary/{title}` | opt-in biography summary (CC BY-SA) |
| Wikimedia Commons | `https://commons.wikimedia.org/w/api.php` | freely licensed artist photos and licence metadata |
| Last.fm | `https://ws.audioscrobbler.com/2.0/` | scrobbling and user history |

### 5.2 Sources that are deliberately not used

| Source | Why not | What to do instead |
|---|---|---|
| AllMusic / TiVo music metadata | Licensed editorial content (reviews, biographies, credits, images); not freely redistributable and not scraped | User-authored notes, MusicBrainz credits, Wikipedia with attribution, or a future licensed feed |
| Gracenote | Commercial metadata and cover art; licence required | MusicBrainz/AcoustID, user files |
| Roon metadata | Roon licenses third-party editorial data; the product is an inspiration, not a data source | Build the UI; license data separately if ever wanted |
| Spotify / Apple Music / Tidal catalogues | Terms restrict metadata/artwork reuse; no general library export | Embedded tags, MusicBrainz, CAA, user files |
| Album review websites | Reviews are copyrighted; scraping is not a licence | User writes the review, or the project licenses a review feed explicitly |
| Lyrics sites | Copyright and licensing complexity; not needed for this paper | User-supplied lyrics or a licensed provider later |
| Fan sites / blogs | No reliable licence; images and bios are copyrighted | Wikimedia Commons, provider APIs with terms, user files |
| Discogs image bulk download | Images are proxied/authenticated and intended for in-app display, not bulk scraping | Use the API for the visible release; respect rate limits; do not bulk archive |
| Google Books / Open Library covers as a blanket source | Cover rights vary by edition; Open Library covers may be copyrighted | Use the cover ID with a per-item rights note, or embedded/user art |

This is not legal advice. The app must show its sources and licences, and the
project must review the terms of each provider before shipping a default
configuration. A provider can change its terms; the app must be able to disable
a provider without breaking the library.

### 5.3 Per-field source matrix

| Field | First choice | Fallback | Licence / attribution | Confidence notes |
|---|---|---|---|---|
| Artist identity | MusicBrainz artist MBID | Discogs artist ID | MusicBrainz core CC0; Discogs terms | match by name + release relationships, not name alone |
| Release identity | MusicBrainz release MBID | Discogs release ID, barcode | CC0 core; Discogs terms | prefer the edition the user owns (barcode/catalog number) |
| Recording identity | AcoustID + MusicBrainz recording MBID | ISRC, then tag match | AcoustID non-commercial service terms; MBID CC0 | fingerprint only when tags are missing/ambiguous |
| Track title | embedded tag | MusicBrainz recording title | user data / CC0 | preserve user title; provider title is a suggestion |
| Artist display name | embedded tag | MusicBrainz artist name | user data / CC0 | keep aliases and sort names separately |
| Album title | embedded tag | MusicBrainz release title | user data / CC0 | deluxe/remaster editions are separate releases |
| Year / date | embedded tag | MusicBrainz release date | user data / CC0 | store full date where known; show year |
| Genre / style | embedded tag | MusicBrainz genres/tags, Discogs styles | user data / CC0 / Discogs terms | multiple values; do not collapse to one |
| Label / catalog number | embedded tag | Discogs release, MusicBrainz label | user data / Discogs terms / CC0 | exact barcode/catalog match is strong evidence |
| Barcode | embedded tag | Discogs identifiers, MusicBrainz barcode | user data / terms / CC0 | exact match; normalise digits |
| Track/disc number | embedded tag | release medium/track list | user data / CC0 | preserve user numbering for compilations |
| Duration | file probe | MusicBrainz recording length | measured / CC0 | ±2 s tolerance; do not overwrite measured duration |
| Credits | user correction | MusicBrainz relationships, Discogs extra artists | user / CC0 / Discogs terms | roles normalised; source per credit |
| Composer / lyricist | embedded tag | MusicBrainz work/recording relationships | user / CC0 | keep composer separate from performer |
| Album notes/description | user | Wikipedia album article extract (CC BY-SA, opt-in) | user / CC BY-SA with attribution | no scraped reviews; store revision and link |
| Album review | user | licensed review feed only | user / explicit licence | no provider review text by default |
| Artist biography | user | Wikipedia summary (CC BY-SA), TheAudioDB (terms) | user / CC BY-SA / provider terms | short device summary; full text host-side |
| Artist image | user scan | Wikimedia Commons, TheAudioDB, fanart.tv, Discogs | per-file licence / provider terms | store author, licence, source URL |
| Cover front | embedded art | Cover Art Archive, Discogs | user / copyright owner / terms | never bundle; opt-in transfer |
| Cover back / booklet | user scan | CAA, Discogs | user / copyright owner / terms | optional; size-capped |
| Podcast show title | feed | Podcast Index/Apple lookup | feed copyright | feed is authoritative |
| Podcast episode title | feed | Podcast Index | feed copyright | GUID is identity, title is display |
| Podcast show notes | feed `content:encoded`/`description` | none | feed copyright | sanitize HTML; plain text on device |
| Podcast chapters | `podcast:chapters` JSON | ID3 CHAP/CTOC, MP4 chapters | feed/provider | validate times and order |
| Podcast transcript | `podcast:transcript` | none | feed/provider | fetch on demand; size cap |
| Audiobook metadata | Open Library / Internet Archive item | publisher metadata | check item licence | LibriVox recordings are public domain |
| Audiobook cover | Open Library cover | Internet Archive item image | check item licence | per-item rights |
| Scrobbles | device play journal | host manual import | user data / Last.fm terms | opt-in, batch 50, offline queue |

### 5.4 Matching: how a file becomes a release

Matching is a ranked, explainable process, not a single fuzzy search.

```text
file
  ├─ read tags, duration, bitrate, sample rate, embedded art
  ├─ compute content hash (lazy, cached by path+mtime+size)
  ├─ if tags are missing or ambiguous:
  │    └─ Chromaprint fingerprint → AcoustID lookup
  │         └─ MusicBrainz recording MBID(s)
  ├─ search MusicBrainz by recording/release/artist with Lucene
  ├─ search Discogs by barcode/catalog number/release title
  ├─ score candidates
  ├─ if score ≥ auto threshold and field is objective:
  │    └─ apply provider ID, duration, track number, release ID
  ├─ if 0.60 ≤ score < auto threshold:
  │    └─ present candidates for user review
  └─ else: mark metadata_incomplete; keep user tags
```

A proposed scoring model (weights sum to 1.00):

| Signal | Weight | Notes |
|---|---:|---|
| Normalised title similarity | 0.22 | token-set ratio after casefold and punctuation removal |
| Artist similarity | 0.18 | exact MBID or high token similarity |
| Album/release title similarity | 0.12 | edition titles are noisy |
| Duration difference | 0.14 | full score within ±2 s; zero beyond ±10 s |
| Track number match | 0.06 | medium/position aware |
| Disc number match | 0.04 | multi-disc releases |
| Year match | 0.06 | ±1 year tolerance |
| Barcode exact | 0.10 | strongest release signal when present |
| Label + catalog number | 0.05 | strong physical-release signal |
| MusicBrainz/AcoustID ID exact | 0.03 | overrides other signals when present |

**Acceptance policy.** Objective identity fields (MBID, release ID, track
number, duration) may auto-apply above 0.85 when the user has enabled
"automatic matching" for that library. Editorial fields (review, biography,
description, credits) are **never** auto-applied; they are proposals until the
user accepts them. A candidate that would change a user-edited field is shown
as a conflict, not applied.

**Classical music.** Do not force classical recordings into "artist — album —
track". Store composer, work, movement, performer, conductor, ensemble and
recording date as separate credits. Match on the work and recording, not on the
performer's name alone. The device can show a simplified hierarchy, but the
host keeps the full relationships.

**Compilations and duplicates.** Album identity is the release MBID when known;
otherwise `(album artist, album title, year, disc count)`. Track identity is the
recording MBID when known; otherwise `(album id, disc, track)` plus duration.
Two files with the same recording on different releases are not duplicates;
they are two library entries that may share a recording identity.

### 5.5 Artwork: right image, right size, right licence

Artwork is the most visible metadata and the most common legal mistake. The
pipeline is therefore conservative:

1. **Candidates.** Embedded art first; user-supplied image; Cover Art Archive
   front; Discogs primary image; provider image. Keep the source URL and
   provider record.
2. **Validation.** Sniff the MIME type by magic bytes, not by extension. Reject
   SVG, PDF, animated formats and unknown containers. Cap bytes (e.g. 20 MiB
   downloaded), pixels (e.g. 40 MP), and decode time. Strip EXIF/GPS.
3. **Normalisation.** Convert to sRGB; flatten alpha onto the paper colour;
   apply the crop policy (square centre crop for covers; focal-point crop for
   artist portraits); never upscale beyond the source; apply a mild sharpening
   only if the source is larger than the target.
4. **Targets.** Host master (longest edge 1200 px, JPEG/PNG), host detail
   (320×320), device list thumb (96×96 RGB565, 18,432 bytes), optional device
   detail (160×160 RGB565, 51,200 bytes), optional artist band (320×70 RGB565,
   44,800 bytes). The 96×96 HDB v1 blob is the only one the v1 reader knows.
5. **Deduplication.** Hash the normalised pixels, store one blob per hash, and
   reference it from multiple albums/artists. A compilation and its original
   album may share a cover; do not copy it twice.
6. **Fallback.** A generated typographic placeholder is drawn at runtime, not
   stored as a fake cover. Missing art is visible as missing, not as a
   copyrighted image the app invented.
7. **Licence.** A cover is copyrighted by its owner even when the Cover Art
   Archive hosts it; the archive says so explicitly [25]. The app may cache it
   for the user's personal library, but it must not bundle it in the installer,
   redistribute it, or present it as freely licensed. Artist photographs from
   Wikimedia Commons require author/licence/source metadata; TheAudioDB and
   fanart.tv require their terms. The default sync policy is "embedded or
   user-supplied art only"; external art is an explicit opt-in per source.
8. **Attribution.** The host stores `source_id`, `source_url`, `license`,
   `attribution` and `fetched_at`. The device can show a small "Artwork:
   Cover Art Archive / copyright owner" line on the album Notes tab. The app
   has a Licences screen that lists every external source and its terms.

### 5.6 Reviews, descriptions and biographies

The word "review" hides three different things:

| Kind | Source | Device treatment |
|---|---|---|
| User review / personal notes | The user writes it in the Companion | Full text (bounded), always shown, no attribution needed |
| Album description | User, or a licensed provider with explicit permission | Short text on the album Notes tab, with source/licence |
| Critical review | A licensed review feed or the user's own copy | Never scraped; only with a recorded licence and attribution |
| Artist biography | User, Wikipedia (CC BY-SA), TheAudioDB (terms), Wikidata facts (CC0) | Short summary; source/licence line; full text host-side |
| Credits | User corrections, MusicBrainz relationships, Discogs extra artists | Role-grouped list, source per credit |

The repository's existing companion notes already say the app "does not scrape
album reviews and does not bundle album artwork or artist biographies"
([`companion-app.md`](companion-app.md)). This paper keeps that boundary and
makes it explicit in the data model.

**Wikipedia reuse.** A Wikipedia extract is CC BY-SA 4.0 (check the page's
licence; some text may be CC BY-SA 3.0 or have other notices) [54][55]. If the app
stores and displays it, it must include the article title, revision ID,
retrieval date, a link to the source and the licence. A shortened extract is
still a derivative; do not strip attribution. The default is **off**; the user
opts in to "use Wikipedia summaries". If the user edits the text, the app must
not present the edited text as the Wikipedia article; it becomes a user note
with a "based on Wikipedia" provenance line.

**Wikidata facts.** Wikidata's structured data is CC0. Facts such as formed
date, origin, members and official website do not require attribution, but the
app should still record the entity ID (`Q…`) for traceability.

**Licensed review feeds.** If the project later licenses a review source, the
contract must state whether text may be stored on the device, displayed
offline and kept after the subscription ends. A fetched review is not
automatically redistributable.

### 5.7 Fetch policy, caching and API etiquette

Every provider gets a client with the same shape:

```text
request
  ├─ canonical URL + query
  ├─ ETag / If-None-Match, Last-Modified / If-Modified-Since
  ├─ User-Agent: HelikonCompanion/<version> (<project URL>)
  ├─ token bucket (per provider, per host)
  ├─ timeout, max redirects, max body
  ├─ 304 → refresh last_checked only
  ├─ 429/503 → honour Retry-After, exponential backoff with jitter
  ├─ 5xx → retry with cap; open circuit after N failures
  └─ 2xx → validate schema, store body hash + parsed fields + fetched_at
```

**Rate limits observed from provider documentation** (summarized from
[22], [26], [28], [30], [32], [34], [36], [39], [48], [49]; verify before
shipping):

| Provider | Auth | Published/observed limit | Caching guidance |
|---|---|---|---|
| MusicBrainz Web Service | none; meaningful User-Agent required | 1 request/second per source IP | cache release/recording lookups; batch where the API allows |
| Cover Art Archive | none | same infrastructure; be polite | cache by release MBID and image id; do not re-download |
| AcoustID | application API key | 3 requests/second for the free/non-commercial service | cache fingerprints by content hash |
| Discogs | token, key/secret or OAuth | 60/min authenticated; 25/min unauthenticated; images require auth | cache release/artist JSON; do not bulk-fetch images |
| TheAudioDB | API key (free tier) | 30/min free, 100/min premium, 120/min business | cache artist/album payloads; respect 429 |
| fanart.tv | API key + optional client key | no fixed public figure; handle 429 | cache image metadata; do not bulk-download |
| Apple iTunes Search | none | approximately 20 calls/min per IP | cache search/lookup results; do not paginate aggressively |
| Deezer | none for public endpoints | 50 requests per 5 seconds | cache; use only for identification/preview |
| Last.fm | API key + session for writes | no numeric limit; “several calls per second” can suspend | batch 50 scrobbles; backoff |
| Podcast Index | API key + secret, signed headers | no fixed public figure in the spec; throttle to ~1/s | cache feed/episode lookups; use `podcast:guid` |
| Open Library | none | covers: 100 requests/IP per 5 minutes for non-cover-ID access; search is rate-limited in practice | cache covers; do not bulk crawl |
| Wikimedia/Wikipedia/Wikidata | none; User-Agent required | follow Wikimedia API etiquette; low concurrency | cache extracts with revision IDs; do not mirror |
| Internet Archive / LibriVox | none | be polite; no bulk mirroring | cache item metadata; download only selected sections |

The table is not a promise that a provider will keep these numbers. Each
client must read `Retry-After`, expose a "provider paused" state, and never
retry a 4xx authentication error in a loop. Discogs' API page also states that
unauthenticated usage is being retired on 1 December; confirm the effective
year and require a token or OAuth before shipping.

**Caching model.** The host stores parsed fields in the library database and
the raw response in a content-addressed cache only when it is needed for audit
or re-parsing. Raw bodies are compressed and expire; parsed fields and
provenance do not. A 304 response is a success and must not clear a cached
field.

**API keys.** Store provider tokens and Last.fm session keys in the macOS
Keychain, never in `UserDefaults`, logs, backups or the repository. A desktop
app cannot keep an embedded secret secret; if Podcast Index requires a
key/secret pair, prefer a user-supplied key, or accept that a bundled key is
extractable and document the risk. Never put a provider secret in a crash log.

### 5.8 Provider outage and schema drift

A provider is an external dependency with a changing schema. The app must:

- validate JSON against a narrow, versioned internal model;
- keep the last good parsed value when a refresh fails;
- mark the field `stale` rather than blank;
- show a provider status in Settings;
- allow the user to disable a provider entirely;
- never block library browsing on a network request.

A schema change is a bug, not a reason to wipe metadata. The host keeps the
raw response hash so a re-parse can be attempted after a fix.

---

## 6. The host library database

### 6.1 Why SQLite

The host database is SQLite because the Companion needs transactions,
migrations, indexes, joins, full-text search and crash recovery in a single
file, without a server. SQLite is a host-only choice. It is **not** proposed
for the device: D-33 explicitly rejects on-device SQLite for the catalog, and
the 64 MiB device budget makes a page cache and a query planner the wrong tool
for a read-only music index.

The host database lives at:

```text
~/Library/Application Support/Helikon Companion/
  library.sqlite
  library.sqlite-wal
  library.sqlite-shm
  blobs/ab/cd/<sha256>            # normalized artwork, transcripts, raw responses
  downloads/
    podcasts/<feed-uuid>/<episode-uuid>.<ext>
    audiobooks/<book-uuid>/<section-uuid>.<ext>
  backups/<device-uuid>/<timestamp>/
    manifest.json
    files/...
  logs/companion.log
  exports/
```

Use WAL mode for normal operation, `foreign_keys=ON`, and
`synchronous=FULL` before a destructive or device-write phase. The database is
included in Time Machine unless the user excludes it; artwork and downloads are
large and may be excluded separately. The app must offer "Export library"
(JSONL + media manifest) so the user is not locked into SQLite.

### 6.2 Schema shape

The schema is normalised around five axes:

1. **Entities:** `artist`, `album`, `track`, `playlist`, `podcast_feed`,
   `podcast_episode`, `audiobook`, `audiobook_section`.
2. **Values:** explicit columns for the fields the UI sorts/filters on; a
   `text_blob` table for long text; an `artwork` table for images.
3. **Provenance:** `source`, `field_provenance`, `match_candidate`,
   `provider_cache`.
4. **State:** `playback_event`, `rating_event`, `podcast_playback`,
   `playlist_item`, `scrobble_queue`.
5. **Operations:** `device`, `sync_session`, `sync_operation`, `conflict`,
   `backup`, `checksum`.

A condensed core schema:

```sql
CREATE TABLE artist (
  id            INTEGER PRIMARY KEY,
  uuid          TEXT NOT NULL UNIQUE,
  mbid          TEXT UNIQUE,
  name          TEXT NOT NULL,
  sort_name     TEXT NOT NULL,
  disambiguation TEXT,
  bio_text_id   INTEGER REFERENCES text_blob(id),
  image_art_id  INTEGER REFERENCES artwork(id),
  created_at    INTEGER NOT NULL,
  updated_at    INTEGER NOT NULL,
  deleted_at    INTEGER
);

CREATE TABLE album (
  id                 INTEGER PRIMARY KEY,
  uuid               TEXT NOT NULL UNIQUE,
  release_group_mbid TEXT,
  release_mbid       TEXT,
  title              TEXT NOT NULL,
  sort_title         TEXT NOT NULL,
  album_artist_id    INTEGER NOT NULL REFERENCES artist(id),
  year               INTEGER,
  release_date       TEXT,
  genre              TEXT,
  label              TEXT,
  catalog_number     TEXT,
  barcode            TEXT,
  country            TEXT,
  disc_count         INTEGER,
  track_count        INTEGER,
  rating             INTEGER CHECK (rating BETWEEN 0 AND 100),
  added_at           INTEGER NOT NULL,
  updated_at         INTEGER NOT NULL,
  deleted_at         INTEGER
);

CREATE TABLE track (
  id               INTEGER PRIMARY KEY,
  uuid             TEXT NOT NULL UNIQUE,
  content_sha256   TEXT,
  device_path      TEXT,
  file_name        TEXT NOT NULL,
  file_size        INTEGER NOT NULL,
  mtime_ns         INTEGER NOT NULL,
  title            TEXT NOT NULL,
  sort_title       TEXT,
  artist_id        INTEGER REFERENCES artist(id),
  album_id         INTEGER REFERENCES album(id),
  album_artist_id  INTEGER REFERENCES artist(id),
  composer_id      INTEGER REFERENCES artist(id),
  recording_mbid   TEXT,
  isrc             TEXT,
  track_no         INTEGER,
  disc_no          INTEGER,
  duration_ms      INTEGER,
  bitrate          INTEGER,
  sample_rate      INTEGER,
  filetype         INTEGER,
  rating           INTEGER CHECK (rating BETWEEN 0 AND 100),
  added_at         INTEGER,
  updated_at       INTEGER NOT NULL,
  deleted_at       INTEGER
);

CREATE TABLE text_blob (
  id               INTEGER PRIMARY KEY,
  sha256           TEXT NOT NULL UNIQUE,
  kind             TEXT NOT NULL,      -- bio, notes, review, credits, show_notes, transcript
  language         TEXT,
  body             TEXT NOT NULL,
  source_id        INTEGER REFERENCES source(id),
  source_record_id TEXT,
  license          TEXT,
  attribution      TEXT,
  revision         TEXT,
  fetched_at       INTEGER,
  UNIQUE(kind, sha256)
);

CREATE TABLE artwork (
  id          INTEGER PRIMARY KEY,
  sha256      TEXT NOT NULL UNIQUE,
  kind        TEXT NOT NULL,           -- cover_front, cover_back, booklet, artist_portrait, artist_band, podcast, audiobook
  mime        TEXT NOT NULL,
  width       INTEGER NOT NULL,
  height      INTEGER NOT NULL,
  master_path TEXT NOT NULL,
  thumb_path  TEXT,
  source_id   INTEGER REFERENCES source(id),
  source_url  TEXT,
  license     TEXT,
  attribution TEXT,
  fetched_at  INTEGER,
  deleted_at  INTEGER
);

CREATE TABLE field_provenance (
  entity_type      TEXT NOT NULL,      -- track, album, artist
  entity_id        INTEGER NOT NULL,
  field            TEXT NOT NULL,      -- title, artist, year, bio, cover, ...
  source_id        INTEGER REFERENCES source(id),
  source_record_id TEXT,
  confidence       REAL,
  value_hash       TEXT,
  fetched_at       INTEGER,
  locked           INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(entity_type, entity_id, field, source_id)
);
```

Appendix A contains the full DDL, including podcasts, playlists, play history,
sync operations and conflicts.

### 6.3 Invariants

The host database must maintain these invariants:

1. **Every external field has provenance.** If `source_id` is null, the value
   came from the user or from a file and must say so.
2. **User edits are never silently replaced.** A `locked` field is skipped by
   provider refresh until the user unlocks it.
3. **A deleted entity is soft-deleted.** `deleted_at` keeps history for the
   journal, playlists and conflicts. A hard delete requires an explicit
   "forget" action and a backup.
4. **Text and art are content-addressed.** The same biography or cover is
   stored once; references are by ID.
5. **Path is not identity.** A track can be renamed; `content_sha256` and
   recording identity survive the rename.
6. **A sync operation is recorded before it starts.** If the app crashes, the
   operation can be found and rolled back or resumed.
7. **No network value is stored without `fetched_at`.** Stale data is visibly
   stale.
8. **The host database is exportable.** A future version must be able to read
   the export without SQLite.

### 6.4 From host rows to HDB records

The HDB builder is a projection, not a second source of truth:

| HDB v1 field | Host source | Rule |
|---|---|---|
| `track.uid` | `track.device_path` | FNV-1a of the NFD device path; must match `tools/hdb_build.py` |
| `track.path` | `track.device_path` | NFD, `/` separators, leading `/` from the volume root |
| `track.title` | `track.title` | NFC; fallback to file name if empty |
| `track.artist_id` | `track.artist_id` | artist table; "Unknown Artist" is explicit |
| `track.album_id` | `track.album_id` | album table; "Unknown Album" is explicit |
| `track.genre` | `track.genre` | first genre for v1; full list stays host-side |
| `track.composer` | `track.composer_id` | display name of the composer |
| `track.duration_ms` | measured | do not substitute provider duration |
| `track.artwork_id` | album front art | 96×96 RGB565; no art means `0xFFFFFFFF` |
| `album.rating` | host album rating | 0–100; 0 means unrated |
| `artist.bio` | short bio | only a bounded summary; full text in sidecar |
| `artist.image_id` | 96×96 portrait | full/band image in sidecar |

The builder must be deterministic: same host rows, same bytes. It must write a
temporary file, `fsync`, verify the CRC, and only then rename.

### 6.5 Migrations, integrity and recovery

- `PRAGMA user_version` is the schema version. Migrations are forward-only and
  transactional. Before a migration, copy the database and verify the copy.
- `PRAGMA integrity_check` runs on launch if the previous shutdown was unclean;
  a full check runs on demand. A corrupt database is restored from a `.bak` or
  from the last export; it is never silently recreated empty.
- `PRAGMA foreign_key_check` runs after migration and before a device sync.
- WAL is checkpointed before backup and before a destructive operation.
- A missing blob is a `DB_STALE` condition: the row remains, the UI shows a
  placeholder, and the Library Doctor offers a re-fetch. Do not delete the row
  because a cache file vanished.

### 6.6 Size and scale targets

These are design targets, not measurements:

| Object | Target | Notes |
|---|---:|---|
| 10k tracks, 2k albums, 2k artists in SQLite | 20–40 MB | excluding blobs and downloads |
| HDB v1 for 10k tracks | 2–4 MB | fixed records + strings |
| 96×96 art for 2k albums | 36.9 MB | 18,432 bytes each, deduplicated |
| 96×96 portraits for 2k artists | 36.9 MB | only where a portrait exists; deduplicated |
| Sidecar text for 2k albums + 2k artists | 5–15 MB | 1–4 KB per item |
| Optional 160×160 art for 2k albums | 102.4 MB | 51,200 bytes each; opt-in |
| 500 podcast episodes, 50 MB each | 25 GB | retention and selection are mandatory |
| Host database with 10k tracks and provenance | 50–100 MB | depends on text and raw cache |

The device must never be asked to hold the optional large-art set unless the
user chooses it and free space is checked. The default sync is the HDB v1
96×96 set plus text sidecars.

The T-088 10k-album/10k-artist target is a browse target, not the same as a
10k-track library. Ten thousand album covers at 96×96 would be 184 MB; ten
thousand portraits would be another 184 MB. The catalog must open with or
without art. The builder skips missing art, deduplicates identical images, and
exposes an optional artwork budget; a 10k/10k library with no art is still a
valid HDB, and a library with art is not allowed to exhaust the volume.

---

## 7. The device metadata contract

### 7.1 Why not put everything in HDB v1

HDB v1 has no field for an album description, a review, a credit list, a
release MBID, a barcode or a second artwork size. Adding fields to the fixed
records would break the frozen format and the tested reader. Storing long text
in the string pool would work for the artist biography (already a string
reference) but not for album-level data, and would make every catalog rebuild
carry data that the list view never reads.

The device needs two different things:

- **browse data** — small, fixed, indexed, read on every list open;
- **detail data** — larger, optional, read only when the user opens a page.

Trying to serve both from one fixed record is how a 10k-track catalog becomes
slow and bloated.

### 7.2 The recommended contract: v1 catalog + generation-bound sidecars

Keep `/.helikon/music.hdb` byte-compatible with HDB v1. Add a sidecar set:

```text
/.helikon/music.hdb          HDB v1: browse index and 96×96 art
/.helikon/music.meta         HMTX: notes, reviews, biographies, credits, provenance
/.helikon/music.art          HART: optional larger artwork
/.helikon/library.json       generation, file sizes, SHA-256, counts
/.helikon/play.journal       runtime play/rating events (T-067)
```

`library.json` is a small host-written manifest, not a boot dependency:

```json
{
  "schema": 1,
  "generation": 42,
  "created_at": 1762400000,
  "device_uuid": "7d3c…",
  "counts": { "tracks": 10000, "albums": 2000, "artists": 2000 },
  "files": {
    "music.hdb": { "size": 3211264, "sha256": "…", "crc32": "…" },
    "music.meta": { "size": 4123456, "sha256": "…", "crc32": "…" },
    "music.art": { "size": 48123456, "sha256": "…", "crc32": "…" }
  }
}
```

The sidecar is **generation-bound** to the catalog. HDB v1 already has a
`generation` field in its header. `music.meta` and `music.art` headers carry
`catalog_generation` and a CRC. The firmware reader:

1. opens `music.hdb`, validates magic/version/header and reads `generation`;
2. opens `music.meta`, checks magic, version, `catalog_generation` and CRC;
3. if the generation or CRC does not match, ignores the sidecar and shows the
   base HDB page;
4. never attempts a repair, migration or partial parse.

This gives atomicity without changing HDB v1: a new catalog with an old sidecar
is a valid catalog without notes; an old catalog with a new sidecar is a valid
catalog without notes. The user sees a complete base library in both cases.

Every successful build must assign a new `generation` and copy it into the
sidecars and `library.json`. Reusing a generation for different content is a
builder bug: the firmware could pair new text with the wrong album. A content
hash can be used as a secondary check; the generation is the primary binding.

### 7.3 HMTX — the editorial sidecar

`HMTX` is a packed, length-prefixed text format. It is deliberately not JSON:
the device should not need a general JSON parser, dynamic allocation or a
schema validator to show a biography.

```text
Header (40 bytes, little-endian)
  0   u8[4]  "HMTX"
  4   u16    version = 1
  6   u16    header_size = 40
  8   u32    catalog_generation
  12  u32    record_count
  16  u32    directory_offset
  20  u32    strings_offset
  24  u32    strings_bytes
  28  u32    text_offset
  32  u32    text_bytes
  36  u32    crc32 (whole file, this field zero)

Directory record (32 bytes)
  0   u32    owner_index        index in the catalog table named by owner_kind
  4   u16    owner_kind         1 album, 2 artist, 3 track, 4 podcast feed,
                                5 podcast episode, 6 book, 7 book section
  6   u16    text_kind          1 notes, 2 review, 3 bio, 4 credits,
                                5 show_notes, 6 transcript, 7 facts
  8   u32    text_offset
  12  u32    text_len
  16  u32    language_offset
  20  u32    source_offset
  24  u32    license_offset
  28  u32    attribution_offset

Strings section
  language, source, license, attribution: u16 length + UTF-8, 4-byte aligned
Text section
  UTF-8, no NUL terminator; max 65,535 bytes per record
```

Rules:

- `owner_index` is valid only for the matching HDB generation and table. A
  rebuilt HDB gets a new generation and a rebuilt sidecar.
- `text_kind` is an enum, not free text. Unknown kinds are ignored.
- Text is NFC. Line endings are `\n`. HTML is not stored; show notes are
  sanitised to a small plain-text subset on the host.
- A missing record is normal: the UI shows "No notes" or "No credits", not an
  error.
- The whole file is CRC-32/ISO-HDLC (the zlib polynomial) checked on open. A
  failed check means "ignore sidecar", not "repair".
- The file is read lazily: the directory is loaded into a bounded RAM window
  (or searched by binary search if sorted by `(owner_kind, owner_index,
  text_kind)`), and one text record is read at a time.

### 7.4 HART — optional larger artwork

`HART` stores images larger than the HDB 96×96 thumbnails:

```text
Header (32 bytes)
  0   u8[4]  "HART"
  4   u16    version = 1
  6   u16    header_size = 32
  8   u32    catalog_generation
  12  u32    record_count
  16  u32    directory_offset
  20  u32    blob_offset
  24  u32    blob_bytes
  28  u32    crc32

Record (40 bytes)
  0   u32    art_id
  4   u16    owner_kind         1 album, 2 artist, 3 podcast, 4 audiobook
  6   u16    role              1 front, 2 back, 3 booklet, 4 portrait, 5 band
  8   u16    format            1 RGB565
  10  u16    width
  12  u16    height
  14  u16    reserved
  16  u32    blob_offset
  20  u32    blob_len
  24  u32    blob_crc32
  28  u32    source_offset
  32  u32    license_offset
  36  u32    attribution_offset
```

The 96×96 HDB art remains the list/thumbnail path. `HART` is read only for the
visible detail page, one image at a time. All CRCs in `HART` and `HMTX` are
CRC-32/ISO-HDLC (zlib). The default policy is:
- 96×96 for every album/artist (mandatory, small);
- 160×160 album detail (optional, opt-in);
- 320×70 artist band (optional, opt-in, size-capped);
- no 320×320 cover unless the user explicitly asks for it and free space
  permits.

### 7.5 Podcasts and audiobooks: separate catalogs

Music, podcasts and audiobooks have different record shapes and different
lifetimes. Do not force them into `music.hdb`.

```text
/.helikon/podcasts.hdb     HLPD: feeds, episodes, chapters, state references
/.helikon/podcasts.meta    HMTX: show notes, transcripts, credits
/.helikon/podcasts.journal podcast state journal (device -> host)
/.helikon/books.hdb        HLBK: books, sections, chapters
/.helikon/books.meta       HMTX: descriptions, credits, chapter titles
```

A separate catalog means:
- opening Music never parses podcast records;
- a podcast refresh never invalidates the music catalog;
- the host can sync one media type without rebuilding the others;
- a corrupt podcast catalog does not make the music library unreadable.

`HLPD` and `HLBK` follow the same design rules as HDB: fixed records, string
pool, precomputed indexes, CRC, generation. Their record layouts are in
Appendix C. They are new formats, so they need their own frozen specification
before implementation; this paper proposes the shape, not a release format.

### 7.6 What the device shows when data is missing

| Missing data | Device behaviour |
|---|---|
| No cover | Typographic placeholder; title/artist still readable |
| No album notes | Notes tab shows "No notes"; Credits tab may still exist |
| No artist biography | Artist overview shows facts/albums, no empty prose |
| No rating | Rating control shows unrated; 0 is not "zero stars" |
| No genre | Genre line hidden; no "Unknown" clutter unless the field is required |
| No podcast show notes | Episode page shows title, date, duration and actions |
| No chapters | Chapter button absent; skip buttons still work |
| Sidecar generation mismatch | Base catalog page; no error dialog |
| HDB CRC failure | `DB_STALE`; keep the previous generation if available; host rebuild |
| Podcast catalog corrupt | Podcasts unavailable; Music remains available |

A missing field is not an error. A corrupt file is an error with a safe state.

---

## 8. Transfer and synchronization

### 8.1 Transport options

| Option | What it is | Writes go through | Status | Use |
|---|---|---|---|---|
| A. macOS Disk Mode | The iPod mounts as an HFS+ volume; the Mac writes files | macOS HFS+ driver, then the device reads them | D-09 proposed first path; host MVP tests only | macOS v1 media, catalog and sidecar transfer |
| B. Read-only LUN | The device exposes a read-only mass-storage LUN | none | Planned T-076 | Inspection, backup, diagnostics; never writes |
| C. Vendor service mode | A framed USB protocol; the device performs file operations through its VFS and transaction engine | Device VFS + HFS+ transaction engine | Proposed T-077/D-38 | Cross-platform writes, atomic state exchange, future |
| D. Writable LUN | The host writes raw blocks/sectors | Host, bypassing the device | Rejected | Never |

The paper's recommendation is **A first, C later, B as a read-only fallback,
D never**. The host data model and file contract are transport-independent:
they describe artifacts and operations, not USB packets. A service mode can be
added without rewriting the library schema.

**The Disk Mode caveat.** macOS writing an HFS+ volume does not use the
firmware's `hfs_txn` writer. It uses macOS's HFS+ implementation and journal.
That is a valid host path for ordinary files, but it does not prove that the
firmware's writer is crash-consistent, and it cannot guarantee that a device
power cut during a host write leaves the firmware's view consistent. The app
must therefore:
- require a clean mount and a clean unmount;
- verify every file after writing;
- keep a backup of any replaced file;
- never write while the device is playing or while the volume is mounted by
  another process;
- run `fsck_hfs -fn` before a destructive catalog replacement or a firmware
  install, and record the result;
- treat a dirty or read-only volume as a hard stop.

### 8.2 Device discovery and identity

The app must not identify a device by its volume name ("IPOD") or by a path
that happens to contain `/.helikon`. It should use Disk Arbitration
notifications, then read:

```text
/.helikon/device.json
```

Proposed content:

```json
{
  "schema": 1,
  "device_uuid": "7d3c…",
  "model": "iPod Classic 6G",
  "board": "820-…",
  "serial_hash": "sha256:…",
  "firmware_version": "0.1.0",
  "firmware_sha256": "…",
  "hdb_version": 1,
  "capabilities": ["hdb_v1", "play_journal_v1", "meta_hmtx_v1"],
  "free_bytes": 61234567890,
  "journal_state": "clean",
  "last_boot": 1762400000,
  "locale": "en-GB",
  "timezone_offset": 0
}
```

Rules:

- `device_uuid` is random at install and survives normal use. It is not a
  serial number.
- `serial_hash` is a salted hash for support correlation, not a published ID.
- `capabilities` is the only basis for feature negotiation. A missing feature
  is unsupported; the host does not guess.
- If `device.json` is missing, the app treats the volume as "unrecognized
  Helikon volume": read-only inspection only, no catalog write, no install.
- The volume UUID from `diskutil` is stored as a second identity for the same
  physical volume. A volume clone has a different UUID; the app must not
  assume it is the same device.

### 8.3 Capability negotiation

| Capability | Meaning | If absent |
|---|---|---|
| `hdb_v1` | Firmware can read `music.hdb` v1 | No music catalog sync; files only, or refuse |
| `meta_hmtx_v1` | Firmware can read `music.meta` | Base metadata only; do not write sidecars |
| `art_hart_v1` | Firmware can read `music.art` | 96×96 HDB art only |
| `play_journal_v1` | Firmware writes `play.journal` | No play-count merge; ratings device-side only |
| `podcast_hdpd_v1` | Firmware can read `podcasts.hdb` | No podcast sync |
| `podcast_state_v1` | Firmware writes `podcasts.journal` | No podcast resume merge |
| `service_mode_v1` | Framed service mode is available | Disk Mode only |
| `firmware_install_v1` | Signed install path exists | No install action |

The app refuses to write a newer artifact to an older firmware. It does not
"try it and see". A downgrade is an explicit user action with a backup.

### 8.4 The sync state machine

```text
IDLE
  → DISCOVERED          volume mounted, device.json read
  → INSPECTING          read catalog headers, manifest, free space, journal
  → PLANNING            compute typed operations from host/device diff
  → PREFLIGHT           space, journal, schema, backup, conflicts
  → BACKING_UP          only for replace/delete/install
  → STAGING             write temp files, hash, fsync
  → VERIFYING           re-read and hash; verify CRC; verify counts
  → COMMITTING          atomic renames; catalog last on add, first on delete
  → CLEANING            remove temp files, write report, optionally eject
  → DONE

Any state → BLOCKED      a precondition failed; no partial commit
Any state → CANCELLED    user cancelled; roll back temp files
Any state → FAILED       I/O or verification failure; keep previous state
STAGING/VERIFYING → INTERRUPTED  device removed or app crashed; resume on next run
```

The state machine is persisted in `sync_session` with an operation log. A
second sync cannot start while a session is `STAGING`, `VERIFYING` or
`COMMITTING`.

### 8.5 The sync plan

Each operation has a stable ID and a type:

```text
COPY_FILE         host file → device path
REPLACE_FILE      host file → device path, backup old
DELETE_FILE       device path → host trash/backup
MKDIR             device directory
REBUILD_HDB       host rows → new music.hdb generation
REBUILD_META      host rows → new music.meta generation
REBUILD_ART       host rows → new music.art generation
IMPORT_JOURNAL    device play.journal → host events
EXPORT_PODCAST    host feed/episode rows → podcasts.hdb
DOWNLOAD_EPISODE  network → host cache (not a device op)
WRITE_MANIFEST    generation, hashes, counts, source versions
EJECT             safe unmount
```

Plan rules:

1. **Add:** copy media, then sidecars, then HDB, then manifest.
2. **Replace:** copy to temp, verify, rename old to `.bak`, rename new, verify
   again, then update HDB; delete `.bak` only after the HDB commit succeeds.
3. **Delete:** remove the HDB reference first (new generation), then delete the
   media after a grace period or in the same session if the user confirmed.
   A dangling media file is harmless; a dangling catalog reference is a
   playback error.
4. **Metadata-only change:** rebuild the HDB/sidecar; do not re-copy audio.
5. **No-op:** if the destination hash matches and the catalog generation is
   current, skip.

The plan is a DAG, not a flat list: HDB depends on all media and sidecars;
manifest depends on HDB and sidecars; delete-media depends on the HDB that no
longer references it.

### 8.6 Free space and preflight

Before any write:

```text
required = sum(new file bytes)
         + sum(replacement bytes until old is deleted)
         + sidecar bytes
         + 2 × HDB size (temp + old)
         + 64 MiB reserve
```

If `free_bytes < required`, the sync is blocked before the first byte. The app
offers a plan to delete old podcasts, remove optional large art, or reduce the
selected library. It never deletes user music to make room without an explicit
confirmation.

Preflight checks, by operation class:

| Operation | Checks |
|---|---|
| Add media | HFS+ v4, writable, clean journal, free space, path validity, name collision, source hash |
| Replace media | all of the above + backup space + old destination hash + no device journal reference that would be lost |
| Delete media | all of the above + backup or explicit "no backup" + catalog generation + user confirmation |
| Rebuild HDB | HFS+ v4, writable, clean journal, `music.hdb` header valid, enough space for temp + old, no open handle |
| Rebuild sidecar | same + generation match with new HDB + CRC |
| Install firmware | full backup, signed manifest, board/version/hash, DFU path, bootstate, recovery instructions |

### 8.7 Verification and commit

The writer verifies at three levels:

1. **Per-file:** SHA-256 while reading the source and while writing the
   destination; compare size and hash. For large files, hash in 4 MiB chunks
   and `fsync` at the end.
2. **Per-artifact:** HDB CRC-32, sidecar CRC-32, manifest SHA-256; counts match
   the plan.
3. **Per-generation:** a Merkle-style root over the sorted `(path, size,
   sha256)` list in `library.json`. The device does not need to verify the root
   at boot; the host verifies it after write and can verify it on demand.

Atomic rename rules:

- The temp file lives in the same directory as the destination (or in
  `/.helikon/tmp` on the same volume) so rename is atomic on HFS+.
- `fsync` the file, then `fsync` the containing directory.
- `rename(2)` over the existing file is the commit point; keep the old file as
  `.bak` until the new catalog is verified.
- Never use a cross-volume copy-and-delete as a commit.

### 8.8 Journal import and merge

At the **start** of a sync, before building a new catalog:

1. Read `/.helikon/play.journal` from the device.
2. Validate the header, sequence and per-record CRC-16.
3. Discard a corrupt tail; keep the longest valid prefix.
4. Fold events into the host database:
   - play count = sum of valid play events;
   - last position = latest timestamp;
   - rating = latest timestamp; equal timestamps keep the higher value and
     record a conflict note;
   - a renamed path is matched by content hash/recording identity before it is
     treated as a deletion.
5. Archive the consumed journal segment in the host session; do not truncate
   the device journal until the host has acknowledged the merge and the device
   is cleanly ejected.
6. Rebuild the HDB with the merged ratings/play counts.
7. Export the new catalog.

Both journals (`play.journal` and `podcasts/state.journal`) are preallocated,
fixed-size files created or grown through the HFS+ transaction engine. A normal
append writes inside an existing extent and advances a tail offset; it does not
change the file size or the catalog. Rotation, growth or creation is a
transaction and is done at sync time, not during playback. If the journal is
missing, full, or on a read-only volume, the device keeps state in RAM and
reports `READ_ONLY`; it never writes outside the allocated extents.

If the journal is corrupt beyond the first record, the host imports nothing,
reports `PLAY_JOURNAL_CORRUPT`, and asks the user to keep the file for
diagnosis. It never deletes the journal automatically.

### 8.9 Conflict handling

| Conflict | Detection | Resolution |
|---|---|---|
| Same path, different content | Hash mismatch | If host changed and device did not, replace with backup; if device changed and host did not, keep device and report; if both changed, keep both with a conflict suffix and a conflict row |
| Rename vs delete | Path missing + content hash appears at a new path | Treat as rename; preserve play history |
| Duplicate files | Same content hash, different paths | Report duplicates; do not auto-delete; user chooses |
| Rating changed both sides | Device event vs host edit timestamps | Latest wins; equal timestamps higher value + conflict note |
| Playlist changed both sides | Host playlist hash vs device playlist hash (future) | Host wins in v1; future device edit becomes a conflict, never a silent merge |
| Podcast episode identity changed | `guid` changed but enclosure/title same | Keep old episode; add new as a new episode; report |
| Podcast feed URL changed | Canonical URL/podcast:guid | Merge feeds by `podcast:guid`; keep aliases |
| Cover changed | Art hash differs | User choice; default keep user-pinned art; provider art is a candidate |

No conflict resolution deletes the losing copy without a backup or an explicit
user confirmation. The conflict row is durable and visible in the Library
Doctor.

### 8.10 Sync report and logs

Every session writes a `sync-report.json`:

```json
{
  "session_id": "…",
  "device_uuid": "…",
  "started_at": 1762400000,
  "finished_at": 1762400123,
  "result": "ok",
  "operations": 1234,
  "bytes_written": 123456789,
  "files_added": 100,
  "files_replaced": 2,
  "files_deleted": 0,
  "catalog_generation": 42,
  "hdb_sha256": "…",
  "warnings": [],
  "errors": []
}
```

Logs redact serial hashes, full paths outside the library, feed URLs containing
tokens and provider API keys. The report is local; nothing is uploaded.

### 8.11 Service mode: a future transport with the same contract

If D-38 is accepted, the service mode should expose file operations, not
blocks. A proposed frame:

```text
Header (32 bytes, little-endian)
  0   u8[4]  "HLKS"
  4   u16    version
  6   u16    opcode
  8   u32    flags
  12  u32    sequence
  16  u32    payload_len
  20  u32    payload_crc32
  24  u32    session_id
  28  u32    reserved

Opcodes
  HELLO, AUTH, GET_INFO, STAT, LIST, OPEN_READ, READ,
  BEGIN_WRITE, WRITE_CHUNK, COMMIT, ABORT, RENAME, DELETE, MKDIR,
  FSYNC, GET_JOURNAL, GET_HEALTH, BYE
```

Rules:

- The device performs every write through its VFS and `hfs_txn`; the host never
  sends an LBA.
- `BEGIN_WRITE` returns a transaction handle; `COMMIT` is the only durability
  point; `ABORT` leaves the previous state.
- Operation IDs are idempotent; a retry returns the previous result.
- Authentication is a pairing code or a host-generated key, not a fixed
  password. The device shows a confirmation screen for the first pairing.
- The device can refuse writes when the journal is dirty, the battery is below
  the write gate, or the volume is read-only. Refusal is a normal response,
  not a transport error.
- The protocol is not a covert network path: it exposes file operations only.

This is a significant USB-stack and security project. It should not delay the
macOS Disk Mode path.

### 8.12 Backups and restore

Before any destructive operation, the app backs up the affected files to:

```text
~/Library/Application Support/Helikon Companion/backups/<device-uuid>/<timestamp>/
  manifest.json
  files/...
```

The manifest records the original path, size, SHA-256, mtime, HFS+ mode and
the reason. The backup is verified by reading each file back and hashing it.
A backup that fails verification blocks the destructive action. Restore is a
copy-back with verification; it never formats or repairs the volume.

A full-volume backup before a firmware install follows the existing
[`outer-volume-test.md`](outer-volume-test.md) protocol. The Companion may
orchestrate it, but it must not replace the backup gate with a progress bar.

---

## 9. Podcasts: subscription, feed tracking and offline playback

### 9.1 The split

The podcast feature is the clearest example of the curator–consumer split.
D-43 already proposes the shape: the desktop app subscribes to RSS/Atom,
downloads episodes, reads show notes, chapters and artwork, and syncs a bounded
set; the device shows show/episode lists, played state, per-episode resume,
speed, skip intervals, chapters and show notes; a sidecar carries state; the
device never fetches a feed and never uploads listening history.

This paper turns that direction into a data contract.

### 9.2 Feed identity and subscription identity

A podcast has several identifiers, and they do not all mean the same thing:

| Identifier | Scope | Stability | Use |
|---|---|---|---|
| `podcast:guid` | global podcast | high | primary subscription identity when present |
| Canonical feed URL | one feed document | medium | identity when `podcast:guid` is absent; changes on host migration |
| Apple Podcasts ID | Apple directory | high for the directory entry | discovery/lookup; maps to a feed URL |
| Podcast Index feed ID | Podcast Index | medium | discovery/lookup; not a device identity |
| Feed title + author | human | low | display only; never the sole identity |
| Episode `<guid>` | feed-scoped | high in well-behaved feeds | episode identity combined with the feed identity |
| Enclosure URL | file | low | fallback identity; changes with CDN/migration |
| Episode title + pubDate + duration | human | low | last-resort fallback; never silently merge on this alone |

The host table `podcast_feed` stores `feed_url`, `canonical_url`,
`podcast_guid`, `itunes_id`, `podcast_index_id`, title, author, image, language,
explicit flag, refresh policy, ETag, Last-Modified, last/next fetch time,
status and error count. A feed can have aliases; an alias table prevents a
re-subscription from creating a duplicate when the URL changes.

**Deduplication order:** `podcast:guid` → canonical URL → Apple/Podcast Index
ID → normalised title + author, with a user confirmation for the last case.
When two subscriptions merge, keep the older `uuid`, merge episodes by
`(feed_id, guid)`, preserve playback state and mark the loser as an alias. Do
not delete downloaded episodes until the merge is verified.

### 9.3 Feed discovery

The app supports five entry paths:

1. **Paste a feed URL.** Accept `http` and `https`; follow redirects; store the
   final canonical URL; show the original URL as an alias.
2. **Import OPML.** Parse `<outline type="rss" xmlUrl="…" text="…">` and
   folders. OPML 2.0 is a simple XML format; treat it as untrusted XML. Import
   is a preview: show the list, let the user select, then subscribe.
3. **Search Podcast Index.** Use `/search/byterm` (or `/search/bytitle`) with
   the user's key. The API uses `X-Auth-Key`, `X-Auth-Date`,
   `Authorization: sha1(apiKey + apiSecret + unixTime)` and a `User-Agent`
   header [39][40]. Show title, author, artwork, feed URL and `podcast:guid`. Do not
   auto-subscribe on a search result; require a click.
4. **Search Apple Podcasts.** Use the iTunes Search API with
   `media=podcast&entity=podcast`; the result includes `feedUrl` and
   `collectionId`. Apple's terms apply; cache the result; do not use Apple
   artwork as a permanent library asset without checking the terms.
5. **Discover from a website.** Given an HTML page, look for
   `<link rel="alternate" type="application/rss+xml" href="…">` or
   `application/atom+xml`. This is a convenience, not a crawler: one page, one
   request, no recursion.

A feed URL is a potential secret if it contains a private token. Store it in
the database but redact it in logs and the UI's share/export view. Do not send
it to a third-party directory.

### 9.4 The refresh scheduler

The host, not the device, decides when to check a feed.

| Input | Effect |
|---|---|
| `fetch_interval_sec` per feed | default 6 hours; minimum 15 minutes; maximum 24 hours |
| RSS `<ttl>` | hint in minutes; clamp to the same bounds |
| `sy:updatePeriod` / `sy:updateFrequency` | hint; clamp |
| `podcast:updateFrequency` | hint; clamp |
| `ETag` / `Last-Modified` | conditional GET; a 304 is a success |
| Previous failures | exponential backoff: 1 m, 5 m, 15 m, 1 h, 6 h, 24 h, with jitter |
| Network state | do not fetch on an expensive/metered link unless the user allows it |
| App state | refresh on launch only if a feed is due; manual refresh always available |
| User action | "Refresh all" is explicit and rate-limited |

The scheduler stores `next_fetch_at` and a `fetch_generation`. It never fetches
a feed on the UI thread. A feed refresh is a background job that writes a new
feed snapshot and episode delta; the UI sees the delta when it is committed.

**Failure states:** `OK`, `STALE` (last success older than 2× interval),
`HTTP_404`/`HTTP_410` (gone; keep the archive, stop refreshing after a
confirmation), `PARSE_ERROR`, `TLS_ERROR`, `TOO_LARGE`, `RATE_LIMITED`,
`BLOCKED` (SSRF or policy), `AUTH_REQUIRED` (private feed). A stale feed is not
deleted. The user can export the OPML and re-subscribe elsewhere.

### 9.5 Fetch and parse rules

**HTTP:**

- HTTPS preferred; HTTP allowed only with a warning. Never downgrade an HTTPS
  URL to HTTP on redirect.
- Maximum redirects: 5. Re-check the destination against the SSRF policy on
  every hop.
- Connect timeout 15 s; total timeout 30 s; maximum feed body 10 MiB
  (configurable to 32 MiB); decompressed body cap 32 MiB.
- Accept `gzip` and `br`; reject unknown content encodings.
- Validate `Content-Type` loosely (`application/rss+xml`,
  `application/atom+xml`, `application/xml`, `text/xml`, `text/html` for
  autodiscovery) and then parse; do not trust the header alone.
- User-Agent: `HelikonCompanion/<version> (+project URL)`.
- Respect `Retry-After`; do not retry 401/403 without new credentials.

**XML:**

- Disable DTDs and external entities. Reject `<!DOCTYPE>` with an external
  subset; reject `XInclude`.
- Maximum depth 256, maximum elements 100,000, maximum attribute length 8 KiB,
  maximum text node 1 MiB. These are safety caps, not a claim that every valid
  feed fits.
- Parse RSS 2.0 and Atom 1.0. Treat unknown namespaces as ignorable.
- Decode using the XML declaration or HTTP charset; default UTF-8. Reject
  invalid UTF-8; do not silently replace bytes in titles.
- Dates: RFC 822/2822 for RSS, RFC 3339 for Atom; if invalid, use the fetch
  time and mark `date_invalid`.
- Durations: `HH:MM:SS`, `MM:SS`, or seconds; invalid → 0 and a warning.
- HTML in `description`/`content:encoded`: sanitize on the host. Remove
  scripts, styles, iframes, forms, event handlers and external objects; keep
  paragraphs, line breaks, lists, emphasis and links as plain text plus an
  optional link list. The device never renders HTML.

### 9.6 The fields that matter

| Field | RSS/Atom source | iTunes/Podcasting 2.0 source | Device use |
|---|---|---|---|
| Show title | `<channel><title>` | — | list/detail |
| Show author | `<managingEditor>` | `itunes:author`, `podcast:person` (host/guest) | detail/credits |
| Show description | `<description>` | `itunes:summary`, `content:encoded` | show notes summary |
| Show image | `<image><url>` | `itunes:image`, `podcast:image` | 96×96 cover; detail optional |
| Language | `<language>` | — | UI and transcript selection |
| Explicit | `<itunes:explicit>` | not redefined by Podcasting 2.0 1.0; keep the iTunes value | badge, not a filter by default |
| Categories | `<category>` | `itunes:category` | host browse/filter |
| Update hint | `<ttl>`, `sy:*` | `podcast:updateFrequency` | scheduler |
| Episode title | `<item><title>` | `podcast:episode` | list/detail |
| Episode guid | `<guid>` | — | identity |
| Publication date | `<pubDate>` | — | sort |
| Duration | `<itunes:duration>` | — | display/resume |
| Enclosure | `<enclosure url length type>` | `podcast:alternateEnclosure` | download |
| Season/episode | `<itunes:season>`, `<itunes:episode>` | `podcast:season`, `podcast:episode` | sort/label |
| Episode type | `<itunes:episodeType>` | `podcast:episodeType` | full/trailer/bonus badge |
| Show notes | `<description>`, `<content:encoded>` | `podcast:transcript` is separate | Notes page |
| Chapters | — | `podcast:chapters` | chapter list |
| Transcript | — | `podcast:transcript` | transcript page |
| People | — | `podcast:person` | credits |
| Funding/value | — | `podcast:funding`, `podcast:value` | link only |
| Alternate media | — | `podcast:alternateEnclosure` | choose audio |
| Integrity | — | `podcast:integrity` | verify if present |
| Live item | — | `podcast:liveItem` | not supported in v1; mark |

**Enclosure selection.** Prefer an audio enclosure with a supported MIME type
and a reasonable bitrate. If `podcast:alternateEnclosure` offers multiple
audio variants, select the one the user's quality/space setting asks for. If
only a video enclosure exists, do not download it as audio; mark
`UNSUPPORTED_ENCLOSURE` and show the URL.

**Integrity.** If `podcast:integrity` provides an SRI-style hash for the chosen
enclosure, verify it after download. If the hash fails, quarantine the file and
do not transfer it. If no integrity value is present, verify the HTTP length
when available and store a SHA-256 computed by the host.

### 9.7 Episode identity and deduplication

The stable key is:

```text
episode_key = feed_uuid + ":" + normalized_guid
```

If `guid` is absent or changes on every fetch (a known feed bug), fall back to:

```text
episode_key = feed_uuid + ":" + sha256(enclosure_url)
```

If the enclosure URL also changes, fall back to:

```text
episode_key = feed_uuid + ":" + sha256(title + pubDate + duration)
```

and mark the episode `identity_fallback = 1` so the UI can warn. Never merge
two episodes solely because their titles match.

When a feed reuses a GUID for a different enclosure, treat it as an update only
if the title/date/duration are compatible; otherwise create a new episode and
report a feed defect. When an episode disappears from the feed, keep it as
`archived`; do not delete it automatically. The user can prune archived
episodes.

### 9.8 The download manager

The host downloads episodes; the device never does.

**Queue:**

- persisted in `podcast_download` with state `queued`, `downloading`,
  `verifying`, `complete`, `failed`, `cancelled`, `paused`;
- priority: user-requested > next unplayed > newest > prefetch;
- concurrency: 2–3 downloads, 2 per host, configurable;
- resumable with HTTP Range when the server supports it; otherwise restart;
- `.part` file in the destination directory; never expose a partial file to
  the device;
- maximum episode size (configurable; default 1 GiB) and maximum total podcast
  cache (default 50 GiB, hard cap);
- bandwidth limit and "pause on metered network" options.

**Verification:**

1. HTTP status 200/206 and `Content-Length` match (if provided).
2. MIME sniff: `audio/mpeg`, `audio/mp4`, `audio/aac`, `audio/ogg`,
   `audio/opus`, `audio/flac`, `audio/wav`, `audio/x-m4a`, `audio/x-m4b`.
3. Extension derived from MIME, not from the URL alone.
4. Size and SHA-256 stored.
5. `podcast:integrity` hash checked if present.
6. A file that fails verification is quarantined and never transferred.

**Retention:**

- per feed: keep newest N episodes (default 10), keep unplayed, keep pinned,
  keep episodes in a playlist/queue, keep starred;
- global: maximum podcast cache bytes; evict oldest played first;
- never evict the currently playing episode or a pinned episode;
- a deleted episode's device file is removed on the next sync after the host
  catalog no longer references it.

### 9.9 Chapters, transcripts and show notes

**Chapters.** Parse `podcast:chapters` JSON (`version`, `chapters` with
`startTime`, `title`, `img`, `url`, `toc`). Also support ID3 `CHAP`/`CTOC` and
MP4 chapter tracks when the file is MP3/M4B. Normalise to:

```text
chapter { start_ms, title, image_art_id?, url?, toc? }
```

Validate: starts are monotonic, first start is 0 or close, no chapter beyond
duration, max 1,000 chapters. Store in `podcast_chapter`; export to
`podcasts.hdb` and `podcasts.meta`. The device can jump to a chapter and show
its title; chapter images are optional and size-capped.

**Transcripts.** `podcast:transcript` has `url`, `type`, `language`, `rel`.
Supported types: `text/plain`, `text/vtt`, `application/x-subrip`,
`application/json` (Podcasting 2.0 JSON), and `text/html` (sanitize to text).
Fetch on demand or per-feed "sync transcripts"; cap at 5 MiB and 500,000
characters; store the original and a plain-text derivative. The device shows
plain text with timestamps; it does not render VTT styling.

**Show notes.** Sanitize to a bounded plain-text/Markdown subset. Store the
full text host-side and a device excerpt (e.g. 16 KiB) in `HMTX`. A "show
notes" page can scroll; the device does not parse HTML.

**People and credits.** `podcast:person` gives name, role, href, img. Store as
credits; show the top roles on the episode page. This is also useful for the
"artist/guest" search on the host.

**Value/funding.** Store links only. Do not implement payments in v1. Do not
send a value block or a lightning address to the device unless a future feature
explicitly supports it.

**Live items.** `podcast:liveItem` is not supported in v1. Mark the episode
`LIVE_UNSUPPORTED`; do not download a live stream as if it were a file.

### 9.10 The device podcast catalog

The device needs a compact, offline list. Proposed `podcasts.hdb` records:

```text
Feed (64 B)
  uid u64, title_off u32, author_off u32, image_art_id u32,
  episode_count u32, first_episode u32, added_time u32, last_update u32,
  flags u16, reserved u16, podcast_guid_off u32, description_off u32

Episode (96 B)
  uid u64, feed_id u32, title_off u32, pub_date u32, duration_ms u32,
  enclosure_path_off u32, enclosure_size u32, played u8, finished u8,
  flags u8, reserved u8, position_ms u32, speed_x100 u16,
  skip_forward_sec u16, skip_back_sec u16, chapter_count u16,
  art_id u32, notes_off u32, enclosure_sha_off u32, reserved...
```

The exact layout must be frozen before implementation. The important properties
are: feed and episode are fixed records; episode ordering is precomputed
(newest first, season/episode); show notes and transcripts are sidecar text;
state is not stored in the catalog except for a snapshot used to render the
list before the journal is read.

`podcasts.hdb` is a separate file from `music.hdb`, so a podcast refresh never
invalidates the music catalog.

### 9.11 Podcast state: journal plus snapshot

D-43 says a sidecar JSON under `/.helikon/podcasts/` carries state. JSON alone
is not power-cut-safe if the device rewrites it on every pause. The stronger
design is a small journal plus a JSON snapshot:

```text
/.helikon/podcasts/state.journal   append-only, written by the device
/.helikon/podcasts/state.json      snapshot, written on clean shutdown or sync
```

Journal header:

```text
magic "HLPJ", version 1, sequence u32, reserved
```

Record (24 bytes):

```text
episode_uid u64
event u8       1 position, 2 played, 3 unplayed, 4 finished,
               5 speed, 6 skip_forward, 7 skip_back, 8 chapter
flags u8
position_ms u32
timestamp u32
value u16
crc16
```

Rules:

- Append a `position` record on pause, stop, track change and every 15 s while
  playing; flush in sector-sized batches on idle/shutdown/USB.
- Append `played`/`unplayed`/`finished` once per user action.
- The journal is bounded; when full, compact it into `state.json` and start a
  new segment with a higher sequence.
- A corrupt tail is discarded; the valid prefix is imported.
- The host reads the journal **before** building the next podcast catalog, then
  folds it into `podcast_playback`. It writes an acknowledgement record or
  truncates only after a verified merge and a clean eject.
- `state.json` is a debugging/export snapshot, not the only copy of truth.
- A power cut can lose at most the last 15 s of position, not the played flag
  or the resume point from the previous pause.

### 9.12 Podcast sync round-trip

At the start of a sync:

1. Import `state.journal` and `state.json` (journal wins if both exist).
2. Merge by `episode_uuid`:
   - `played`: latest timestamp wins; if equal, `played = true` wins over
     `false`, but a host explicit "mark unplayed" event at a later timestamp
     wins.
   - `position_ms`: latest timestamp wins; equal timestamps keep the larger
     position.
   - `finished`: derived from `played` or `position >= duration - 5 s`; store
     both the user flag and the derived flag.
   - `speed`, `skip_forward_sec`, `skip_back_sec`: latest timestamp wins;
     per-episode overrides a per-show default.
3. Build `podcasts.hdb` from the host rows with the merged state.
4. Export the selected episodes and the new catalog.
5. On the device, the next playback event appends to the journal; the cycle
   repeats.

A 500-episode library must sync without duplicates: the test creates 500
episodes, plays a subset, marks some played on both sides, changes a feed URL,
and verifies one row per episode and no lost state.

### 9.13 Podcast artwork and storage

- Feed image: prefer `itunes:image`/`podcast:image`, then `<image>`. Validate
  1400–3000 px per Apple's requirements [33], but accept smaller images with a
  warning; do not reject an otherwise valid feed because its art is 1200 px.
- Episode image: optional; fall back to show image.
- Device: 96×96 RGB565 for the podcast wall/list; optional 160×160 detail.
- Keep a `podcast_art` provenance row; podcast images are copyrighted by their
  owners. The app does not bundle them.
- Storage: episode media under `/.helikon/podcasts/media/<feed_uuid>/`; do not
  mix it into the music tree, where the music HDB builder might scan it.
- Per-feed size display: episodes, bytes, kept/played counts. The user can see
  why the device is full.

### 9.14 Podcast privacy and network hygiene

- The device never fetches; the host does.
- Feed refresh is an explicit subscription action; the app shows the last
  refresh and the next scheduled refresh.
- No listening history leaves the Mac unless the user opts into scrobbling
  (which is for music, not podcasts) or explicitly exports.
- Podcast analytics prefixes (for example `op3.dev`, `pdst.fm`, `chtbl.com`)
  are visible in the download URL. The app must not silently strip them,
  because that can break the publisher's measurement or the file. It can warn
  and offer to copy the original URL. A future "privacy proxy" would be a
  separate, explicit feature.
- Private feed tokens in URLs are redacted in logs and never included in crash
  reports.
- OPML export is available so the user can leave with their subscriptions.

---

## 10. Audiobooks

Audiobooks are longer, chaptered and often multi-file. They deserve their own
catalog, not a podcast with a different label.

### 10.1 Sources

| Source | What it provides | Licence note |
|---|---|---|
| Open Library | Work/edition metadata, authors, covers, subjects, identifiers | Check the record and cover rights; do not assume every cover is free |
| LibriVox | Volunteer recordings of public-domain texts, hosted by the Internet Archive | LibriVox recordings are public domain; verify the item |
| Internet Archive | Item metadata and files for LibriVox and other public-domain collections | Check each item's rights statement |
| User's own files | DRM-free M4B/MP3/FLAC audiobooks | User's responsibility |

**Not supported:** Audible/DRM-protected audiobooks, commercial audiobook
catalogues without a licence, and any "download the whole store" feature.

### 10.2 The model

```text
book
  uuid, title, sort_title, author(s), narrator(s), language,
  publisher, year, description, cover_art_id, source, license,
  total_duration_ms, section_count, series, series_index

section
  uuid, book_id, index, title, path, duration_ms, size_bytes,
  content_sha256, chapter_start, chapter_end
```

The device catalog `books.hdb` mirrors this with fixed records (Appendix C).
The device shows:

- book list (cover, title, author, progress);
- book page (description, chapters, play/resume);
- chapter list;
- now playing (title, chapter, position, speed, sleep timer);
- bookmarks (optional, host-synced).

Resume is per section plus a global position; a section change is a normal
event. `state.journal` records position for the current section and the book's
global offset. The host folds both.

### 10.3 Download and transfer

- Multi-section items are downloaded as a set; a missing section marks the
  book `incomplete` and prevents "play all" until resolved.
- Ordering is by section index, then track number, then filename; the host
  records the chosen order and does not reorder on every fetch.
- Each section is verified by size and SHA-256; the book total duration is the
  sum of sections (or the item metadata if consistent).
- Cover art follows the same licence rules as music.
- The device's `books.hdb` points to section paths under
  `/.helikon/audiobooks/<book_uuid>/`.
- A 500-section book is the scale test; the catalog must open in bounded memory.

### 10.4 Audiobook state

The same journal/snapshot pattern as podcasts, with book/section IDs instead
of episode IDs. Merge rules: latest position wins, a finished section is
durable, and a book is finished when all sections are finished or the user
marks it. A power cut preserves the last flushed position.

---

## 11. The rest of the Companion

The metadata and sync contract is the foundation for the other product jobs.

### 11.1 Tag editing

The Companion may edit tags **on the host copy**, never on the device in place.

| Format | Tag family | Writer notes |
|---|---|---|
| MP3 | ID3v2.3/v2.4, ID3v1 fallback | preserve unknown frames; handle unsynchronisation; rewrite padding; verify audio frames unchanged where possible |
| M4A/M4B/MP4 | MP4 atoms (`©nam`, `©ART`, `aART`, `©alb`, `trkn`, `disk`, `covr`, `----`) | preserve unknown atoms; update `meta`/`ilst`; do not break chapter tracks |
| FLAC | Vorbis comments, picture block | preserve unknown fields; update STREAMINFO only if needed |
| Ogg Vorbis/Opus | Vorbis comments | preserve unknown fields; re-page correctly |
| APE/WavPack | APE tags | optional; test corpus required |
| WAV/AIFF | limited/no tags | do not invent a tag format; store sidecar metadata |

Safeguards:

1. Copy the file to a temp file in the same directory.
2. Write tags into the temp copy; never modify the source.
3. Verify the audio stream and duration; for formats where a tag rewrite
   changes the file, compare a decoded audio hash or frame count.
4. `fsync`, rename over the destination, keep a backup of the old file.
5. On failure, leave the old file in place and report `TAG_WRITE_FAILED`.
6. There is no in-place device tag write in v1. The only path is host copy →
   tag write → verify → transfer. The device itself never edits audio tags.

The app must not claim "lossless tag editing" for every format. Each writer
has a corpus and a documented supported-tag list.

### 11.2 Album and artist pages

The device UI defined in [`ui.md`](ui.md) needs:

| UI element | Host source | Device artifact |
|---|---|---|
| Album cover | `artwork` front | HDB 96×96; optional HART 160×160 |
| Album title, artist, year, genre | tags + provider | HDB album/track records |
| Rating | user/device | HDB album rating; journal events |
| Description / notes | user / licensed provider | `music.meta` HMTX `notes` |
| Review | user / licensed | `music.meta` HMTX `review` |
| Credits | user corrections / MusicBrainz / Discogs | HMTX `credits` |
| Track list | tags + release | HDB track table |
| Artist portrait | user / Commons / provider | HDB 96×96; HART band optional |
| Artist biography | user / Wikipedia / TheAudioDB | HMTX `bio` + HDB short bio |
| Artist facts | Wikidata / user | HMTX `facts` |
| Related artists | MusicBrainz relationships / user | host-only in v1; not transferred |
| Discography | release table | HDB albums |
| Top tracks | play history / user | host computes; HDB index `recent` or a playlist |

The device page must render with missing data. The Tabs are data-driven: if
there is no notes record, the Notes tab can still exist with an empty state, or
be hidden; the UI must not crash on a missing sidecar.

### 11.3 Playlists

- Host playlists are static or smart. Smart playlists are evaluated on the
  host and exported as static HDB playlist entries; the device does not run a
  query engine.
- Static playlists are ordered lists of stable track UUIDs. Export maps UUID →
  HDB track index; missing tracks are skipped and reported.
- Duplicate tracks in a playlist are allowed with an explicit position.
- M3U8 export is available for interoperability; the native format is a
  versioned JSON or HDB playlist table.
- A playlist rename is metadata; a reorder is a new generation; both are
  host-driven.
- The device can play a playlist, shuffle it and show the next track. Editing
  on the device is a future feature; it must write a playlist journal and
  produce a conflict, never a silent host overwrite.

### 11.4 Play history and scrobbling

The device writes play events to `play.journal`; the host folds them into
`play_history`. Scrobbling is a separate, opt-in projection:

1. A track is scrobble-eligible after a configurable threshold. A common
   Last.fm-compatible rule is: the track is longer than 30 seconds and was
   played for at least half its duration or four minutes, whichever comes
   first. The app documents the rule it uses and lets the user review the
   queue before the first send.
2. The host queues a scrobble with track title, artist, album, album artist,
   duration, timestamp and source.
3. The queue is offline-first, persisted, deduplicated by
   `(track_uuid, started_at)`.
4. When the user enables Last.fm and presses "Send queue", the app sends
   batches of up to 50 scrobbles, in order, with the documented signature.
5. 429/5xx retries are rate-limited and resumable; 26 (suspended key) and
   9/10 (auth) stop the queue and show a clear message.
6. No listening history leaves the machine without this explicit action.

The app must not scrobble from a background process without a visible setting.
The default is off.

### 11.5 Firmware, app, game and skin installation

Installation is the highest-risk Companion feature. The paper keeps the
existing rules:

- signed manifest; verify magic, board, version, load address, size, SHA-256
  and Ed25519 signature before writing or jumping;
- stage under `/.helikon/update/`, flush, read back and verify;
- bootstate promotion through the transaction engine;
- bootloader changes are DFU-only; never `--single`;
- a failed install leaves the old image bootable or fails closed to recovery;
- no A/B rollback claim (D-02/T-050);
- no writable LUN;
- app/game/skin install only after the package format, sandbox and signing
  model exist. Skins are data-only packages; a malformed skin falls back to
  the first-party UI.

The Companion may orchestrate the install but must not hide the recovery
instructions. The install screen shows versions, hashes, free space, backup
status, bootstate and the exact DFU/recovery path.

### 11.6 Backup and restore

- Backup `/.helikon` (catalog, sidecars, state, playlists, logs) and any
  user-selected media.
- Verify every file by reading it back and hashing it.
- Store a manifest with the device UUID, firmware version, HDB generation,
  file list and reason.
- Restore is a copy-back with verification; it never formats, erases,
  repartitions or deletes the journal.
- A backup that fails verification blocks the destructive action. The UI says
  which file failed, not "backup failed".

### 11.7 Health and Library Doctor

The Companion's health page is read-only:

| Check | Source | Action |
|---|---|---|
| Volume is HFS+ v4 | `diskutil`/VFS report | explain `NOT_HFS`; no format |
| Journal clean | device report / `fsck_hfs -fn` when required | ask the user to repair on macOS; no write |
| Free space | volume | plan deletions/retention; no silent delete |
| HDB header/CRC | host read | rebuild on next sync; keep previous generation |
| Sidecar generation/CRC | host read | ignore sidecar; rebuild |
| Checksum sidecars | `/.helikon/checksums` if present | report mismatches; quarantine, don't repair |
| Missing files | catalog vs volume | `DB_STALE`; host rebuild |
| Duplicate tracks | content hash + recording ID | report; user chooses; no auto-delete |
| Missing/malformed tags | tag parser | show queue; user fixes |
| Corrupt artwork | image validation | placeholder; re-fetch |
| Stale podcasts | feed status | show last success; retry |
| Backup age | backup manifest | remind; no automatic backup claim |
| Firmware/loader version | device manifest | show; no update without signed release |

The Library Doctor never auto-deletes, auto-repairs or "optimizes" the library.
It reports and recommends.

### 11.8 Migration and coexistence

The host importer reads Apple, Rockbox and donor state **read-only**:

- Apple iTunesDB: tracks, playlists, ratings, play counts where the format is
  understood; no writes to the Apple database.
- Rockbox: database/tagcache, playlists, settings where documented; no
  Rockbox runtime dependency.
- Donor overlay: export the user-visible state; the overlay is migration
  material, not a runtime format.
- Map what is known, report what is not, preserve the source, verify counts and
  hashes, and offer a rollback to the pre-migration tree.
- The device never reads the old container at runtime.

### 11.9 Settings, search and export

Settings are grouped and explicit:

- **General:** library folder, device name, language, appearance.
- **Metadata:** providers on/off, source priority, auto-match threshold,
  Wikipedia opt-in, artwork source policy.
- **Network:** metered-network policy, proxy (if supported), provider status,
  clear cache.
- **Sync:** what to sync, artwork size, podcast retention, free-space reserve,
  eject after sync.
- **Podcasts:** refresh interval, download concurrency, retention, transcript
  sync.
- **Scrobbling:** Last.fm connection, queue, threshold, privacy notice.
- **Backups:** location, retention, verify, restore.
- **Privacy:** network log, export, delete local data.
- **Licences:** every external source, its terms and the data it provided.

Search on the host uses SQLite FTS5 over title, artist, album, composer,
genre, notes and podcast text. The device uses the four HDB indexes plus
precomputed playlist order; it does not run FTS.

Export formats:

- `library.jsonl` — entities and provenance, one object per line;
- `playlists.m3u8` — interoperable playlists;
- `podcasts.opml` — subscriptions;
- `sync-report.json` — the last session;
- `licenses.json` — sources, licences, attribution;
- optional media files with a manifest.

---

## 12. Security, privacy and legal boundaries

### 12.1 Threat model

| Threat | Vector | Control |
|---|---|---|
| Malicious feed XML | DTD/entity expansion, huge depth, XInclude | disable DTD/external entities; depth/node/text caps; reject external subsets |
| Malicious enclosure | HTML error page, huge file, wrong MIME, path injection | size cap, MIME sniff, extension from MIME, filename sanitization, quarantine |
| SSRF | feed URL/redirect/enclosure points at loopback, private IP, metadata service | resolve host, reject private/link-local/loopback, re-check every redirect, allow only http/https and ports 80/443 by default |
| Image bomb | decompression bomb, huge dimensions, malformed JPEG/PNG | byte/pixel caps, ImageIO, reject unknown formats, strip metadata |
| Path traversal | `../`, absolute paths, NUL, reserved names | canonicalize, reject, NFD, length cap, case-collision check |
| Symlink escape | a library file is a symlink to `/etc` or a device path | do not follow symlinks for device writes; report `UNSUPPORTED` |
| HFS+ case collision | `Song.mp3` and `song.mp3` | detect on host with HFS+ casefold; rename with a stable suffix or report |
| Tampered HDB | modified catalog points at wrong file or art | CRC, generation, manifest SHA-256; device reader bounds checks; host rebuild |
| Spoofed device | a random HFS+ volume with `/.helikon/device.json` | require device UUID + capabilities + catalog header; never trust a path alone; no install on mismatch |
| Stolen Mac | provider tokens, library data | Keychain, FileVault, no plaintext secrets, redacted logs |
| Token leak in logs | feed URL token, API key | redact query strings, headers and key names; structured logging |
| Metadata injection | a title contains control characters/HTML | sanitize, NFC, strip control characters, escape in UI |
| Scrobble privacy | listening history leaves the Mac | opt-in, show what will be sent, local queue, no background send |
| Podcast tracking | analytics prefix in enclosure URL | warn, do not silently rewrite, document |
| Supply chain | malicious dependency/app update | no third-party packages if possible; sign/notarize the app; verify updates |

### 12.2 Privacy rules

- The device has no network stack and no telemetry. The Companion does not
  add one on the device's behalf.
- The app does not contact a provider on launch. Feed refresh is a subscribed
  action; metadata search is a user action; scrobbling is an explicit send.
- The network log shows method, host, path (redacted), status, bytes and time.
  It does not log response bodies or tokens.
- No unique device identifier is published. `serial_hash` is salted and local.
- Crash reports are local. Exporting a support bundle is a manual action with
  a preview and redaction.
- Deleting local data is a real action: remove the library DB, blobs, caches,
  downloads and backups after confirmation; export first if the user wants a
  copy.

### 12.3 Legal and licensing rules

This is engineering research, not legal advice.

- **Code licence:** Helikon-OS is GPL-2.0-or-later. The host app must be
  licence-compatible. Third-party libraries must be reviewed before use.
- **Metadata licences:** MusicBrainz core is CC0; supplementary data and the
  live feed are CC BY-NC-SA 3.0 [23]. Do not use NC data in a way that
  conflicts with the project's distribution. Cover Art Archive images remain copyrighted
  by their owners. Discogs data has CC0 and restricted portions; the API terms
  govern the rest. TheAudioDB and fanart.tv have their own terms. Wikipedia is
  CC BY-SA; Wikidata is CC0; Wikimedia Commons files have per-file licences.
  Open Library and Internet Archive items need per-item checks. Last.fm data
  is for scrobbling under its terms.
- **No bundled art:** The installer must not contain album covers, artist
  photos or podcast covers unless the project has a documented licence. The
  app can cache provider art locally for the user's personal library, but it
  must not redistribute it.
- **Attribution:** The device's Notes/Credits page shows a short source and
  licence line for external text. The app's Licences screen lists every
  provider and the data used. CC BY-SA text keeps its attribution and licence;
  a user edit becomes a user note with a "based on" provenance line.
- **Provider terms can change:** A provider integration is disabled by default
  until its terms are reviewed. The app can disable a provider without losing
  cached user data.
- **No scraping:** Do not scrape AllMusic, Roon, Gracenote, streaming services,
  review sites or artist sites. Use an API with terms or the user's own data.

### 12.4 Data retention

| Data | Default retention | User control |
|---|---|---|
| Host library database | indefinite | export/delete |
| Provider raw cache | 30 days or until re-parse | clear cache |
| Artwork blobs | until no reference | clear unused |
| Podcast media | retention policy / cache cap | per-feed, global |
| Podcast transcripts | until episode removed | per-feed |
| Play history | indefinite | export/clear |
| Scrobble queue | until sent or deleted | send/clear |
| Sync reports | last 20 sessions | clear |
| Backups | user-defined | delete/restore |
| Logs | 7 days | export/clear |

A "delete" that would break a user's library requires a confirmation and a
backup. A cache delete is safe and does not need a backup.

---

## 13. Failure model

### 13.1 Error taxonomy

The existing [`failures.md`](failures.md) taxonomy covers the firmware and
filesystem. The Companion adds host-side codes with the same style: a specific
code, a safe response, and a user action.

| Code | Meaning | Safe response | Never do |
|---|---|---|---|
| `SYNC_DEVICE_NOT_FOUND` | No mounted Helikon volume | Show cable/mode help; no force mount | Do not `diskutil` mount/erase |
| `SYNC_NOT_HELIKON` | Volume lacks `/.helikon/device.json` or catalog | Read-only inspect; explain | Do not write a catalog |
| `SYNC_HFS_DIRTY` | Journal/volume dirty | Stop; ask user to repair on macOS; no write | Do not delete journal, format, erase |
| `SYNC_READ_ONLY` | Volume mounted read-only | Explain; no write | Do not remount read-write by force |
| `SYNC_NO_SPACE` | Plan exceeds free space/reserve | Show required/free; propose retention | Do not delete user files silently |
| `SYNC_PLAN_STALE` | Host/device changed since plan | Re-inspect and re-plan | Do not apply a stale plan |
| `SYNC_IO` | Copy/read/flush failure | Abort op; keep previous file; report | Do not continue a partial commit |
| `SYNC_VERIFY_FAILED` | Hash/size/CRC mismatch | Keep old file; quarantine temp; retry | Do not rename over the old file |
| `SYNC_INTERRUPTED` | Device removed/app crash | Resume/rollback temp on next run | Do not present `.part` as final |
| `SYNC_CONFLICT` | Both sides changed | Keep both/backup; conflict row | Do not overwrite silently |
| `SYNC_SCHEMA_UNSUPPORTED` | Device capability older/newer | Refuse the artifact; explain | Do not guess or downgrade |
| `HDB_CRC` | Catalog CRC mismatch | Ignore generation; keep previous; rebuild host-side | Do not repair in place |
| `HDB_VERSION` | Unknown HDB version | Refuse; explain | Do not parse by guess |
| `HDB_GENERATION_MISMATCH` | Sidecar and catalog differ | Ignore sidecar; show base page | Do not mix records |
| `META_PROVIDER_UNAVAILABLE` | Provider down | Keep cache; mark stale | Do not blank metadata |
| `META_RATE_LIMITED` | 429/503 | Backoff; pause provider | Do not hammer |
| `META_MATCH_AMBIGUOUS` | Low-confidence candidates | Show choices; keep user tags | Do not auto-apply |
| `META_LICENSE_UNKNOWN` | No licence/attribution | Do not transfer by default | Do not assume free |
| `ART_INVALID` | Malformed/oversized image | Placeholder; report | Do not crash or store |
| `TAG_WRITE_FAILED` | Tag writer failed | Keep old file; backup; report | Do not corrupt in place |
| `TAG_UNSUPPORTED` | Format/tag not supported | Explain; keep sidecar metadata | Do not invent a tag |
| `FEED_PARSE` | XML/schema invalid | Keep last good snapshot; mark stale | Do not delete episodes |
| `FEED_TOO_LARGE` | Body/depth/node cap | Reject; report | Do not parse unbounded |
| `FEED_SSRF_BLOCKED` | URL resolves to private/loopback | Reject; explain | Do not fetch |
| `FEED_GONE` | 404/410 | Keep archive; stop refreshing after confirmation | Do not delete state |
| `EPISODE_DOWNLOAD_FAILED` | HTTP/verify failure | Quarantine `.part`; retry | Do not transfer partial |
| `EPISODE_SIZE_MISMATCH` | Length/hash mismatch | Re-download once; then quarantine | Do not accept silently |
| `PODCAST_STATE_CORRUPT` | Journal CRC/tail invalid | Keep valid prefix; report; preserve file | Do not delete the journal |
| `PLAY_JOURNAL_CORRUPT` | Music journal invalid | Keep valid prefix; report | Do not rewrite catalog |

### 13.2 Scenario walk-throughs

**Power cut during a catalog swap.** The new `music.hdb` was written to a temp
file and not renamed: the old catalog remains. If the rename happened but the
sidecar did not, the sidecar generation mismatch is ignored; base metadata is
still valid. The host verifies on the next sync and rebuilds.

**Device unplugged during a media copy.** The destination is a `.part` file;
the old file is untouched. On the next sync, the app finds the `.part`, checks
its hash against the plan, and either resumes or deletes it. It never renames a
partial file.

**Host crashes after replacing a file but before updating HDB.** The new media
file is present but the old HDB still points to the same path; playback uses
the new content. The host detects the HDB generation is stale and rebuilds. If
the old file was renamed to `.bak`, the app keeps it until the next verified
sync.

**HDB CRC fails on the device.** The device shows `DB_STALE`; if the firmware
supports a retained previous generation, it uses that. Otherwise Music is
unavailable and the Companion offers a rebuild. It never scans the whole
filesystem as a fallback in v1.

**Feed returns 304.** Nothing changed; update `last_checked`; do not clear
episodes or metadata.

**Feed returns a 500-character HTML error page with 200 OK.** The parser
rejects it (`FEED_PARSE`), keeps the last good snapshot, and marks the feed
stale. It does not interpret the page as an empty feed and delete episodes.

**Episode enclosure is 50 MB larger than the feed's `length`.** Download to
`.part`; verify actual bytes and SHA-256; if the file is valid audio, accept it
with a warning; if it is HTML, reject. Some feeds have wrong lengths, so
`length` is a hint, not a hard gate.

**Podcast journal tail is corrupt after a power cut.** The host imports the
valid prefix, reports the lost tail, and preserves the whole file for
diagnosis. The last 15 s of position may be lost; played flags from the last
flush survive.

**The clock is wrong on the device.** Device timestamps are monotonic-ish but
not trustworthy across boots. The host stores both the device timestamp and
the host receive time; for conflict resolution, use the device timestamp only
if it is within a sane window, otherwise use the host receive time and mark
`clock_skew`. A device with no RTC can have large skew.

**A podcast feed changes its URL.** The `podcast:guid` matches, so the host
merges the feeds, keeps the old UUID, updates the canonical URL, and preserves
episodes and state. If there is no `podcast:guid`, the host shows a merge
preview and requires confirmation.

**A track is renamed outside the Companion.** The path UID changes; the host
sees the old path missing and the new path unknown. It matches by content hash
and recording ID, offers "this looks like a rename", and preserves play
history. It does not treat the file as deleted until the user confirms or the
grace period expires.

**The user has two iPods.** Each device has its own `device_uuid` and sync
state. The host library is shared; the per-device selection, catalog
generation and backup are separate. A conflict between devices is a host
library conflict, not a device-to-device sync.

---

## 14. Performance, scale and measurement

### 14.1 Workloads

| Workload | Scale | Why it matters |
|---|---:|---|
| Music library | 10,000 tracks, 2,000 albums, 2,000 artists | the D-33/T-088 target |
| Album/artist page | 10,000 albums/artists | open < 200 ms, scroll at 30 fps (device target) |
| HDB build | 10k tracks cold/warm | host builder must not block the UI |
| Initial sync | 20 GB / 10k tracks | USB 2.0 and HFS+ |
| Incremental sync | 100–1,000 tracks | typical library change |
| Podcast library | 500 episodes, 50 feeds | D-43/T-082 acceptance |
| Audiobook | 500 sections | long-book scale |
| Play journal | 50,000 events | months of listening |
| Provider refresh | 100 feeds, 500 metadata jobs | rate limits dominate |

### 14.2 Design targets

These are targets, not measurements:

| Metric | Target | Notes |
|---|---:|---|
| Host library scan, warm | < 5 s for 10k tracks | stat + cache, no tag re-read |
| Host library scan, cold | < 5 min for 10k tracks | depends on disk and tag parser |
| HDB build, warm | < 10 s for 10k tracks | no tag pass; reuse cache |
| HDB build, cold | < 5 min for 10k tracks | tag + art extraction |
| Initial 10k sync | < 30 min for 20 GB | USB 2.0 real throughput; target only |
| Incremental 100-track sync | < 2 min | plan + copy + verify |
| Sync plan | < 1 s for 10k tracks | indexed host DB |
| Feed refresh, 100 feeds | < 2 min at 1 req/s | network-bound; rate limits |
| 500-episode feed parse | < 2 s per feed | bounded parser |
| Album page open (device) | < 200 ms | T-088 target |
| List scroll (device) | 30 fps | T-088 target |
| HDB 10k + 2k album covers | ~40 MB | 96×96 art dominates |
| HDB with 2k artist portraits | ~77 MB | another 36.9 MB of 96×96 art |
| Host DB 10k | 50–100 MB | with provenance/text |

### 14.3 Measurement method

- Host: `os_signpost` intervals around scan, match, fetch, build, copy, verify,
  commit; Instruments Time Profiler and Allocations; SQLite `EXPLAIN QUERY
  PLAN`; a benchmark fixture with 10k synthetic tracks and 2k albums.
- Network: count requests, bytes, 429s and retries; no timing claim without a
  recorded run.
- Device: firmware timing counters, panel photos, `fsck_hfs -fn`, power-cut
  tests. A host simulation is not a device measurement.
- Every published number names the device, firmware hash, HDB generation,
  library size, adapter, cable, macOS version and app version.

No number in this paper is a measured result. The only existing measurements
are the HelikonDB reference corpus (100 tracks, 2.1 s cold, 0.007 s warm) and
the device runs recorded in [`state.md`](state.md) and
[`device-matrix.md`](device-matrix.md); they do not validate this design.

---

## 15. Testing and evidence plan

### 15.1 Test layers

| Layer | What it proves | Where |
|---|---|---|
| Unit | parsers, schema, matching, path rules, state folding | macOS XCTest, no device |
| Fixture | known inputs produce known HDB/sidecar bytes | host |
| Property | planner idempotency, sync ordering, no lost events | host |
| Integration | mounted HFS+ disk images, copy/rename/backup | macOS host |
| Crash injection | kill/truncate at each sync step; recover | macOS host |
| Provider contract | recorded JSON fixtures; optional live smoke | host, opt-in |
| Security | XXE/SSRF/image bomb/path traversal fuzz | host |
| UI | macOS task matrix, accessibility, error states | host |
| Device | real 6G, USB, playback, power cut, `fsck_hfs` | device matrix |
| Long-run | 100 connect/disconnect cycles, 72-hour soak | device |

### 15.2 Fixtures

- **Libraries:** 100 tracks (existing reference), 10k synthetic tracks with
  Unicode/CJK/emoji/Hangul, long names, duplicates, missing tags, multi-disc,
  compilations, classical, 2k albums/artists.
- **HDB:** v1 fixtures with/without art, ratings, bios; corrupted header,
  truncated section, bad CRC, wrong generation.
- **Sidecars:** HMTX/HART valid, mismatched generation, truncated, bad CRC,
  unknown `text_kind`, 65,535-byte text, missing record.
- **Feeds:** 500-episode RSS, Atom, iTunes namespace, Podcasting 2.0
  chapters/transcripts/persons/alternate enclosures, OPML, 304, 404, 410,
  redirect, private token, malformed XML, XXE, huge body, HTML error page,
  duplicate GUID, changed GUID, changed enclosure, missing GUID.
- **Artwork:** JPEG, PNG, progressive JPEG, huge dimensions, decompression
  bomb, wrong MIME, CMYK, alpha, 1×1, 10,000×10,000, EXIF/GPS.
- **Tags:** ID3v1/v2.2/v2.3/v2.4, APE, Vorbis, MP4, ASF, malformed frames,
  multi-value, Unicode, ReplayGain, embedded art.
- **Journals:** valid, wrapped, corrupt tail, bad CRC, sequence gap, duplicate
  events, clock skew.
- **Transfers:** free-space edge, name collision, case collision, symlink,
  read-only, unplug at every state, power cut at commit, stale plan.

No disk images, backups, personal data or secrets are committed to the
repository. Fixtures are generated by scripts and hashed; the repository stores
the generator and the expected result, not the user's library.

### 15.3 Requirements traceability

| ID | Requirement | Proof |
|---|---|---|
| R1 | Device never initiates network | network monitor on device/host; code inspection |
| R2 | 10k-track HDB opens without full scan | host fixture + device timing |
| R3 | HDB v1 remains byte-compatible | golden HDB fixtures; existing tests |
| R4 | Sidecar generation mismatch is safe | fixture + UI test |
| R5 | Add writes media before catalog | operation log + crash injection |
| R6 | Delete removes catalog reference first | operation log + crash injection |
| R7 | Every transfer is SHA-256 verified | integration test |
| R8 | Interrupted transfer resumes or rolls back | crash injection |
| R9 | Conflicts never overwrite silently | property test + UI test |
| R10 | User edits are never replaced | provenance test |
| R11 | Source/licence is stored for external data | schema test |
| R12 | No bundled cover art | release audit |
| R13 | Podcast feed 304/error does not delete episodes | fixture |
| R14 | 500 episodes round-trip without duplicates | integration test |
| R15 | Podcast state survives power cut | device test |
| R16 | Chapters/transcripts parse safely | fixture + fuzz |
| R17 | 10k library sync plan is stable/idempotent | property test |
| R18 | 100 connect/disconnect cycles | device test |
| R19 | No network on launch | network monitor |
| R20 | Scrobbling is opt-in and batched | UI test + recorded HTTP |
| R21 | Install refuses unsigned/wrong board | host test |
| R22 | Recovery instructions are shown | UI test |
| R23 | Logs contain no secrets | log audit |
| R24 | Export/restore works without the app | import test |

### 15.4 What this paper does not prove

- It does not prove that HDB v1 can be read from a production VFS; only the
  reader and the embedded-catalog image are host-tested.
- It does not prove that macOS Disk Mode writes are crash-consistent with the
  firmware writer; they are different writers.
- It does not prove a 10k-track sync time, a 500-episode podcast run, a
  100-cycle USB test or a power-cut result.
- It does not prove any provider's terms are acceptable for the project; that
  is a legal/product review.
- It does not prove the sidecar format is frozen; it is a proposal.

---

## 16. Implementation roadmap

The work should be incremental and tied to the existing tasks. The paper uses
work packages (WP) so it does not invent task IDs in `tasks.md`.

### WP-1 — Capability inventory (read-only)

- Read `/.helikon/device.json`, HDB headers, `library.json`, journals.
- Show device model, firmware, capabilities, free space, catalog generation,
  counts, journal state.
- No write, no network, no installer.
- Exit: one mounted test image and one device read are recorded; no claim
  beyond inspection.

Dependencies: T-083 current reset; T-076 read-only LUN is not required for a
mounted volume.

### WP-2 — Host library and scanner

- SQLite schema, migrations, FTS5.
- File scan with AVFoundation/tag parser; content hash cache; artwork extraction.
- Import existing JSON library if one exists; export JSONL.
- No device write.
- Exit: 10k synthetic scan and 100 real-file scan; integrity/foreign-key tests.

Dependencies: none beyond a macOS host.

### WP-3 — HDB builder port

- Port `tools/hdb_build.py` semantics to the Companion, with golden-byte tests.
- Ratings, biographies, artist images, playlists, artwork dedup.
- Atomic build; CRC; generation.
- Exit: 100-track golden HDB matches the Python reference; 10k warm build
  target; existing reader test passes.

Dependencies: T-066.

### WP-4 — Metadata provider framework

- Provider clients, rate limits, cache, provenance, matching.
- MusicBrainz, CAA, AcoustID first; Discogs, TheAudioDB/fanart, Wikipedia/
  Wikidata/Commons behind explicit settings.
- Candidate review UI; no auto-apply of editorial fields.
- Exit: recorded fixture tests; one live search matrix (manual, opt-in);
  licence/provenance rows; provider disable test.

Dependencies: WP-2.

### WP-5 — Sidecar contract and builder

- Freeze HMTX/HART v1.
- Build from host DB; generation binding; CRC; missing-record behaviour.
- Host reader tests; device reader later.
- Exit: golden sidecars; mismatch/truncation safe; size targets.

Dependencies: WP-3.

### WP-6 — Disk Mode sync

- Device discovery, plan, preflight, backup, staging, verify, commit, report.
- Media files, HDB, sidecars, manifest.
- No install.
- Exit: mounted-image integration; unplug/crash matrix; 100-cycle test;
  `fsck_hfs -fn` clean after a device run.

Dependencies: WP-1, WP-3, WP-5.

### WP-7 — Play journal import and catalog rebuild

- Read `play.journal`; fold events; rebuild HDB; export; acknowledge.
- Rename/delete handling; orphan events.
- Exit: fixture + device power-cut test; play count/rating round-trip.

Dependencies: T-067, WP-6.

### WP-8 — Podcasts

- Feed subscriptions, OPML, Podcast Index/Apple discovery.
- Fetch scheduler, parser, episode DB, download manager, chapters/transcripts.
- `podcasts.hdb` and state journal; sync subset.
- Exit: 500-episode fixture, 304/error tests, round-trip, device power cut.

Dependencies: D-43, T-082, WP-6.

### WP-9 — Audiobooks

- Open Library/LibriVox/IA catalog; multi-section download; `books.hdb`.
- Resume, chapters, bookmarks, sleep timer.
- Exit: 500-section fixture; real public-domain book transfer; device test.

Dependencies: WP-8.

### WP-10 — Tag editing, playlists, scrobbling

- Per-format tag writers, atomic replace, corpus.
- Static/smart playlists, M3U8, HDB playlist export.
- Last.fm auth, queue, batch, opt-in.
- Exit: tag corpus; playlist round-trip; recorded scrobble batch; no network
  on launch.

Dependencies: WP-2, WP-6.

### WP-11 — Install and health

- Signed manifest verification, staging, bootstate, DFU guidance, backup.
- Library Doctor, checksum reports, support bundle.
- App/skin install only after the package/sandbox exists.
- Exit: host tests for bad board/hash/signature; one device install run with
  recovery; not a release without the validation scorecard.

Dependencies: T-051, T-053, T-062, T-064.

### WP-12 — Service mode

- USB composite, framed protocol, auth, VFS operations, transaction commit.
- Host driver; cross-platform.
- Exit: disposable-volume operations, fuzzing, failed-auth tests, device run.

Dependencies: D-38, T-077; do not delay WP-6.

---

## 17. Open questions and proposed decisions

These are proposed for [`decisions.md`](decisions.md); they are not accepted
here, and the owner must decide them. The paper records them so the project
does not silently choose a side.

### 17.1 Proposed decisions

| ID | Proposal | Why it matters |
|---|---|---|
| PD-1 | **Host metadata source and licence policy.** Embedded/user first; MusicBrainz core, CAA, AcoustID, Discogs, TheAudioDB/fanart, Wikidata/Wikipedia/Commons within terms; no scraping of AllMusic/TiVo/Gracenote/Roon/streaming/review sites; no bundled art. | Prevents a legal/ethical mistake and a metadata dependency the project cannot ship. |
| PD-2 | **HDB v1 stays frozen; editorial and larger art live in generation-bound HMTX/HART sidecars; HDB v2 is deferred.** | Keeps the tested reader; gives the UI notes/credits without a format break. |
| PD-3 | **Sync contract.** Plan-first; adds copy media then catalog; deletes remove catalog reference then media; HDB/sidecar generation-bound; manifest last; SHA-256/CRC verify; backup before replace/delete. | Makes interrupted syncs recoverable and keeps the catalog from pointing at missing files. |
| PD-4 | **Podcast state.** Journal (`state.journal`) plus snapshot (`state.json`); latest timestamp per field; played true wins on equal timestamps; position keeps the larger value on equal timestamps; device never fetches. | Meets D-43's round-trip and power-cut requirements without rewriting JSON on every pause. |
| PD-5 | **Host library is SQLite; device is HDB; no SQLite on device.** | Uses the right database for each side; respects D-33. |
| PD-6 | **Artwork policy.** 96×96 RGB565 in HDB v1; larger art opt-in; no bundled proprietary art; provenance/licence per image. | Storage, memory and legal safety. |
| PD-7 | **Identity policy.** HDB v1 UID is path-based; host content hash/recording ID survives rename; podcast feed identity `podcast:guid` > canonical URL > title+author; episode `guid` > enclosure URL > hash. | Prevents duplicate episodes and lost play history. |
| PD-8 | **Network/privacy.** No device network or telemetry; host network only for explicit actions; scrobbling off by default; Keychain for tokens; provider disable. | Aligns with D-48 and the repository's security rules. |
| PD-9 | **Disk Mode first, service mode later.** macOS Disk Mode is the v1 transport; a framed service mode is the cross-platform path; writable LUN remains rejected. | Matches D-09/D-38 and avoids a USB-stack project blocking the app. |
| PD-10 | **Editorial text policy.** User notes/reviews first; Wikipedia only opt-in with attribution; no external reviews without a licence; source/licence line on device. | Answers "where do reviews and bios come from?" without scraping. |

### 17.2 Open questions

1. **D-09 transport:** does the owner want a service mode before Windows/Linux,
   or is macOS Disk Mode the only v1 path?
2. **HDB v2:** is a future fixed-record format worth the migration, or should
   sidecars remain the long-term extension mechanism?
3. **Artwork:** should external art be opt-in per album, per source, or
   globally off by default? What does the owner want the default to be?
4. **Wikipedia:** is a CC BY-SA biography acceptable on the device, given the
   attribution/share-alike obligations, or should the device only show
   user-written facts?
5. **Podcast retention:** what are the default keep counts and global cache
   cap for a 6G with 80 GB, 160 GB and 256 GB drives?
6. **Podcast transcripts:** sync them by default, or only on demand?
7. **Device identity:** is `device_uuid` in `/.helikon/device.json` enough, or
   should the app also bind to the HFS+ volume UUID? What happens after an
   Apple restore?
8. **Multiple Macs:** can two Companions share one library database via a
   sync service, or is the library local to one Mac in v1?
9. **Multiple devices:** is the host library shared across devices with
   per-device selections, or does each device have its own library?
10. **Tag writers:** which library/implementation is acceptable for MP4,
    Vorbis and APE, given the no-third-party-dependency preference?
11. **Last.fm key ownership:** does the project register a client, or does each
    user supply a key? A bundled secret is extractable.
12. **App sandbox/notarization:** which file-access entitlements are needed for
    a mounted volume, and does the app ship outside the Mac App Store?
13. **Service-mode auth:** pairing code, host key, or both? How is a lost host
    key recovered without weakening the device?
14. **Classical model:** how much work/composer hierarchy does the device need
    in v1, and how is it represented in HDB/sidecar?
15. **Podcast value/funding:** display links only, or a later payment feature?
16. **Analytics prefixes:** warn only, or offer an explicit "privacy proxy"
    setting that rewrites common prefixes?
17. **Review licensing:** is there a review source the project is willing to
    license, or is "user review" the only v1 feature?
18. **Backup scope:** does the Companion back up the whole music library or
    only Helikon state and replaced files? The existing protocol requires a
    full backup before install, not before every sync.

---

## 18. Conclusion

The iPod should not become a networked computer, and the Mac should not become
a filesystem driver for the iPod. The durable design is a curator–consumer
split with explicit contracts:

- the Mac owns discovery, acquisition, licensing, normalization, editing,
  backup and build;
- the iPod owns offline playback and a small, durable event journal;
- the music catalog is an immutable, bounded HDB v1 projection;
- editorial text, credits and larger artwork live in generation-bound
  sidecars, not in the frozen records;
- podcasts are host-subscribed, host-downloaded and device-played, with a
  journal/snapshot state model;
- every external field has a source, a licence and a confidence;
- every write is planned, verified and recoverable;
- no external metadata is scraped, bundled or presented as free when it is
  not.

The most important near-term work is not a new database format or a USB
protocol. It is the unglamorous foundation: a host library schema, a faithful
HDB builder port, a sidecar format with a generation check, a transfer planner
with verification and crash recovery, and a metadata policy that the project
can defend. Those pieces make the later features — podcasts, audiobooks,
scrobbling, installation, service mode — additive rather than a rewrite.

The device is at its best when it is a quiet, fast, offline player. The
Companion is at its best when it does the noisy, networked, fallible work and
then hands the device a file it can trust.

---

## 19. References

Repository sources:

1. [`agents.md`](../agents.md) — working contract, hard stops, evidence rules.
2. [`state.md`](state.md) — current phase and evidence.
3. [`tasks.md`](tasks.md) — T-066, T-067, T-082, T-083, T-088.
4. [`decisions.md`](decisions.md) — D-07, D-09, D-33, D-38, D-43, D-48, D-53.
5. [`helikondb.md`](helikondb.md) — HDB v1 format and builder contract.
6. [`companion-app.md`](companion-app.md) — companion product/reset record.
7. [`design-paper.md`](design-paper.md) — §12, §25, §28.3.5–6, §28.4.1–2.
8. [`audio-formats.md`](audio-formats.md) — §5.5 metadata and artwork.
9. [`ui.md`](ui.md) — album/artist views and the 96×96 sleeve.
10. [`security.md`](security.md) — secrets, network and device rules.
11. [`failures.md`](failures.md) — error taxonomy and safe responses.
12. [`testing.md`](testing.md) — host/device test layers.
13. [`file-map.md`](file-map.md) — module ownership.
14. [`goals.md`](goals.md) — goals 19 and 20.
15. [`roadmap.md`](roadmap.md) — phases 6–9.
16. [`invariants.md`](invariants.md) — safety invariants.
17. [`outer-volume-test.md`](outer-volume-test.md) — backup and device-write gate.
18. [`../tools/desktop/README.md`](../tools/desktop/README.md) — current app
    shell and build.
19. [`../tools/hdb_build.py`](../tools/hdb_build.py) — reference HDB builder.
20. [`../firmware/db/hdb.c`](../firmware/db/hdb.c) — firmware HDB reader.

External sources (accessed 2026-11-06; terms and limits can change):

21. MusicBrainz API — <https://musicbrainz.org/doc/MusicBrainz_API>.
22. MusicBrainz rate limiting — <https://musicbrainz.org/doc/MusicBrainz_API/Rate_Limiting>.
23. MusicBrainz data licence — <https://musicbrainz.org/doc/About/Data_License>.
24. Cover Art Archive API — <https://musicbrainz.org/doc/Cover_Art_Archive/API>.
25. Cover Art Archive home and copyright notice — <https://coverartarchive.org/>.
26. AcoustID web service — <https://acoustid.org/webservice>.
27. Chromaprint — <https://acoustid.org/chromaprint>.
28. Discogs API — <https://www.discogs.com/developers>.
29. Discogs API terms — <https://support.discogs.com/hc/articles/360009334593-API-Terms-of-Use>.
30. TheAudioDB API — <https://theaudiodb.com/free_music_api>.
31. fanart.tv API — <https://fanart.tv/api-docs/>.
32. Apple iTunes Search API — <https://performance-partners.apple.com/search-api>.
33. Apple Podcasts RSS requirements — <https://podcasters.apple.com/support/823-podcast-requirements>.
34. Deezer API — <https://developers.deezer.com/api>.
35. Deezer terms — <https://developers.deezer.com/termsofuse>.
36. Last.fm API — <https://www.last.fm/api/intro>.
37. Last.fm scrobbling — <https://www.last.fm/api/scrobbling>.
38. Last.fm `track.scrobble` — <https://www.last.fm/api/show/track.scrobble>.
39. Podcast Index API — <https://podcastindex-org.github.io/docs-api/>.
40. Podcast Index API source — <https://github.com/Podcastindex-org/docs-api>.
41. Podcast Index terms — <https://github.com/Podcastindex-org/legal/blob/main/TermsOfService.md>.
42. Podcasting 2.0 namespace — <https://podcastnamespace.org/>.
43. Podcasting 2.0 specification — <https://github.com/Podcastindex-org/podcast-namespace/blob/main/docs/1.0.md>.
44. Podcasting 2.0 transcript tag — <https://podcasting2.org/docs/podcast-namespace/tags/transcript>.
45. RSS 2.0 specification — <https://www.rssboard.org/rss-specification>.
46. Atom RFC 4287 — <https://www.rfc-editor.org/info/rfc4287>.
47. OPML 2.0 — <http://opml.org/spec2.opml>.
48. Open Library APIs — <https://openlibrary.org/developers/api>.
49. Open Library covers API — <https://openlibrary.org/dev/docs/api/covers>.
50. LibriVox — <https://librivox.org/>.
51. Internet Archive metadata API — <https://archive.org/developers/md-read.html>.
52. Wikidata licensing — <https://www.wikidata.org/wiki/Wikidata:Licensing>.
53. Wikimedia Commons licensing — <https://commons.wikimedia.org/wiki/Commons:Licensing>.
54. MediaWiki API — <https://www.mediawiki.org/wiki/API:Main_page>.
55. Creative Commons attribution practices — <https://wiki.creativecommons.org/wiki/Recommended_practices_for_attribution>.
56. SQLite WAL — <https://www.sqlite.org/wal.html>.
57. SQLite FTS5 — <https://www.sqlite.org/fts5.html>.
58. SQLite PRAGMA — <https://www.sqlite.org/pragma.html>.
59. Apple HFS+ TN1150 — <https://developer.apple.com/library/archive/technotes/tn/tn1150.html>.
60. Apple Disk Arbitration — <https://developer.apple.com/documentation/diskarbitration>.

---

## Appendix A — Host SQLite DDL (core)

The following is a starting schema, not a frozen migration. It is deliberately
normalised enough to test the contract and small enough to review. File paths
are relative to the Companion's Application Support directory unless stated
otherwise.

```sql
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
PRAGMA user_version = 1;

CREATE TABLE schema_migration (
  version     INTEGER PRIMARY KEY,
  applied_at  INTEGER NOT NULL,
  note        TEXT NOT NULL
);

CREATE TABLE source (
  id                   INTEGER PRIMARY KEY,
  key                  TEXT NOT NULL UNIQUE,
  display_name         TEXT NOT NULL,
  kind                 TEXT NOT NULL,      -- embedded, user, provider, device
  base_url             TEXT,
  license              TEXT,
  attribution_required INTEGER NOT NULL DEFAULT 0,
  enabled              INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE text_blob (
  id               INTEGER PRIMARY KEY,
  sha256           TEXT NOT NULL UNIQUE,
  kind             TEXT NOT NULL,
  language         TEXT,
  body             TEXT NOT NULL,
  source_id        INTEGER REFERENCES source(id),
  source_record_id TEXT,
  license          TEXT,
  attribution      TEXT,
  revision         TEXT,
  fetched_at       INTEGER,
  UNIQUE(kind, sha256)
);

CREATE TABLE artwork (
  id          INTEGER PRIMARY KEY,
  sha256      TEXT NOT NULL UNIQUE,
  kind        TEXT NOT NULL,
  mime        TEXT NOT NULL,
  width       INTEGER NOT NULL,
  height      INTEGER NOT NULL,
  master_path TEXT NOT NULL,
  thumb_path  TEXT,
  source_id   INTEGER REFERENCES source(id),
  source_url  TEXT,
  license     TEXT,
  attribution TEXT,
  fetched_at  INTEGER,
  deleted_at  INTEGER
);

CREATE TABLE artist (
  id             INTEGER PRIMARY KEY,
  uuid           TEXT NOT NULL UNIQUE,
  mbid           TEXT UNIQUE,
  name           TEXT NOT NULL,
  sort_name      TEXT NOT NULL,
  disambiguation TEXT,
  bio_text_id    INTEGER REFERENCES text_blob(id),
  image_art_id   INTEGER REFERENCES artwork(id),
  created_at     INTEGER NOT NULL,
  updated_at     INTEGER NOT NULL,
  deleted_at     INTEGER
);

CREATE TABLE album (
  id                 INTEGER PRIMARY KEY,
  uuid               TEXT NOT NULL UNIQUE,
  release_group_mbid TEXT,
  release_mbid       TEXT,
  title              TEXT NOT NULL,
  sort_title         TEXT NOT NULL,
  album_artist_id    INTEGER NOT NULL REFERENCES artist(id),
  year               INTEGER,
  release_date       TEXT,
  genre              TEXT,
  label              TEXT,
  catalog_number     TEXT,
  barcode            TEXT,
  country            TEXT,
  disc_count         INTEGER,
  track_count        INTEGER,
  rating             INTEGER CHECK (rating BETWEEN 0 AND 100),
  added_at           INTEGER NOT NULL,
  updated_at         INTEGER NOT NULL,
  deleted_at         INTEGER
);

CREATE TABLE track (
  id              INTEGER PRIMARY KEY,
  uuid            TEXT NOT NULL UNIQUE,
  content_sha256  TEXT,
  device_path     TEXT,
  file_name       TEXT NOT NULL,
  file_size       INTEGER NOT NULL,
  mtime_ns        INTEGER NOT NULL,
  title           TEXT NOT NULL,
  sort_title      TEXT,
  artist_id       INTEGER REFERENCES artist(id),
  album_id        INTEGER REFERENCES album(id),
  album_artist_id INTEGER REFERENCES artist(id),
  composer_id     INTEGER REFERENCES artist(id),
  recording_mbid  TEXT,
  isrc            TEXT,
  track_no        INTEGER,
  disc_no         INTEGER,
  duration_ms     INTEGER,
  bitrate         INTEGER,
  sample_rate     INTEGER,
  filetype        INTEGER,
  rating          INTEGER CHECK (rating BETWEEN 0 AND 100),
  added_at        INTEGER,
  updated_at      INTEGER NOT NULL,
  deleted_at      INTEGER
);

-- source rows are seeded for 'embedded', 'user', 'device' and each provider;
-- source_id is therefore never NULL.
CREATE TABLE field_provenance (
  entity_type      TEXT NOT NULL,
  entity_id        INTEGER NOT NULL,
  field            TEXT NOT NULL,
  source_id        INTEGER NOT NULL REFERENCES source(id),
  source_record_id TEXT,
  confidence       REAL,
  value_hash       TEXT,
  fetched_at       INTEGER,
  locked           INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(entity_type, entity_id, field, source_id)
);

CREATE TABLE album_text (
  album_id  INTEGER NOT NULL REFERENCES album(id),
  role      TEXT NOT NULL,                 -- notes, review, credits
  text_id   INTEGER NOT NULL REFERENCES text_blob(id),
  rank      INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(album_id, role, text_id)
);

CREATE TABLE credit (
  id               INTEGER PRIMARY KEY,
  entity_type      TEXT NOT NULL,          -- album, track, episode, book
  entity_id        INTEGER NOT NULL,
  role             TEXT NOT NULL,
  person_name      TEXT NOT NULL,
  artist_id        INTEGER REFERENCES artist(id),
  source_id        INTEGER REFERENCES source(id),
  source_record_id TEXT,
  confidence       REAL,
  UNIQUE(entity_type, entity_id, role, person_name, artist_id)
);

CREATE TABLE album_artwork (
  album_id   INTEGER NOT NULL REFERENCES album(id),
  artwork_id INTEGER NOT NULL REFERENCES artwork(id),
  role       TEXT NOT NULL,
  rank       INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(album_id, artwork_id, role)
);

CREATE TABLE playlist (
  id         INTEGER PRIMARY KEY,
  uuid       TEXT NOT NULL UNIQUE,
  name       TEXT NOT NULL,
  sort_name  TEXT NOT NULL,
  kind       TEXT NOT NULL,                -- static, smart
  rules_json TEXT,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  deleted_at INTEGER
);

CREATE TABLE playlist_item (
  playlist_id INTEGER NOT NULL REFERENCES playlist(id),
  position    INTEGER NOT NULL,
  track_id    INTEGER REFERENCES track(id),
  PRIMARY KEY(playlist_id, position)
);

CREATE TABLE podcast_feed (
  id                INTEGER PRIMARY KEY,
  uuid              TEXT NOT NULL UNIQUE,
  feed_url          TEXT NOT NULL UNIQUE,
  canonical_url     TEXT,
  podcast_guid      TEXT UNIQUE,
  itunes_id         TEXT,
  podcast_index_id  TEXT,
  title             TEXT NOT NULL,
  author            TEXT,
  description_text_id INTEGER REFERENCES text_blob(id),
  image_art_id      INTEGER REFERENCES artwork(id),
  language          TEXT,
  explicit          INTEGER,
  fetch_interval_sec INTEGER NOT NULL DEFAULT 21600,
  fetch_generation  INTEGER NOT NULL DEFAULT 0,
  etag              TEXT,
  last_modified     TEXT,
  last_fetch_at     INTEGER,
  next_fetch_at     INTEGER,
  last_http_status  INTEGER,
  error_count       INTEGER NOT NULL DEFAULT 0,
  last_error        TEXT,
  active            INTEGER NOT NULL DEFAULT 1,
  created_at        INTEGER NOT NULL,
  updated_at        INTEGER NOT NULL
);

CREATE TABLE podcast_feed_alias (
  alias_url  TEXT PRIMARY KEY,
  feed_id    INTEGER NOT NULL REFERENCES podcast_feed(id),
  added_at   INTEGER NOT NULL
);

CREATE TABLE podcast_episode (
  id                INTEGER PRIMARY KEY,
  uuid              TEXT NOT NULL UNIQUE,
  feed_id           INTEGER NOT NULL REFERENCES podcast_feed(id),
  guid              TEXT NOT NULL,
  title             TEXT NOT NULL,
  pub_date          INTEGER,
  duration_ms       INTEGER,
  enclosure_url     TEXT NOT NULL,
  enclosure_type    TEXT,
  enclosure_length  INTEGER,
  episode_no        INTEGER,
  season_no         INTEGER,
  episode_type      TEXT,
  explicit          INTEGER,
  show_notes_text_id INTEGER REFERENCES text_blob(id),
  image_art_id      INTEGER REFERENCES artwork(id),
  chapters_url      TEXT,
  transcript_json   TEXT,
  download_state    TEXT NOT NULL DEFAULT 'none',
  local_path        TEXT,
  content_sha256    TEXT,
  downloaded_at     INTEGER,
  identity_fallback INTEGER NOT NULL DEFAULT 0,
  archived          INTEGER NOT NULL DEFAULT 0,
  created_at        INTEGER NOT NULL,
  updated_at        INTEGER NOT NULL,
  UNIQUE(feed_id, guid)
);

CREATE TABLE podcast_chapter (
  episode_id INTEGER NOT NULL REFERENCES podcast_episode(id),
  start_ms   INTEGER NOT NULL,
  title      TEXT NOT NULL,
  image_art_id INTEGER REFERENCES artwork(id),
  url        TEXT,
  toc        INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(episode_id, start_ms)
);

CREATE TABLE podcast_playback (
  episode_id       INTEGER PRIMARY KEY REFERENCES podcast_episode(id),
  position_ms      INTEGER NOT NULL DEFAULT 0,
  played           INTEGER NOT NULL DEFAULT 0,
  finished         INTEGER NOT NULL DEFAULT 0,
  speed            REAL NOT NULL DEFAULT 1.0,
  skip_forward_sec INTEGER NOT NULL DEFAULT 30,
  skip_back_sec    INTEGER NOT NULL DEFAULT 15,
  updated_at       INTEGER NOT NULL,
  origin           TEXT NOT NULL,          -- host, device
  device_uuid      TEXT
);

CREATE TABLE podcast_download (
  id          INTEGER PRIMARY KEY,
  episode_id  INTEGER NOT NULL REFERENCES podcast_episode(id),
  state       TEXT NOT NULL,
  priority    INTEGER NOT NULL DEFAULT 0,
  bytes_done  INTEGER NOT NULL DEFAULT 0,
  bytes_total INTEGER,
  attempts    INTEGER NOT NULL DEFAULT 0,
  next_try_at INTEGER,
  error       TEXT,
  created_at  INTEGER NOT NULL,
  updated_at  INTEGER NOT NULL
);

CREATE TABLE play_history (
  id          INTEGER PRIMARY KEY,
  track_id    INTEGER REFERENCES track(id),
  started_at  INTEGER NOT NULL,
  ended_at    INTEGER,
  position_ms INTEGER,
  completed   INTEGER NOT NULL DEFAULT 0,
  skipped     INTEGER NOT NULL DEFAULT 0,
  source      TEXT NOT NULL,               -- device, host
  device_uuid TEXT,
  scrobble_state TEXT NOT NULL DEFAULT 'local'
);

CREATE TABLE scrobble_queue (
  id          INTEGER PRIMARY KEY,
  play_id     INTEGER NOT NULL REFERENCES play_history(id),
  state       TEXT NOT NULL DEFAULT 'queued',
  attempts    INTEGER NOT NULL DEFAULT 0,
  next_try_at INTEGER,
  error       TEXT,
  created_at  INTEGER NOT NULL
);

CREATE TABLE device (
  id            INTEGER PRIMARY KEY,
  device_uuid   TEXT NOT NULL UNIQUE,
  volume_uuid   TEXT,
  model         TEXT,
  board         TEXT,
  firmware_version TEXT,
  capabilities_json TEXT,
  last_seen_at  INTEGER,
  last_mount_path TEXT,
  last_catalog_generation INTEGER,
  notes         TEXT
);

CREATE TABLE sync_session (
  id            INTEGER PRIMARY KEY,
  device_id     INTEGER REFERENCES device(id),
  started_at    INTEGER NOT NULL,
  finished_at   INTEGER,
  state         TEXT NOT NULL,
  plan_json     TEXT,
  report_json   TEXT,
  error         TEXT
);

CREATE TABLE sync_operation (
  id            INTEGER PRIMARY KEY,
  session_id    INTEGER NOT NULL REFERENCES sync_session(id),
  op_id         TEXT NOT NULL,
  kind          TEXT NOT NULL,
  source        TEXT,
  destination   TEXT,
  bytes         INTEGER,
  sha256        TEXT,
  state         TEXT NOT NULL,
  error         TEXT,
  UNIQUE(session_id, op_id)
);

CREATE TABLE conflict (
  id            INTEGER PRIMARY KEY,
  entity_type   TEXT NOT NULL,
  entity_id     INTEGER NOT NULL,
  field         TEXT NOT NULL,
  host_value    TEXT,
  device_value  TEXT,
  host_time     INTEGER,
  device_time   INTEGER,
  resolution    TEXT,
  created_at    INTEGER NOT NULL,
  resolved_at   INTEGER
);

CREATE TABLE backup (
  id            INTEGER PRIMARY KEY,
  device_id     INTEGER REFERENCES device(id),
  created_at    INTEGER NOT NULL,
  path          TEXT NOT NULL,
  manifest_sha256 TEXT NOT NULL,
  verified      INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE setting (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE INDEX track_album_idx ON track(album_id, disc_no, track_no);
CREATE INDEX track_artist_idx ON track(artist_id, sort_title);
CREATE INDEX album_artist_idx ON album(album_artist_id, year, sort_title);
CREATE INDEX episode_feed_idx ON podcast_episode(feed_id, pub_date DESC);
CREATE INDEX feed_next_idx ON podcast_feed(next_fetch_at) WHERE active = 1;
CREATE INDEX history_track_idx ON play_history(track_id, started_at DESC);
```

Full-text search is added with FTS5 virtual tables and triggers for track,
album, artist, podcast and text blobs. The FTS index is derived and can be
rebuilt; it is not a source of truth.

---

## Appendix B — Worked example: one album, end to end

This appendix makes the contract concrete. It follows one album from the
user's file system to the device.

### B.1 Host rows

```text
artist 42  "The Helikon Trio"  sort "Helikon Trio, The"  mbid 0f1e…
album 17   "Nocturnes"  album_artist 42  year 2024  genre "Contemporary Classical"
track 901  path "/Music/The Helikon Trio/Nocturnes/01 Nocturne in C-sharp minor.flac"
           duration 312000  track 1  disc 1  recording mbid 9a2b…
track 902  path "/Music/The Helikon Trio/Nocturnes/02 Nocturne in E-flat.flac"
           duration 287000  track 2  disc 1  recording mbid 7c3d…
text 55    kind bio   body "The Helikon Trio is a …"  source 3 (Wikipedia)
           licence "CC BY-SA 4.0"  attribution "Wikipedia, revision 123…"
text 56    kind notes body "Recorded in …"  source 1 (user)
art 77     kind cover_front  sha256 4f…  96×96 and 320×320 masters
```

### B.2 HDB records

```text
album 17:
  title = "Nocturnes"
  artist = 42
  year = 2024
  rating = 80
  track_count = 2
  first_track = 901
  artwork = 77

track 901:
  uid = FNV-1a("/Music/The Helikon Trio/Nocturnes/01 Nocturne in C-sharp minor.flac")
  path = same (NFD)
  title = "Nocturne in C-sharp minor"
  artist = 42
  album = 17
  duration = 312000
  rating = 90
  artwork = 77

artist 42:
  name = "The Helikon Trio"
  sort = "Helikon Trio, The"
  bio = "The Helikon Trio is a …" (short)
  image = 78
  album_count = 1
  first_album = 17
```

### B.3 Sidecar records

```text
music.meta:
  record owner_kind=1 (album), owner_index=17, text_kind=1 (notes)
    text = "Recorded in …"
    source = "user"
    license = "user"
  record owner_kind=1, owner_index=17, text_kind=4 (credits)
    text = "Composer: …\nProducer: …"
    source = "MusicBrainz"
    license = "CC0"
  record owner_kind=2 (artist), owner_index=42, text_kind=3 (bio)
    text = "The Helikon Trio is a …"
    source = "Wikipedia"
    license = "CC BY-SA 4.0"
    attribution = "Wikipedia, revision 123…"

music.art:
  record art_id=77, owner_kind=1, owner_index=17, role=front,
    width=160, height=160, format=RGB565
  record art_id=78, owner_kind=2, owner_index=42, role=portrait,
    width=160, height=160, format=RGB565
```

### B.4 Device files

```text
/.helikon/music.hdb
/.helikon/music.meta       catalog_generation = 42
/.helikon/music.art        catalog_generation = 42
/.helikon/library.json     generation 42, hashes
/.helikon/play.journal     empty or last events
```

### B.5 Sync plan

```text
1. COPY 01 Nocturne…flac        → /Music/…/01 Nocturne…flac
2. COPY 02 Nocturne…flac        → /Music/…/02 Nocturne…flac
3. COPY music.meta              → .helikon/music.meta.tmp
4. COPY music.art               → .helikon/music.art.tmp
5. BUILD music.hdb generation 42 → .helikon/music.hdb.tmp
6. VERIFY hashes/CRC
7. RENAME music.meta.tmp        → music.meta
8. RENAME music.art.tmp         → music.art
9. RENAME music.hdb.tmp         → music.hdb
10. WRITE library.json
11. EJECT (optional)
```

If the process dies after step 8, the new sidecar has generation 42 but the
old HDB has generation 41. The firmware ignores the sidecar. If it dies after
step 9 but before step 10, the catalog is valid; the manifest is stale but is
not read by the firmware. The next sync repairs the manifest.

---

## Appendix C — Podcast and audiobook record proposal

These layouts are proposals; they must be frozen with tests before
implementation.

### C.1 `podcasts.hdb` header

```text
0   u8[4]  "HLPD"
4   u16    version = 1
6   u16    header_size = 128
8   u32    flags
12  u32    generation
16  u32    feed_count
20  u32    episode_count
24  u32    chapter_count
28  u32    feed_table_offset
32  u32    episode_table_offset
36  u32    chapter_table_offset
40  u32    string_offset
44  u32    string_bytes
48  u32    index_offset
52  u32    index_count
56  u32    file_size
60  u32    crc32
64–127 reserved
```

### C.2 Feed record (64 B)

```text
0   u64    uid (FNV-1a of canonical feed URL)
8   u32    title_off
12  u32    author_off
16  u32    description_off
20  u32    image_art_id
24  u32    episode_count
28  u32    first_episode
32  u32    added_time
36  u32    last_update
40  u32    podcast_guid_off
44  u16    flags (explicit, active)
46  u16    reserved
48–63 reserved
```

### C.3 Episode record (96 B)

```text
0   u64    uid (FNV-1a of feed_uid + ":" + guid)
8   u32    feed_id
12  u32    title_off
16  u32    pub_date
20  u32    duration_ms
24  u32    enclosure_path_off
28  u32    enclosure_size
32  u32    position_ms
36  u32    art_id
40  u32    notes_off
44  u32    enclosure_sha_off
48  u16    episode_no
50  u16    season_no
52  u16    speed_x100
54  u16    skip_forward_sec
56  u16    skip_back_sec
58  u16    chapter_count
60  u8     played
61  u8     finished
62  u8     flags (explicit, trailer, bonus, archived)
63  u8     reserved
64–95 reserved
```

### C.4 Chapter record (16 B)

```text
0   u32    start_ms
4   u32    title_off
8   u32    image_art_id
12  u16    flags
14  u16    reserved
```

### C.5 `books.hdb` outline

Feed/episode records are replaced by book/section records:

```text
Book (64 B): uid, title, sort_title, author, narrator, language,
             description, cover_art_id, section_count, first_section,
             total_duration_ms, flags, source/licence offsets

Section (48 B): uid, book_id, index, title, path, duration_ms,
                size_bytes, content_sha_off, flags
```

The exact fields need a separate freeze. The important rule is that a book and
its sections are one catalog, and a missing section is visible as incomplete,
not silently skipped.

---

## Appendix D — Podcast state journal

```text
Header (16 B)
  0   u8[4]  "HLPJ"
  4   u16    version = 1
  6   u16    header_size = 16
  8   u32    sequence
  12  u32    reserved

Record (24 B)
  0   u64    episode_uid
  8   u8     event
  9   u8     flags
  10  u16    value
  12  u32    position_ms
  16  u32    timestamp
  20  u16    crc16
  22  u16    reserved
```

`episode_uid` is the same 64-bit UID used by `podcasts.hdb`, so the host can
join a journal record to a catalog episode without a path lookup.

Events: `1 position`, `2 played`, `3 unplayed`, `4 finished`, `5 speed`,
`6 skip_forward`, `7 skip_back`, `8 chapter`. `value` holds speed×100 or the
skip seconds; `position_ms` holds the position. The per-record `crc16` is
CRC-16/CCITT-FALSE (polynomial `0x1021`, initial value `0xFFFF`). The journal is
little-endian, append-only, and wraps to a new segment when it reaches its size
cap. A corrupt tail is discarded; the valid prefix is imported.

`state.json` is a snapshot:

```json
{
  "schema": 1,
  "device_uuid": "…",
  "generation": 12,
  "episodes": [
    {
      "uid": "…",
      "position_ms": 123456,
      "played": true,
      "finished": false,
      "speed": 1.25,
      "skip_forward_sec": 30,
      "skip_back_sec": 15,
      "updated_at": 1762400000
    }
  ]
}
```

The journal wins over the snapshot when both are present and the timestamps
overlap; the snapshot is a debugging/export aid, not the only truth.

---

## Appendix E — Sync report and operation schema

```json
{
  "schema": 1,
  "session_id": "01J…",
  "device": {
    "uuid": "7d3c…",
    "model": "iPod Classic 6G",
    "firmware": "0.1.0",
    "hdb_version": 1
  },
  "plan": {
    "operations": 1234,
    "bytes": 123456789,
    "files_added": 100,
    "files_replaced": 2,
    "files_deleted": 0,
    "catalog_generation": 42
  },
  "result": {
    "state": "ok",
    "started_at": 1762400000,
    "finished_at": 1762400123,
    "hdb_sha256": "…",
    "manifest_sha256": "…",
    "warnings": [],
    "errors": []
  }
}
```

An operation record:

```json
{
  "op_id": "01J…",
  "kind": "copy",
  "source": "host:Music/…/01.flac",
  "destination": "device:Music/…/01.flac",
  "bytes": 34567890,
  "sha256": "…",
  "state": "verified"
}
```

`state` values: `planned`, `staging`, `staged`, `verifying`, `verified`,
`committing`, `committed`, `failed`, `cancelled`, `rolled_back`.

---

## Appendix F — Glossary additions

| Term | Meaning in this paper |
|---|---|
| **Companion** | The macOS host app; not the firmware. |
| **Device** | The iPod running Helikon-OS. |
| **HDB** | HelikonDB, the immutable music catalog; v1 is frozen. |
| **HMTX** | The proposed editorial text sidecar. |
| **HART** | The proposed optional larger-artwork sidecar. |
| **Generation** | A monotonically increasing identifier binding a catalog and its sidecars. |
| **Projection** | A host-built device artifact derived from the host library; not a second source of truth. |
| **Plan** | A typed, ordered set of sync operations with hashes and sizes. |
| **Journal** | A small append-only device file of runtime events; host imports it. |
| **Snapshot** | A full state file written on clean shutdown; not the only truth. |
| **Provenance** | The source, record ID, confidence, time, licence and attribution for a field. |
| **Pinned field** | A user-edited value that provider refresh must not replace. |
| **Stale** | Data that is still usable but older than its refresh policy. |
| **Quarantine** | A file that failed validation and is kept for diagnosis, never transferred. |
| **Service mode** | A framed USB file-operation protocol executed by the device VFS. |
| **LUN** | USB mass-storage logical unit; read-only here, never writable. |
| **CC0 / CC BY / CC BY-SA** | Creative Commons licence families; attribution and share-alike obligations vary. |
| **SRI** | Subresource Integrity, a hash-based check for a downloaded resource. |
| **SSRF** | Server-side request forgery; here, a feed URL that targets a private address. |
