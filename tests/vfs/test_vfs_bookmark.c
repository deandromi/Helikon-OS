/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The resume bookmark over a real journaled volume (T-093).
 *
 * The text format is proved in tests/player/test_bookmark.c; this suite proves
 * the file half, in the order it matters:
 *
 *   1. A save creates the temporary sibling, reads it back and renames it, and
 *      afterwards the journal is clean, `hfs_check_volume` finds nothing and the
 *      temporary file is gone.
 *   2. A second save with a shorter record replaces the first: no tail of the
 *      old one survives, because the file is replaced rather than rewritten.
 *   3. Loading what was saved returns every field, so the round trip crosses the
 *      medium and not just the parser.
 *   4. The refusals are refusals: a read-only writer, an invalid record, and a
 *      missing file is NOT_FOUND rather than an error.
 *   5. A file somebody else wrote -- garbage, or longer than the record limit --
 *      is FORMAT, never a resumed position.
 *
 * The volume is `writer_fixture_build_journaled()`: the fixture plus a 64 KiB
 * in-FS journal, so a save costs the same append/flush/replay a device would.
 */
#include "vfs_bookmark.h"

#include <stdio.h>
#include <string.h>

#include "hfs_check.h"
#include "hfs_volume.h"
#include "helikon_error.h"
#include "test_check.h"
#include "writer_fixture.h"

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
    vfs.catalog.now = 3800000000u; /* a fixed date, so the fixture is stable */
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

static int expect_missing(const char *path)
{
    struct vfs_entry entry;
    int rc = vfs_stat(&vfs, path, &entry);
    if (rc != HELIKON_ERR_NOT_FOUND) {
        fprintf(stderr, "  FAIL %s should be absent, stat said %d (%s)\n", path,
                rc, hfs_strerror(rc));
        return 1;
    }
    return 0;
}

static int write_raw(const char *path, const void *bytes, size_t length)
{
    uint32_t id = 0u;
    int rc = vfs_rw_create(&rw, path, &id);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL create %s: %d (%s)\n", path, rc,
                hfs_strerror(rc));
        return 1;
    }
    size_t written = 0u;
    rc = vfs_rw_write(&rw, path, 0u, bytes, length, &written);
    if (rc != HELIKON_OK || written != length) {
        fprintf(stderr, "  FAIL write %s: %d (%s)\n", path, rc,
                hfs_strerror(rc));
        return 1;
    }
    return 0;
}

static void fill(struct helikon_bookmark *bookmark, uint32_t position)
{
    memset(bookmark, 0, sizeof(*bookmark));
    strcpy(bookmark->path, "/Music/Air/Moon Safari/01 La Femme d'Argent.flac");
    bookmark->position_ms = position;
    bookmark->duration_ms = 432000u;
    bookmark->track_id = 17u;
    bookmark->saved_at = 1795483200u;
}

static int check_save_load(void)
{
    struct helikon_bookmark bookmark;
    fill(&bookmark, 61000u);

    take_snapshot();
    int rc = helikon_vfs_bookmark_save(&rw, HELIKON_BOOKMARK_FILE, &bookmark);
    CHECK(rc == HELIKON_OK);
    CHECK(!snapshot_unchanged());
    if (check_clean("save"))
        return 1;
    if (expect_missing("/.helikon/resume.txt.tmp"))
        return 1;

    struct vfs_entry entry;
    CHECK(vfs_stat(&vfs, HELIKON_BOOKMARK_FILE, &entry) == HELIKON_OK);
    CHECK(entry.kind == VFS_KIND_FILE);

    struct helikon_bookmark back;
    CHECK(helikon_vfs_bookmark_load(&vfs, HELIKON_BOOKMARK_FILE, &back) ==
          HELIKON_OK);
    CHECK(strcmp(back.path, bookmark.path) == 0);
    CHECK(back.position_ms == bookmark.position_ms);
    CHECK(back.duration_ms == bookmark.duration_ms);
    CHECK(back.track_id == bookmark.track_id);
    CHECK(back.saved_at == bookmark.saved_at);
    /* The file holds exactly the record the text module would write. */
    char text[HELIKON_BOOKMARK_TEXT_MAX];
    size_t bytes = helikon_bookmark_write(&bookmark, text, sizeof(text));
    CHECK(bytes > 0u);
    CHECK(entry.size == (uint64_t)bytes);
    return 0;
}

static int check_replace(void)
{
    /* A shorter record must not leave the tail of the previous one behind. */
    struct helikon_bookmark bookmark;
    fill(&bookmark, 900u);
    strcpy(bookmark.path, "/m.flac");
    bookmark.duration_ms = 0u;
    bookmark.track_id = 0u;
    bookmark.saved_at = 0u;

    int rc = helikon_vfs_bookmark_save(&rw, HELIKON_BOOKMARK_FILE, &bookmark);
    CHECK(rc == HELIKON_OK);
    if (check_clean("replace"))
        return 1;

    struct vfs_entry entry;
    CHECK(vfs_stat(&vfs, HELIKON_BOOKMARK_FILE, &entry) == HELIKON_OK);
    char text[HELIKON_BOOKMARK_TEXT_MAX];
    size_t bytes = helikon_bookmark_write(&bookmark, text, sizeof(text));
    CHECK(bytes > 0u);
    CHECK(entry.size == (uint64_t)bytes);

    struct helikon_bookmark back;
    CHECK(helikon_vfs_bookmark_load(&vfs, HELIKON_BOOKMARK_FILE, &back) ==
          HELIKON_OK);
    CHECK(strcmp(back.path, "/m.flac") == 0);
    CHECK(back.position_ms == 900u);
    CHECK(back.duration_ms == 0u);
    return 0;
}

static int check_refusals(void)
{
    /* A record this module would not accept is refused before any write. */
    struct helikon_bookmark bookmark;
    fill(&bookmark, 1000u);
    strcpy(bookmark.path, "relative.flac");
    take_snapshot();
    CHECK(helikon_vfs_bookmark_save(&rw, HELIKON_BOOKMARK_FILE, &bookmark) ==
          HELIKON_ERR_ARGUMENT);
    CHECK(snapshot_unchanged());

    /* A path with no room for its temporary sibling is refused. */
    char long_path[300];
    memset(long_path, 'a', sizeof(long_path));
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1u] = '\0';
    CHECK(helikon_vfs_bookmark_save(&rw, long_path, &bookmark) ==
          HELIKON_ERR_ARGUMENT);

    /* A missing file is the normal state, not an error. */
    struct helikon_bookmark back;
    CHECK(helikon_vfs_bookmark_load(&vfs, "/.helikon/none.txt", &back) ==
          HELIKON_ERR_NOT_FOUND);
    CHECK(helikon_vfs_bookmark_save(NULL, HELIKON_BOOKMARK_FILE, &bookmark) ==
          HELIKON_ERR_ARGUMENT);
    CHECK(helikon_vfs_bookmark_load(NULL, HELIKON_BOOKMARK_FILE, &back) ==
          HELIKON_ERR_ARGUMENT);
    return 0;
}

static int check_foreign_files(void)
{
    /* Somebody else's bytes, and a file past the record limit. */
    static const char garbage[] = "this is not a bookmark\n";
    if (write_raw("/.helikon/garbage.txt", garbage, sizeof(garbage) - 1u))
        return 1;
    struct helikon_bookmark back;
    CHECK(helikon_vfs_bookmark_load(&vfs, "/.helikon/garbage.txt", &back) ==
          HELIKON_ERR_FORMAT);
    if (check_clean("garbage file"))
        return 1;

    static char big[HELIKON_BOOKMARK_TEXT_MAX * 2u];
    memset(big, 'a', sizeof(big));
    if (write_raw("/.helikon/big.txt", big, sizeof(big)))
        return 1;
    CHECK(helikon_vfs_bookmark_load(&vfs, "/.helikon/big.txt", &back) ==
          HELIKON_ERR_FORMAT);
    if (check_clean("oversized file"))
        return 1;

    /* A directory where the record should be is not a record either. */
    uint32_t id = 0u;
    CHECK(vfs_rw_mkdir(&rw, "/.helikon/dir.txt", &id) == HELIKON_OK);
    CHECK(helikon_vfs_bookmark_load(&vfs, "/.helikon/dir.txt", &back) ==
          HELIKON_ERR_FORMAT);
    return check_clean("directory in place of a record");
}

static int test_writable(void)
{
    if (mount_volume() != HELIKON_OK) {
        fprintf(stderr, "  FAIL fixture does not mount\n");
        return 1;
    }
    CHECK(vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush,
                       NULL) == HELIKON_OK);
    CHECK(rw.writable == 1u);

    uint32_t id = 0u;
    CHECK(vfs_rw_mkdir(&rw, HELIKON_BOOKMARK_DIR, &id) == HELIKON_OK);
    if (check_clean("mkdir"))
        return 1;

    if (check_save_load())
        return 1;
    if (check_replace())
        return 1;
    if (check_refusals())
        return 1;
    if (check_foreign_files())
        return 1;
    return 0;
}

/* A volume with no journal is refused, so there is no writer to save through. */
static int test_read_only(void)
{
    struct hfs_volume plain;
    CHECK(writer_fixture_build(&plain) == 0);
    if (mount_volume() != HELIKON_OK) {
        fprintf(stderr, "  FAIL fixture does not mount read-only\n");
        return 1;
    }
    CHECK(vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush,
                       NULL) == HELIKON_ERR_UNSUPPORTED);
    CHECK(rw.writable == 0u);

    struct helikon_bookmark bookmark;
    fill(&bookmark, 5u);
    take_snapshot();
    int rc = helikon_vfs_bookmark_save(&rw, HELIKON_BOOKMARK_FILE, &bookmark);
    if (rc != HELIKON_ERR_READ_ONLY) {
        fprintf(stderr, "  FAIL save on a journal-less volume said %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    CHECK(snapshot_unchanged());
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
    if (test_writable())
        return 1;
    if (test_read_only())
        return 1;
    printf("PASS bookmark file: saved, replaced, read back, refusals left no "
           "byte\n");
    return 0;
}
