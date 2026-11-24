/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The resume policy over a real journaled volume (T-093).
 *
 * `tests/player/test_bookmark.c` proves the record's text and
 * `test_vfs_bookmark.c` proves the file; this suite proves the decisions in
 * between, because they are what a listener actually feels:
 *
 *   1. Nothing is remembered from the first three seconds, and a track that has
 *      been open for a while is resumable at the position it was left at.
 *   2. A position inside the last five seconds is dropped as well: resuming
 *      there plays silence and skips to the next track.
 *   3. The throttle holds a save that comes too soon after the last one and
 *      reports BUSY, while `force` -- which is pause and power-off -- writes.
 *   4. A record survives a fresh session, and a record for another track is not
 *      offered to this one.
 *   5. A volume with no journal has no writer, so the session saves nothing and
 *      still reports success: playback must not fail because a resume could not
 *      be stored.
 *   6. A file somebody else wrote is dropped rather than resumed, and every
 *      path leaves the journal clean and `hfs_check_volume` quiet.
 *
 * The volume is `writer_fixture_build_journaled()`, so a save costs the append,
 * flush and replay it would cost on a device.
 */
#include "session.h"

#include "vfs_bookmark.h"

#include <stdio.h>
#include <string.h>

#include "hfs_check.h"
#include "hfs_volume.h"
#include "helikon_error.h"
#include "test_check.h"
#include "writer_fixture.h"

#define TRACK "/Music/Air/Moon Safari/01 La Femme d'Argent.flac"
#define OTHER "/Music/Air/Moon Safari/02 Sexy Boy.flac"

static struct vfs vfs;
static struct vfs_rw rw;
static uint8_t catalog_scratch[WRITER_NODE_SIZE];
static uint8_t extent_scratch[WRITER_NODE_SIZE];
static uint8_t snapshot[WRITER_IMAGE_SIZE];

static int image_read(void *context, uint64_t offset, void *buffer,
                      size_t length)
{
    return writer_fixture_read(context, offset, buffer, length);
}

static int mount_volume(void)
{
    memset(&vfs, 0, sizeof(vfs));
    int rc = vfs_mount(&vfs, image_read, NULL, 0, WRITER_IMAGE_SIZE,
                       catalog_scratch, sizeof(catalog_scratch),
                       extent_scratch, sizeof(extent_scratch));
    if (rc != HELIKON_OK)
        return rc;
    vfs.catalog.now = 3800000000u;
    return HELIKON_OK;
}

static void take_snapshot(void)
{
    memcpy(snapshot, writer_fixture_image(), sizeof(snapshot));
}

static int snapshot_unchanged(void)
{
    return memcmp(snapshot, writer_fixture_image(), sizeof(snapshot)) == 0;
}

static int check_clean(const char *what)
{
    if (!hfs_journal_is_clean(&rw.journal)) {
        fprintf(stderr, "  FAIL journal dirty after %s\n", what);
        return 1;
    }
    struct hfs_check_report report;
    memset(&report, 0, sizeof(report));
    int rc = hfs_check_volume(&vfs.volume, &report);
    if (rc != HELIKON_OK || report.problems != 0u) {
        fprintf(stderr, "  FAIL check after %s: rc %d problems 0x%08x\n", what,
                rc, report.problems);
        return 1;
    }
    return 0;
}

/* Builds the journaled fixture; every scenario starts from it. */
static int build_volume(void);

/* Everything the session needs is a mount plus `/.helikon/`. */
static int mount_writable(struct helikon_session *session)
{
    if (mount_volume() != HELIKON_OK) {
        fprintf(stderr, "  FAIL fixture does not mount\n");
        return 1;
    }
    if (vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush,
                     NULL) != HELIKON_OK) {
        fprintf(stderr, "  FAIL writable mount refused\n");
        return 1;
    }
    /* The directory is created on a fresh fixture and already there on a
     * rebuilt one, so its absence -- not its existence -- is what to fix. */
    struct vfs_entry entry;
    int rc = vfs_stat(&vfs, HELIKON_BOOKMARK_DIR, &entry);
    if (rc == HELIKON_ERR_NOT_FOUND) {
        uint32_t id = 0u;
        rc = vfs_rw_mkdir(&rw, HELIKON_BOOKMARK_DIR, &id);
        if (rc != HELIKON_OK) {
            fprintf(stderr, "  FAIL mkdir %s: %d (%s)\n", HELIKON_BOOKMARK_DIR,
                    rc, hfs_strerror(rc));
            return 1;
        }
    } else if (rc != HELIKON_OK || entry.kind != VFS_KIND_DIR) {
        fprintf(stderr, "  FAIL %s is not a folder: %d\n", HELIKON_BOOKMARK_DIR,
                rc);
        return 1;
    }
    rc = helikon_session_open(session, &vfs, &rw);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL session open: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    return 0;
}

static int expect_resume(const struct helikon_session *session, const char *path,
                         uint32_t want, const char *what)
{
    uint32_t got = helikon_session_resume_ms(session, path);
    if (got != want) {
        fprintf(stderr, "  FAIL resume after %s: %u, wanted %u\n", what, got,
                want);
        return 1;
    }
    return 0;
}

/* 1 and 2: what is worth remembering, and what is not. */
static int check_floor_and_tail(void)
{
    /* Each scenario starts from the same image. */
    if (build_volume())
        return 1;
    struct helikon_session session;
    if (mount_writable(&session))
        return 1;
    CHECK(helikon_session_record(&session) == NULL);
    CHECK(helikon_session_resume_ms(&session, TRACK) == 0u);

    /* Two seconds in is the start of the track, not a position. */
    take_snapshot();
    CHECK(helikon_session_note(&session, TRACK, 2000u, 432000u, 17u, 1795483200u,
                               1) == HELIKON_OK);
    CHECK(snapshot_unchanged());
    CHECK(helikon_session_record(&session) == NULL);

    /* The tail resumes into silence, so it is dropped before the write. */
    CHECK(helikon_session_note(&session, TRACK, 431500u, 432000u, 17u, 0u, 1) ==
          HELIKON_OK);
    CHECK(snapshot_unchanged());
    /* Past the end is nonsense from a bad duration, not a position. */
    CHECK(helikon_session_note(&session, TRACK, 500000u, 432000u, 17u, 0u, 1) ==
          HELIKON_OK);
    CHECK(snapshot_unchanged());

    /* Past the floor and clear of the tail is worth a commit. */
    CHECK(helikon_session_note(&session, TRACK, 10000u, 432000u, 17u,
                               1795483200u, 0) == HELIKON_OK);
    CHECK(!snapshot_unchanged());
    if (check_clean("first save"))
        return 1;
    if (expect_resume(&session, TRACK, 10000u, "first save"))
        return 1;
    CHECK(helikon_session_record(&session) != NULL);
    CHECK(helikon_session_record(&session)->track_id == 17u);
    CHECK(helikon_session_record(&session)->duration_ms == 432000u);
    CHECK(helikon_session_record(&session)->saved_at == 1795483200u);
    CHECK(strcmp(helikon_session_record(&session)->path, TRACK) == 0);
    /* An unknown duration cannot place the tail, so the position is kept. */
    CHECK(helikon_session_note(&session, TRACK, 20000u, 0u, 17u, 0u, 1) ==
          HELIKON_OK);
    if (check_clean("unknown duration"))
        return 1;
    if (expect_resume(&session, TRACK, 20000u, "unknown duration"))
        return 1;
    return 0;
}

/* 3: the throttle, and why pause and power-off pass `force`. */
static int check_throttle(void)
{
    /* Each scenario starts from the same image. */
    if (build_volume())
        return 1;
    struct helikon_session session;
    if (mount_writable(&session))
        return 1;
    CHECK(helikon_session_note(&session, TRACK, 8000u, 432000u, 17u, 0u, 0) ==
          HELIKON_OK);
    if (check_clean("throttle baseline"))
        return 1;

    /* Two seconds later is inside the interval: no commit, and the position
     * already stored stays. */
    take_snapshot();
    int rc = helikon_session_note(&session, TRACK, 10000u, 432000u, 17u, 0u, 0);
    if (rc != HELIKON_ERR_BUSY) {
        fprintf(stderr, "  FAIL throttled save said %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    CHECK(snapshot_unchanged());
    if (expect_resume(&session, TRACK, 8000u, "throttled save"))
        return 1;

    /* The interval itself is enough. */
    CHECK(helikon_session_note(&session, TRACK, 13000u, 432000u, 17u, 0u, 0) ==
          HELIKON_OK);
    if (check_clean("after the interval"))
        return 1;
    if (expect_resume(&session, TRACK, 13000u, "after the interval"))
        return 1;

    /* `force` writes through the throttle, which is what pausing does. */
    CHECK(helikon_session_note(&session, TRACK, 14000u, 432000u, 17u, 0u, 1) ==
          HELIKON_OK);
    if (check_clean("forced save"))
        return 1;
    if (expect_resume(&session, TRACK, 14000u, "forced save"))
        return 1;
    return 0;
}

/* 4: across a fresh session, and only for the track that was left. */
static int check_reload(void)
{
    /* Each scenario starts from the same image. */
    if (build_volume())
        return 1;
    struct helikon_session first;
    if (mount_writable(&first))
        return 1;
    CHECK(helikon_session_note(&first, TRACK, 61000u, 432000u, 17u,
                               1795483200u, 0) == HELIKON_OK);
    helikon_session_close(&first);
    CHECK(first.has_record == 0u);
    CHECK(first.rw == NULL);
    if (check_clean("close"))
        return 1;

    struct helikon_session second;
    CHECK(helikon_session_open(&second, &vfs, &rw) == HELIKON_OK);
    if (expect_resume(&second, TRACK, 61000u, "reopen"))
        return 1;
    /* Another track does not inherit it. */
    if (expect_resume(&second, OTHER, 0u, "another track"))
        return 1;
    /* Nor does a prefix, which is the mistake a naive match would make. */
    if (expect_resume(&second, "/Music/Air/Moon Safari/01 La Femme", 0u,
                      "a shorter path"))
        return 1;
    CHECK(helikon_session_record(&second) != NULL);

    /* The throttle resumes from the stored position, so a reopen does not
     * immediately commit again. */
    take_snapshot();
    CHECK(helikon_session_note(&second, TRACK, 62000u, 432000u, 17u, 0u, 0) ==
          HELIKON_ERR_BUSY);
    CHECK(snapshot_unchanged());
    return 0;
}

/* 5: no journal means no writer, and that is not a failure. */
static int check_no_writer(void)
{
    struct hfs_volume plain;
    CHECK(writer_fixture_build(&plain) == 0);
    if (mount_volume() != HELIKON_OK) {
        fprintf(stderr, "  FAIL fixture does not mount read-only\n");
        return 1;
    }
    CHECK(vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush,
                       NULL) == HELIKON_ERR_UNSUPPORTED);

    struct helikon_session session;
    CHECK(helikon_session_open(&session, &vfs, &rw) == HELIKON_OK);
    take_snapshot();
    CHECK(helikon_session_note(&session, TRACK, 61000u, 432000u, 17u, 0u, 1) ==
          HELIKON_OK);
    CHECK(snapshot_unchanged());
    CHECK(helikon_session_record(&session) == NULL);
    if (expect_resume(&session, TRACK, 0u, "no writer"))
        return 1;

    /* A session with no mount at all behaves the same way. */
    struct helikon_session bare;
    CHECK(helikon_session_open(&bare, NULL, NULL) == HELIKON_OK);
    CHECK(helikon_session_note(&bare, TRACK, 61000u, 432000u, 17u, 0u, 1) ==
          HELIKON_OK);
    CHECK(helikon_session_record(&bare) == NULL);
    return 0;
}

/* 6: somebody else's file, and the argument checks. */
static int check_foreign_record(void)
{
    /* Each scenario starts from the same image. */
    if (build_volume())
        return 1;
    struct helikon_session session;
    if (mount_writable(&session))
        return 1;

    static const char garbage[] = "this is not a bookmark\n";
    uint32_t id = 0u;
    CHECK(vfs_rw_create(&rw, HELIKON_BOOKMARK_FILE, &id) == HELIKON_OK);
    size_t written = 0u;
    CHECK(vfs_rw_write(&rw, HELIKON_BOOKMARK_FILE, 0u, garbage,
                       sizeof(garbage) - 1u, &written) == HELIKON_OK);
    CHECK(written == sizeof(garbage) - 1u);
    if (check_clean("garbage record"))
        return 1;

    /* Dropped, not resumed, and opening still succeeds. */
    struct helikon_session again;
    CHECK(helikon_session_open(&again, &vfs, &rw) == HELIKON_OK);
    CHECK(helikon_session_record(&again) == NULL);
    if (expect_resume(&again, TRACK, 0u, "garbage record"))
        return 1;

    /* The argument checks are refusals, not crashes. */
    CHECK(helikon_session_open(NULL, &vfs, &rw) == HELIKON_ERR_ARGUMENT);
    CHECK(helikon_session_note(NULL, TRACK, 10000u, 0u, 0u, 0u, 1) ==
          HELIKON_ERR_ARGUMENT);
    CHECK(helikon_session_note(&again, NULL, 10000u, 0u, 0u, 0u, 1) ==
          HELIKON_ERR_ARGUMENT);
    CHECK(helikon_session_note(&again, "", 10000u, 0u, 0u, 0u, 1) ==
          HELIKON_ERR_ARGUMENT);
    CHECK(helikon_session_resume_ms(NULL, TRACK) == 0u);
    CHECK(helikon_session_resume_ms(&again, NULL) == 0u);
    CHECK(helikon_session_record(NULL) == NULL);
    helikon_session_close(NULL);
    return 0;
}

static int build_volume(void)
{
    struct hfs_volume built;
    if (writer_fixture_build_journaled(&built) != 0) {
        fprintf(stderr, "  FAIL journaled fixture did not build\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    if (build_volume())
        return 1;
    if (check_floor_and_tail())
        return 1;
    if (check_throttle())
        return 1;
    if (check_reload())
        return 1;
    if (check_no_writer())
        return 1;
    if (check_foreign_record())
        return 1;
    printf("PASS resume session: floor, tail, throttle, force and reload all "
           "hold, journal clean\n");
    return 0;
}
