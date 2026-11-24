/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The two edges of the missing-track report: reading the numbers out of an HDB,
 * and writing the report as plain text through the journaled VFS.
 *
 * Kept apart from `missing_tracks.c` so the numbering rules stay a pure
 * function with no catalog and no disk, and so a test can exercise them without
 * either.
 *
 * The adapter reads only what the database states. `album.track_count` is the
 * album's own count (the tags' total), `album.disc_count` tells it whether that
 * total spans discs, and each track contributes `track_no`. It never looks at a
 * folder, a file name or a title -- a track present under a different name is
 * matched by its number, which is the whole point.
 *
 * The export writes one file in one transaction, so the file is either the
 * whole report or its previous contents. It refuses a report that makes no
 * claim: writing "no claim" to disk would invite somebody to read it as one.
 */
#ifndef HELIKON_LIBRARY_MISSING_TRACKS_IO_H
#define HELIKON_LIBRARY_MISSING_TRACKS_IO_H

#include <stddef.h>
#include <stdint.h>

#include "missing_tracks.h"
#include "vfs_rw.h"

/*
 * Writes the report as plain text to `path`, through the journaled writer, in
 * one transaction.
 *
 * Returns HELIKON_OK when the file was committed, ARGUMENT for a NULL argument
 * or a report that makes no claim (nothing is written in that case), and the
 * vfs_rw error otherwise. The caller owns the path; nothing here creates a
 * folder.
 */
int helikon_missing_export(struct vfs_rw *rw, const char *path,
                           const struct helikon_missing_report *report,
                           const char *album_name);

#endif
