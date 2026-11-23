/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The write VFS against a real, macOS-formatted, journaled volume.
 *
 * `tests/vfs/test_vfs_write.c` proves the layer over a hand-built in-memory
 * fixture. This one answers the two questions a fixture cannot: does `fsck_hfs`
 * accept what we wrote, and does macOS see the same tree afterwards? It opens an
 * existing image (the driver creates a fresh HFS+J image per run), mounts it
 * read-only through the VFS and then read-write through `vfs_rw`, performs the
 * operations, and leaves two entries behind for the driver to inspect after the
 * unmount.
 *
 * It never creates, formats or deletes an image, and it refuses a volume that is
 * not journaled or whose journal is dirty: this is evidence, not a recovery
 * tool.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "helikon_error.h"
#include "hfs_catalog.h"
#include "hfs_check.h"
#include "hfs_volume.h"
#include "vfs.h"
#include "vfs_rw.h"

#define CATALOG_SCRATCH 8192u
#define EXTENT_SCRATCH 8192u

/* What the driver checks for after the unmount. */
#define KEPT_FILE "/helikon-vfs.txt"
#define KEPT_DIR "/helikon-dir"
#define KEPT_MOVED "/helikon-dir/moved.txt"
#define KEPT_FILE_BYTES 4096u
#define KEPT_MOVED_TEXT "moved by the write VFS\n"
/* 20 KiB is three VFS_RW_IO_MAX calls: the short-write path, on purpose. */
#define PAYLOAD_BYTES (20u * 1024u)

static struct vfs vfs;
static struct vfs_rw rw;
static uint8_t catalog_scratch[CATALOG_SCRATCH];
static uint8_t extent_scratch[EXTENT_SCRATCH];
static uint8_t payload[PAYLOAD_BYTES];
static uint8_t received[PAYLOAD_BYTES];

/* The taxonomy has no strerror(); a failed run has to name its error anyway. */
static const char *err_name(int code)
{
    switch (code) {
    case HELIKON_OK: return "OK";
    case HELIKON_ERR_IO: return "IO";
    case HELIKON_ERR_NOT_HFS: return "NOT_HFS";
    case HELIKON_ERR_FORMAT: return "FORMAT";
    case HELIKON_ERR_UNSUPPORTED: return "UNSUPPORTED";
    case HELIKON_ERR_DIRTY: return "DIRTY";
    case HELIKON_ERR_ARGUMENT: return "ARGUMENT";
    case HELIKON_ERR_BUFFER: return "BUFFER";
    case HELIKON_ERR_NOT_FOUND: return "NOT_FOUND";
    case HELIKON_ERR_NOT_DIR: return "NOT_DIR";
    case HELIKON_ERR_IS_DIR: return "IS_DIR";
    case HELIKON_ERR_NAME_INVALID: return "NAME_INVALID";
    case HELIKON_ERR_NO_SPACE: return "NO_SPACE";
    case HELIKON_ERR_READ_ONLY: return "READ_ONLY";
    case HELIKON_ERR_BUSY: return "BUSY";
    case HELIKON_ERR_POWER: return "POWER";
    case HELIKON_ERR_NOT_EMPTY: return "NOT_EMPTY";
    default: return "other";
    }
}

static int fail(const char *what, int rc)
{
    fprintf(stderr, "  FAIL %s: %s (%d)\n", what, err_name(rc), rc);
    return 1;
}

static int expect(int condition, const char *what)
{
    if (condition)
        return 0;
    fprintf(stderr, "  FAIL %s\n", what);
    return 1;
}

static int read_at(void *context, uint64_t offset, void *buffer, size_t length)
{
    int fd = *(int *)context;
    ssize_t done = pread(fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int write_at(void *context, uint64_t offset, const void *buffer,
                    size_t length)
{
    int fd = *(int *)context;
    ssize_t done = pwrite(fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int flush_at(void *context)
{
    int fd = *(int *)context;
    return fsync(fd) == 0 ? 0 : -1;
}

/*
 * Reads back through the read VFS and compares. A short read is legal (the read
 * core hands out one fork chunk at a time), so this loops until it has the whole
 * file; a zero-length read before the end is the failure.
 */
static int read_check(const char *path, const uint8_t *expected, size_t length)
{
    struct vfs_file_handle handle;
    int rc = vfs_file_open(&vfs, path, &handle);
    if (rc != HELIKON_OK)
        return fail("open for read-back", rc);
    if (length > sizeof(received))
        return expect(0, "read-back buffer is too small");
    size_t total = 0u;
    while (total < length) {
        size_t done = 0u;
        rc = vfs_file_read(&vfs, handle, received + total, length - total,
                           &done);
        if (rc != HELIKON_OK) {
            (void)vfs_file_close(&vfs, handle);
            return fail("read-back", rc);
        }
        if (done == 0u) {
            (void)vfs_file_close(&vfs, handle);
            return expect(0, "read-back stopped before the file end");
        }
        total += done;
    }
    (void)vfs_file_close(&vfs, handle);
    if (memcmp(received, expected, length) != 0)
        return expect(0, "read-back content differs from what was written");
    return 0;
}

static int write_all(const char *path, const uint8_t *data, size_t length)
{
    size_t done = 0u;
    while (done < length) {
        size_t applied = 0u;
        int rc = vfs_rw_write(&rw, path, done, data + done, length - done,
                              &applied);
        if (rc != HELIKON_OK)
            return fail("write", rc);
        if (applied == 0u)
            return expect(0, "a write applied nothing");
        done += applied;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s IMAGE\n", argv[0]);
        return 2;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "cannot open %s: %s\n", argv[1], strerror(errno));
        return 2;
    }
    off_t end = lseek(fd, 0, SEEK_END);
    if (end <= 0) {
        fprintf(stderr, "cannot size %s\n", argv[1]);
        (void)close(fd);
        return 2;
    }

    memset(&vfs, 0, sizeof(vfs));
    int rc = vfs_mount(&vfs, read_at, &fd, 0, (uint64_t)end, catalog_scratch,
                       sizeof(catalog_scratch), extent_scratch,
                       sizeof(extent_scratch));
    if (rc != HELIKON_OK)
        return fail("vfs_mount", rc);
    if (vfs.volume.journaled == 0u)
        return expect(0, "the image is not journaled");
    if (vfs.volume.journal_dirty != 0u)
        return expect(0, "the image's journal is dirty");
    /* A fixed timestamp keeps the run reproducible. */
    vfs.catalog.now = 3800000000u;

    rc = vfs_rw_mount(&rw, &vfs, write_at, flush_at, &fd);
    if (rc != HELIKON_OK)
        return fail("vfs_rw_mount", rc);

    uint32_t id = 0u;
    rc = vfs_rw_create(&rw, KEPT_FILE, &id);
    if (rc != HELIKON_OK)
        return fail("create " KEPT_FILE, rc);

    for (size_t index = 0u; index < sizeof(payload); ++index)
        payload[index] = (uint8_t)(index * 31u + 7u);
    if (write_all(KEPT_FILE, payload, sizeof(payload)))
        return 1;
    if (read_check(KEPT_FILE, payload, sizeof(payload)))
        return 1;

    rc = vfs_rw_truncate(&rw, KEPT_FILE, KEPT_FILE_BYTES);
    if (rc != HELIKON_OK)
        return fail("truncate " KEPT_FILE, rc);
    if (read_check(KEPT_FILE, payload, KEPT_FILE_BYTES))
        return 1;

    rc = vfs_rw_mkdir(&rw, KEPT_DIR, &id);
    if (rc != HELIKON_OK)
        return fail("mkdir " KEPT_DIR, rc);

    const char *source = KEPT_DIR "/source.txt";
    const char *removed = KEPT_DIR "/remove.txt";
    rc = vfs_rw_create(&rw, source, &id);
    if (rc != HELIKON_OK)
        return fail("create the source file", rc);
    if (write_all(source, (const uint8_t *)KEPT_MOVED_TEXT,
                  strlen(KEPT_MOVED_TEXT)))
        return 1;
    rc = vfs_rw_rename(&rw, source, KEPT_MOVED);
    if (rc != HELIKON_OK)
        return fail("rename source -> moved", rc);
    if (read_check(KEPT_MOVED, (const uint8_t *)KEPT_MOVED_TEXT,
                   strlen(KEPT_MOVED_TEXT)))
        return 1;

    rc = vfs_rw_create(&rw, removed, &id);
    if (rc != HELIKON_OK)
        return fail("create the file to remove", rc);
    rc = vfs_rw_unlink(&rw, removed);
    if (rc != HELIKON_OK)
        return fail("unlink the file to remove", rc);

    /* The structural check runs before the close so it sees the final trees. */
    struct hfs_check_report report;
    memset(&report, 0, sizeof(report));
    rc = hfs_check_volume(&vfs.volume, &report);
    if (rc != HELIKON_OK)
        return fail("hfs_check_volume", rc);
    if (report.problems != 0u) {
        fprintf(stderr, "  FAIL the structural check reported 0x%08x\n",
                report.problems);
        return 1;
    }

    vfs_rw_close(&rw);
    if (flush_at(&fd) != 0)
        return expect(0, "flush after close");
    if (close(fd) != 0)
        return expect(0, "close");

    printf("  vfs_rw: %u bytes in %s, %s kept, %s renamed, check clean\n",
           (unsigned)KEPT_FILE_BYTES, KEPT_FILE, KEPT_DIR, KEPT_MOVED);
    return 0;
}
