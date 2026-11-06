#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""HelikonDB reference builder and inspector.

HelikonDB is the native, immutable music catalog. The companion app owns the
production builder; this tool freezes the format, exercises the firmware
reader and gives the companion a tested reference implementation. It never
reads or writes iTunesDB.

Layout (little-endian, all sections 4-byte aligned):

    header 128 B
    tracks   88 B each, sorted by album/disc/track
    albums   48 B each, sorted by artist/year/title
    artists  48 B each, sorted by sort name
    playlists 24 B each
    playlist entries u32 track indices
    strings  length-prefixed UTF-8
    indexes  16 B headers + u32 arrays
    artwork  32 B records + RGB565 blobs

Usage:
    hdb_build.py build MUSIC_DIR --out OUT.hdb [--sample 100] [--cache C]
    hdb_build.py inspect OUT.hdb [--json]
    hdb_build.py fixture --out OUT.hdb
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import struct
import sys
import time
import unicodedata
import zlib
from dataclasses import dataclass, field
from io import BytesIO
from pathlib import Path
from typing import Any

try:
    import mutagen
    from PIL import Image, ImageOps
except ImportError as exc:  # pragma: no cover - environment guard
    raise SystemExit("hdb_build.py needs mutagen and Pillow") from exc

MAGIC = b"HLDB"
VERSION = 1
HEADER_SIZE = 128
TRACK_SIZE = 88
ALBUM_SIZE = 48
ARTIST_SIZE = 48
PLAYLIST_SIZE = 24
INDEX_SIZE = 16
ART_SIZE = 32
NO_ART = 0xFFFFFFFF
NO_STR = 0xFFFFFFFF
ART_SIZE_PX = 96
AUDIO_EXTS = {
    ".mp3", ".m4a", ".mp4", ".aac", ".alac", ".flac", ".ogg", ".oga",
    ".opus", ".wav", ".aif", ".aiff",
}
FILETYPE = {
    ".mp3": 1, ".m4a": 2, ".mp4": 2, ".aac": 2, ".alac": 3,
    ".flac": 4, ".wav": 5, ".aif": 6, ".aiff": 6, ".ogg": 7,
    ".oga": 7, ".opus": 8,
}
TRACK_F_ALBUM = 0


def nfc(value: str) -> str:
    return unicodedata.normalize("NFC", value or "")


def nfd(value: str) -> str:
    return unicodedata.normalize("NFD", value or "")


def sort_key(value: str) -> str:
    return unicodedata.normalize("NFKD", value or "").casefold().strip()


def first(value: Any, default: str = "") -> str:
    if value is None:
        return default
    if isinstance(value, (list, tuple)):
        return str(value[0]) if value else default
    return str(value)


def parse_pair(value: str) -> tuple[int, int]:
    text = first(value)
    if "/" in text:
        left, right = text.split("/", 1)
        try:
            return int(left), int(right)
        except ValueError:
            return 0, 0
    try:
        return int(text), 0
    except (TypeError, ValueError):
        return 0, 0


def parse_year(value: str) -> int:
    text = first(value)
    for token in text.replace("-", " ").split():
        if token.isdigit() and len(token) >= 4:
            return int(token[:4])
    return 0


def uid_for(device_path: str) -> int:
    """Stable 64-bit identity: FNV-1a over the canonical path."""
    value = 0xCBF29CE484222325
    for byte in device_path.encode("utf-8"):
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value or 1


class Strings:
    def __init__(self) -> None:
        self.data = bytearray()
        self.map: dict[str, int] = {}

    def ref(self, value: str) -> int:
        if not value:
            return NO_STR
        if value in self.map:
            return self.map[value]
        encoded = value.encode("utf-8")
        if len(encoded) > 0xFFFF:
            encoded = encoded[:0xFFFF]
        offset = len(self.data)
        self.data += struct.pack("<H", len(encoded))
        self.data += encoded
        self.map[value] = offset
        return offset


class ArtPool:
    def __init__(self) -> None:
        self.blobs: dict[str, bytes] = {}

    def add(self, blob: bytes | None) -> str | None:
        if not blob:
            return None
        digest = hashlib.sha1(blob).hexdigest()
        self.blobs.setdefault(digest, blob)
        return digest


def rgb565_blob(image: Image.Image, size: int = ART_SIZE_PX) -> bytes:
    fitted = ImageOps.fit(image.convert("RGB"), (size, size),
                          method=Image.Resampling.LANCZOS)
    source = fitted.load()
    out = bytearray(size * size * 2)
    offset = 0
    for y in range(size):
        for x in range(size):
            r, g, b = source[x, y]
            value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            struct.pack_into("<H", out, offset, value)
            offset += 2
    return bytes(out)


def art_from_bytes(data: bytes | None, size: int = ART_SIZE_PX) -> bytes | None:
    if not data:
        return None
    try:
        image = Image.open(BytesIO(data))
        image.load()
    except Exception:
        return None
    return rgb565_blob(image, size)


def artwork_bytes(audio: Any) -> bytes | None:
    tags = getattr(audio, "tags", None)
    if tags is None:
        return None
    # ID3 APIC
    apic = getattr(tags, "getall", lambda _key: [])("APIC")
    if apic:
        return bytes(apic[0].data)
    # MP4 covr
    try:
        covers = tags.get("covr")
    except Exception:
        covers = None
    if covers:
        return bytes(covers[0])
    # FLAC pictures
    pictures = getattr(audio, "pictures", None)
    if pictures:
        return bytes(pictures[0].data)
    return None


def tag_text(audio: Any, key: str) -> str:
    tags = getattr(audio, "tags", None)
    if tags is None:
        return ""
    # ID3 frames expose getall and .text; MP4/Vorbis dicts use get().
    getter = getattr(tags, "getall", None)
    if getter is not None:
        try:
            frames = getter(key)
        except Exception:
            frames = []
        if frames:
            frame = frames[0]
            if hasattr(frame, "text"):
                return first(frame.text)
            return first(frame)
    try:
        value = tags.get(key)
    except Exception:
        value = None
    return first(value)


def media_pair(tags: Any, key: str) -> str:
    if tags is None:
        return ""
    try:
        value = tags.get(key)
    except Exception:
        return ""
    if isinstance(value, (list, tuple)):
        if not value:
            return ""
        value = value[0]
    if isinstance(value, tuple):
        if len(value) > 1 and value[1]:
            return f"{value[0]}/{value[1]}"
        return str(value[0]) if value else ""
    return str(value)


def tag_map(audio: Any) -> dict[str, str]:
    """Return the common fields for MP3, MP4/M4A, FLAC/Ogg, WAV/AIFF."""
    ext = Path(getattr(audio, "filename", "")).suffix.lower()
    title = tag_text(audio, "TIT2") or tag_text(audio, "\xa9nam") or tag_text(audio, "title")
    artist = tag_text(audio, "TPE1") or tag_text(audio, "\xa9ART") or tag_text(audio, "artist")
    album_artist = (tag_text(audio, "TPE2") or tag_text(audio, "aART")
                    or tag_text(audio, "albumartist") or artist)
    album = tag_text(audio, "TALB") or tag_text(audio, "\xa9alb") or tag_text(audio, "album")
    genre = tag_text(audio, "TCON") or tag_text(audio, "\xa9gen") or tag_text(audio, "genre")
    composer = (tag_text(audio, "TCOM") or tag_text(audio, "\xa9wrt")
                or tag_text(audio, "composer"))
    date = (tag_text(audio, "TDRC") or tag_text(audio, "TYER")
            or tag_text(audio, "\xa9day") or tag_text(audio, "date"))
    tags_obj = getattr(audio, "tags", None)
    track = (tag_text(audio, "TRCK") or tag_text(audio, "tracknumber")
             or media_pair(tags_obj, "trkn"))
    disc = (tag_text(audio, "TPOS") or tag_text(audio, "discnumber")
            or media_pair(tags_obj, "disk"))
    sort_title = tag_text(audio, "TSOT") or tag_text(audio, "sonm") or tag_text(audio, "titlesort")
    sort_artist = tag_text(audio, "TSOP") or tag_text(audio, "soar") or tag_text(audio, "artistsort")
    sort_album = tag_text(audio, "TSOA") or tag_text(audio, "soal") or tag_text(audio, "albumsort")
    compilation = (tag_text(audio, "TCMP") or tag_text(audio, "cpil")
                   or tag_text(audio, "compilation")).lower() in ("1", "true", "yes")
    track_no, _ = parse_pair(track)
    disc_no, _ = parse_pair(disc)
    rating = 0
    tags = getattr(audio, "tags", None)
    if tags is not None:
        popm = getattr(tags, "getall", lambda _key: [])("POPM")
        if popm:
            raw = int(getattr(popm[0], "rating", 0))
            rating = round(raw * 100 / 255) if raw else 0
    info = getattr(audio, "info", None)
    return {
        "title": nfc(title),
        "artist": nfc(artist) or "Unknown Artist",
        "album_artist": nfc(album_artist) or "Unknown Artist",
        "album": nfc(album) or "Unknown Album",
        "genre": nfc(genre),
        "composer": nfc(composer),
        "sort_title": nfc(sort_title),
        "sort_artist": nfc(sort_artist),
        "sort_album": nfc(sort_album),
        "year": parse_year(date),
        "track_no": track_no,
        "disc_no": disc_no,
        "compilation": compilation,
        "rating": rating,
        "duration_ms": int(round(getattr(info, "length", 0) * 1000)),
        "bitrate": int(getattr(info, "bitrate", 0) or 0),
        "sample_rate": int(getattr(info, "sample_rate", 0) or 0),
        "filetype": FILETYPE.get(ext, 255),
    }


def extract_job(local_path: str) -> tuple[str, int, int, dict[str, Any] | None]:
    """Top-level worker: must stay picklable for ProcessPoolExecutor."""
    path = Path(local_path)
    stat = path.stat()
    return str(path), stat.st_mtime_ns, stat.st_size, extract_file(path)


def extract_file(local_path: Path) -> dict[str, Any] | None:
    try:
        audio = mutagen.File(local_path)
    except Exception:
        return None
    if audio is None or getattr(audio, "info", None) is None:
        return None
    try:
        meta = tag_map(audio)
        meta["filetype"] = FILETYPE.get(local_path.suffix.lower(), 255)
        art = artwork_bytes(audio)
    except Exception:
        return None
    return {"meta": meta, "art": art}


def load_cache(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text())
    except Exception:
        return {}


def save_cache(path: Path, cache: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(cache, separators=(",", ":"), sort_keys=True))
    os.replace(tmp, path)


def art_cache_dir(cache_path: Path) -> Path:
    return cache_path.with_suffix(cache_path.suffix + ".art")


def art_cache_load(directory: Path, digest: str) -> bytes | None:
    try:
        return (directory / f"{digest}.rgb565").read_bytes()
    except Exception:
        return None


def art_cache_store(directory: Path, digest: str, blob: bytes) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{digest}.rgb565"
    if not path.exists():
        tmp = path.with_suffix(".tmp")
        tmp.write_bytes(blob)
        os.replace(tmp, path)


@dataclass
class Track:
    uid: int
    path: str
    title: str
    artist_id: int = -1
    album_id: int = -1
    genre: str = ""
    composer: str = ""
    sort_title: str = ""
    sort_artist: str = ""
    sort_album: str = ""
    duration_ms: int = 0
    size_bytes: int = 0
    bitrate: int = 0
    sample_rate: int = 0
    added_time: int = 0
    artwork_id: int = NO_ART
    filetype: int = 0
    track_no: int = 0
    disc_no: int = 0
    year: int = 0
    rating: int = 0
    flags: int = 0


@dataclass
class Album:
    title: str
    artist_id: int
    sort: str
    genre: str
    year: int
    rating: int
    flags: int
    art_sha: str | None = None
    artwork_id: int = NO_ART
    first_track: int = 0
    track_count: int = 0
    disc_count: int = 0
    added_time: int = 0


@dataclass
class Artist:
    name: str
    sort: str
    bio: str = ""
    image_sha: str | None = None
    image_id: int = NO_ART
    rating: int = 0
    flags: int = 0
    album_count: int = 0
    first_album: int = 0
    track_count: int = 0
    added_time: int = 0


@dataclass
class Playlist:
    name: str
    sort: str
    entries: list[int] = field(default_factory=list)
    flags: int = 0


def build_library(
    files: list[Path],
    music_dir: Path | None,
    prefix: str,
    cache: dict[str, Any],
    cache_path: Path,
    jobs: int,
    art_size: int,
    track_ratings: dict[str, int],
    album_ratings: dict[str, int],
    artist_info: dict[str, dict[str, Any]],
    artist_image_dir: Path | None,
) -> tuple[list[Artist], list[Album], list[Track], list[Playlist], ArtPool, dict[str, Any]]:
    started = time.time()
    strings = Strings()
    art_pool = ArtPool()
    art_dir = art_cache_dir(cache_path)
    tracks: list[Track] = []
    albums: dict[tuple[str, str], Album] = {}
    artists: dict[str, Artist] = {}
    cache_hits = 0
    errors = 0

    def device_path(local: Path) -> str:
        if music_dir is not None:
            rel = os.path.relpath(local, music_dir)
        else:
            rel = local.name
        rel = rel.replace(os.sep, "/")
        root = prefix.rstrip("/")
        return nfd((root + "/" + rel) if root else "/" + rel)

    pending: list[tuple[Path, str, os.stat_result]] = []
    for local in files:
        try:
            stat = local.stat()
        except OSError:
            continue
        key = str(local)
        entry = cache.get(key)
        if entry and entry.get("mtime_ns") == stat.st_mtime_ns and entry.get("size") == stat.st_size:
            cache_hits += 1
            continue
        pending.append((local, key, stat))

    extracted: dict[str, tuple[int, int, dict[str, Any] | None]] = {}
    if pending:
        with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, jobs)) as pool:
            for key, mtime_ns, size, result in pool.map(
                    extract_job, [str(item[0]) for item in pending], chunksize=1):
                extracted[key] = (mtime_ns, size, result)

    all_entries: list[tuple[Path, dict[str, Any], bytes | None, str]] = []
    for local in files:
        try:
            stat = local.stat()
        except OSError:
            continue
        key = str(local)
        dpath = device_path(local)
        entry = cache.get(key)
        if entry and entry.get("mtime_ns") == stat.st_mtime_ns and entry.get("size") == stat.st_size:
            meta = entry["meta"]
            art_sha = entry.get("art_sha")
            art = art_cache_load(art_dir, art_sha) if art_sha else None
        else:
            result = extracted.get(key)
            if result is None or result[2] is None:
                errors += 1
                continue
            result_mtime, result_size, result_meta = result
            meta = result_meta["meta"]
            art = art_from_bytes(result_meta["art"], art_size)
            art_sha = None
            if art:
                digest = hashlib.sha1(art).hexdigest()
                art_cache_store(art_dir, digest, art)
                art_sha = digest
            cache[key] = {
                "mtime_ns": result_mtime,
                "size": result_size,
                "meta": meta,
                "art_sha": art_sha,
            }
        all_entries.append((local, meta, art, dpath))
        art_pool.add(art)

    for local, meta, art, dpath in all_entries:
        rating = int(meta.get("rating", 0))
        rating = int(track_ratings.get(dpath, track_ratings.get(str(local), rating)))
        rating = max(0, min(100, rating))
        artist_name = meta["artist"]
        album_artist = meta["album_artist"]
        album_title = meta["album"]
        artist = artists.get(artist_name)
        if artist is None:
            artist = Artist(artist_name, meta.get("sort_artist") or sort_key(artist_name))
            artists[artist_name] = artist
        album_artist_entry = artists.get(album_artist)
        if album_artist_entry is None:
            album_artist_entry = Artist(album_artist, sort_key(album_artist))
            artists[album_artist] = album_artist_entry
        key = (album_artist, album_title)
        album = albums.get(key)
        if album is None:
            album = Album(album_title, -1, meta.get("sort_album") or sort_key(album_title),
                          meta.get("genre", ""), int(meta.get("year", 0)), 0, 0)
            albums[key] = album
        if album.art_sha is None and art is not None:
            album.art_sha = hashlib.sha1(art).hexdigest()
        album.disc_count = max(album.disc_count, int(meta.get("disc_no", 0)))
        track = Track(
            uid=uid_for(dpath), path=dpath, title=meta["title"],
            genre=meta.get("genre", ""), composer=meta.get("composer", ""),
            sort_title=meta.get("sort_title") or sort_key(meta["title"]),
            sort_artist=meta.get("sort_artist") or sort_key(artist_name),
            sort_album=meta.get("sort_album") or sort_key(album_title),
            duration_ms=int(meta.get("duration_ms", 0)), size_bytes=local.stat().st_size,
            bitrate=int(meta.get("bitrate", 0)), sample_rate=int(meta.get("sample_rate", 0)),
            added_time=int(local.stat().st_mtime), filetype=int(meta.get("filetype", 255)),
            track_no=int(meta.get("track_no", 0)), disc_no=int(meta.get("disc_no", 0)),
            year=int(meta.get("year", 0)), rating=rating,
            flags=(1 if meta.get("compilation") else 0),
        )
        track._artist_name = artist_name  # type: ignore[attr-defined]
        track._album_key = key  # type: ignore[attr-defined]
        tracks.append(track)

    artist_list = sorted(artists.values(), key=lambda a: (a.sort, a.name.casefold()))
    artist_ids = {artist.name: index for index, artist in enumerate(artist_list)}
    for track in tracks:
        track.artist_id = artist_ids[track._artist_name]  # type: ignore[attr-defined]
    for (album_artist, _title), album in albums.items():
        album.artist_id = artist_ids[album_artist]
        album.rating = max(0, min(100, int(album_ratings.get(
            f"{album_artist}|{album.title}", 0))))

    album_list = sorted(albums.values(),
                        key=lambda a: (a.artist_id, a.year, a.sort.casefold()))
    album_ids = {id(album): index for index, album in enumerate(album_list)}
    for track in tracks:
        track.album_id = album_ids[id(albums[track._album_key])]  # type: ignore[attr-defined]
    tracks.sort(key=lambda t: (t.album_id, t.disc_no, t.track_no, t.sort_title.casefold()))

    for index, track in enumerate(tracks):
        album = album_list[track.album_id]
        if album.track_count == 0:
            album.first_track = index
        album.track_count += 1
        artist = artist_list[track.artist_id]
        artist.track_count += 1
        artist.added_time = max(artist.added_time, track.added_time)
        album.added_time = max(album.added_time, track.added_time)

    for album in album_list:
        album.artwork_id = NO_ART
    for index, artist in enumerate(artist_list):
        albums_for = [i for i, album in enumerate(album_list) if album.artist_id == index]
        artist.album_count = len(albums_for)
        artist.first_album = albums_for[0] if albums_for else 0
        info = artist_info.get(artist.name) or {}
        artist.bio = nfc(str(info.get("bio", "")))
        image = None
        image_path = info.get("image")
        if image_path:
            try:
                image = art_from_bytes(Path(image_path).read_bytes(), art_size)
            except Exception:
                image = None
        if image is None and artist_image_dir is not None:
            for suffix in (".png", ".jpg", ".jpeg", ".webp"):
                candidate = artist_image_dir / (artist.name.replace("/", "_") + suffix)
                if candidate.is_file():
                    image = art_from_bytes(candidate.read_bytes(), art_size)
                    break
        if image is not None:
            artist.image_sha = art_pool.add(image)

    # Indexes: title order, recent order, genre order, album title order.
    title_order = sorted(range(len(tracks)),
                         key=lambda i: (tracks[i].sort_title.casefold(), tracks[i].title.casefold()))
    recent_order = sorted(range(len(tracks)), key=lambda i: -tracks[i].added_time)
    genre_order = sorted(range(len(tracks)),
                         key=lambda i: (sort_key(tracks[i].genre), tracks[i].artist_id,
                                        tracks[i].album_id, tracks[i].disc_no,
                                        tracks[i].track_no))
    album_title_order = sorted(range(len(album_list)),
                               key=lambda i: (album_list[i].sort.casefold(),
                                              album_list[i].title.casefold()))

    playlist_list: list[Playlist] = []
    # Playlists are added by the caller after track indices are final.

    summary = {
        "elapsed_s": round(time.time() - started, 3),
        "files": len(files),
        "cache_hits": cache_hits,
        "errors": errors,
        "tracks": len(tracks),
        "artists": len(artist_list),
        "albums": len(album_list),
    }
    return artist_list, album_list, tracks, playlist_list, art_pool, summary


def pack_string_ref(strings: Strings, value: str, base: int) -> int:
    ref = strings.ref(value)
    return NO_STR if ref == NO_STR else base + ref


def write_hdb(
    out_path: Path,
    artists: list[Artist],
    albums: list[Album],
    tracks: list[Track],
    playlists: list[Playlist],
    art_pool: ArtPool,
    source_digest: int,
    art_size: int = ART_SIZE_PX,
) -> dict[str, Any]:
    strings = Strings()
    art_records: list[tuple[int, int, str]] = []
    for index, album in enumerate(albums):
        if album.artwork_id != NO_ART:
            pass
    # Reconstruct art records from the objects: kind, owner, sha.
    for index, album in enumerate(albums):
        if album.art_sha and album.art_sha in art_pool.blobs:
            art_records.append((1, index, album.art_sha))
    for index, artist in enumerate(artists):
        if artist.image_sha and artist.image_sha in art_pool.blobs:
            art_records.append((2, index, artist.image_sha))
    art_records.sort(key=lambda item: (item[0], item[1]))
    art_id_for = {(kind, owner): art_id for art_id, (kind, owner, _sha) in enumerate(art_records)}
    for index, album in enumerate(albums):
        album.artwork_id = art_id_for.get((1, index), NO_ART)
    for index, artist in enumerate(artists):
        artist.image_id = art_id_for.get((2, index), NO_ART)
    for track in tracks:
        track.artwork_id = albums[track.album_id].artwork_id

    # Strings must be collected before section offsets are known.
    track_refs = []
    for track in tracks:
        track_refs.append({
            "path": strings.ref(track.path),
            "title": strings.ref(track.title),
            "genre": strings.ref(track.genre),
            "composer": strings.ref(track.composer),
            "sort_title": strings.ref(track.sort_title),
            "sort_artist": strings.ref(track.sort_artist),
            "sort_album": strings.ref(track.sort_album),
        })
    album_refs = [{
        "title": strings.ref(album.title),
        "sort": strings.ref(album.sort),
        "genre": strings.ref(album.genre),
    } for album in albums]
    artist_refs = [{
        "name": strings.ref(artist.name),
        "sort": strings.ref(artist.sort),
        "bio": strings.ref(artist.bio),
    } for artist in artists]
    playlist_refs = [{
        "name": strings.ref(playlist.name),
        "sort": strings.ref(playlist.sort),
    } for playlist in playlists]

    track_off = HEADER_SIZE
    album_off = track_off + len(tracks) * TRACK_SIZE
    artist_off = album_off + len(albums) * ALBUM_SIZE
    playlist_off = artist_off + len(artists) * ARTIST_SIZE
    playlist_entry_off = playlist_off + len(playlists) * PLAYLIST_SIZE
    playlist_entries = []
    playlist_entries_meta = []
    for playlist in playlists:
        playlist_entries_meta.append((len(playlist_entries), len(playlist.entries)))
        playlist_entries.extend(playlist.entries)
    string_off = (playlist_entry_off + len(playlist_entries) * 4 + 3) & ~3
    string_bytes = len(strings.data)
    index_off = (string_off + string_bytes + 3) & ~3
    index_types = [
        (1, sorted(range(len(tracks)),
                   key=lambda i: (tracks[i].sort_title.casefold(), tracks[i].title.casefold()))),
        (2, sorted(range(len(tracks)), key=lambda i: -tracks[i].added_time)),
        (3, sorted(range(len(tracks)),
                   key=lambda i: (sort_key(tracks[i].genre), tracks[i].artist_id,
                                  tracks[i].album_id, tracks[i].disc_no,
                                  tracks[i].track_no))),
        (4, sorted(range(len(albums)),
                   key=lambda i: (albums[i].sort.casefold(), albums[i].title.casefold()))),
    ]
    index_data_off = index_off + len(index_types) * INDEX_SIZE
    index_data = []
    index_meta = []
    for kind, entries in index_types:
        index_meta.append((kind, len(entries), index_data_off + len(index_data) * 4))
        index_data.extend(entries)
    art_off = (index_data_off + len(index_data) * 4 + 3) & ~3
    art_table_bytes = len(art_records) * ART_SIZE
    art_blob_off = (art_off + art_table_bytes + 3) & ~3
    blob_offsets: dict[str, int] = {}
    blob_data = bytearray()
    for _kind, _owner, sha in art_records:
        if sha not in blob_offsets:
            blob_offsets[sha] = art_blob_off + len(blob_data)
            blob_data += art_pool.blobs[sha]
    file_size = art_blob_off + len(blob_data)

    data = bytearray(file_size)
    # Header
    struct.pack_into("<4sHH", data, 0, MAGIC, VERSION, HEADER_SIZE)
    struct.pack_into("<I", data, 8, 0x3)  # artwork + ratings present
    struct.pack_into("<I", data, 12, 1)  # generation
    struct.pack_into("<I", data, 16, len(tracks))
    struct.pack_into("<I", data, 20, len(albums))
    struct.pack_into("<I", data, 24, len(artists))
    struct.pack_into("<I", data, 28, len(playlists))
    struct.pack_into("<I", data, 32, track_off)
    struct.pack_into("<I", data, 36, album_off)
    struct.pack_into("<I", data, 40, artist_off)
    struct.pack_into("<I", data, 44, playlist_off)
    struct.pack_into("<I", data, 48, playlist_entry_off)
    struct.pack_into("<I", data, 52, string_off)
    struct.pack_into("<I", data, 56, string_bytes)
    struct.pack_into("<I", data, 60, index_off)
    struct.pack_into("<I", data, 64, len(index_types))
    struct.pack_into("<I", data, 68, art_off)
    struct.pack_into("<I", data, 72, len(art_records))
    struct.pack_into("<I", data, 76, art_blob_off)
    struct.pack_into("<I", data, 80, len(blob_data))
    struct.pack_into("<I", data, 84, source_digest & 0xFFFFFFFF)
    created = max((track.added_time for track in tracks), default=0) & 0xFFFFFFFF
    struct.pack_into("<I", data, 88, created)
    struct.pack_into("<I", data, 92, file_size)
    # crc at 96 stays zero until the end

    def sabs(ref: int) -> int:
        return NO_STR if ref == NO_STR else string_off + ref

    for index, track in enumerate(tracks):
        off = track_off + index * TRACK_SIZE
        refs = track_refs[index]
        struct.pack_into(
            "<Q" + "I" * 16 + "HHHHBBHI", data, off,
            track.uid,
            sabs(refs["path"]), sabs(refs["title"]),
            track.artist_id, track.album_id,
            sabs(refs["genre"]), sabs(refs["composer"]),
            sabs(refs["sort_title"]), sabs(refs["sort_artist"]),
            sabs(refs["sort_album"]),
            track.duration_ms, track.size_bytes, track.bitrate,
            track.sample_rate, track.added_time, track.artwork_id,
            track.filetype, track.track_no, track.disc_no, track.year,
            track.rating, track.flags, 0, 0, 0,
        )

    for index, album in enumerate(albums):
        off = album_off + index * ALBUM_SIZE
        refs = album_refs[index]
        struct.pack_into(
            "<10IHHI", data, off,
            sabs(refs["title"]), album.artist_id,
            sabs(refs["sort"]), sabs(refs["genre"]),
            album.artwork_id, album.year, album.track_count,
            album.first_track, album.disc_count, album.added_time,
            album.rating, album.flags, 0,
        )

    for index, artist in enumerate(artists):
        off = artist_off + index * ARTIST_SIZE
        refs = artist_refs[index]
        struct.pack_into(
            "<8IHHIII", data, off,
            sabs(refs["name"]), sabs(refs["sort"]), sabs(refs["bio"]),
            artist.image_id,
            artist.album_count, artist.first_album, artist.track_count,
            artist.added_time, artist.rating, artist.flags, 0, 0, 0,
        )

    for index, playlist in enumerate(playlists):
        off = playlist_off + index * PLAYLIST_SIZE
        start, count = playlist_entries_meta[index]
        struct.pack_into(
            "<4IHHI", data, off,
            sabs(playlist_refs[index]["name"]),
            sabs(playlist_refs[index]["sort"]),
            playlist_entry_off + start * 4, count,
            playlist.flags, 0, 0,
        )
    for index, entry in enumerate(playlist_entries):
        struct.pack_into("<I", data, playlist_entry_off + index * 4, entry)

    data[string_off:string_off + string_bytes] = strings.data
    for index, (kind, count, offset) in enumerate(index_meta):
        struct.pack_into("<IIII", data, index_off + index * INDEX_SIZE,
                         kind, count, offset, 0)
    for index, entry in enumerate(index_data):
        struct.pack_into("<I", data, index_data_off + index * 4, entry)

    for index, (kind, owner, sha) in enumerate(art_records):
        off = art_off + index * ART_SIZE
        blob_off = blob_offsets[sha]
        blob = art_pool.blobs[sha]
        struct.pack_into("<IIHHHHIIII", data, off,
                         kind, owner, art_size, art_size, 1, 0,
                         blob_off, len(blob), zlib.crc32(blob) & 0xFFFFFFFF, 0)
    data[art_blob_off:art_blob_off + len(blob_data)] = blob_data

    struct.pack_into("<I", data, 96, 0)
    crc = zlib.crc32(data) & 0xFFFFFFFF
    struct.pack_into("<I", data, 96, crc)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    tmp = out_path.with_suffix(out_path.suffix + ".tmp")
    with open(tmp, "wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(tmp, out_path)
    return {"file": str(out_path), "size": len(data), "crc32": crc}


def inspect_hdb(path: Path, as_json: bool = False) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise SystemExit("truncated header")
    if data[:4] != MAGIC:
        raise SystemExit("bad magic")
    version, header_size = struct.unpack_from("<HH", data, 4)
    if version != VERSION or header_size != HEADER_SIZE:
        raise SystemExit(f"unsupported version {version}")
    (flags, generation, track_count, album_count, artist_count,
     playlist_count, track_off, album_off, artist_off, playlist_off,
     playlist_entry_off, string_off, string_bytes, index_off, index_count,
     art_off, art_count, art_blob_off, art_blob_bytes, source_digest,
     created, file_size, stored_crc) = struct.unpack_from(
        "<23I", data, 8)
    if file_size != len(data):
        raise SystemExit("file size mismatch")
    check = bytearray(data)
    struct.pack_into("<I", check, 96, 0)
    if (zlib.crc32(check) & 0xFFFFFFFF) != stored_crc:
        raise SystemExit("crc mismatch")
    result = {
        "file": str(path), "size": len(data), "version": version,
        "generation": generation, "flags": flags,
        "tracks": track_count, "albums": album_count,
        "artists": artist_count, "playlists": playlist_count,
        "strings": string_bytes, "indexes": index_count,
        "artwork": art_count, "artwork_bytes": art_blob_bytes,
        "source_digest": source_digest, "created": created,
        "crc32": f"{stored_crc:08x}",
    }
    if as_json:
        print(json.dumps(result, indent=2))
        return result
    print(f"HelikonDB {path}")
    print(f"  {result['tracks']} tracks, {result['albums']} albums, "
          f"{result['artists']} artists, {result['playlists']} playlists")
    print(f"  {result['artwork']} artwork records, {result['artwork_bytes']} bytes; "
          f"{result['size']} bytes total")
    print(f"  generation {result['generation']}, crc {result['crc32']}")
    # Spot-check table bounds and first records.
    for name, offset, count, size in (
        ("track", track_off, track_count, TRACK_SIZE),
        ("album", album_off, album_count, ALBUM_SIZE),
        ("artist", artist_off, artist_count, ARTIST_SIZE),
        ("playlist", playlist_off, playlist_count, PLAYLIST_SIZE),
        ("art", art_off, art_count, ART_SIZE),
    ):
        if offset + count * size > len(data):
            raise SystemExit(f"{name} table out of bounds")
    def string_at(offset: int) -> str:
        if offset == NO_STR or offset == 0:
            return ""
        if offset + 2 > len(data):
            raise SystemExit("string offset out of bounds")
        length = struct.unpack_from("<H", data, offset)[0]
        if offset + 2 + length > len(data):
            raise SystemExit("string length out of bounds")
        return data[offset + 2:offset + 2 + length].decode("utf-8", "replace")
    for index in range(min(track_count, 5)):
        off = track_off + index * TRACK_SIZE
        uid = struct.unpack_from("<Q", data, off)[0]
        path_ref = struct.unpack_from("<I", data, off + 8)[0]
        title_ref = struct.unpack_from("<I", data, off + 12)[0]
        print(f"  track {index}: {string_at(title_ref)} | {string_at(path_ref)} | uid {uid:016x}")
    return result


def fixture_library() -> tuple[list[Artist], list[Album], list[Track], list[Playlist], ArtPool]:
    art = ArtPool()
    red = bytes([0xF8, 0x00] * (96 * 96))
    blue = bytes([0x00, 0x1F] * (96 * 96))
    green = bytes([0x07, 0xE0] * (96 * 96))
    red_sha = art.add(red)
    blue_sha = art.add(blue)
    green_sha = art.add(green)
    artists = [
        Artist("The Ember Set", "ember set", "A test artist.", red_sha),
        Artist("Lighthouse", "lighthouse", "Another test artist.", blue_sha),
        Artist("Various Artists", "various artists", "", green_sha),
    ]
    albums = [
        Album("Night Harbour", 0, "night harbour", "Ambient", 2004, 80, 0, red_sha),
        Album("Static Bloom", 1, "static bloom", "Electronic", 2019, 60, 0, blue_sha),
        Album("Mixtape", 2, "mixtape", "", 2020, 0, 1, green_sha),
    ]
    tracks: list[Track] = []
    rows = [
        ("Night Harbour", 0, 0, 1, "First Light", 0, 90, 210000),
        ("Night Harbour", 0, 0, 2, "Second Light", 0, 70, 230000),
        ("Static Bloom", 1, 1, 1, "Signal", 0, 0, 180000),
        ("Static Bloom", 1, 1, 2, "Noise", 0, 0, 195000),
        ("Mixtape", 2, 2, 1, "Ember", 1, 80, 200000),
        ("Mixtape", 2, 2, 2, "Tide", 1, 0, 220000),
    ]
    for index, (album, artist_id, album_id, track_no, title, compilation, rating, duration) in enumerate(rows):
        path = f"/Music/{artists[artist_id].name}/{album}/{track_no:02d} {title}.m4a"
        tracks.append(Track(
            uid=uid_for(path), path=path, title=title, artist_id=artist_id,
            album_id=album_id, genre=albums[album_id].genre,
            sort_title=sort_key(title), sort_artist=sort_key(artists[artist_id].name),
            sort_album=sort_key(album), duration_ms=duration, size_bytes=1000 + index,
            bitrate=256000, sample_rate=44100, added_time=1700000000 + index,
            filetype=2, track_no=track_no, disc_no=1, year=albums[album_id].year,
            rating=rating, flags=(1 if compilation else 0),
        ))
    for album in albums:
        album_art_id = None
    # first_track/count
    for album_id, album in enumerate(albums):
        album_tracks = [i for i, track in enumerate(tracks) if track.album_id == album_id]
        album.first_track = album_tracks[0] if album_tracks else 0
        album.track_count = len(album_tracks)
        album.disc_count = 1
        album.added_time = max((tracks[i].added_time for i in album_tracks), default=0)
    for artist_id, artist in enumerate(artists):
        album_ids = [i for i, album in enumerate(albums) if album.artist_id == artist_id]
        artist.first_album = album_ids[0] if album_ids else 0
        artist.album_count = len(album_ids)
        artist.track_count = sum(1 for track in tracks if track.artist_id == artist_id)
        artist.added_time = max((track.added_time for track in tracks if track.artist_id == artist_id), default=0)
    playlists = [Playlist("Favourites", "favourites", [0, 2, 5], 0)]
    return artists, albums, tracks, playlists, art


def cmd_build(args: argparse.Namespace) -> int:
    music_dir = Path(args.music_dir).expanduser().resolve()
    if not music_dir.is_dir():
        raise SystemExit(f"not a directory: {music_dir}")
    files = sorted(p for p in music_dir.rglob("*")
                   if p.is_file() and p.suffix.lower() in AUDIO_EXTS)
    if args.sample and args.sample < len(files):
        step = (len(files) - 1) / max(1, args.sample - 1) if args.sample > 1 else 0
        files = [files[round(i * step)] for i in range(args.sample)]
    elif args.limit:
        files = files[:args.limit]
    if not files:
        raise SystemExit("no audio files found")
    cache_path = Path(args.cache).expanduser() if args.cache else Path(args.out + ".cache.json")
    cache = load_cache(cache_path)
    ratings = json.loads(Path(args.ratings).read_text()) if args.ratings else {}
    artist_info = json.loads(Path(args.artist_info).read_text()) if args.artist_info else {}
    artist_dir = Path(args.artist_images).expanduser() if args.artist_images else None
    artists, albums, tracks, playlists, art_pool, summary = build_library(
        files, music_dir, args.prefix, cache, cache_path, args.jobs,
        args.art_size, ratings.get("tracks", {}), ratings.get("albums", {}),
        artist_info, artist_dir)
    save_cache(cache_path, cache)
    digest = zlib.crc32("|".join(f"{p}:{p.stat().st_mtime_ns}:{p.stat().st_size}"
                                 for p in files).encode()) & 0xFFFFFFFF
    result = write_hdb(Path(args.out), artists, albums, tracks, playlists, art_pool,
                       digest, args.art_size)
    summary.update(result)
    print(json.dumps(summary, indent=2))
    return 0


def cmd_inspect(args: argparse.Namespace) -> int:
    inspect_hdb(Path(args.file), args.json)
    return 0


def cmd_fixture(args: argparse.Namespace) -> int:
    artists, albums, tracks, playlists, art_pool = fixture_library()
    result = write_hdb(Path(args.out), artists, albums, tracks, playlists,
                       art_pool, 0x12345678, ART_SIZE_PX)
    print(json.dumps(result, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    build = sub.add_parser("build", help="scan a music tree and write a HelikonDB")
    build.add_argument("music_dir")
    build.add_argument("--out", required=True)
    build.add_argument("--sample", type=int, default=0)
    build.add_argument("--limit", type=int, default=0)
    build.add_argument("--cache")
    build.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    build.add_argument("--art-size", type=int, default=ART_SIZE_PX)
    build.add_argument("--prefix", default="/")
    build.add_argument("--ratings")
    build.add_argument("--artist-info")
    build.add_argument("--artist-images")
    build.set_defaults(func=cmd_build)
    inspect = sub.add_parser("inspect", help="validate and summarise a HelikonDB")
    inspect.add_argument("file")
    inspect.add_argument("--json", action="store_true")
    inspect.set_defaults(func=cmd_inspect)
    fixture = sub.add_parser("fixture", help="write the deterministic test DB")
    fixture.add_argument("--out", required=True)
    fixture.set_defaults(func=cmd_fixture)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
