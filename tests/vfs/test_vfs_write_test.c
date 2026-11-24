/*
 * The first firmware write, proven on a host: `storage/write_test.c`.
 *
 * The module is the caller the transaction engine never had -- a sequence that
 * mounts, asks the D-57 gate, replaces one file, reads it back through the
 * read-only VFS and compares. This suite runs that sequence over the 1 MiB
 * journaled writer fixture and checks what a person would care about:
 *
 *   1. a volume without the opt-in refuses at GATE and is byte-identical
 *      afterwards, so a normal library never sees a write;
 *   2. a folder named like the opt-in is IS_DIR, not consent;
 *   3. bad arguments are ARGUMENT and touch nothing;
 *   4. with the opt-in, the file commits, reads back identical, and a *fresh*
 *      mount of the same image -- a different `struct helikon_storage`, as a
 *      second boot would have -- sees the same bytes;
 *   5. running again replaces: a longer file, then a shorter one, with
 *      `hfs_check_volume` clean after each commit;
 *   6. the stage names a failure is reported with are the ones the header
 *      promises.
 */
#include "write_test.h"

#include <stdio.h>
#include <string.h>

#include "hfs_check.h"
#include "helikon_error.h"
#include "test_check.h"
#include "writer_fixture.h"

static struct helikon_storage storage;
static struct vfs direct_vfs;
static struct vfs_rw direct_rw;
static uint8_t direct_catalog[WRITER_NODE_SIZE];
static uint8_t direct_extent[WRITER_NODE_SIZE];
static uint8_t snapshot[WRITER_IMAGE_SIZE];
static struct helikon_write_test test;

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

static void open_device(uint32_t flags)
{
    struct helikon_block_info info;
    memset(&info, 0, sizeof(info));
    info.sector_size = WRITER_BLOCK_SIZE;
    info.sector_count = WRITER_IMAGE_SIZE / WRITER_BLOCK_SIZE;
    info.flags = flags;
    memset(&device, 0, sizeof(device));
    device.ops = &device_ops;
    device.context = NULL;
    device.info = info;
}

/* --------------------------------------------------------------------------
 * Fixtures
 * ----------------------------------------------------------------------- */

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

/* A mount that writes directly, standing in for the host path that creates the
 * opt-in: Disk Mode, or the companion over macOS's HFS+ support. The firmware
 * never writes this file itself -- that is the point of it being an opt-in. */
static int direct_mount(void)
{
    memset(&direct_vfs, 0, sizeof(direct_vfs));
    int rc = vfs_mount(&direct_vfs, writer_fixture_read, NULL, 0,
                       WRITER_IMAGE_SIZE, direct_catalog, sizeof(direct_catalog),
                       direct_extent, sizeof(direct_extent));
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL direct mount: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    direct_vfs.catalog.now = 3800000000u;
    rc = vfs_rw_mount(&direct_rw, &direct_vfs, writer_fixture_write,
                      writer_fixture_flush, NULL);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL direct write mount: %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    return 0;
}

static int direct_mkdir(const char *path)
{
    struct vfs_entry entry;
    int rc = vfs_stat(&direct_vfs, path, &entry);
    if (rc == HELIKON_OK)
        return 0;
    uint32_t id = 0u;
    rc = vfs_rw_mkdir(&direct_rw, path, &id);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_BUSY) {
        fprintf(stderr, "  FAIL mkdir %s: %d (%s)\n", path, rc, hfs_strerror(rc));
        return 1;
    }
    return 0;
}

/* Creates the opt-in file, or a folder with its name when `as_folder` is set. */
static int write_flag(int as_folder)
{
    if (direct_mount())
        return 1;
    if (direct_mkdir(HELIKON_STORAGE_DIR))
        return 1;
    if (as_folder) {
        uint32_t id = 0u;
        int rc = vfs_rw_mkdir(&direct_rw, HELIKON_STORAGE_WRITE_FLAG, &id);
        if (rc != HELIKON_OK) {
            fprintf(stderr, "  FAIL mkdir %s: %d (%s)\n",
                    HELIKON_STORAGE_WRITE_FLAG, rc, hfs_strerror(rc));
            return 1;
        }
        return 0;
    }
    uint32_t id = 0u;
    int rc = vfs_rw_create(&direct_rw, HELIKON_STORAGE_WRITE_FLAG, &id);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL create %s: %d (%s)\n",
                HELIKON_STORAGE_WRITE_FLAG, rc, hfs_strerror(rc));
        return 1;
    }
    static const char body[] = "helikon-write-enabled 1\n";
    size_t written = 0u;
    rc = vfs_rw_write(&direct_rw, HELIKON_STORAGE_WRITE_FLAG, 0u, body,
                      sizeof(body) - 1u, &written);
    if (rc != HELIKON_OK || written != sizeof(body) - 1u) {
        fprintf(stderr, "  FAIL write %s: %d (%s)\n", HELIKON_STORAGE_WRITE_FLAG,
                rc, hfs_strerror(rc));
        return 1;
    }
    return 0;
}

/* Every scenario passes its own device, so a flag from the previous one cannot
 * leak into the next. */
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

static int check_clean(struct hfs_volume *volume, const char *what)
{
    struct hfs_check_report report;
    memset(&report, 0, sizeof(report));
    int rc = hfs_check_volume(volume, &report);
    if (rc != HELIKON_OK || report.problems != 0u) {
        fprintf(stderr, "  FAIL check after %s: rc %d problems 0x%08x\n", what,
                rc, report.problems);
        return 1;
    }
    return 0;
}

/* Reads the test file through a *new* read-only mount of the same image: the
 * same thing a second boot sees, and the reason a pass is not just a buffer in
 * RAM agreeing with itself. */
static int read_from_fresh_mount(uint32_t want_size)
{
    static uint8_t fresh_catalog[WRITER_NODE_SIZE];
    static uint8_t fresh_extent[WRITER_NODE_SIZE];
    struct vfs fresh;
    memset(&fresh, 0, sizeof(fresh));
    int rc = vfs_mount(&fresh, writer_fixture_read, NULL, 0, WRITER_IMAGE_SIZE,
                       fresh_catalog, sizeof(fresh_catalog), fresh_extent,
                       sizeof(fresh_extent));
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL fresh mount: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    struct vfs_entry entry;
    rc = vfs_stat(&fresh, HELIKON_WRITE_TEST_PATH, &entry);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL fresh stat: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    CHECK(entry.size == want_size);

    struct vfs_file_handle handle;
    rc = vfs_file_open(&fresh, HELIKON_WRITE_TEST_PATH, &handle);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL fresh open: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    uint8_t buffer[64];
    size_t done = 0u;
    rc = vfs_file_read(&fresh, handle, buffer, sizeof(buffer), &done);
    if (rc != HELIKON_OK || done != sizeof(buffer)) {
        fprintf(stderr, "  FAIL fresh read: %d after %zu bytes\n", rc, done);
        (void)vfs_file_close(&fresh, handle);
        return 1;
    }
    for (uint32_t index = 0u; index < sizeof(buffer); ++index)
        CHECK(buffer[index] == helikon_write_pattern(index));
    (void)vfs_file_close(&fresh, handle);
    return 0;
}

/* --------------------------------------------------------------------------
 * 1, 2: the refusals that keep a library read-only
 * ----------------------------------------------------------------------- */

static int check_no_flag(void)
{
    if (build_volume())
        return 1;
    open_device(0u);
    if (mount_storage())
        return 1;
    take_snapshot();

    int rc = helikon_write_test_run(&test, &storage, HELIKON_WRITE_TEST_BYTES);
    if (rc != HELIKON_ERR_NOT_FOUND) {
        fprintf(stderr, "  FAIL write test without the opt-in said %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    CHECK(test.stage == HELIKON_WRITE_STAGE_GATE);
    CHECK(strcmp(helikon_write_stage_name(test.stage), "GATE") == 0);
    CHECK(test.passed == 0u);
    CHECK(test.error == HELIKON_ERR_NOT_FOUND);
    CHECK(storage.writable == 0u);
    CHECK(snapshot_unchanged());
    return check_clean(&storage.vfs.volume, "the refused write test");
}

static int check_flag_is_a_folder(void)
{
    if (build_volume())
        return 1;
    if (write_flag(1))
        return 1;
    open_device(0u);
    if (mount_storage())
        return 1;
    take_snapshot();

    int rc = helikon_write_test_run(&test, &storage, HELIKON_WRITE_TEST_BYTES);
    if (rc != HELIKON_ERR_IS_DIR) {
        fprintf(stderr, "  FAIL write test with a folder flag said %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    CHECK(test.stage == HELIKON_WRITE_STAGE_GATE);
    CHECK(snapshot_unchanged());
    return 0;
}

/* --------------------------------------------------------------------------
 * 3: arguments
 * ----------------------------------------------------------------------- */

static int check_arguments(void)
{
    if (build_volume())
        return 1;
    if (write_flag(0))
        return 1;
    open_device(0u);
    if (mount_storage())
        return 1;
    take_snapshot();

    CHECK(helikon_write_test_run(NULL, &storage, 1u) == HELIKON_ERR_ARGUMENT);

    /* Nothing mounted: the volume is not touched and the report says MOUNT. */
    struct helikon_storage empty;
    memset(&empty, 0, sizeof(empty));
    int rc = helikon_write_test_run(&test, &empty, HELIKON_WRITE_TEST_BYTES);
    CHECK(rc == HELIKON_ERR_ARGUMENT);
    CHECK(test.stage == HELIKON_WRITE_STAGE_MOUNT);
    CHECK(test.passed == 0u);

    rc = helikon_write_test_run(&test, NULL, HELIKON_WRITE_TEST_BYTES);
    CHECK(rc == HELIKON_ERR_ARGUMENT);

    rc = helikon_write_test_run(&test, &storage, 0u);
    CHECK(rc == HELIKON_ERR_ARGUMENT);
    CHECK(test.stage == HELIKON_WRITE_STAGE_MOUNT);

    rc = helikon_write_test_run(&test, &storage, HELIKON_WRITE_TEST_MAX + 1u);
    CHECK(rc == HELIKON_ERR_ARGUMENT);

    /* None of the four reached the gate, so nothing was written. */
    CHECK(storage.writable == 0u);
    CHECK(snapshot_unchanged());
    return 0;
}

/* --------------------------------------------------------------------------
 * 4: a real commit, and a fresh mount that agrees
 * ----------------------------------------------------------------------- */

static int check_pass(void)
{
    if (build_volume())
        return 1;
    if (write_flag(0))
        return 1;
    open_device(0u);
    if (mount_storage())
        return 1;

    int rc = helikon_write_test_run(&test, &storage, HELIKON_WRITE_TEST_BYTES);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL write test said %d (%s) at %s\n", rc,
                hfs_strerror(rc), helikon_write_stage_name(test.stage));
        return 1;
    }
    CHECK(test.passed == 1u);
    CHECK(test.stage == HELIKON_WRITE_STAGE_DONE);
    CHECK(strcmp(helikon_write_stage_name(test.stage), "DONE") == 0);
    CHECK(test.file_size == HELIKON_WRITE_TEST_BYTES);
    CHECK(storage.writable == 1u);

    if (check_clean(&storage.vfs.volume, "the committed write"))
        return 1;
    return read_from_fresh_mount(HELIKON_WRITE_TEST_BYTES);
}

/* --------------------------------------------------------------------------
 * 5: replace, longer then shorter
 * ----------------------------------------------------------------------- */

static int check_replace(void)
{
    if (build_volume())
        return 1;
    if (write_flag(0))
        return 1;
    open_device(0u);
    if (mount_storage())
        return 1;

    int rc = helikon_write_test_run(&test, &storage, HELIKON_WRITE_TEST_BYTES);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL long write said %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }

    /* Grown: a different length must replace, not append. */
    rc = helikon_write_test_run(&test, &storage, HELIKON_WRITE_TEST_MAX);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL grown write said %d (%s) at %s\n", rc,
                hfs_strerror(rc), helikon_write_stage_name(test.stage));
        return 1;
    }
    CHECK(test.file_size == HELIKON_WRITE_TEST_MAX);
    if (check_clean(&storage.vfs.volume, "the grown write"))
        return 1;

    /* Shrunk: the tail has to go, or `file_size` would still be the old one. */
    rc = helikon_write_test_run(&test, &storage, 300u);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL shrunk write said %d (%s) at %s\n", rc,
                hfs_strerror(rc), helikon_write_stage_name(test.stage));
        return 1;
    }
    CHECK(test.file_size == 300u);
    if (check_clean(&storage.vfs.volume, "the shrunk write"))
        return 1;
    return read_from_fresh_mount(300u);
}

/* --------------------------------------------------------------------------
 * 6: the names a report uses
 * ----------------------------------------------------------------------- */

static int check_names(void)
{
    CHECK(strcmp(helikon_write_stage_name(HELIKON_WRITE_STAGE_NONE), "NONE") == 0);
    CHECK(strcmp(helikon_write_stage_name(HELIKON_WRITE_STAGE_WRITE), "WRITE") == 0);
    CHECK(strcmp(helikon_write_stage_name(HELIKON_WRITE_STAGE_DONE), "DONE") == 0);
    CHECK(strcmp(helikon_write_stage_name(99u), "NONE") == 0);
    /* The pattern varies, or a compare against a constant would pass forever. */
    CHECK(helikon_write_pattern(0u) != helikon_write_pattern(1u));
    CHECK(helikon_write_pattern(0u) == 7u);
    return 0;
}

int main(void)
{
    if (check_no_flag())
        return 1;
    if (check_flag_is_a_folder())
        return 1;
    if (check_arguments())
        return 1;
    if (check_pass())
        return 1;
    if (check_replace())
        return 1;
    if (check_names())
        return 1;
    printf("PASS write test: an opt-in decides, the pattern survives a fresh "
           "mount, and a replace grows and shrinks (struct %zu B)\n",
           sizeof(struct helikon_write_test));
    return 0;
}
