/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Reading the numbering out of an HDB, and writing the report as text.
 * See `missing_tracks_io.h` for the contract.
 */
#include "missing_tracks_io.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"

/* The rendered report, sized by the core's own bound. */
static char text[HELIKON_MISSING_TEXT_MAX];

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
