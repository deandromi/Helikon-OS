/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "music_source.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"
#include "playlist.h"

/* The folder a playlist sits in: everything up to its last slash, or "/". */
static int playlist_base(const char *path, char *out, size_t capacity)
{
    size_t length = 0u;
    while (path[length] != '\0')
        ++length;
    if (length == 0u || path[0] != '/')
        return HELIKON_ERR_ARGUMENT;
    if (length >= capacity)
        return HELIKON_ERR_BUFFER;

    size_t slash = length;
    while (slash > 0u && path[slash - 1u] != '/')
        --slash;
    if (slash == 0u) {
        out[0] = '/';
        out[1] = '\0';
        return HELIKON_OK;
    }
    /*
     * No trailing slash: the parser splits the base into components, so
     * "/Lists/" would make ".." pop an empty component instead of "Lists" and a
     * relative entry would resolve under the playlist's own folder. That is how
     * an escaping entry got accepted once — a test caught it, this comment is
     * the reason it stays fixed.
     */
    helikon_memcpy(out, path, slash - 1u);
    out[slash - 1u] = '\0';
    return HELIKON_OK;
}

int helikon_music_source_open(struct helikon_music_source *music, struct vfs *vfs,
                              const struct hdb *db, struct helikon_pmu *pmu,
                              struct helikon_clock *clock, uint32_t timeout_ms)
{
    if (music == NULL || vfs == NULL || db == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(music, 0, sizeof(*music));
    music->vfs = vfs;
    music->db = db;
    music->pmu = pmu;
    music->clock = clock;
    music->timeout_ms = timeout_ms;
    music->track = HELIKON_MUSIC_SOURCE_NO_TRACK;

    int rc = helikon_library_index_build(&music->index, db);
    if (rc != HELIKON_OK)
        return rc;
    music->opened = 1u;
    return HELIKON_OK;
}

void helikon_music_source_stop(struct helikon_music_source *music)
{
    if (music == NULL)
        return;
    if (music->playing != 0u) {
        helikon_player_stop(&music->player);
        music->playing = 0u;
    }
    if (music->track != HELIKON_MUSIC_SOURCE_NO_TRACK) {
        (void)vfs_file_close(music->vfs, music->handle);
        music->track = HELIKON_MUSIC_SOURCE_NO_TRACK;
    }
}

void helikon_music_source_close(struct helikon_music_source *music)
{
    if (music == NULL)
        return;
    helikon_music_source_stop(music);
    helikon_memset(music, 0, sizeof(*music));
    music->track = HELIKON_MUSIC_SOURCE_NO_TRACK;
}

int helikon_music_source_play_track(struct helikon_music_source *music,
                                    uint32_t track)
{
    if (music == NULL || music->opened == 0u)
        return HELIKON_ERR_ARGUMENT;

    /* One file at a time: the old track never stays half-open behind the new. */
    helikon_music_source_stop(music);

    struct hdb_track record;
    int rc = hdb_track(music->db, track, &record);
    if (rc != HELIKON_OK)
        return rc;

    char path[HELIKON_LIBRARY_PATH_MAX];
    uint32_t path_length = 0u;
    rc = hdb_string(music->db, record.path_off, path, sizeof(path), &path_length);
    if (rc != HELIKON_OK)
        return rc;

    struct vfs_entry entry;
    rc = vfs_stat(music->vfs, path, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if (entry.kind != VFS_KIND_FILE || entry.size == 0u)
        return HELIKON_ERR_NOT_FOUND;

    rc = vfs_file_open(music->vfs, path, &music->handle);
    if (rc != HELIKON_OK)
        return rc;

    rc = helikon_vfs_media_init(&music->media, music->vfs, music->handle,
                               entry.size, &music->source);
    if (rc != HELIKON_OK) {
        (void)vfs_file_close(music->vfs, music->handle);
        return rc;
    }

    rc = helikon_player_open_media(&music->player, &music->source, music->pmu,
                                   music->clock, music->timeout_ms);
    if (rc != HELIKON_OK) {
        (void)vfs_file_close(music->vfs, music->handle);
        return rc;
    }

    /*
     * The file stays open from here: the codec keeps reading through it. Both
     * flags are set before the first chunk, so a failing start goes through the
     * same stop path as a playing track rather than leaving a file open with no
     * player attached to it.
     */
    music->track = track;
    music->playing = 1u;
    rc = helikon_player_play_chunk(&music->player,
                                   HELIKON_MUSIC_SOURCE_CHUNK_FRAMES);
    if (rc != 0) {
        helikon_music_source_stop(music);
        return rc;
    }
    return HELIKON_OK;
}

int helikon_music_source_play_playlist(struct helikon_music_source *music,
                                       const char *path,
                                       struct helikon_playlist_queue_result *result)
{
    if (music == NULL || music->opened == 0u || path == NULL || result == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(result, 0, sizeof(*result));

    struct vfs_entry entry;
    int rc = vfs_stat(music->vfs, path, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if (entry.kind != VFS_KIND_FILE || entry.size == 0u)
        return HELIKON_ERR_NOT_FOUND;
    if (entry.size > sizeof(music->playlist_text))
        return HELIKON_ERR_BUFFER;

    struct vfs_file_handle handle;
    rc = vfs_file_open(music->vfs, path, &handle);
    if (rc != HELIKON_OK)
        return rc;
    size_t loaded = 0u;
    rc = vfs_file_read(music->vfs, handle, music->playlist_text,
                       (size_t)entry.size, &loaded);
    int close_rc = vfs_file_close(music->vfs, handle);
    if (rc != HELIKON_OK)
        return rc;
    if (close_rc != HELIKON_OK)
        return close_rc;
    if (loaded != (size_t)entry.size)
        return HELIKON_ERR_IO;

    char base[HELIKON_PLAYLIST_BASE_MAX];
    rc = playlist_base(path, base, sizeof(base));
    if (rc != HELIKON_OK)
        return rc;

    rc = helikon_library_queue_from_playlist(&music->index, &music->queue,
                                             (const char *)music->playlist_text,
                                             loaded, base, result);
    if (rc != HELIKON_OK)
        return rc;

    /* Nothing resolved: an empty queue would make `current` lie about a track. */
    if (result->added == 0u) {
        helikon_music_source_stop(music);
        return HELIKON_ERR_NOT_FOUND;
    }

    rc = helikon_queue_select(&music->queue, 0u);
    if (rc != HELIKON_OK)
        return rc;
    uint32_t track = 0u;
    rc = helikon_queue_current(&music->queue, &track);
    if (rc != HELIKON_OK)
        return rc;
    return helikon_music_source_play_track(music, track);
}

/*
 * The queue's answer decides whether a track is opened at all. `END` and `EMPTY`
 * stop playback and report NOT_FOUND; `RESTART` replays the same id from the top,
 * which is what repeat-one means for a stream that has no memory of its own.
 */
static int music_source_step(struct helikon_music_source *music, int answer)
{
    uint32_t track = 0u;
    switch (answer) {
    case HELIKON_QUEUE_MOVED:
    case HELIKON_QUEUE_WRAPPED:
    case HELIKON_QUEUE_RESTART:
        if (helikon_queue_current(&music->queue, &track) != HELIKON_OK)
            return HELIKON_ERR_NOT_FOUND;
        return helikon_music_source_play_track(music, track);
    case HELIKON_QUEUE_END:
    case HELIKON_QUEUE_EMPTY:
        helikon_music_source_stop(music);
        return HELIKON_ERR_NOT_FOUND;
    default:
        return HELIKON_ERR_ARGUMENT;
    }
}

int helikon_music_source_next(struct helikon_music_source *music)
{
    if (music == NULL || music->opened == 0u)
        return HELIKON_ERR_ARGUMENT;
    return music_source_step(music, helikon_queue_next(&music->queue));
}

int helikon_music_source_previous(struct helikon_music_source *music)
{
    if (music == NULL || music->opened == 0u)
        return HELIKON_ERR_ARGUMENT;
    return music_source_step(music, helikon_queue_previous(&music->queue));
}

int helikon_music_source_current(const struct helikon_music_source *music,
                                 uint32_t *track)
{
    if (music == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_queue_current(&music->queue, track);
}

int helikon_music_source_path(const struct helikon_music_source *music,
                              char *out, size_t capacity)
{
    if (music == NULL || out == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (music->opened == 0u || music->track == HELIKON_MUSIC_SOURCE_NO_TRACK)
        return HELIKON_ERR_NOT_FOUND;

    struct hdb_track record;
    int rc = hdb_track(music->db, music->track, &record);
    if (rc != HELIKON_OK)
        return rc;
    if (capacity > 0xFFFFFFFFu)
        return HELIKON_ERR_BUFFER;
    uint32_t length = 0u;
    rc = hdb_string(music->db, record.path_off, out, (uint32_t)capacity, &length);
    return rc;
}

int helikon_music_source_pull(struct helikon_music_source *music, uint32_t frames)
{
    if (music == NULL || music->playing == 0u)
        return HELIKON_ERR_ARGUMENT;
    return helikon_player_play_chunk(&music->player, frames);
}
