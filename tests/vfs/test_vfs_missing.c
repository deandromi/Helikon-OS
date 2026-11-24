/*
 * The two edges of the missing-track report: `library/missing_tracks_io.c`.
 *
 * The numbering rules live in `tests/firmware/test_missing.c` and need no disk.
 * This suite covers what does: the report committed to a real journaled volume
 * in one transaction and read back byte for byte, the refusals that write
 * nothing, and the adapter answering for a catalog that is not there.
 */
#include "missing_tracks_io.h"

#include <stdio.h>
#include <string.h>

#include "block.h"
#include "hfs_check.h"
#include "helikon_error.h"
#include "mount.h"
#include "test_check.h"
#include "writer_fixture.h"

#define REPORT_PATH "/.helikon/missing.txt"

static struct helikon_storage storage;
static struct vfs direct_vfs;
static struct vfs_rw direct_rw;
static uint8_t direct_catalog[WRITER_NODE_SIZE];
static uint8_t direct_extent[WRITER_NODE_SIZE];
static uint8_t snapshot[WRITER_IMAGE_SIZE];
static char text[HELIKON_MISSING_TEXT_MAX];
static char read_back[HELIKON_MISSING_TEXT_MAX];

/* --------------------------------------------------------------------------
 * The disk
 * ----------------------------------------------------------------------- */

static int device_read(void *context, uint64_t lba, uint32_t count,
                       void *buffer, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    uint64_t offset = lba * WRITER_BLOCK_SIZE;
    uint64_t length = (uint64_t)count * WRITER_BLOCK_SIZE;
    if (offset > WRITER_IMAGE_SIZE || length > WRITER_IMAGE_SIZE - offset)
        return HELIKON_ERR_ARGUMENT;
    memcpy(buffer, writer_fixture_image() + offset, (size_t)length);
    return HELIKON_OK;
}

static int device_write(void *context, uint64_t lba, uint32_t count,
                        const void *buffer, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    uint64_t offset = lba * WRITER_BLOCK_SIZE;
    uint64_t length = (uint64_t)count * WRITER_BLOCK_SIZE;
    if (offset > WRITER_IMAGE_SIZE || length > WRITER_IMAGE_SIZE - offset)
        return HELIKON_ERR_ARGUMENT;
    memcpy(writer_fixture_image() + offset, buffer, (size_t)length);
    return HELIKON_OK;
}

static int device_flush(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    return HELIKON_OK;
}

static const struct helikon_block_ops device_ops = {
    device_read, device_write, device_flush, NULL
};

static struct helikon_block_device device;

static void open_device(void)
{
    struct helikon_block_info info;
    memset(&info, 0, sizeof(info));
    info.sector_size = WRITER_BLOCK_SIZE;
    info.sector_count = WRITER_IMAGE_SIZE / WRITER_BLOCK_SIZE;
    memset(&device, 0, sizeof(device));
    device.ops = &device_ops;
    device.context = NULL;
    device.info = info;
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

static void take_snapshot(void)
{
    memcpy(snapshot, writer_fixture_image(), sizeof(snapshot));
}

static int snapshot_unchanged(void)
{
    return memcmp(snapshot, writer_fixture_image(), sizeof(snapshot)) == 0;
}

/* Creates the D-57 opt-in, as Disk Mode or macOS would. */
static int write_flag(void)
{
    memset(&direct_vfs, 0, sizeof(direct_vfs));
    int rc = vfs_mount(&direct_vfs, writer_fixture_read, NULL, 0,
                       WRITER_IMAGE_SIZE, direct_catalog, sizeof(direct_catalog),
                       direct_extent, sizeof(direct_extent));
    if (rc != HELIKON_OK)
        return 1;
    direct_vfs.catalog.now = 3800000000u;
    rc = vfs_rw_mount(&direct_rw, &direct_vfs, writer_fixture_write,
                      writer_fixture_flush, NULL);
    if (rc != HELIKON_OK)
        return 1;

    struct vfs_entry entry;
    if (vfs_stat(&direct_vfs, HELIKON_STORAGE_DIR, &entry) != HELIKON_OK) {
        uint32_t id = 0u;
        if (vfs_rw_mkdir(&direct_rw, HELIKON_STORAGE_DIR, &id) != HELIKON_OK)
            return 1;
    }
    uint32_t id = 0u;
    rc = vfs_rw_create(&direct_rw, HELIKON_STORAGE_WRITE_FLAG, &id);
    if (rc != HELIKON_OK)
        return 1;
    static const char body[] = "helikon-write-enabled 1\n";
    size_t written = 0u;
    rc = vfs_rw_write(&direct_rw, HELIKON_STORAGE_WRITE_FLAG, 0u, body,
                      sizeof(body) - 1u, &written);
    return rc == HELIKON_OK ? 0 : 1;
}

static int mount_storage(void)
{
    memset(&storage, 0, sizeof(storage));
    int rc = helikon_storage_mount(&storage, &device, 0u);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL storage mount: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    return 0;
}

static int check_clean(const char *what)
{
    struct hfs_check_report report;
    memset(&report, 0, sizeof(report));
    int rc = hfs_check_volume(&storage.vfs.volume, &report);
    if (rc != HELIKON_OK || report.problems != 0u) {
        fprintf(stderr, "  FAIL check after %s: rc %d problems 0x%08x\n", what,
                rc, report.problems);
        return 1;
    }
    return 0;
}

/* A report with a stated total: 9 of 12, missing 10, 11 and 12. */
static void build_partial(struct helikon_missing_report *report)
{
    static const uint16_t numbers[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    helikon_missing_report(numbers, 9u, 12u, 1u, report);
}

/* --------------------------------------------------------------------------
 * 1: a report commits, and reads back as the same text
 * ----------------------------------------------------------------------- */

static int check_export(void)
{
    if (build_volume())
        return 1;
    if (write_flag())
        return 1;
    open_device();
    if (mount_storage())
        return 1;
    int rc = helikon_storage_enable_write(&storage);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL gate: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }

    struct helikon_missing_report report;
    build_partial(&report);
    size_t want = helikon_missing_text(&report, "Kind of Blue", text,
                                       sizeof(text));
    CHECK(want != 0u);

    rc = helikon_missing_export(&storage.rw, REPORT_PATH, &report,
                                "Kind of Blue");
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL export: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }

    struct vfs_entry entry;
    rc = vfs_stat(&storage.vfs, REPORT_PATH, &entry);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL stat after export: %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    CHECK(entry.size == want);

    struct vfs_file_handle handle;
    rc = vfs_file_open(&storage.vfs, REPORT_PATH, &handle);
    if (rc != HELIKON_OK)
        return 1;
    size_t done = 0u;
    rc = vfs_file_read(&storage.vfs, handle, read_back, want + 1u, &done);
    (void)vfs_file_close(&storage.vfs, handle);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL read back: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    CHECK(done == want);
    read_back[done] = '\0';
    CHECK(memcmp(read_back, text, want) == 0);
    if (memcmp(read_back, text, want) != 0) {
        fprintf(stderr, "  wanted:\n%s\n  got:\n%s\n", text, read_back);
        return 1;
    }
    return check_clean("the exported report");
}

/* --------------------------------------------------------------------------
 * 2: the refusals write nothing
 * ----------------------------------------------------------------------- */

static int check_refusals(void)
{
    if (build_volume())
        return 1;
    if (write_flag())
        return 1;
    open_device();
    if (mount_storage())
        return 1;
    int rc = helikon_storage_enable_write(&storage);
    if (rc != HELIKON_OK)
        return 1;
    take_snapshot();

    struct helikon_missing_report report;
    build_partial(&report);

    CHECK(helikon_missing_export(NULL, REPORT_PATH, &report, "x") ==
          HELIKON_ERR_ARGUMENT);
    CHECK(helikon_missing_export(&storage.rw, NULL, &report, "x") ==
          HELIKON_ERR_ARGUMENT);
    CHECK(helikon_missing_export(&storage.rw, REPORT_PATH, NULL, "x") ==
          HELIKON_ERR_ARGUMENT);

    /* A report that makes no claim is not a file. */
    struct helikon_missing_report silent;
    helikon_missing_report(NULL, 0u, 0u, 1u, &silent);
    CHECK(silent.known == 0u);
    CHECK(helikon_missing_export(&storage.rw, REPORT_PATH, &silent, "x") ==
          HELIKON_ERR_ARGUMENT);

    struct vfs_entry entry;
    CHECK(vfs_stat(&storage.vfs, REPORT_PATH, &entry) == HELIKON_ERR_NOT_FOUND);
    CHECK(snapshot_unchanged());
    return 0;
}

/* --------------------------------------------------------------------------
 * 3: the adapter's own refusals
 * ----------------------------------------------------------------------- */

static int check_adapter_refusals(void)
{
    struct helikon_missing_report report;
    CHECK(helikon_missing_from_album(NULL, 0u, &report) ==
          HELIKON_ERR_ARGUMENT);
    CHECK(helikon_missing_from_album(NULL, 0u, NULL) == HELIKON_ERR_ARGUMENT);

    /* A catalog that was never opened answers with a code, not a crash, and
     * leaves a report that makes no claim. */
    struct hdb empty;
    memset(&empty, 0, sizeof(empty));
    int rc = helikon_missing_from_album(&empty, 0u, &report);
    CHECK(rc != HELIKON_OK);
    CHECK(report.known == 0u);
    CHECK(report.missing_count == 0u);
    return 0;
}

int main(void)
{
    if (check_export())
        return 1;
    if (check_refusals())
        return 1;
    if (check_adapter_refusals())
        return 1;
    printf("PASS missing export: a stated total is written in one transaction "
           "and reads back byte for byte, and a report that makes no claim is "
           "not a file\n");
    return 0;
}
