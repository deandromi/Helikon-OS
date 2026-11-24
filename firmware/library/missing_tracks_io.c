/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Reading the numbering out of an HDB, and writing the report as text.
 * See `missing_tracks_io.h` for the contract.
 */
#include "missing_tracks_io.h"

#include <stddef.h>
#include <stdint.h>

#include "hdb.h"
#include "helikon_error.h"
#include "mem.h"

/* The numbers of one album, collected before the core decides what they mean. */
static uint16_t numbers[HELIKON_MISSING_MAX];
/* The rendered report, sized by the core's own bound. */
static char text[HELIKON_MISSING_TEXT_MAX];

int helikon_missing_from_album(const struct hdb *db, uint32_t album_index,
                               struct helikon_missing_report *report)
{
    if (db == NULL || report == NULL)
        return HELIKON_ERR_ARGUMENT;

    struct hdb_album album;
    helikon_memset(&album, 0, sizeof(album));
    int rc = hdb_album(db, album_index, &album);
    if (rc != HELIKON_OK) {
        helikon_memset(report, 0, sizeof(*report));
        return rc;
    }

    size_t collected = 0u;
    uint32_t index = 0u;
    while (index < db->track_count) {
        struct hdb_track track;
        helikon_memset(&track, 0, sizeof(track));
        if (hdb_track(db, index, &track) == HELIKON_OK &&
            track.album_id == album_index && collected < HELIKON_MISSING_MAX) {
            numbers[collected] = track.track_no;
            ++collected;
        }
        ++index;
    }

    helikon_missing_report(numbers, collected, album.track_count,
                           album.disc_count, report);
    return HELIKON_OK;
}

int helikon_missing_export(struct vfs_rw *rw, const char *path,
                           const struct helikon_missing_report *report,
                           const char *album_name)
{
    if (rw == NULL || path == NULL || report == NULL)
        return HELIKON_ERR_ARGUMENT;
    /* A report that makes no claim is not written: a file saying "no claim" is
     * read as a claim by whoever finds it next week. */
    if (report->known == 0u)
        return HELIKON_ERR_ARGUMENT;

    size_t length = helikon_missing_text(report, album_name, text, sizeof(text));
    if (length == 0u)
        return HELIKON_ERR_ARGUMENT;

    int rc = vfs_rw_txn_begin(rw);
    if (rc != HELIKON_OK) {
        vfs_rw_txn_abort(rw);
        return rc;
    }
    rc = vfs_rw_txn_replace(rw, path, text, length);
    if (rc != HELIKON_OK) {
        vfs_rw_txn_abort(rw);
        return rc;
    }
    rc = vfs_rw_txn_commit(rw);
    if (rc != HELIKON_OK)
        vfs_rw_txn_abort(rw);
    return rc;
}
