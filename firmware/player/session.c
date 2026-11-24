/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The resume policy (T-093). See `session.h` for the three rules and why each
 * one exists; this file is only their arithmetic.
 */
#include "session.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"
#include "mem.h"
#include "vfs_bookmark.h"

/*
 * Two small bounded helpers rather than the libc names: `firmware/lib/string.h`
 * shadows `<string.h>` on the target and the host tests include the real one,
 * so a firmware file that needs both stays on `helikon_*` only.
 */
static size_t path_length(const char *path)
{
    size_t length = 0u;
    while (length < HELIKON_BOOKMARK_PATH_MAX && path[length] != '\0')
        ++length;
    return length;
}

static int path_equal(const char *left, const char *right)
{
    for (size_t index = 0u; index < HELIKON_BOOKMARK_PATH_MAX; ++index) {
        if (left[index] != right[index])
            return 0;
        if (left[index] == '\0')
            return 1;
    }
    return 0;
}

int helikon_session_open(struct helikon_session *session, struct vfs *vfs,
                         struct vfs_rw *rw)
{
    if (session == NULL)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(session, 0, sizeof(*session));
    session->vfs = vfs;
    session->rw = rw;

    if (vfs == NULL)
        return HELIKON_OK;

    struct helikon_bookmark record;
    int rc = helikon_vfs_bookmark_load(vfs, HELIKON_BOOKMARK_FILE, &record);
    if (rc == HELIKON_OK) {
        session->record = record;
        session->has_record = 1u;
        session->last_save_ms = record.position_ms;
        return HELIKON_OK;
    }
    /* Nothing to resume is the normal case; a damaged record is dropped. */
    if (rc == HELIKON_ERR_NOT_FOUND || rc == HELIKON_ERR_FORMAT ||
        rc == HELIKON_ERR_UNSUPPORTED)
        return HELIKON_OK;
    return rc;
}

void helikon_session_close(struct helikon_session *session)
{
    if (session == NULL)
        return;
    helikon_memset(session, 0, sizeof(*session));
}

int helikon_session_note(struct helikon_session *session, const char *path,
                         uint32_t position_ms, uint32_t duration_ms,
                         uint32_t track_id, uint32_t saved_at, int force)
{
    if (session == NULL || path == NULL)
        return HELIKON_ERR_ARGUMENT;
    /* No writer is not an error: it is a read-only volume, and a resume that
     * cannot be stored is simply not offered. */
    if (session->rw == NULL || session->rw->writable == 0u)
        return HELIKON_OK;

    /* The start of a track is not worth remembering. */
    if (position_ms < HELIKON_RESUME_MIN_MS)
        return HELIKON_OK;
    /* Nor is the tail, where a resume would land in silence or in the next
     * track; and a position past the end is nonsense from a bad duration. */
    if (duration_ms != 0u) {
        if (position_ms >= duration_ms)
            return HELIKON_OK;
        if (duration_ms - position_ms < HELIKON_RESUME_TAIL_MS)
            return HELIKON_OK;
    }

    if (force == 0 && session->last_save_ms != 0u &&
        position_ms > session->last_save_ms &&
        position_ms - session->last_save_ms < HELIKON_RESUME_INTERVAL_MS)
        return HELIKON_ERR_BUSY;

    struct helikon_bookmark record;
    helikon_memset(&record, 0, sizeof(record));
    size_t length = path_length(path);
    if (length == 0u || length >= HELIKON_BOOKMARK_PATH_MAX)
        return HELIKON_ERR_ARGUMENT;
    for (size_t index = 0u; index < length; ++index)
        record.path[index] = path[index];
    record.path[length] = '\0';
    record.position_ms = position_ms;
    record.duration_ms = duration_ms;
    record.track_id = track_id;
    record.saved_at = saved_at;

    int rc = helikon_vfs_bookmark_save(session->rw, HELIKON_BOOKMARK_FILE,
                                       &record);
    if (rc != HELIKON_OK)
        return rc;

    session->record = record;
    session->has_record = 1u;
    session->last_save_ms = position_ms;
    return HELIKON_OK;
}

uint32_t helikon_session_resume_ms(const struct helikon_session *session,
                                   const char *path)
{
    if (session == NULL || path == NULL || session->has_record == 0u)
        return 0u;
    if (session->record.position_ms < HELIKON_RESUME_MIN_MS)
        return 0u;
    if (path_equal(session->record.path, path) == 0)
        return 0u;
    return session->record.position_ms;
}

const struct helikon_bookmark *
helikon_session_record(const struct helikon_session *session)
{
    if (session == NULL || session->has_record == 0u)
        return NULL;
    return &session->record;
}
