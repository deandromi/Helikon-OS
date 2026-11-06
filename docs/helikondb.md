# HelikonDB — native music catalog

> Purpose: the on-disk music catalog, its builder contract and the firmware reader.
> Status: format v1 implemented; reference builder and reader host-tested on a
> 100-song macOS corpus; functional embedded-catalog Music hub/wall image built,
> host-verified and included in Alpha 2.0.0; production builder belongs to the
> companion (T-066); the owner reports the release-set 6G test is complete, with
> per-image detail still to attach.
> Owner: database/host
> Last updated: 2026-11-06
> Source: D-07, D-33, D-53; [`tasks.md`](tasks.md) T-066/T-067

HelikonDB (HDB) is the only music catalog. It never reads or writes iTunesDB,
libgpod metadata or Rockbox tagcache. The companion scans tags on the host,
builds one immutable file, and the firmware reads slices of it lazily. The
device never scans the filesystem for music and never rewrites the catalog
during playback.

```text
/.helikon/music.hdb      immutable catalog (this document)
/.helikon/play.journal   runtime delta log (planned, T-067)
/.helikon/artcache/      optional decoded thumbnail cache
```

## 1. Why this shape

Rockbox builds tagcache on the device: a scan reads every audio file, a commit
writes several `.tcd` files, and runtime data is folded into the database.
Apple writes iTunesDB on the host and the device writes only a small
Play Counts return file. HDB follows the Apple split without the Apple format:
host builds once, device reads little and writes only a journal.

Consequences:

- Boot and the launcher read **zero** database bytes.
- Opening Music reads the header and one list slice.
- A track start or stop writes 16–24 bytes to the journal, not the catalog.
- No audio tag parser is needed on the device for browsing.
- Existing files are imported by the host by reading their tags; the Apple
  firmware's library is not updated by Helikon (D-53).

## 2. File layout

All integers are little-endian. Every section is 4-byte aligned. Offsets are
absolute from the start of the file.

| Section | Size | Contents |
|---|---:|---|
| Header | 128 B | magic, version, counts, section offsets, CRC |
| Tracks | 88 B × N | sorted by album, disc, track, title |
| Albums | 48 B × N | sorted by artist id, year, title |
| Artists | 48 B × N | sorted by sort name |
| Playlists | 24 B × N | static playlists |
| Playlist entries | 4 B × N | track record indices |
| Strings | variable | `u16 length` + UTF-8, deduplicated |
| Indexes | 16 B × N + arrays | lookup orders |
| Artwork | 32 B × N | album/artist thumbnails |
| Artwork blobs | variable | RGB565, default 96×96 each |

### 2.1 Header

| Offset | Type | Field |
|---:|---|---|
| 0 | `u8[4]` | `HLDB` |
| 4 | `u16` | version, 1 |
| 6 | `u16` | header size, 128 |
| 8 | `u32` | flags: bit 0 artwork, bit 1 ratings |
| 12 | `u32` | generation |
| 16 | `u32` | track count |
| 20 | `u32` | album count |
| 24 | `u32` | artist count |
| 28 | `u32` | playlist count |
| 32 | `u32` | track table offset |
| 36 | `u32` | album table offset |
| 40 | `u32` | artist table offset |
| 44 | `u32` | playlist table offset |
| 48 | `u32` | playlist entry table offset |
| 52 | `u32` | string section offset |
| 56 | `u32` | string section bytes |
| 60 | `u32` | index table offset |
| 64 | `u32` | index count |
| 68 | `u32` | artwork table offset |
| 72 | `u32` | artwork record count |
| 76 | `u32` | artwork blob offset |
| 80 | `u32` | artwork blob bytes |
| 84 | `u32` | source digest |
| 88 | `u32` | created (max track mtime) |
| 92 | `u32` | file size |
| 96 | `u32` | CRC-32 of the whole file with this field zero |
| 100–127 | — | reserved, zero |

### 2.2 Track record (88 B)

| Offset | Type | Field |
|---:|---|---|
| 0 | `u64` | stable uid (FNV-1a of the device path) |
| 8 | `u32` | path string |
| 12 | `u32` | title string |
| 16 | `u32` | artist id |
| 20 | `u32` | album id |
| 24 | `u32` | genre string |
| 28 | `u32` | composer string |
| 32 | `u32` | sort title |
| 36 | `u32` | sort artist |
| 40 | `u32` | sort album |
| 44 | `u32` | duration ms |
| 48 | `u32` | file size |
| 52 | `u32` | bitrate |
| 56 | `u32` | sample rate |
| 60 | `u32` | added time (Unix) |
| 64 | `u32` | artwork id, `0xFFFFFFFF` none |
| 68 | `u32` | filetype code |
| 72 | `u16` | track number |
| 74 | `u16` | disc number |
| 76 | `u16` | year |
| 78 | `u16` | rating 0–100 |
| 80 | `u8` | flags: compilation, podcast, audiobook, explicit |
| 81–87 | — | reserved |

Filetype: 1 MP3, 2 AAC/M4A, 3 ALAC, 4 FLAC, 5 WAV, 6 AIFF, 7 Ogg, 8 Opus,
255 other.

### 2.3 Album record (48 B)

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32` | title string |
| 4 | `u32` | album artist id |
| 8 | `u32` | sort title |
| 12 | `u32` | genre string |
| 16 | `u32` | artwork id |
| 20 | `u32` | year |
| 24 | `u32` | track count |
| 28 | `u32` | first track index |
| 32 | `u32` | disc count |
| 36 | `u32` | added time |
| 40 | `u16` | album rating 0–100 |
| 42 | `u16` | flags |
| 44–47 | — | reserved |

Albums are sorted by album artist id, then year, then title. An artist's albums
are therefore contiguous: `first_album` + `album_count`.

### 2.4 Artist record (48 B)

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32` | name string |
| 4 | `u32` | sort name |
| 8 | `u32` | biography string |
| 12 | `u32` | artwork id for the profile picture |
| 16 | `u32` | album count |
| 20 | `u32` | first album index |
| 24 | `u32` | track count |
| 28 | `u32` | added time |
| 32 | `u16` | rating (reserved, 0) |
| 34 | `u16` | flags |
| 36–47 | — | reserved |

Artists are sorted by sort name; the artist list is the table order.

### 2.5 Playlist record (24 B)

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32` | name string |
| 4 | `u32` | sort name |
| 8 | `u32` | entry array offset |
| 12 | `u32` | entry count |
| 16 | `u16` | flags |
| 18–23 | — | reserved |

Entries are `u32` track record indices. Smart playlists are precomputed by the
host for v1.

### 2.6 Strings and indexes

A string is `u16 length` followed by UTF-8 bytes (no terminator). `0xFFFFFFFF`
means “none”. Paths are stored in HFS+ NFD form with `/` separators; display
strings are NFC.

Index headers are 16 bytes: `type`, `count`, `array offset`, reserved. Arrays
are `u32` track or album record indices.

| Type | Array |
|---:|---|
| 1 | track indices sorted by sort title |
| 2 | track indices sorted by added time, newest first |
| 3 | track indices sorted by genre, artist, album, disc, track |
| 4 | album indices sorted by sort title |

No runtime sort is required: a view reads a contiguous array slice.

### 2.7 Artwork

A 32-byte record:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32` | kind: 1 album, 2 artist |
| 4 | `u32` | owner album/artist id |
| 8 | `u16` | width |
| 10 | `u16` | height |
| 12 | `u16` | format, 1 = RGB565 |
| 14 | `u16` | reserved |
| 16 | `u32` | blob offset |
| 20 | `u32` | blob bytes |
| 24 | `u32` | blob CRC-32 |
| 28 | `u32` | reserved |

Default thumbnails are 96×96 RGB565 (18,432 bytes). Blobs are deduplicated by
content; an album and an artist can share one blob. Full-size cover decoding is
a separate, lazy path.

## 3. Builder contract

The production builder is the companion app (T-066). `tools/hdb_build.py` is the
tested reference and the format oracle; it must not be copied into the Swift app
without porting the same checks.

```sh
python3 tools/hdb_build.py build MUSIC_DIR --out music.hdb --sample 100
python3 tools/hdb_build.py build MUSIC_DIR --out music.hdb \
  --ratings ratings.json --artist-info artists.json --artist-images DIR
python3 tools/hdb_build.py inspect music.hdb
python3 tools/hdb_build.py fixture --out fixture.hdb
```

Rules:

- Parse tags on the host; never scan on the device for a normal build.
- Cache metadata by `(path, mtime_ns, size)` and artwork blobs by SHA-1. A
  warm rebuild is a header/stat pass, not a tag pass.
- Extract one album cover per album, deduplicate it, resize to 96×96.
- Optional overlays: track ratings, album ratings, artist biographies and
  artist profile pictures. The schema already carries them; missing overlay
  data is zero/none, never an error.
- Write a temporary file, `fsync`, then atomically rename. A failed build leaves
  the previous generation untouched.
- No iTunesDB, libgpod metadata or Apple-format writes.

Tested corpus on 2026-11-06: 100 tracks sampled across 6,587 local audio files —
2.1 s cold, 0.007 s warm, 78 artists, 100 albums, 98 album-art records,
1.85 MB; 100-track reader test passes. An overlay run produced 10/10 track
ratings, 5/5 album ratings, one artist biography and one 96×96 artist image.

## 4. Firmware reader

`firmware/db/hdb.c` exposes `hdb_open`, `hdb_track`, `hdb_album`,
`hdb_artist`, `hdb_playlist`, `hdb_playlist_entry`, `hdb_string`, `hdb_index`,
`hdb_index_get`, `hdb_art`, `hdb_art_read` and an optional `hdb_verify_crc`.

- Caller supplies a `read(context, offset, buffer, length)` function; the reader
  owns no memory and does not depend on stdio, malloc or the VFS.
- Every table, string and blob access is bounds-checked against the header.
- `hdb_open` validates magic, version, header size and all section extents; it
  does not read the whole file.
- `hdb_verify_crc` is for the host/test path; the device can trust the
  companion-built file or verify it once while charging.

## 5. Runtime journal (T-067, next)

The catalog is immutable. Runtime state goes to `/.helikon/play.journal`:

```text
header "HLPJ", version, sequence
record: uid u64, event u8, position_ms u32, timestamp u32, rating_delta i8, crc16
```

- Buffered in RAM; flushed in sector-sized batches on idle, shutdown or USB.
- Companion folds the journal into the host library on sync.
- No per-song catalog write and no Apple database write.
- A corrupt tail is discarded; the catalog is never affected.

## 6. Functional device demo (embedded catalog)

Until the VFS/ATA handoff is solved, a signed test image can embed an HDB in
`.rodata`. The loader verifies and loads the whole image; the firmware's Music
section reads the catalog from DRAM and never touches storage. This is the
functional library test, not the production path.

Build with the contiguous 100-song sample (6 artists, 10 albums, 10 covers)
and the diagnostic launcher:

```sh
make -C firmware BOARD=6g VERSION=13 BOOTSTATE_STATE=good UI=1 \
  HDB=../build/helikondb/first100/music.hdb \
  EXTRA_CFLAGS='-DHELIKON_DIAG_LAUNCHER' \
  BUILD=../build/firmware/6g-music image
```

Artifact: 583,820 bytes, SHA-256 `372e1412…`; manifest `7b50ab0a…`; bootstate
`7501068c…`. The loader is `7f13e7e5…` (106,184 bytes). The host loader core
and `tests/ui` previews pass.

Controls: launcher → Music opens a launcher-style hub (Albums / Artists /
Tracks). Albums and Artists use the 4×3 wall from the album-wall mockup; the
wheel or left/right moves, SELECT opens and MENU returns one level. The artist
page implements the `assets/ui/artist-view` layout: portrait band, name,
genre/facts, PLAY plate, OVERVIEW / ALBUMS / TRACKS tabs, biography and facts.
Left/right switches tabs; the wheel moves album/track selection; SELECT opens
the focused item; MENU returns. Album, artist and track pages show 96×96
artwork, year and ratings; artist entries without a profile picture use a
monogram. The 3×5 fallback font is used for dynamic text in this build; the
first-party Plex atlas remains T-042/T-057. The search mark has no action yet.
The launcher input init no longer fails when no button is pressed during boot.
Playback (F5/T-058) and reading `/.helikon/music.hdb` from the real VFS are not
part of this test.

## 7. Open items

1. Companion port of the builder (T-066) and a Swift/ObjC reader for its tests.
2. Production firmware Music screens from the VFS-backed HDB (T-056/T-088);
   the embedded-catalog browser is the current test harness.
3. Play journal writer/reader and host merge (T-067).
4. Artwork cache policy on device: one thumbnail at a time, RGB565, LRU.
5. Full-library scale run (10k tracks) and device timing once the Music screen
   exists.
