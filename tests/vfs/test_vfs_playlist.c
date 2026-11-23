/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Playlist export through the write VFS (T-093/T-065 seam).
 *
 * `tests/player/test_playlist` proves the text. This proves the other half: that
 * the bytes reach a journaled volume through `vfs_rw`, that the read VFS gets
 * them back unchanged, that a refusal or a failing sink leaves no half-written
 * playlist behind, and that the structural check stays clean after every commit.
 *
 * The fixture is the writer fixture plus a 64 KiB in-FS journal, so every write
 * is a real journaled transaction rather than a memory edit.
 */
#include "vfs_playlist.h"

#include <stdio.h>
#include <string.h>

#include "helikon_error.h"
#include "hfs_catalog.h"
#include "hfs_check.h"
#include "hfs_volume.h"
#include "playlist.h"
#include "test_check.h"
#include "vfs.h"
#include "writer_fixture.h"

static struct vfs vfs;
static struct vfs_rw rw;
static uint8_t catalog_scratch[WRITER_NODE_SIZE];
static uint8_t extent_scratch[WRITER_NODE_SIZE];
static uint8_t buffer[4096];

static int image_read(void *context, uint64_t offset, void *data, size_t length)
{
    return writer_fixture_read(context, offset, data, length);
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

static int read_file(const char *path, uint8_t *data, size_t capacity,
                     size_t *length)
{
    struct vfs_file_handle handle;
    int rc = vfs_file_open(&vfs, path, &handle);
    if (rc != HELIKON_OK)
        return rc;
    size_t total = 0u;
    for (;;) {
        size_t done = 0u;
        rc = vfs_file_read(&vfs, handle, data + total, capacity - total, &done);
        if (rc != HELIKON_OK) {
            (void)vfs_file_close(&vfs, handle);
            return rc;
        }
        if (done == 0u)
            break;
        total += done;
        if (total == capacity)
            break;
    }
    (void)vfs_file_close(&vfs, handle);
    *length = total;
    return HELIKON_OK;
}

static void make_entry(struct helikon_playlist_entry *entry, const char *path,
                       const char *title, uint32_t duration)
{
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    if (title != NULL) {
        snprintf(entry->title, sizeof(entry->title), "%s", title);
        entry->has_title = 1u;
    }
    if (duration != 0u) {
        entry->duration_s = duration;
        entry->has_duration = 1u;
    }
}

/* The whole point: text -> vfs_rw -> journal -> medium -> read VFS. */
static int test_export_round_trip(void)
{
    struct helikon_vfs_playlist destination;
    struct helikon_playlist_sink sink;
    struct helikon_playlist_writer writer;

    CHECK(helikon_vfs_playlist_begin(&destination, &rw, "/exported.m3u") ==
          HELIKON_OK);
    helikon_vfs_playlist_sink(&destination, &sink);
    CHECK(helikon_playlist_write_open(&writer, &sink, HELIKON_PLAYLIST_M3U,
                                      0u) == HELIKON_OK);

    struct helikon_playlist_entry entry;
    make_entry(&entry, "/Music/one.mp3", "One", 42u);
    CHECK(helikon_playlist_write_entry(&writer, &entry) == HELIKON_OK);
    make_entry(&entry, "/Music/two.mp3", NULL, 0u);
    CHECK(helikon_playlist_write_entry(&writer, &entry) == HELIKON_OK);
    CHECK(helikon_playlist_write_close(&writer) == HELIKON_OK);
    CHECK(writer.written == 2u);
    CHECK(check_clean("playlist export") == 0);

    const char expected[] =
        "#EXTM3U\n"
        "#EXTINF:42,One\n"
        "/Music/one.mp3\n"
        "/Music/two.mp3\n";
    size_t length = 0u;
    CHECK(read_file("/exported.m3u", buffer, sizeof(buffer), &length) ==
          HELIKON_OK);
    CHECK(length == strlen(expected));
    CHECK(memcmp(buffer, expected, length) == 0);
    CHECK(helikon_vfs_playlist_size(&destination) == strlen(expected));

    /* A second export over the same name replaces the file instead of growing it:
     * a longer first export must not leave a tail behind. */
    CHECK(helikon_vfs_playlist_begin(&destination, &rw, "/exported.m3u") ==
          HELIKON_OK);
    helikon_vfs_playlist_sink(&destination, &sink);
    CHECK(helikon_playlist_write_open(&writer, &sink, HELIKON_PLAYLIST_M3U,
                                      0u) == HELIKON_OK);
    make_entry(&entry, "/Music/one.mp3", NULL, 0u);
    CHECK(helikon_playlist_write_entry(&writer, &entry) == HELIKON_OK);
    CHECK(helikon_playlist_write_close(&writer) == HELIKON_OK);
    CHECK(check_clean("playlist rewrite") == 0);

    const char shorter[] = "#EXTM3U\n/Music/one.mp3\n";
    CHECK(read_file("/exported.m3u", buffer, sizeof(buffer), &length) ==
          HELIKON_OK);
    CHECK(length == strlen(shorter));
    CHECK(memcmp(buffer, shorter, length) == 0);

    /* What we wrote, our own parser reads back. */
    struct helikon_playlist_parser parser;
    CHECK(helikon_playlist_open(&parser, (const char *)buffer, length, "/Music",
                                HELIKON_PLAYLIST_AUTO) == HELIKON_OK);
    CHECK(helikon_playlist_next(&parser, &entry) == 1);
    CHECK(strcmp(entry.path, "/Music/one.mp3") == 0);
    CHECK(helikon_playlist_next(&parser, &entry) == 0);
    return 0;
}

/* PLS through the same destination, to prove the counting and the closing line. */
static int test_export_pls(void)
{
    struct helikon_vfs_playlist destination;
    struct helikon_playlist_sink sink;
    struct helikon_playlist_writer writer;

    CHECK(helikon_vfs_playlist_begin(&destination, &rw, "/exported.pls") ==
          HELIKON_OK);
    helikon_vfs_playlist_sink(&destination, &sink);
    CHECK(helikon_playlist_write_open(&writer, &sink, HELIKON_PLAYLIST_PLS,
                                      1u) == HELIKON_OK);
    struct helikon_playlist_entry entry;
    make_entry(&entry, "/Music/one.mp3", "One", 42u);
    CHECK(helikon_playlist_write_entry(&writer, &entry) == HELIKON_OK);
    CHECK(helikon_playlist_write_close(&writer) == HELIKON_OK);
    CHECK(check_clean("pls export") == 0);

    const char expected[] =
        "[playlist]\n"
        "NumberOfEntries=1\n"
        "File1=/Music/one.mp3\n"
        "Title1=One\n"
        "Length1=42\n"
        "Version=2\n";
    size_t length = 0u;
    CHECK(read_file("/exported.pls", buffer, sizeof(buffer), &length) ==
          HELIKON_OK);
    CHECK(length == strlen(expected));
    CHECK(memcmp(buffer, expected, length) == 0);
    return 0;
}

/* A refused entry writes nothing, and a read-only writer is refused up front. */
static int test_export_refusals(void)
{
    struct helikon_vfs_playlist destination;
    struct helikon_playlist_sink sink;
    struct helikon_playlist_writer writer;

    /* begin() removes an existing file before creating it, so an export that is
     * refused afterwards leaves an empty playlist rather than a stale one. */
    CHECK(helikon_vfs_playlist_begin(&destination, &rw, "/refused.m3u") ==
          HELIKON_OK);
    helikon_vfs_playlist_sink(&destination, &sink);
    CHECK(helikon_playlist_write_open(&writer, &sink, HELIKON_PLAYLIST_M3U,
                                      0u) == HELIKON_OK);
    struct helikon_playlist_entry entry;
    make_entry(&entry, "no-slash.mp3", NULL, 0u);
    CHECK(helikon_playlist_write_entry(&writer, &entry) == HELIKON_ERR_ARGUMENT);
    CHECK(writer.refused == 1u);
    size_t length = 0u;
    CHECK(read_file("/refused.m3u", buffer, sizeof(buffer), &length) ==
          HELIKON_OK);
    CHECK(length == strlen("#EXTM3U\n"));

    /* A read-only writer cannot start an export at all. */
    rw.writable = 0u;
    CHECK(helikon_vfs_playlist_begin(&destination, &rw, "/nope.m3u") ==
          HELIKON_ERR_READ_ONLY);
    rw.writable = 1u;

    /* A path the VFS refuses is refused here too, and nothing is created. */
    CHECK(helikon_vfs_playlist_begin(&destination, &rw, "/deep/nested.m3u") ==
          HELIKON_ERR_NOT_FOUND);
    return 0;
}

/* A sink that fails mid-file leaves the file short, and says so. */
static int failing_write(void *context, const void *data, size_t length)
{
    (void)context;
    (void)data;
    (void)length;
    return HELIKON_ERR_IO;
}

static int test_export_sink_failure(void)
{
    struct helikon_vfs_playlist destination;
    struct helikon_playlist_writer writer;
    struct helikon_playlist_sink failing = { failing_write, NULL };

    CHECK(helikon_vfs_playlist_begin(&destination, &rw, "/broken.m3u") ==
          HELIKON_OK);
    CHECK(helikon_playlist_write_open(&writer, &failing,
                                      HELIKON_PLAYLIST_M3U, 0u) == HELIKON_ERR_IO);
    return 0;
}

int main(void)
{
    struct hfs_volume built;
    if (writer_fixture_build_journaled(&built) != 0) {
        fprintf(stderr, "  FAIL the journaled fixture did not build\n");
        return 1;
    }
    CHECK(mount_volume() == HELIKON_OK);
    CHECK(vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush,
                       NULL) == HELIKON_OK);

    if (test_export_round_trip())
        return 1;
    if (test_export_pls())
        return 1;
    if (test_export_refusals())
        return 1;
    if (test_export_sink_failure())
        return 1;

    vfs_rw_close(&rw);
    printf("PASS playlist export: m3u and pls written through the journaled VFS, "
           "read back byte for byte, refusals left the file short\n");
    return 0;
}
