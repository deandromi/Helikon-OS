/* SPDX-License-Identifier: GPL-2.0-or-later */
/* One album's tracks, by scan. See `album_tracks.h` for why it scans. */
#include "album_tracks.h"

#include <stddef.h>

#include "hdb.h"
#include "helikon_error.h"
#include "mem.h"

uint32_t helikon_album_track_count(const struct hdb *db, uint32_t album_id)
{
    if (db == NULL)
        return 0u;
    uint32_t count = 0u;
    uint32_t index = 0u;
    while (index < db->track_count) {
        struct hdb_track track;
        helikon_memset(&track, 0, sizeof(track));
        if (hdb_track(db, index, &track) == HELIKON_OK &&
            track.album_id == album_id)
            ++count;
        ++index;
    }
    return count;
}

int helikon_album_track_at(const struct hdb *db, uint32_t album_id,
                           uint32_t index, uint32_t *track_index)
{
    if (db == NULL || track_index == NULL)
        return HELIKON_ERR_ARGUMENT;
    uint32_t seen = 0u;
    uint32_t cursor = 0u;
    while (cursor < db->track_count) {
        struct hdb_track track;
        helikon_memset(&track, 0, sizeof(track));
        if (hdb_track(db, cursor, &track) == HELIKON_OK &&
            track.album_id == album_id) {
            if (seen == index) {
                *track_index = cursor;
                return HELIKON_OK;
            }
            ++seen;
        }
        ++cursor;
    }
    return HELIKON_ERR_NOT_FOUND;
}
