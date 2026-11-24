/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The resume session (T-093): when to remember a position, and where to start.
 *
 * `bookmark.c` spells the record and `vfs_bookmark.c` stores it; this module is
 * the policy in between, and the policy is what makes a resume feel right or
 * wrong. Three rules, each a decision rather than an implementation detail:
 *
 *   - Throttle. A position is written at most once per
 *     `HELIKON_RESUME_INTERVAL_MS`, because every save is a journaled commit --
 *     a small filesystem transaction with a flush behind it -- and a player that
 *     commits every second is a player that wears the medium out.
 *   - Do not remember the beginning. Resuming at 0:02 is not resuming, it is
 *     starting over with extra steps, and the file would then disagree with the
 *     Now Playing screen the next time it opens.
 *   - Do not remember the tail. A bookmark in the last seconds of a track
 *     resumes into silence and an immediate next-track, which reads as a bug.
 *
 * The session never allocates, never formats and never touches a path the caller
 * did not hand it: on a volume with no writer it degrades to "no resume",
 * silently and correctly.
 */
#ifndef HELIKON_PLAYER_SESSION_H
#define HELIKON_PLAYER_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "bookmark.h"
#include "vfs.h"
#include "vfs_rw.h"

/* One commit per interval, at most. Five seconds is under a second of audio at
 * any bitrate the player supports and well over a flash write budget. */
#define HELIKON_RESUME_INTERVAL_MS 5000u

/* Below this the position is indistinguishable from the start of the track. */
#define HELIKON_RESUME_MIN_MS 3000u

/* A position this close to the end resumes into silence; do not keep it. */
#define HELIKON_RESUME_TAIL_MS 5000u

struct helikon_session {
    struct vfs *vfs;       /* the read mount; may be NULL */
    struct vfs_rw *rw;     /* the write mount; NULL means "no resume" */
    struct helikon_bookmark record;
    uint32_t last_save_ms; /* position of the last write, for the throttle */
    uint8_t has_record;
};

/*
 * Binds the session to a mounted pair and loads any record. A missing record is
 * the normal case and leaves `has_record` clear; a malformed one is dropped
 * (and reported as HELIKON_OK with no record) because a resume that cannot be
 * trusted should not stop playback.
 */
int helikon_session_open(struct helikon_session *session, struct vfs *vfs,
                         struct vfs_rw *rw);

/* Forgets the in-memory record; the file, if any, is left alone. */
void helikon_session_close(struct helikon_session *session);

/*
 * Records a position if the policy allows it: a writer exists, the position is
 * past `HELIKON_RESUME_MIN_MS`, at least `HELIKON_RESUME_INTERVAL_MS` has
 * passed since the last save (unless `force` is set, which is what pause and
 * stop use), and -- when the duration is known -- the position is not inside
 * the tail. Returns HELIKON_OK when a save happened or was not needed,
 * HELIKON_ERR_BUSY when the throttle held, or the error from the write.
 */
int helikon_session_note(struct helikon_session *session, const char *path,
                         uint32_t position_ms, uint32_t duration_ms,
                         uint32_t track_id, uint32_t saved_at, int force);

/*
 * Where to resume `path`, in milliseconds: the stored position when the record
 * is for this track, otherwise 0 -- which is also what a caller gets on a
 * volume with no writer.
 */
uint32_t helikon_session_resume_ms(const struct helikon_session *session,
                                   const char *path);

/* The loaded record, or NULL when there is none. */
const struct helikon_bookmark *
helikon_session_record(const struct helikon_session *session);

#endif
