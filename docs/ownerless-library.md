# The Ownerless Library — an append-only commons for personal media, with a one-sentence artist model

> Purpose: a research paper on a device-resident library that no Mac owns, that
> Macs contribute to without joining, and that can carry a hidden, grounded
> one-sentence description for the artist view.
> Status: living — research paper; design only, not implemented and not a
> support claim.
> Owner: host/product research
> Last updated: 2026-11-07
> Source: session-start prompt; [`companion-sync-research.md`](companion-sync-research.md);
> [`helikondb.md`](helikondb.md); [`ui.md`](ui.md); D-07, D-33, D-43, D-48,
> D-53; tasks T-066, T-067, T-082, T-088.
> Base commit: `1b7ed0a` (`main`, 2026-11-07).

**Relationship to the companion paper.** This paper extends
[`companion-sync-research.md`](companion-sync-research.md). It keeps that
paper's HDB v1 + HMTX/HART device contract, its transfer safety rules and its
host-side metadata sourcing. It **replaces** two assumptions:

1. that the host library is the source of truth for metadata; and
2. that a Mac joins, owns, or is a registered host of the library.

In this paper the library is a **commons on the iPod**. Macs are contributors.
There is no host registry, no join ceremony, no owner role, no replica set and
no "primary Mac." The person holding the device has custodial authority over
destructive acts, not ownership of the library in the software model.

**Scope.** This paper is about the technical and interaction model: the
append-only library log, its projection into the device catalog, multi-Mac
contribution, deletion and compaction authority, and a hidden one-sentence
artist-description model. It deliberately does **not** address DRM, copyright
licensing or media rights; it assumes the person contributing media has the
right to put it on their own device. Those questions belong in a separate
policy paper.

**Status of the claims.** The repository has a host-tested HDB v1 builder and
reader, a host-side artist view with a two-line biography, and an Alpha 2.0.0
embedded-catalog image. It has no production VFS, no playback, no library log,
no contributor protocol and no event-sourced library. Everything below is
`proposed`, `planned` or a research question. Design statements use the present
tense for readability; they are not implementation claims.

---

## 1. Abstract

A personal music library is usually modelled as a database owned by a computer.
The Mac is the master; the iPod is a satellite; a second Mac must "join" the
library, be registered as a host, or be treated as a foreign library. That model
is convenient for software and wrong for the object. The iPod is the thing that
moves between Macs. It is the only shared physical place where the library can
live.

This paper proposes the **ownerless library**:

- the library is held by the device and stewarded by the person holding it,
  not owned by a Mac;
- a Mac does not join, register or become a member;
- a Mac reads the device library, contributes new media and metadata events,
  and leaves;
- the canonical record is an **append-only contribution log** on the device;
- the music catalog (`music.hdb`) is a **projection** of that log, not the
  truth;
- a correction is a new event, never an in-place overwrite;
- deletion is a proposal that only the device custodian can confirm;
- a guest Mac can contribute a capsule without reading the library;
- no account, no cloud, no host registry, no distributed consensus.

The physical device provides the one thing a distributed system usually needs a
server for: a **total order**. Only one Mac can write at a time because only one
Mac can mount the device at a time. The append-only log serializes contributions
without clocks, vector clocks, joins, elections or a blockchain.

The second contribution is a narrow, hidden AI: a **one-sentence artist model**
that turns a fact sheet into one calm sentence for the artist view. It is not a
chat assistant, not a recommendation engine and not a visible feature. It runs
inside the Companion during contribution, or — as a research question — as a
tiny grounded sentence realizer on the device. It never invents facts. If the
facts are insufficient, it produces nothing. The sentence is a contribution
event with provenance, not an authoritative biography.

Together the two ideas describe a different kind of personal media system: a
library that is owned by no computer, contributed to by many, and described in
one quiet sentence at a time.

### 1.1 Method and limits

This is conceptual and design research, not an implementation report. The method
was:

1. read the current HDB v1, artist-view, state, tasks and companion papers;
2. write the scenarios that break the one-Mac model;
3. reduce the model to the smallest contract that removes ownership and
   membership while preserving provenance and recovery;
4. separate the device contract (log + projection) from the host contract
   (workspace + contribution);
5. design a narrow AI use case that fits the device constraints and the
   project's no-cloud rule;
6. check the result against the current evidence and the "what not to claim"
   rules.

Limits: no code, no device run, no event-log prototype, no model benchmark and
no user study was performed. The log format, projection policy and model
placement are proposals. The rights/DRM question is deliberately out of scope.

---

## 2. Why ownership is the wrong metaphor

### 2.1 The model we inherited

The companion paper says:

> the host is the source of truth for library metadata, editorial content,
> subscriptions, playlists and artwork choices.

That is the standard architecture: a Mac library database owns the library; the
iPod receives a projection; a second Mac must join, merge or be treated as a
separate library. It works, but it makes the Mac the centre of gravity. The
device becomes a peripheral. The person who owns the iPod does not own the
library in the software model; the Mac does.

That is the assumption this paper rejects.

### 2.2 The device is the shared object

A person may have a desktop Mac, a laptop, an old Mac in a drawer, a work Mac
and a friend's Mac. The iPod is the one object that travels between them. It is
also the one object that must keep working when none of the Macs is present.

If the library lives on a Mac, the iPod is a copy. If the library lives on the
iPod, the Macs are tools. The second model is closer to how the object is
actually used.

### 2.3 "Nobody owns it" needs a precise meaning

"Nobody owns the library" does not mean "there are no rules." It means:

| Claim | Meaning |
|---|---|
| No Mac owns the library | No Mac is authoritative, privileged or required. A Mac can be wiped, replaced or absent without destroying the library. |
| No Mac joins the library | There is no membership list, no host registry, no join ceremony and no role. A Mac can contribute on first contact. |
| The person is the custodian | The person holding the device can confirm deletion, compaction and guest promotion. That is custodial authority, not a software owner account. |
| The device has an identity | `library_id` names the collection so contributions can be associated with it. It is an identifier, not an ownership token. |
| Contributors have provenance | A random `contributor_id` records where an event came from. It is not a membership credential. |
| Additive is not immutable | A correction is a new event that supersedes the old value for display. The old event remains in the log. |

This distinction matters. The project can reject ownership without rejecting
safety, provenance or the ability to remove something from the device.

### 2.4 Commons, not federation

A federation has members, roles and a shared protocol. A commons has
participants and a shared place. The model here is the second:

```text
iPod library (the commons)
  ├── identity.json          library identity
  ├── log/                   append-only contributions
  ├── media/                 contributed audio files
  ├── projection/            last built music.hdb + sidecars
  └── play.journal           device-owned playback events

Mac A ── reads, contributes, leaves
Mac B ── reads, contributes, leaves
Mac C ── reads, contributes, leaves
```

There is no "Mac A is the owner." There is no "Mac B joined." There is no
"Mac C is a replica." Each Mac is a contributor with a local workspace.

### 2.5 What changes from the companion paper

| Area | Companion paper | This paper |
|---|---|---|
| Source of truth | Host library database | Device contribution log |
| Host role | Owner, builder, registry | Contributor, workspace, projection builder |
| Second Mac | Joins or is a separate library | Contributes; no join |
| Metadata edit | Host row update | Append `FIELD_SET` event |
| Deletion | Host plans and deletes | Mac proposes; device custodian confirms |
| Catalog | Host-built projection | Any Mac-built projection of the log |
| Conflict | Host merge rules | Append order + projection policy + history |
| AI | Optional host metadata provider | Hidden one-sentence realizer, grounded in facts |
| Vocabulary | host, device, sync, owner | contributor, commons, contribution, add |

The HDB/sidecar contract and the transfer safety rules remain. The ownership
model changes.

---

## 3. Definitions and principles

### 3.1 Definitions

| Term | Meaning |
|---|---|
| **Library** | The collection of media, metadata, artwork, playlists, podcast subscriptions and playback state on the device, plus the contribution log that describes how it came to be. |
| **Commons** | A shared place that many contributors can add to without owning it. |
| **Contribution** | An append-only event that adds media, metadata, artwork, a playlist, a podcast subscription, a description or a proposal. |
| **Contributor** | A Mac or a process that appends an event. Identified by a random `contributor_id` for provenance, not membership. |
| **Custodian** | The person holding the device, who can confirm deletion, compaction and guest promotion. |
| **Event log** | The ordered sequence of contribution records stored on the device. |
| **Projection** | A deterministic catalog built from the log: `music.hdb`, `music.meta`, `music.art`, `podcasts.hdb`, `books.hdb`. |
| **Guest capsule** | A write-only contribution from a guest Mac that the custodian can play, promote or discard. |
| **Fact sheet** | A small set of structured facts about an artist: formed year, origin, genres, members, key albums, active years. |
| **Sentence** | One grounded sentence derived from a fact sheet for the artist view. |
| **Custodial token** | A short code generated by the device to confirm a destructive proposal. |

### 3.2 Principles

**P1 — The device is the canonical place.** The library lives on the device. A
Mac's database is a workspace and cache, not the library.

**P2 — A Mac contributes; it does not join.** There is no registry, no
membership and no role. A random contributor ID exists for provenance only.

**P3 — Append, don't overwrite.** A correction is a new event. The old event
remains in the log. The projection chooses what to display.

**P4 — One physical device gives the total order.** There is no concurrent
multi-writer problem to solve: only one Mac can mount and write at a time. The
append order is the authoritative order for display.

**P5 — The projection is derived and disposable.** `music.hdb` is a cache. If it
is corrupt, rebuild it from the log. The log is the memory.

**P6 — Only the custodian destroys.** A Mac can propose a tombstone. The person
holding the device confirms it. No Mac can erase another Mac's contribution.

**P7 — Physical access is the trust boundary.** There are no accounts, no cloud
and no membership keys. A Mac that can mount the device can contribute. A guest
contributes into a quarantine capsule, not the library.

**P8 — AI is a typesetter, not an author.** The one-sentence model turns facts
into prose. It does not discover facts, browse the web or make decisions.

**P9 — No hidden network, no telemetry.** The device has no network. The
Companion does network work only for explicit user actions. The contribution log
is local.

**P10 — Bounded growth.** Logs are compacted, projections are regenerated,
media is deduplicated and the device never becomes an unbounded event store.

### 3.3 Append-only is not a blockchain

There is no chain, no mining, no consensus and no distributed ledger. There is
one device, one mount at a time, and one ordered log. The event hash and CRC
exist to detect a torn tail and duplicate records, not to prove global
agreement. This is a **single-device event log**, not a distributed system.

### 3.4 Additive correction

A Mac cannot edit another Mac's event. It can append:

```text
event 1: FIELD_SET  artist=42  field=genre  value="post-rock"  contributor=A
event 2: FIELD_SET  artist=42  field=genre  value="ambient"    contributor=B
```

The projection shows the value from event 2 because it was appended later. The
Companion can show the history and both contributors. Nothing is lost. If the
custodian prefers the first value, a new event can pin it; the old events remain.

This is the difference between **additive** and **immutable**: values can be
superseded, but history is not erased.

---

## 4. The device library layout

### 4.1 Directory layout

```text
/.helikon/library/
  identity.json                 library identity, format version
  log/
    000001.hlev                 append-only segment
    000002.hlev
  media/                        contributed audio, content-addressed or path-based
  projection/
    music.hdb                   HDB v1 catalog
    music.meta                  HMTX editorial sidecar
    music.art                   HART artwork sidecar
    music.manifest.json         log sequence, generation, hashes
    maintenance.json            small pending-proposal file (not the log)
  play.journal                  device-owned playback events
  guests/
    <capsule_id>/               guest contribution, write-only
      manifest.json
      media/
      events.hlev
```

The exact paths are a proposal. The important property is that the **log**,
**media** and **projection** are separate. A corrupt projection never damages
the log; a torn log tail never damages the last good projection.

### 4.2 `identity.json`

```json
{
  "schema": 1,
  "library_id": "b7f1…",
  "created_at": 1762400000,
  "format": "helikon-library",
  "format_version": 1,
  "device_uuid": "7d3c…",
  "notes": "The library is stewarded by the person holding the device."
}
```

`library_id` is generated on the device or by the first Mac that initializes
the library. It is a name, not an owner token. `device_uuid` links the library
to the physical device for diagnostics; it is not a serial number.

### 4.3 The HLEV event segment

A segment is an append-only file of records:

```text
Segment header (32 bytes)
  0   u8[4]  "HLEV"
  4   u16    version = 1
  6   u16    header_size = 32
  8   u32    segment_number
  12  u32    base_sequence
  16  u32    reserved
  20  u32    header_crc32
  24  u32    reserved
  28  u32    reserved

Record (32 bytes + payload)
  0   u64    event_id        hash of the record metadata + payload
  8   u64    contributor_id  random per Mac install
  16  u16    kind
  18  u16    flags
  20  u32    payload_len
  24  u32    payload_crc32
  28  u16    header_crc16
  30  u16    reserved
  ...        payload (UTF-8 canonical JSON, max 64 KiB)
```

Rules:

- Records are appended in the order the device receives them.
- A record is valid only if the header CRC and payload CRC match and the
  payload length fits the segment.
- A torn tail is ignored. The device and Macs keep the longest valid prefix.
- `event_id` is a content hash of the record; duplicate event IDs are ignored.
- `contributor_id` is random per Mac install. It is not a login, a membership
  or a stable person identity.
- `timestamp` is inside the payload for human history; display order uses the
  append order, not the timestamp.
- `payload` is canonical JSON: sorted keys, UTF-8, no insignificant whitespace.
  A binary CBOR variant can be considered later; JSON is easier to inspect and
  the device does not parse it.

### 4.4 Event kinds

| Kind | Payload | Meaning |
|---|---|---|
| `TRACK_ADD` | path, content hash, size, tags, duration | A Mac contributed a media file and its initial tags |
| `TRACK_MOVE` | old path, new path, content hash | A file moved; identity survives |
| `FIELD_SET` | entity type/id, field, value, source, confidence | A metadata value was contributed |
| `ART_ADD` | entity type/id (album/artist), image hash, role, dimensions, source | Artwork was contributed |
| `PLAYLIST_ADD` | playlist id, name, ordered track ids | A playlist was contributed |
| `PLAYLIST_ITEM` | playlist id, position, track id | A playlist edit |
| `PODCAST_ADD` | feed URL, canonical URL, podcast:guid, title, author | A subscription was contributed |
| `EPISODE_ADD` | feed id, episode guid, enclosure, duration, notes | An episode was contributed |
| `FACT_SET` | artist id, fact key, value, source | A grounded fact for the sentence model |
| `DESCRIPTION_SET` | artist id, one sentence, fact ids used, model id/version | A one-sentence artist description |
| `TOMBSTONE_PROPOSE` | entity type/id, reason | A Mac proposes removal |
| `TOMBSTONE_CONFIRM` | proposal id, custodial token | The custodian confirms removal |
| `SNAPSHOT` | snapshot id, first sequence, last sequence, state hash | A compacted view of prior events |
| `GUEST_CAPSULE` | capsule id, manifest hash | A guest contribution is present |

The list is deliberately small. A library is not a general database; it is a
set of media, metadata, artwork, playlists, subscriptions and playback events.

### 4.5 Sequence, order and time

The device is the serialization point. A Mac mounts the device, appends records,
flushes and ejects. The next Mac sees the new tail. There is no concurrent
append and no need for vector clocks.

The log therefore has a **total append order**. The projection uses that order
for "latest wins" fields. The timestamp inside an event is for human history —
"this description was written on 7 November" — not for conflict resolution.
A Mac with a wrong clock cannot reorder the library.

### 4.6 Segments, snapshots and compaction

An append-only log grows. Compaction keeps it bounded:

1. A Mac builds a `SNAPSHOT` event that summarizes the current projection state
   and references the last sequence it covers.
2. The snapshot is written as a new segment; the old segments remain until the
   custodian confirms.
3. The custodian (or a device-side maintenance action) confirms the snapshot and
   allows old segments to be archived or removed.
4. At least one previous snapshot and its segments are retained for recovery.

A snapshot is not a new source of truth; it is a shorter representation of the
same event history. A Mac that has been offline can replay from a snapshot plus
the segments after it.

### 4.7 What the device does with the log

The firmware must **not** parse the event log at boot. The launcher still reads
zero database bytes. The device reads the projection (`music.hdb` and sidecars)
when the user opens Music. The log is for Macs, diagnostics and rebuilds.

This preserves the current boot budget and the HDB v1 lazy-read design. The log
is a host-facing contract, not a firmware parser.

---

## 5. Projection: from contributions to the device catalog

### 5.1 The projection is a pure function

```text
projection = project(identity, log, play.journal, facts, policy)
```

The output is the existing device artifact set:

```text
projection/music.hdb          HDB v1
projection/music.meta         HMTX
projection/music.art          HART
projection/music.manifest.json
```

Any Mac can run the projection. It is deterministic for the same log, play
journal, facts and policy version. Two Macs with the same inputs should produce
the same bytes. If they do not, the builder is wrong.

### 5.2 Projection rules

| Data | Rule |
|---|---|
| Track identity | Content hash first; recording ID second; path is a locator, not identity |
| Duplicate media | Same content hash → one media blob, multiple path references if needed |
| Metadata field | Latest `FIELD_SET` in append order wins for display; all values remain in history |
| Artist identity | MusicBrainz artist MBID when known; otherwise normalized name + disambiguation |
| Album identity | Release MBID when known; otherwise `(album artist, title, year, disc count)` |
| Artwork | Latest accepted `ART_ADD` for the role; larger art in HART; 96×96 in HDB |
| Description | Latest `DESCRIPTION_SET` in append order; history in the log |
| Playlists | Latest full playlist event plus later item events; missing tracks skipped and reported |
| Podcasts | Deduplicate by `podcast:guid`; episodes by `(feed, guid)` |
| Ratings | Latest contribution or device journal event; equal timestamps keep the higher value |
| Deletion | A track is hidden only after `TOMBSTONE_CONFIRM`; a proposal alone does not remove it |
| Unknown event | Ignored by the projection, preserved in the log |

The "latest wins" rule is a **display policy**, not a claim that the latest
value is true. The Companion can show the history and let the person contribute
a new value. The device shows one calm value, not a conflict dialog.

### 5.3 Generation and manifest

`projection/music.manifest.json`:

```json
{
  "schema": 1,
  "library_id": "b7f1…",
  "log_sequence": 1842,
  "catalog_generation": 73,
  "builder_contributor_id": "c91a…",
  "built_at": 1762400000,
  "policy_version": 1,
  "files": {
    "music.hdb": { "size": 3211264, "sha256": "…", "crc32": "…" },
    "music.meta": { "size": 4123456, "sha256": "…", "crc32": "…" },
    "music.art": { "size": 48123456, "sha256": "…", "crc32": "…" }
  }
}
```

The HDB `generation` and the sidecar `catalog_generation` remain the binding
from the companion paper. The manifest's `log_sequence` says how much of the
log the projection covers. A newer log with an older projection is a normal
state: the device shows the last good catalog; the next contributor builds a new
projection.

### 5.4 Stale projection

A Mac may add a track and leave without building. The media file exists; the
catalog does not point to it. The device shows the old catalog. The manifest
says `log_sequence` is behind. The Companion shows "projection pending" when it
next connects.

A contributor should build a projection before ejecting if it changed the
catalog. If it does not, the next Mac can build it. The device never scans the
filesystem to discover the new file; that would violate the HDB contract.

### 5.5 Why any Mac can build

There is no owner, so there is no privileged builder. Any Mac that can read the
log and the media can build the projection. The manifest records which
contributor built it. If two Macs build the same log, the result should be
identical; if it is not, the policy or builder is wrong.

The role of "builder" is a task, not a status. A Mac that builds is not the
owner. It is a typesetter.

### 5.6 HDB v1 remains the projection target

The event log does not replace HDB. It feeds it. HDB v1 stays frozen, and the
sidecars carry the editorial data that v1 cannot hold. This preserves the
existing firmware reader, the embedded-catalog test image and the 96×96 artwork
contract.

The log can carry more than HDB can display. The projection selects the subset
the device needs. A future HDB v2 could consume more of the log, but it is not
required for this model.

### 5.7 The log is canonical, the projection is disposable

If `music.hdb` is corrupt, rebuild it. If `music.meta` is stale, rebuild it. If
the log's tail is torn, keep the valid prefix. If the log is lost, the library
loses its history but the media files remain; a new log can be seeded from the
current files and tags. The log is the memory, but it is not the only copy of
the media.

This is the recovery story:

```text
projection corrupt  → rebuild from log
sidecar stale       → ignore it; rebuild
log tail torn       → keep valid prefix; preserve tail for diagnosis
log lost            → reseed from media + tags; history is lost
media missing       → projection reports DB_STALE; no automatic delete
```

---

## 6. Multi-Mac contribution without membership

### 6.1 The scenarios that break the one-Mac model

| Scenario | What happens in a host-owned model | What should happen here |
|---|---|---|
| Desktop iMac + work MacBook | The second Mac must join or be treated as a second library; one becomes primary | Both contribute; the iPod is the shared place; no primary |
| Two family members, one iPod | One person's library is the master; the other is a guest or a separate library | Both contribute; the custodian can curate; contributor IDs preserve provenance |
| Old Mac during migration | The old Mac owns the library until the new Mac takes over | The old Mac contributes a final set of files/events; the new Mac reads and continues |
| A friend's Mac for a mixtape | The friend needs an account, a library or a full sync | The friend writes a guest capsule; the custodian promotes or discards |
| A Mac that only has 20 albums | It must mirror the whole library to participate | It can be a thin contributor; it adds 20 albums and reads the device projection |
| A Mac that disappears | The library is tied to it | Its contributions remain; its contributor ID stops appearing; no cleanup ceremony |
| Two iPods | Each device has its own library | A Mac can contribute to both; `library_id` keeps them separate |

The ownerless model is not a rare edge case. It is the normal case for a
personal media object that outlives a computer.

### 6.2 The read-add-leave protocol

A Mac does not join. It only adds. It performs four steps:

```text
1. READ     mount the device, read identity.json, the log and the projection
2. WORK     open a local workspace; decide what to contribute
3. ADD      copy new media and append new events to the log
4. BUILD    rebuild the projection if the catalog changed; eject
```

There is no registration, no handshake, no membership record and no "leave."
The Mac simply stops contributing. The log keeps its events; the contributor ID
keeps its provenance.

### 6.3 The local workspace is not the library

A Mac still needs a local SQLite database, but its role changes:

| Local database | Device library |
|---|---|
| Workspace and cache | Canonical commons |
| Holds the Mac's own files and pending contributions | Holds all contributions |
| Can be discarded and rebuilt | Must survive the Mac |
| May be partial | Is the shared whole |
| Uses the schema from the companion paper | Uses the log + projection |

This means the Companion can show the device library without mirroring it. It
reads `music.hdb` and the sidecars, renders the library, and opens the local
workspace only when the user wants to add or edit something.

The old `device` and `sync_session` tables become local history, not a registry
of members. They may record which device was touched and what was contributed,
but they do not grant authority or membership.

### 6.4 Thin contributors

A thin contributor does not have the whole library. It has a folder of music, a
podcast feed or a few album edits. It can still contribute:

- it reads the device projection to see what already exists;
- it matches its files by content hash and recording ID;
- it appends only the new tracks and field events;
- it can build a projection for the whole device because the media and log are
  on the device, not in its local library.

This is the key difference from a replica model. A Mac does not need a full copy
to participate. The device is the shared storage; the Mac is a tool.

### 6.5 Media contribution

A contributed file carries:

```json
{
  "path": "/Music/Artist/Album/01 Track.flac",
  "content_sha256": "…",
  "size": 34567890,
  "duration_ms": 312000,
  "tags": { "title": "…", "artist": "…", "album": "…" }
}
```

Rules:

- Content hash is identity. Two Macs contributing the same bytes produce one
  media blob and two `TRACK_ADD` events that resolve to the same track.
- A path collision with different content gets a disambiguated path
  (`01 Track (2).flac`) or a content-addressed media path; the projection keeps
  the display path in HDB.
- A move is a `TRACK_MOVE` event; the content hash preserves identity and play
  history.
- A `TRACK_ADD` is valid only if the media blob exists on the device and its
  hash matches. A missing blob is reported, not silently added to the catalog.
- A Mac cannot delete another Mac's media by unlinking it. Removal is a
  tombstone proposal plus custodian confirmation.

### 6.6 Metadata contribution

A metadata edit is a `FIELD_SET` event. It never mutates the previous value.
The projection displays the latest event in append order. The Companion can show
the full history:

```text
genre: "post-rock"   Studio Mac   2 Nov
genre: "ambient"     MacBook      5 Nov
genre: "post-rock"   Custodian    6 Nov   (pinned)
```

A "pin" is itself a `FIELD_SET` with `source=custodian`. It is not an ownership
claim; it is a contribution with a higher display priority.

### 6.7 Deletion: proposal and custodial confirmation

A Mac cannot destroy. It can propose:

```text
TOMBSTONE_PROPOSE  entity=track/1234  reason="duplicate"  contributor=MacBook
```

The proposal is mirrored in a tiny
`projection/maintenance.json` file written by the contributing Mac. The file is
fixed-format; the JSON shape is illustrative. The device's maintenance screen
reads only that file, never the full log, and shows a short custodial token. The
person confirms on the device (button gesture or token entry). The Mac then
appends:

```text
TOMBSTONE_CONFIRM  proposal=…  token=…  contributor=MacBook
```

Only after that does the projection hide the track. The file is removed after a
grace period or at the next compaction, not immediately. A Mac that has been
offline for months cannot resurrect a confirmed removal; its old events remain
historical.

This is the one place where a Mac writes a destructive-looking event, and it is
why the custodial token exists. The token is not a membership credential; it is
proof that the person holding the device agreed.

### 6.8 Guest capsules

A guest Mac — a friend, a borrowed laptop, a machine at a studio — should not
read or write the library log. It writes a capsule:

```text
/.helikon/library/guests/<capsule_id>/
  manifest.json       title, author, notes, created_at, contributor_label
  media/              the contributed files
  events.hlev         a small event log scoped to the capsule
```

The guest can create a mixtape, a live recording, a playlist or a set of album
notes. It cannot see the rest of the library through the Companion's guest mode.
The custodian can:

- play the capsule directly;
- promote it into the library (append its events to the main log and move its
  media);
- discard it.

Promotion is a custodian action, not a join. The guest never becomes a member.

### 6.9 Lost Macs, wiped Macs and backups

A Mac can be lost, wiped or replaced. Nothing needs to be unregistered:

- its past events remain, labelled with its contributor ID;
- its future contributions stop;
- a new Mac gets a new contributor ID and continues;
- the library does not depend on the old Mac.

Any Mac can back up the library by copying `/.helikon/library` and verifying the
manifest. A backup is a copy of the commons, not an authoritative replica. The
person can restore it to the device; the log's `library_id` and `log_sequence`
tell the Macs what they are looking at.

### 6.10 The vocabulary of the Companion

The UI should not say "join," "host," "master," "owner" or "sync account." It
should say:

| Instead of | Say |
|---|---|
| Join this library | Add to this iPod |
| This Mac owns the library | This iPod's library |
| Sync from master | Contribute changes |
| Host registry | Contributors (history only) |
| Conflict resolution | Choose a value / view history |
| Delete | Propose removal |

The device itself should show the library as one thing: "Music," "Artists,"
"Albums." It should not expose contributor politics. A small settings page can
show "Last updated," "Contributors," "Pending projection" and "Guest capsules"
when the custodian asks for it.

### 6.11 Why this is not a distributed system

There is no concurrent write, because only one Mac can mount the device at a
time. There is no leader election, because there is no leader. There is no
vector clock, because append order is total. There is no merge conflict in the
device, because the device never merges; the projection applies a display policy.

The hard part is not consensus. It is **semantics**: what does a contribution
mean, what does deletion mean, and what should the person see when two
contributions disagree.

---

## 7. The one-sentence artist model

### 7.1 The gap in the artist view

The current artist view has a portrait band, name, genre/facts, tabs and a
two-line biography or facts area. A full biography is often too long, too
generic or missing. A **single sentence** is enough to give the artist a voice
without turning the page into a wall of text.

The sentence is not a summary of a Wikipedia article. It is a small, grounded
statement built from facts the library already has. It is a typesetting step,
not a personality.

Artist is the first target because the artist view already reserves the space.
The same `DESCRIPTION_SET` pattern can later describe an album, a podcast show
or an audiobook, but it should not be generalized before the artist case is
evaluated.

### 7.2 Hidden by design

There is no chat, no prompt box, no "Ask AI," no assistant and no model badge on
the artist page. The sentence is simply a field. In the Companion's history it
has provenance:

```text
"Formed in Reykjavík in 1994, Sigur Rós is a post-rock group known for Ágætis byrjun."
source: model:helikon-sentence-v1
facts: formed_year, origin, genre, key_album
contributor: Studio Mac
generated_at: 2026-11-06
```

The device shows the sentence if it exists. If it does not, it shows a facts line
or nothing. The user can edit the sentence like any other metadata; the edit
becomes a human contribution.

### 7.3 Facts first

The model receives a fact sheet, not a web page:

```json
{
  "artist_id": "mbid:…",
  "name": "Sigur Rós",
  "formed_year": 1994,
  "origin": "Reykjavík, Iceland",
  "genres": ["post-rock", "ambient"],
  "members": ["Jónsi", "Georg Hólm", "Orri Páll Dýrason"],
  "key_albums": ["Ágætis byrjun", "( )"],
  "active_years": "1994–present"
}
```

The fact sheet comes from the Companion's metadata layer: MusicBrainz,
Wikidata, embedded tags or user edits. The model may not introduce a fact that
is not in the sheet. If the sheet has only a name and a genre, the sentence is
allowed to be short; if it has only a name, there is no sentence.

### 7.4 Where the model runs

| Placement | What it is | Assessment |
|---|---|---|
| **Companion-side** | A local model or sentence realizer runs during contribution; the sentence is written as a `DESCRIPTION_SET` event | Recommended first. Best quality, no device cost, no network, invisible to the user. |
| **Device-side realizer** | A tiny fact-grounded model runs on the 6G at display time | The interesting research question. 64 MiB, 216 MHz, no NPU; must be a tiny realizer, not an LLM. |
| **Hybrid** | Companion generates; device can regenerate if facts change | More complexity; only worth it if device-side quality is proven. |

The Companion-side version is not a compromise. It is the right place for a
model that needs training, evaluation and updates. The device-side version is a
constraint research project: what is the smallest model that can turn a fact
sheet into one good sentence, on a 6G, without hallucinating?

An on-device model is not rejected on principle. It is a constraint question:
64 MiB of RAM, a slow CPU, no neural accelerator and a battery budget that should
go to audio. The research should establish the smallest useful realizer before
promising one. If a template beats the model on factuality and specificity, the
template wins.

### 7.5 Model options, from smallest to largest

| Option | Size | Where | Trade-off |
|---|---:|---|---|
| Template / slot filling | 0 | Anywhere | Deterministic, factual, repetitive; a useful baseline and fallback |
| Phrase-selection model | 100k–1M params | Device or host | Chooses among grammatical templates; tiny, safe, limited |
| Tiny neural realizer | 1M–10M params | Device or host | More natural; needs training data; quality drops with tiny size |
| Small local language model | 0.5B–3B params | Host only | Best fluency; heavier, slower, still needs fact grounding |
| Large cloud model | Billions | Rejected | Violates local-first, privacy and the no-network device rule |

The paper does not assume a specific architecture. It proposes a research
program: train or evaluate a realizer on fact sheets, constrain it to one
sentence, and compare it with templates. If the tiny model cannot beat a good
template on factuality and specificity, use the template.

### 7.6 Constrained generation

The model is constrained:

- one sentence, target 80–140 characters, hard cap 200;
- no lists, no quotations, no second person, no hype adjectives ("legendary,"
  "iconic," "revolutionary") unless they are in the facts;
- every clause must be supported by the fact sheet;
- no invented dates, places, members, albums or genres;
- no external knowledge, no browsing, no retrieval at generation time;
- a grammar/template fallback if decoding fails;
- if the fact sheet is too thin, return nothing.

A simple constrained decoder can reject a candidate that contains a number,
name or place not present in the facts. A grammar can enforce "one sentence,
no semicolon, no newline." The model is allowed to choose phrasing, not facts.

### 7.7 Evaluation

The sentence is evaluated on:

| Criterion | Question | Method |
|---|---|---|
| Factuality | Is every factual claim in the fact sheet? | Human fact-check; automatic entity/number check |
| Specificity | Does it say something real, not "is a musical artist"? | Rubric 1–5; at least two facts |
| Fluency | Is it grammatical and readable? | Rubric 1–5 |
| Tone | Is it calm and editorial, not hype? | Rubric 1–5 |
| Length | Is it one sentence within the cap? | Automatic |
| Diversity | Do 100 artists get varied sentences? | Template/pattern frequency |
| Trust | Would a person keep it? | User rating; edit rate |
| Cost | Latency, memory, battery | Host and device measurement |

A single hallucinated fact is a failure. A generic sentence is a quality
failure. A sentence that sounds like an advertisement is a tone failure.

### 7.8 Examples

**Good:**

> Formed in Reykjavík in 1994, Sigur Rós is a post-rock group known for
> *Ágætis byrjun*.

**Good, sparse facts:**

> Sigur Rós is an Icelandic post-rock group.

**Bad — unsupported:**

> Sigur Rós is a legendary Icelandic band that changed music forever.

Why: "legendary" and "changed music forever" are not facts; the sentence is
hype.

**Bad — invented:**

> Sigur Rós was formed in 1994 by Jónsi, Georg Hólm, Orri Páll Dýrason and
> Kjartan Sveinsson.

Why: if Kjartan is not in the fact sheet, the model invented him.

**Bad — generic:**

> Sigur Rós is a musical artist from Iceland.

Why: it says almost nothing; it could describe thousands of artists.

### 7.9 One sentence is cheap

A sentence is tiny:

- 2,000 artists × 140 bytes ≈ 280 KB;
- 10,000 artists × 140 bytes ≈ 1.4 MB;
- 10,000 sentences with provenance ≈ a few megabytes in HMTX.

This fits the sidecar model easily. The cost is generation and evaluation, not
storage.

### 7.10 The model is a contributor

In the ownerless model, the model is not an owner or an authority. It is a
narrow contributor:

```text
DESCRIPTION_SET  artist=42
  text="Formed in Reykjavík in 1994, Sigur Rós is a post-rock group known for Ágætis byrjun."
  facts=[formed_year, origin, genre, key_album]
  model=helikon-sentence-v1
  contributor=Studio Mac
```

Another Mac can contribute a different sentence. The latest append wins for
display; the history keeps both. The custodian can pin one. The model has no
special privilege.

The projection writes the sentence into the HDB v1 artist biography string (one
sentence fits easily) and an HMTX `bio` record for provenance. The device shows
the sentence; the Companion can show the facts, model and contributor history.

---

## 8. Beyond music

The same commons works for the rest of the library, with different projections.

### 8.1 Podcasts

A subscription is a `PODCAST_ADD` event; an episode is an `EPISODE_ADD` event.
Any Mac can contribute a feed or an episode. Deduplicate by `podcast:guid` and
`(feed, guid)`. The device's play state remains device-owned.

A one-sentence show description is optional; the same fact-grounded model can
use show title, author, category and recent episode topics. It is not required
for v1.

### 8.2 Audiobooks

Books and sections are contributions; covers and descriptions are contributions;
progress is device-owned. The projection is `books.hdb` plus sidecars. A book
can be contributed by one Mac and resumed on another because the device holds
the state.

### 8.3 Playlists

A playlist is a contribution with an ordered list of track identities. A Mac can
add a playlist without owning the tracks. Missing tracks are skipped and
reported; the playlist remains editable. The device plays the projection.

### 8.4 Ratings and play state

Ratings can be contributed by any Mac or changed on the device. Play history is
device-owned: it is the one thing a Mac cannot reconstruct. The device journal
is imported by whichever Mac mounts next; duplicate events are ignored by event
ID. A Mac that never mounts again does not lose the history; it stays on the
device.

### 8.5 Artwork and descriptions

Artwork and descriptions are contributions with provenance. A cover from the
Cover Art Archive, a user scan, a model sentence and a Wikipedia summary are
different contributions; the projection chooses the display value by policy.
The device does not need to know which was generated where.

---

## 9. Security, trust and failure

### 9.1 The trust boundary is physical

There are no accounts, no cloud tokens and no membership keys. A Mac that can
mount the device can append to the log. That is the model: the person who holds
the device controls it.

This does not mean there is no security:

| Threat | Control |
|---|---|
| A stranger's Mac writes junk | Guest capsules live in quarantine; only the custodian promotes them |
| A Mac appends a malicious event | Event CRC; projection ignores unknown kinds; Companion history shows contributor; custodian can propose removal |
| A Mac spoofs another contributor ID | Optional contributor signing; without it, provenance is best-effort, not identity |
| A torn append | CRC and length check; ignore the torn tail; preserve it for diagnosis |
| A corrupted segment | Keep valid prefix; rebuild from snapshot + later segments |
| A stale projection | Manifest `log_sequence`; rebuild before showing new content |
| A deleted file | Tombstone + custodian confirmation; no silent unlink |
| A lost device | The library is on the device; backups are copies; no cloud recovery |
| A stolen Mac | The Mac's workspace may contain a cache; the device is not unlocked by the Mac |

The physical model is not weaker than an account model for a personal iPod. It
is different: possession is the permission, and the custodian is the person.

### 9.2 Optional contributor signing

A Mac may generate a local Ed25519 key and sign its events. The public key is
stored with the event or in a tiny contributor record. Signing is optional; it
strengthens provenance and makes spoofing harder. It is not a join and not a
membership list; it is a signature on a contribution.

The device does not need to verify signatures at boot. The projection builder
can verify them and report unsigned/unknown contributors. The custodian can
ignore or hide unsigned contributions.

### 9.3 Failure table

| Failure | Detection | Safe response | Never do |
|---|---|---|---|
| Torn log tail | CRC/length fail at the end | Keep valid prefix; preserve tail; report | Do not truncate the whole segment |
| Duplicate event | Event ID already present | Ignore duplicate | Do not apply twice |
| Corrupt payload | JSON/schema fail | Skip event; keep raw for diagnosis | Do not guess the value |
| Stale projection | `log_sequence` behind | Show last good catalog; rebuild | Do not scan the filesystem as a fallback |
| Conflicting description | Two `DESCRIPTION_SET` events | Latest display; history available | Do not delete the older event |
| Model hallucination | Fact check fails | Reject sentence; fallback to facts | Do not ship the sentence |
| Missing facts | Fact sheet too thin | No sentence; facts line | Do not invent a fact |
| Guest pollution | Capsule not promoted | Quarantine; discard | Do not merge into the library |
| Device full | Free-space check | Block contribution; propose GC | Do not delete media silently |
| Clock skew | Timestamp outside window | Use append order; mark history | Do not reorder the log |
| Log format upgrade | Unknown version | Read-only; preserve; migrate with a copy | Do not rewrite in place |

### 9.4 Recovery

Recovery follows the same hierarchy as the companion paper:

1. keep the last good projection;
2. keep the valid prefix of the log;
3. rebuild the projection from the log;
4. if the log is lost, reseed from media and tags;
5. if the media is lost, report `DB_STALE`; do not delete the log entry;
6. preserve every failed artifact for diagnosis.

There is no firmware "repair" of the log and no Mac-side auto-delete. The
custodian decides, with a backup.

### 9.5 Out of scope: DRM and rights

This paper does not discuss DRM, copyright licensing or media rights. It assumes
the person contributing a file has the right to put it on their own device. The
guest capsule is a technical boundary, not a legal one; a separate policy paper
must address sharing, copying and publication.

---

## 10. Evaluation plan

### 10.1 What to prototype first

This is research, not implementation, but the research needs artifacts:

| Artifact | Purpose |
|---|---|
| Host-side event log writer/reader | Prove append, CRC, torn tail, duplicate handling |
| Projection builder | Prove deterministic HDB/sidecar output from the log |
| Three-Mac simulation | Prove no-join contribution, thin contributors, conflicts |
| Guest capsule prototype | Prove quarantine and promotion |
| One-sentence benchmark | Compare template, tiny realizer and small local model |
| Device-side simulation | Estimate latency/memory of a realizer on 6G constraints |
| User study | Test whether "add, don't join" is understandable and trustworthy |

Relationship to the current tasks:

| Task | What this paper adds |
|---|---|
| T-066 — host library index builder | The builder consumes the contribution log and produces the projection; it no longer owns the library |
| T-067 — play journal | The play journal stays device-owned; the contribution log is a separate host-facing artifact |
| T-082 — podcast/audiobook model | Podcasts and audiobooks use the same contribution + projection pattern |
| T-088 — album/artist pages | The artist page consumes the one-sentence description and facts |

### 10.2 Test matrix

| Area | Cases |
|---|---|
| Log | append, torn tail, duplicate, corrupt payload, segment rollover, snapshot, replay |
| Projection | deterministic bytes, latest field, tombstone, missing media, stale sequence, corrupt HDB |
| Multi-Mac | 3 Macs, thin contributor, same track from two Macs, same path different content, lost Mac, new Mac |
| Deletion | proposal, wrong token, confirmed, offline Mac resurrection attempt |
| Guest | create, play, promote, discard, malicious capsule |
| Model | facts complete, sparse facts, contradictory facts, invented entity, overlong, hype tone |
| Device | 10k tracks, 2k albums/artists, 10k artists, 10k sentences, low battery, full volume |
| UX | user understands no owner, no join, history, promote, delete |

### 10.3 Measures

- **Correctness:** projection matches a reference on the same log; no lost events.
- **Recovery:** every cut point leaves a readable log prefix and a rebuildable
  projection; `fsck_hfs -fn` clean on a disposable image.
- **Scale:** log size, projection time, HDB size, artist page open time, RAM.
- **Model quality:** factuality, specificity, fluency, tone, diversity, edit
  rate, latency.
- **User understanding:** can a person explain who owns the library, what a
  contributor is, and what happens when they delete something?
- **Trust:** do users keep, edit or reject the one-sentence descriptions?

### 10.4 What this paper does not claim

- It does not claim a device-side model is feasible at useful quality.
- It does not claim the log format is frozen.
- It does not claim the event log is crash-safe on a real 6G; it is a design
  that must be tested.
- It does not claim users want an ownerless library; that is a research question.
- It does not claim the one-sentence description is better than a facts line;
  that is what the benchmark is for.

---

## 11. Open questions and proposed decisions

These are proposed for [`decisions.md`](decisions.md); they are not accepted
here. The prefix `OL-` avoids colliding with the companion paper's `PD-` items.

### 11.1 Proposed decisions

| ID | Proposal | Why it matters |
|---|---|---|
| OL-1 | **The device library is the commons; the host database is a workspace.** Replace the companion paper's host-as-source-of-truth assumption. | This is the core ownerless model; it matches the project direction. |
| OL-2 | **Append-only contribution log is canonical; `music.hdb` is a projection.** | Preserves history and lets any Mac rebuild. |
| OL-3 | **No join, no registry, no membership.** A random `contributor_id` is provenance only. | Matches the project direction and removes a whole class of distributed-systems complexity. |
| OL-4 | **Deletion requires a `TOMBSTONE_PROPOSE` plus custodial confirmation.** A Mac cannot erase. | Protects the person and the commons. |
| OL-5 | **Guest contributions live in capsules; promotion is custodial.** | Enables mixtapes and borrowed Macs without library access. |
| OL-6 | **The one-sentence artist model is hidden, fact-grounded and host-side first.** Device-side realizer is research, not a promise. | Delivers the desired micro-AI without risking the device. |
| OL-7 | **A sentence is a `DESCRIPTION_SET` event with facts and model provenance.** | Keeps the model accountable and the library plural. |
| OL-8 | **The projection uses append order for display; timestamps are history only.** | One device gives a total order; no vector clocks. |
| OL-9 | **Log compaction uses snapshots and custodian-approved GC.** | Keeps growth bounded without losing recovery. |
| OL-10 | **The Companion vocabulary is "add," not "join."** | The interface must teach the model. |

### 11.2 Open questions

1. **Log encoding:** canonical JSON, CBOR or a binary TLV? JSON is inspectable;
   CBOR is smaller; the device does not parse either.
2. **Media layout:** keep the user-visible `Music/Artist/Album` tree, or move to
   content-addressed `media/<hash>` with a display path in HDB?
3. **Projection policy:** is "latest append wins" the right default for every
   field, or should some fields prefer embedded tags or custodian pins?
4. **Projection frequency:** must a contributor build before ejecting, or can a
   projection be pending across sessions?
5. **Deletion token:** button gesture, on-device code, or both? How does a Mac
   prove the custodian confirmed without becoming a member?
6. **Contributor signing:** optional or required? If optional, how is unsigned
   provenance displayed?
7. **Multi-device:** can one Mac contribute to two iPods with different
   libraries? How are library IDs shown?
8. **Backups:** is a backup a plain copy of the commons, or a signed export? How
   is a restore verified?
9. **Model size:** what is the smallest realizer that beats a template on
   factuality and specificity? Is device-side generation worth it?
10. **Fact sheet:** which facts are required for a sentence, and which sources
    are allowed? What happens when facts contradict?
11. **User understanding:** do people actually want no owner, or do they expect
    "my Mac" to be in charge? The vocabulary may need to change.
12. **History UI:** should the device show contributor history, or only the
    Companion? How much provenance is useful before it becomes noise?
13. **Guest capsules:** how do you prevent a guest from reading the library when
    the filesystem itself is readable? Is guest mode a host-app convention or a
    device-enforced mount mode?
14. **Log loss:** if the log is lost but media remains, is a reseeded library
    acceptable? What provenance is lost?
15. **Rights:** explicitly out of scope here; a separate policy paper must
    decide sharing, copying and publication.

### 11.3 Conflict with the companion paper

The companion paper says the host is the source of truth and proposes a host
registry/join model. This paper says the device is the commons and Macs do not
join. The two cannot both be true.

The paper does not silently overwrite the earlier document. It records the
conflict and proposes the ownerless model for owner decision. If accepted, the
companion paper's §2.4, §6 and §8 need revision; the HDB/sidecar and transfer
contracts remain.

---

## 12. Conclusion

The ownerless library is a small shift in wording with a large shift in
architecture. It says:

- the library is not a Mac's database; it is the device's commons;
- a Mac does not join; it contributes;
- a correction is a new event, not an overwrite;
- the catalog is a projection, not the truth;
- the person holding the device is the custodian, not the software owner;
- deletion is a custodial act, not a sync operation;
- a guest can contribute a capsule without becoming a member;
- the one-sentence artist model is a typesetter, not an author.

The model is deliberately modest about AI. It does not put a chatbot on the
iPod. It does not turn the artist page into a conversation. It puts one factual,
calm sentence where a paragraph would not fit. The sentence is a contribution
with provenance, like a note from a friend.

The result is a library that can be contributed to by many Macs, survives any
one of them, and still feels like one coherent object in the hand. That is a
better fit for an iPod than a tiny computer pretending to be a cloud.

---

## 13. References

Repository sources:

1. [`companion-sync-research.md`](companion-sync-research.md) — the host/device
   architecture this paper revises.
2. [`helikondb.md`](helikondb.md) — HDB v1 format and builder contract.
3. [`ui.md`](ui.md) — artist view, typography and the two-line biography area.
4. [`companion-app.md`](companion-app.md) — companion reset record.
5. [`state.md`](state.md) — current phase and evidence.
6. [`tasks.md`](tasks.md) — T-066, T-067, T-082, T-088.
7. [`decisions.md`](decisions.md) — D-07, D-33, D-43, D-48, D-53.
8. [`design-paper.md`](design-paper.md) — §12, §25, §28.3.5–6, §28.4.1–2.
9. [`security.md`](security.md) — physical/network/device rules.
10. [`failures.md`](failures.md) — error taxonomy and safe responses.

External and conceptual sources:

11. Martin Fowler, “Event Sourcing” —
    <https://martinfowler.com/eaaDev/EventSourcing.html>.
12. Kleppmann et al., “Local-first software” —
    <https://www.inkandswitch.com/local-first/>.
13. Eldan and Li, “TinyStories: How Small Can Language Models Be and Still Speak
    Coherent English?” —
    <https://arxiv.org/abs/2305.07759>.
14. MusicBrainz API — <https://musicbrainz.org/doc/MusicBrainz_API>.
15. Wikidata — <https://www.wikidata.org/>.
16. Podcast Index API — <https://podcastindex-org.github.io/docs-api/>.
17. Podcasting 2.0 namespace — <https://podcastnamespace.org/>.
18. Apple HFS+ TN1150 —
    <https://developer.apple.com/library/archive/technotes/tn/tn1150.html>.

---

## Appendix A — Event payload examples

All payloads are canonical JSON in the proposed format; the device stores them
without parsing.

### A.1 `TRACK_ADD`

```json
{
  "path": "/Music/Sigur Rós/Ágætis byrjun/01 Intro.flac",
  "content_sha256": "…",
  "size": 34567890,
  "duration_ms": 312000,
  "tags": {
    "title": "Intro",
    "artist": "Sigur Rós",
    "album": "Ágætis byrjun",
    "track": 1,
    "disc": 1,
    "year": 1999
  }
}
```

### A.2 `FIELD_SET`

```json
{
  "entity": "artist",
  "entity_id": "mbid:…",
  "field": "genre",
  "value": "post-rock",
  "source": "MusicBrainz",
  "confidence": 0.92,
  "timestamp": 1762400000
}
```

### A.3 `DESCRIPTION_SET`

```json
{
  "entity": "artist",
  "entity_id": "mbid:…",
  "text": "Formed in Reykjavík in 1994, Sigur Rós is a post-rock group known for Ágætis byrjun.",
  "facts": ["formed_year", "origin", "genre", "key_album"],
  "model": "helikon-sentence-v1",
  "model_version": 1,
  "generated_at": 1762400000,
  "contributor_label": "Studio Mac"
}
```

### A.4 `TOMBSTONE_PROPOSE` / `TOMBSTONE_CONFIRM`

```json
{
  "proposal_id": "t-9f2c…",
  "entity": "track",
  "entity_id": "content:…",
  "reason": "duplicate",
  "timestamp": 1762400000
}
```

```json
{
  "proposal_id": "t-9f2c…",
  "token": "482913",
  "confirmed_at": 1762400100
}
```

### A.5 `SNAPSHOT`

```json
{
  "snapshot_id": "s-001",
  "first_sequence": 1,
  "last_sequence": 1842,
  "state_sha256": "…",
  "projection_policy_version": 1,
  "created_at": 1762400000
}
```

---

## Appendix B — Projection pseudocode

```text
function project(log, play_journal, facts, policy):
    state = empty_library()

    for event in log.valid_prefix():
        switch event.kind:
            TRACK_ADD:
                state.upsert_track(event.payload, event.contributor)
            TRACK_MOVE:
                state.move_track(event.payload)
            FIELD_SET:
                state.set_field(event.payload, event.append_order)
            ART_ADD:
                state.add_art(event.payload)
            PLAYLIST_ADD:
                state.upsert_playlist(event.payload)
            PLAYLIST_ITEM:
                state.set_playlist_item(event.payload)
            PODCAST_ADD:
                state.upsert_feed(event.payload)
            EPISODE_ADD:
                state.upsert_episode(event.payload)
            DESCRIPTION_SET:
                state.set_description(event.payload)
            TOMBSTONE_CONFIRM:
                state.hide(event.payload)
            SNAPSHOT:
                state = load_snapshot(event.payload)
            unknown:
                ignore

    state.apply_play_journal(play_journal)
    state.apply_policy(policy)

    write_hdb(state)
    write_hmtx(state)
    write_hart(state)
    write_manifest(state)
```

Rules:

- Unknown events are preserved and ignored by the projection.
- A snapshot replaces the state up to its last sequence; later events apply on
  top.
- The projection never mutates the log.
- The projection is deterministic; the manifest records the policy version.

---

## Appendix C — Fact sheet and sentence rubric

### C.1 Fact sheet fields

| Field | Required | Source | Example |
|---|---|---|---|
| `name` | yes | tags/MusicBrainz | Sigur Rós |
| `formed_year` | no | Wikidata/MusicBrainz | 1994 |
| `origin` | no | Wikidata | Reykjavík, Iceland |
| `genres` | no | MusicBrainz/tags | post-rock, ambient |
| `members` | no | MusicBrainz/Wikidata | Jónsi, Georg Hólm, Orri Páll Dýrason |
| `key_albums` | no | release data/user | Ágætis byrjun |
| `active_years` | no | Wikidata | 1994–present |

### C.2 Sentence rubric

| Score | Factuality | Specificity | Fluency | Tone |
|---:|---|---|---|---|
| 5 | Every claim in facts | Three or more facts, distinctive | Natural, one sentence | Calm, editorial |
| 4 | Every claim in facts | Two facts | Minor awkwardness | Mostly calm |
| 3 | One minor unsupported phrase | One fact plus name | Understandable | Slightly generic |
| 2 | Unsupported claim | Generic | Awkward | Hype or filler |
| 1 | Invented fact | Empty | Broken | Advertisement |

A sentence with any invented fact scores 0 on factuality and is rejected.

### C.3 Rejection examples

- Contains a member not in `members`.
- Contains a year not in `formed_year` or `active_years`.
- Contains a genre not in `genres`.
- Contains “legendary,” “iconic,” “revolutionary” without a fact.
- More than one sentence.
- Over 200 characters.
- Second person (“you will love”).

---

## Appendix D — Worked example: three Macs, one artist

### D.1 The commons

```text
iPod library b7f1…
  Studio Mac   contributed 400 tracks, 40 artist facts, 30 descriptions
  MacBook      contributed 120 tracks, 10 descriptions
  Old Mac      contributed 20 tracks, 5 artist facts
```

There is no owner. The device holds the log; the Macs come and go.

### D.2 The artist page

```text
Artist: Sigur Rós
Facts: formed 1994, Reykjavík, post-rock
Sentence: Formed in Reykjavík in 1994, Sigur Rós is a post-rock group known for Ágætis byrjun.
History: Studio Mac, 2 Nov; MacBook, 5 Nov; custodian pin, 6 Nov
```

The device shows one sentence. The Companion can show the history and the facts
used. The model's contribution is one line among many.

### D.3 A guest mixtape

```text
Guest Mac → /guests/mix-2026-11-07/
  manifest: "Late Night Reykjavík"
  media: 12 tracks
  events: playlist, notes, cover
```

The guest cannot read the library. The custodian plays the capsule, then promotes
it. The tracks become contributions; the guest does not join.

### D.4 A deletion

```text
MacBook: TOMBSTONE_PROPOSE track/content:… reason="duplicate"
Device:  shows token 482913
Custodian: confirms on device
MacBook: TOMBSTONE_CONFIRM proposal=… token=482913
Projection: hides the duplicate; file removed at next compaction
```

The old `TRACK_ADD` remains in the log. The history explains what happened.

---

## Appendix E — Glossary

| Term | Meaning |
|---|---|
| **Commons** | A shared library that many Macs can add to without owning it. |
| **Contribution** | An append-only event that adds media, metadata, art, a playlist, a podcast, a description or a proposal. |
| **Contributor** | A Mac or process that appends an event; identified for provenance, not membership. |
| **Custodian** | The person holding the device; confirms deletion, compaction and guest promotion. |
| **Custodial token** | A short device-generated code that proves the custodian confirmed a destructive proposal. |
| **Event log** | The ordered, append-only record of contributions on the device. |
| **Projection** | The deterministic HDB/sidecar catalog built from the log. |
| **HLEV** | The proposed append-only event segment format. |
| **Fact sheet** | Structured facts used to ground a one-sentence description. |
| **Sentence realizer** | A tiny model or template that turns a fact sheet into one sentence. |
| **Guest capsule** | A write-only contribution from a guest Mac, promotable by the custodian. |
| **Thin contributor** | A Mac that contributes without holding a full copy of the library. |
| **Append order** | The order in which the device receives events; the display order. |
| **Ownerless** | No Mac owns or joins the library; the person is the custodian. |
