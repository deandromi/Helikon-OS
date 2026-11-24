/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * One album's tracks, as the album page needs them: how many there are, and the
 * catalog track index of the n-th one.
 *
 * A scan on purpose. An HDB builder happens to write an album's tracks
 * contiguously, but that is a property of one writer, not of the format, and an
 * album page that silently shows nothing for an externally built catalog is
 * worse than one that costs a linear pass. It is a pass per page open, not per
 * row.
 *
 * Kept out of `library.c` because that file also builds playlist queues, and a
 * boot path that shows albums should not drag the playlist reader into the
 * image.
 */
#ifndef HELIKON_LIBRARY_ALBUM_TRACKS_H
#define HELIKON_LIBRARY_ALBUM_TRACKS_H

#include <stdint.h>

struct hdb;

/* 0 for an album that does not exist, and for a NULL catalog. */
uint32_t helikon_album_track_count(const struct hdb *db, uint32_t album_id);

/* ARGUMENT for a NULL output, NOT_FOUND for an index past the end. */
int helikon_album_track_at(const struct hdb *db, uint32_t album_id,
                           uint32_t index, uint32_t *track_index);

#endif
