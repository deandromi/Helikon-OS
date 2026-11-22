/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The storage-backed music source: mount + catalog + queue + player, owned once.
 *
 * Every piece of "play music from the iPod's own storage" exists separately —
 * the read-only VFS, `hdb_vfs_open`, the library index, the playlist importer,
 * the play queue, the VFS media adapter and the player — and each has its own
 * host test. What was missing is the object that owns the combination, because
 * a call site that wires seven modules by hand is a call site that gets the
 * order wrong once and plays silence for a release.
 *
 * This is that object. It is the whole of T-058's host half: open a volume,
 * open a playlist that lives on it, resolve the playlist against the catalog,
 * and walk the queue with next/previous, reopening the file each time.
 *
 * It allocates nothing. The index (8 KiB), the queue (4 KiB), the playlist text
 * (8 KiB) and the player's codec workspace (31 KiB) and read-ahead window
 * (16 KiB) all live in the struct, so one instance is roughly 68 KiB of `.bss`
 * that the linker accounts for at build time rather than a heap the 6G cannot
 * afford to fragment.
 *
 * Rules, because a player that half-opens a file is worse than one that refuses:
 *
 *   - one file open at a time; opening a track closes the previous one first,
 *     and a failed open leaves nothing playing rather than the old track
 *     pretending to be the new one;
 *   - a track whose catalog path is missing from the volume is `NOT_FOUND`, and
 *     the queue position is left where it was so a retry means the same track;
 *   - the end of the queue stops playback and says so (`NOT_FOUND`); it does not
 *     wrap, because a wrap at the end is how a device plays an album forever at
 *     3 a.m.;
 *   - `REPEAT_ONE` restarts the same track from the top, because that is what
 *     the queue's `RESTART` answer means;
 *   - paths are the catalog's, compared byte for byte by the library index, so a
 *     playlist written with different case is a missing entry, not a guess.
 *
 * The firmware does not mount a volume at boot yet (T-056's device half), so
 * nothing above `tests/library` calls this. It is a host-proven object waiting
 * for the mount, not a claim that the hub plays from storage.
 */
#ifndef HELIKON_LIBRARY_MUSIC_SOURCE_H
#define HELIKON_LIBRARY_MUSIC_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "hdb.h"
#include "library.h"
#include "player.h"
#include "queue.h"
#include "vfs.h"
#include "vfs_media.h"

/* `track` when nothing is open: no HDB id can be this, so it cannot be confused. */
#define HELIKON_MUSIC_SOURCE_NO_TRACK 0xFFFFFFFFu

/* A playlist on the volume is a text file; 8 KiB is a few hundred entries. */
#define HELIKON_MUSIC_SOURCE_PLAYLIST_MAX 8192u
/* The 6G's HAL wants a chunk budget, not a byte count. */
#define HELIKON_MUSIC_SOURCE_CHUNK_FRAMES 2047u

struct helikon_music_source {
    struct vfs *vfs;
    const struct hdb *db;
    struct helikon_pmu *pmu;
    struct helikon_clock *clock;
    uint32_t timeout_ms;

    struct helikon_library_index index;
    struct helikon_queue queue;
    struct helikon_player player;
    struct helikon_vfs_media media;
    struct helikon_codec_source source;
    struct vfs_file_handle handle;
    uint8_t playlist_text[HELIKON_MUSIC_SOURCE_PLAYLIST_MAX];

    uint32_t track;   /* HDB track id of the open file, or MUSIC_NO_TRACK */
    uint8_t opened;   /* the index was built and a volume is bound */
    uint8_t playing;  /* a file is open and the ring is running */
};

/*
 * Binds a mounted volume and a catalog and builds the path index once. The
 * index may be incomplete when the catalog holds more than
 * `HELIKON_LIBRARY_INDEX_CAPACITY` tracks; `index.complete` says so and a
 * playlist entry beyond it reads as missing, which is the documented trade
 * rather than a silent wrong answer.
 */
int helikon_music_source_open(struct helikon_music_source *music, struct vfs *vfs,
                              const struct hdb *db, struct helikon_pmu *pmu,
                              struct helikon_clock *clock, uint32_t timeout_ms);

/*
 * Reads the playlist at `path` from the volume, resolves it against the catalog
 * and starts the first track it can play. `result` receives the four counts the
 * importer produces, so a caller can say "3 of 5 entries played, 1 missing,
 * 1 refused" instead of nothing. An empty or fully missing playlist is
 * `NOT_FOUND` with nothing playing.
 */
int helikon_music_source_play_playlist(struct helikon_music_source *music,
                                       const char *path,
                                       struct helikon_playlist_queue_result *result);

/*
 * Opens one HDB track by id and starts it. The queue is not touched, so this is
 * also how a caller plays a track it picked from the browser without building a
 * playlist first.
 */
int helikon_music_source_play_track(struct helikon_music_source *music,
                                    uint32_t track);

/*
 * Steps the queue and plays what it lands on. Returns `HELIKON_OK` when a track
 * is playing, `HELIKON_ERR_NOT_FOUND` at the end of the queue (playback stopped,
 * position unchanged), and an open error when the next track could not be opened.
 */
int helikon_music_source_next(struct helikon_music_source *music);
int helikon_music_source_previous(struct helikon_music_source *music);

/* Queue answers: the id that is current, even when nothing is playing. */
int helikon_music_source_current(const struct helikon_music_source *music,
                                 uint32_t *track);

/*
 * The catalog path of the open track, NUL-terminated inside `capacity`.
 * `NOT_FOUND` when nothing is open, `BUFFER` when the path does not fit.
 */
int helikon_music_source_path(const struct helikon_music_source *music,
                              char *out, size_t capacity);

/* Fills the ring with up to `frames`; the caller's loop calls this. */
int helikon_music_source_pull(struct helikon_music_source *music,
                              uint32_t frames);

/* Stops playback and closes the file. Safe twice; the queue is untouched. */
void helikon_music_source_stop(struct helikon_music_source *music);

/* Stops, clears the whole struct. Safe twice. */
void helikon_music_source_close(struct helikon_music_source *music);

#endif
