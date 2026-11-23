/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The write half of the VFS (T-065) over a real, journaled, in-memory volume.
 *
 * What is being proved here, in the order the test asks it:
 *
 *   1. The journaled fixture really is a journaled volume: it probes, the
 *      journal opens clean, and `vfs_rw_mount` accepts it. A volume without a
 *      journal is refused, because I-02 has no non-journaled mode.
 *   2. Every operation is one transaction: after each call the journal is clean
 *      again, the catalog re-reads what was written, and `hfs_check_volume`
 *      finds no structural problem.
 *   3. A refused call changes no byte of the image. The test snapshots the whole
 *      1 MiB fixture before the call and compares after, which is the only
 *      version of "atomic" that cannot be argued with.
 *   4. The refusals are the documented ones: a non-absolute path, a nested path,
 *      `..`, a trailing slash, a duplicate name, unlinking a folder, removing a
 *      file with rmdir, a non-empty folder, truncating a folder, growing through
 *      truncate, and an unknown parent.
 *   5. A successful commit invalidates open handles, so a reader cannot serve a
 *      stale extent list: the handle that worked before the write answers
 *      ARGUMENT after it, and a re-open works.
 *   6. A write longer than VFS_RW_IO_MAX is shortened and counted, and the next
 *      call continues from where it stopped.
 *
 * The fixture is `writer_fixture_build_journaled()`: the writer fixture plus a
 * 64 KiB in-FS journal, so commits go append -> flush -> replay exactly as they
 * do on a device, instead of being applied by hand.
 */
#include "vfs_rw.h"

#include <stdio.h>
#include <string.h>

#include "helikon_error.h"
#include "hfs_catalog.h"
#include "hfs_check.h"
#include "hfs_volume.h"
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
    /* A fixed timestamp, so the record dates are reproducible. */
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

static int expect_refused(const char *what, int rc, int expected)
{
    if (rc != expected) {
        fprintf(stderr, "  FAIL %s: %d (%s), expected %d (%s)\n", what, rc,
                hfs_strerror(rc), expected, hfs_strerror(expected));
        return 1;
    }
    if (!snapshot_unchanged()) {
        fprintf(stderr, "  FAIL %s changed the image while refusing\n", what);
        return 1;
    }
    return 0;
}

static int stat_size(const char *path, uint64_t *size)
{
    struct vfs_entry entry;
    int rc = vfs_stat(&vfs, path, &entry);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL stat %s: %d (%s)\n", path, rc,
                hfs_strerror(rc));
        return 1;
    }
    *size = entry.size;
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

static int read_back(const char *path, uint8_t *buffer, size_t length,
                     size_t *done)
{
    struct vfs_file_handle handle;
    int rc = vfs_file_open(&vfs, path, &handle);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL open %s: %d (%s)\n", path, rc,
                hfs_strerror(rc));
        return 1;
    }
    rc = vfs_file_read(&vfs, handle, buffer, length, done);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL read %s: %d (%s)\n", path, rc,
                hfs_strerror(rc));
        return 1;
    }
    (void)vfs_file_close(&vfs, handle);
    return 0;
}

/* The slice's whole story, in one place, in the order a caller would do it. */
static int test_lifecycle(void)
{
    uint32_t id = 0u;
    take_snapshot();
    CHECK(vfs_rw_create(&rw, "/notes.txt", &id) == HELIKON_OK);
    CHECK(id >= 16u); /* nextCatalogID starts at 16 in the fixture */
    CHECK(check_clean("create") == 0);

    const char *line = "helikon writes through the journal\n";
    size_t written = 0u;
    CHECK(vfs_rw_write(&rw, "/notes.txt", 0u, line, strlen(line), &written) ==
          HELIKON_OK);
    CHECK(written == strlen(line));
    CHECK(check_clean("write") == 0);

    uint64_t size = 0u;
    CHECK(stat_size("/notes.txt", &size) == 0);
    CHECK(size == strlen(line));

    uint8_t buffer[128];
    size_t done = 0u;
    CHECK(read_back("/notes.txt", buffer, sizeof(buffer), &done) == 0);
    CHECK(done == strlen(line));
    CHECK(memcmp(buffer, line, strlen(line)) == 0);

    /* Append past the end through the write call, then truncate back. */
    const char *more = "and again\n";
    CHECK(vfs_rw_write(&rw, "/notes.txt", (uint64_t)strlen(line), more,
                       strlen(more), &written) == HELIKON_OK);
    CHECK(stat_size("/notes.txt", &size) == 0);
    CHECK(size == strlen(line) + strlen(more));
    CHECK(vfs_rw_truncate(&rw, "/notes.txt", strlen(line)) == HELIKON_OK);
    CHECK(stat_size("/notes.txt", &size) == 0);
    CHECK(size == strlen(line));
    CHECK(check_clean("truncate") == 0);

    /* A folder, a file inside it, then a rename across the tree. */
    CHECK(vfs_rw_mkdir(&rw, "/music", &id) == HELIKON_OK);
    CHECK(vfs_rw_create(&rw, "/music/list.m3u", &id) == HELIKON_OK);
    CHECK(check_clean("nested create") == 0);
    CHECK(vfs_rw_rename(&rw, "/notes.txt", "/music/notes.txt") ==
          HELIKON_OK);
    CHECK(check_clean("rename") == 0);
    CHECK(expect_missing("/notes.txt") == 0);
    CHECK(stat_size("/music/notes.txt", &size) == 0);
    CHECK(size == strlen(line));

    CHECK(vfs_rw_unlink(&rw, "/music/notes.txt") == HELIKON_OK);
    CHECK(vfs_rw_unlink(&rw, "/music/list.m3u") == HELIKON_OK);
    CHECK(vfs_rw_rmdir(&rw, "/music") == HELIKON_OK);
    CHECK(check_clean("delete") == 0);
    CHECK(expect_missing("/music") == 0);
    return 0;
}

static int test_refusals(void)
{
    uint32_t id = 0u;
    CHECK(vfs_rw_create(&rw, "/refuse.txt", &id) == HELIKON_OK);
    CHECK(check_clean("refuse fixture create") == 0);

    struct {
        const char *path;
        int expected;
    } paths[] = {
        { "refuse.txt", HELIKON_ERR_ARGUMENT },      /* not absolute */
        { "/", HELIKON_ERR_ARGUMENT },               /* root, no leaf */
        { "/a/b/c", HELIKON_ERR_ARGUMENT },          /* nested */
        { "/..", HELIKON_ERR_ARGUMENT },             /* dot-dot is not a name */
        { "/.", HELIKON_ERR_ARGUMENT },              /* ... and neither is dot */
        { "/a/", HELIKON_ERR_ARGUMENT },             /* trailing slash */
        { "/missing/file.txt", HELIKON_ERR_NOT_FOUND },
        { "/refuse.txt", HELIKON_ERR_BUSY },         /* already there */
    };
    for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        take_snapshot();
        int rc = vfs_rw_create(&rw, paths[index].path, &id);
        if (expect_refused(paths[index].path, rc, paths[index].expected))
            return 1;
    }

    /* A file is not a folder and a folder is not a file. */
    take_snapshot();
    CHECK(expect_refused("unlink a folder", vfs_rw_unlink(&rw, "/"),
                         HELIKON_ERR_ARGUMENT) == 0);
    take_snapshot();
    CHECK(expect_refused("rmdir a file", vfs_rw_rmdir(&rw, "/refuse.txt"),
                         HELIKON_ERR_ARGUMENT) == 0);

    /* Truncate grows nothing and refuses a folder. */
    take_snapshot();
    CHECK(expect_refused("truncate a folder", vfs_rw_truncate(&rw, "/", 0u),
                         HELIKON_ERR_ARGUMENT) == 0);
    take_snapshot();
    CHECK(expect_refused("truncate growth",
                         vfs_rw_truncate(&rw, "/refuse.txt", 4096u),
                         HELIKON_ERR_ARGUMENT) == 0);

    /* A folder with children is NOT_EMPTY, and stays. */
    CHECK(vfs_rw_mkdir(&rw, "/busy", &id) == HELIKON_OK);
    CHECK(vfs_rw_create(&rw, "/busy/child", &id) == HELIKON_OK);
    take_snapshot();
    CHECK(expect_refused("rmdir non-empty", vfs_rw_rmdir(&rw, "/busy"),
                         HELIKON_ERR_NOT_EMPTY) == 0);
    CHECK(check_clean("refusals") == 0);

    /* A rename is refused when the destination name is taken. */
    CHECK(vfs_rw_create(&rw, "/other.txt", &id) == HELIKON_OK);
    take_snapshot();
    CHECK(expect_refused("rename onto an existing name",
                         vfs_rw_rename(&rw, "/refuse.txt", "/other.txt"),
                         HELIKON_ERR_BUSY) == 0);
    return 0;
}

/* A commit invalidates handles: a stale fork is an ARGUMENT, not a wrong read. */
static int test_handles_invalidate(void)
{
    uint32_t id = 0u;
    CHECK(vfs_rw_create(&rw, "/stale.txt", &id) == HELIKON_OK);
    const char *text = "0123456789";
    size_t written = 0u;
    CHECK(vfs_rw_write(&rw, "/stale.txt", 0u, text, strlen(text), &written) ==
          HELIKON_OK);

    struct vfs_file_handle handle;
    CHECK(vfs_file_open(&vfs, "/stale.txt", &handle) == HELIKON_OK);
    uint8_t buffer[16];
    size_t done = 0u;
    CHECK(vfs_file_read(&vfs, handle, buffer, 4u, &done) == HELIKON_OK);
    CHECK(done == 4u);

    CHECK(vfs_rw_write(&rw, "/stale.txt", 4u, "abc", 3u, &written) ==
          HELIKON_OK);
    CHECK(vfs_file_read(&vfs, handle, buffer, 4u, &done) ==
          HELIKON_ERR_ARGUMENT);

    /* "0123456789" with "abc" written over offsets 4..6. */
    const char *expected = "0123abc789";
    CHECK(vfs_file_open(&vfs, "/stale.txt", &handle) == HELIKON_OK);
    CHECK(vfs_file_read(&vfs, handle, buffer, sizeof(buffer), &done) ==
          HELIKON_OK);
    CHECK(done == strlen(expected));
    CHECK(memcmp(buffer, expected, strlen(expected)) == 0);
    (void)vfs_file_close(&vfs, handle);
    return 0;
}

/* A long write is a short write to continue, not an error and not a silent cap. */
static int test_long_write(void)
{
    static uint8_t payload[3u * VFS_RW_IO_MAX];
    for (size_t index = 0u; index < sizeof(payload); ++index)
        payload[index] = (uint8_t)(index * 7u + 1u);

    uint32_t id = 0u;
    CHECK(vfs_rw_create(&rw, "/long.bin", &id) == HELIKON_OK);
    uint64_t offset = 0u;
    size_t total = 0u;
    while (offset < sizeof(payload)) {
        size_t written = 0u;
        CHECK(vfs_rw_write(&rw, "/long.bin", offset, payload + offset,
                           sizeof(payload) - (size_t)offset, &written) ==
              HELIKON_OK);
        CHECK(written > 0u);
        offset += written;
        total += written;
        CHECK(check_clean("long write") == 0);
    }
    CHECK(total == sizeof(payload));
    uint64_t size = 0u;
    CHECK(stat_size("/long.bin", &size) == 0);
    CHECK(size == sizeof(payload));

    static uint8_t readback[3u * VFS_RW_IO_MAX];
    size_t done = 0u;
    CHECK(read_back("/long.bin", readback, sizeof(readback), &done) == 0);
    CHECK(done == sizeof(payload));
    CHECK(memcmp(readback, payload, sizeof(payload)) == 0);
    return 0;
}

/* A volume with no journal is refused: there is no writer without one. */
static int test_no_journal_refused(void)
{
    struct hfs_volume plain;
    CHECK(writer_fixture_build(&plain) == 0);
    CHECK(mount_volume() == HELIKON_OK);
    int rc = vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush, NULL);
    CHECK(rc == HELIKON_ERR_UNSUPPORTED);
    CHECK(rw.writable == 0u);

    take_snapshot();
    uint32_t id = 0u;
    CHECK(vfs_rw_create(&rw, "/nope.txt", &id) == HELIKON_ERR_READ_ONLY);
    CHECK(snapshot_unchanged());
    return 0;
}

int main(void)
{
    struct hfs_volume built;
    if (writer_fixture_build_journaled(&built) != 0) {
        fprintf(stderr, "  FAIL the journaled fixture did not build\n");
        return 1;
    }
    if (!built.journaled) {
        fprintf(stderr, "  FAIL the fixture is not journaled\n");
        return 1;
    }
    CHECK(mount_volume() == HELIKON_OK);
    CHECK(vfs.volume.journaled != 0u);
    CHECK(vfs.journal_dirty == 0u);
    CHECK(vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush, NULL) ==
          HELIKON_OK);
    CHECK(rw.writable == 1u);

    if (test_lifecycle())
        return 1;
    if (test_refusals())
        return 1;
    if (test_handles_invalidate())
        return 1;
    if (test_long_write())
        return 1;

    vfs_rw_close(&rw);
    CHECK(rw.writable == 0u);

    if (test_no_journal_refused())
        return 1;

    printf("PASS vfs_rw: create, write, append, truncate, mkdir, rename, "
           "unlink, rmdir through the journal; refusals changed no byte\n");
    return 0;
}
