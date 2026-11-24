/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The catalog reader for the missing-track report: numbers in, report out.
 *
 * Declared apart from `missing_tracks_io.h` on purpose. The UI needs this and
 * nothing else; the io header pulls `vfs_rw` for the export, and a screen that
 * shows a count has no business linking a journal writer.
 *
 * Returns OK with `report` filled -- including when the report makes no claim,
 * which is an answer. ARGUMENT for a NULL argument; whatever `hdb_album` says
 * for an album index that does not exist.
 */
#ifndef HELIKON_LIBRARY_MISSING_TRACKS_DB_H
#define HELIKON_LIBRARY_MISSING_TRACKS_DB_H

#include <stdint.h>

#include "missing_tracks.h"

struct hdb;

int helikon_missing_from_album(const struct hdb *db, uint32_t album_index,
                               struct helikon_missing_report *report);

#endif
