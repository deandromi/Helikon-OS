/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The read-write mount gate (T-126's write half).
 *
 * `storage/mount.c` mounts the music volume read-only at boot. This suite covers
 * the second step that turns writes on, and the reason it is a step rather than
 * a default: `helikon_storage_enable_write()` opens the volume for writing only
 * when the volume itself carries `/.helikon/write-enabled`.
 *
 * Six claims, in the order they matter:
 *
 *   1. With the flag absent the volume stays read-only and the call says
 *      NOT_FOUND. Nothing is written: the image is byte-identical afterwards.
 *      That is the normal state of every device that did not ask for writes.
 *   2. A folder named `write-enabled` is IS_DIR, not consent.
 *   3. A device that reports itself read-only answers READ_ONLY before a path is
 *      even read.
 *   4. A volume with no journal is UNSUPPORTED even with the flag present, so
 *      I-02 keeps holding: there is no non-journaled writer behind this door.
 *   5. With a journal and the flag, the volume becomes writable and a real
 *      journaled commit through the mounted storage -- create, write, read back
 *      over the same device -- lands on the image and leaves `hfs_check_volume`
 *      clean.
 *   6. Nothing mounted, or no opt-in, leaves `rw.writable` at 0: a failed gate
 *      never leaves a half-writable volume.
 *
 * The device is the writer fixture seen through a block device: 1 MiB, 512-byte
 * sectors, read, write and flush over the same image the fixture builds.
 */
#include "mount.h"

#include <stdio.h>
#include <string.h>

#include "hfs_check.h"
#include "helikon_error.h"
#include "test_check.h"
#include "writer_fixture.h"

/* The fixture seeds only the root folder, so a probe file lives there. */
#define PROBE "/write-probe.txt"

static struct helikon_storage storage;
static struct vfs direct_vfs;
static struct vfs_rw direct_rw;
static uint8_t direct_catalog[WRITER_NODE_SIZE];
static uint8_t direct_extent[WRITER_NODE_SIZE];
static uint8_t snapshot[WRITER_IMAGE_SIZE];

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

/* There is no cache in front of the image, so a flush is a successful no-op --
 * the same contract the fixture itself documents. */
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

/*
 * A mount of the same image that writes directly, standing in for the host path
 * that creates the opt-in: Disk Mode, or the companion with macOS's HFS+
 * support. The firmware never writes this file itself -- that is the point of it
 * being an opt-in.
 */
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

/* A volume that carries the opt-in but has no journal: the bit is cleared in
 * both volume headers, which is what "journaled" means on disk. */
static int clear_journaled_bit(void)
{
    uint8_t *image = writer_fixture_image();
    static const uint64_t headers[2] = {WRITER_VOLUME_HEADER,
                                        WRITER_ALT_VOLUME_HEADER};
    for (unsigned int index = 0u; index < 2u; ++index) {
        uint8_t *attributes = image + headers[index] + 4u;
        uint32_t value = ((uint32_t)attributes[0] << 24) |
                         ((uint32_t)attributes[1] << 16) |
                         ((uint32_t)attributes[2] << 8) |
                         (uint32_t)attributes[3];
        value &= ~(1u << 13);
        attributes[0] = (uint8_t)(value >> 24);
        attributes[1] = (uint8_t)(value >> 16);
        attributes[2] = (uint8_t)(value >> 8);
        attributes[3] = (uint8_t)value;
    }
    return 0;
}

/* Every scenario passes its own device, so a flag from the previous one cannot
 * leak into the next. */
static int mount_storage(void)
{
    memset(&storage, 0, sizeof(storage));
    /* Every read and write through the mount goes over the fixture image. */
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

static int expect_gate(int want, const char *what)
{
    int rc = helikon_storage_enable_write(&storage);
    if (rc != want) {
        fprintf(stderr, "  FAIL enable_write for %s said %d (%s), wanted %d\n",
                what, rc, hfs_strerror(rc), want);
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * 1, 2, 3, 4: the refusals
 * ----------------------------------------------------------------------- */

/* No flag at all: the volume did not ask to be written. */
static int check_no_flag(void)
{
    if (build_volume())
        return 1;
    open_device(0u);
    if (mount_storage())
        return 1;
    take_snapshot();
    if (expect_gate(HELIKON_ERR_NOT_FOUND, "a volume with no flag"))
        return 1;
    CHECK(storage.writable == 0u);
    CHECK(storage.rw.writable == 0u);
    CHECK(snapshot_unchanged());
    if (check_clean(&storage.vfs.volume, "the refused gate"))
        return 1;

    /* The flag's folder alone is not consent either. */
    if (build_volume())
        return 1;
    if (direct_mount())
        return 1;
    if (direct_mkdir(HELIKON_STORAGE_DIR))
        return 1;
    if (mount_storage())
        return 1;
    take_snapshot();
    if (expect_gate(HELIKON_ERR_NOT_FOUND, "a folder without the flag"))
        return 1;
    CHECK(snapshot_unchanged());
    return 0;
}

/* A folder named like the flag is a typo, and a typo is not consent. */
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
    if (expect_gate(HELIKON_ERR_IS_DIR, "a folder named write-enabled"))
        return 1;
    CHECK(storage.writable == 0u);
    CHECK(snapshot_unchanged());
    return 0;
}

/* The device says it cannot write: believe it before reading a path. */
static int check_read_only_device(void)
{
    if (build_volume())
        return 1;
    if (write_flag(0))
        return 1;
    open_device(HELIKON_BLOCK_FLAG_READ_ONLY);
    if (mount_storage())
        return 1;
    take_snapshot();
    if (expect_gate(HELIKON_ERR_READ_ONLY, "a read-only device"))
        return 1;
    CHECK(storage.writable == 0u);
    CHECK(snapshot_unchanged());
    return 0;
}

/* The flag is there but the volume has no journal: I-02 still holds. */
static int check_no_journal(void)
{
    if (build_volume())
        return 1;
    if (write_flag(0))
        return 1;
    clear_journaled_bit();
    open_device(0u);
    if (mount_storage())
        return 1;
    CHECK(storage.vfs.volume.journaled == 0u);
    take_snapshot();
    if (expect_gate(HELIKON_ERR_UNSUPPORTED, "a volume with no journal"))
        return 1;
    CHECK(storage.writable == 0u);
    CHECK(storage.rw.writable == 0u);
    CHECK(snapshot_unchanged());
    return 0;
}

/* --------------------------------------------------------------------------
 * 5: the gate opens, and a write really goes through it
 * ----------------------------------------------------------------------- */

static int check_write_path(void)
{
    if (build_volume())
        return 1;
    if (write_flag(0))
        return 1;
    open_device(0u);
    if (mount_storage())
        return 1;

    if (expect_gate(HELIKON_OK, "a volume that asked for writes"))
        return 1;
    CHECK(storage.writable == 1u);
    CHECK(storage.rw.writable == 1u);
    /* Twice is not an error, and it does not remount. */
    if (expect_gate(HELIKON_OK, "a second call"))
        return 1;
    CHECK(storage.writable == 1u);

    /*
     * A real commit through the mounted storage, so the gate is proved open and
     * not merely reported open: create a file, write it, read it back through
     * the same mount.
     */
    static const char body[] = "the write half, through the mount\n";
    uint32_t id = 0u;
    take_snapshot();
    CHECK(vfs_rw_create(&storage.rw, PROBE, &id) == HELIKON_OK);
    size_t written = 0u;
    CHECK(vfs_rw_write(&storage.rw, PROBE, 0u, body, sizeof(body) - 1u,
                       &written) == HELIKON_OK);
    CHECK(written == sizeof(body) - 1u);
    CHECK(!snapshot_unchanged());
    if (check_clean(&storage.vfs.volume, "a file through the mount"))
        return 1;

    struct vfs_entry entry;
    CHECK(vfs_stat(&storage.vfs, PROBE, &entry) == HELIKON_OK);
    CHECK(entry.kind == VFS_KIND_FILE);
    CHECK(entry.size == sizeof(body) - 1u);

    /* And it reads back through the same mount, so the commit crossed the
     * device rather than a cache in front of it. */
    struct vfs_file_handle handle;
    memset(&handle, 0, sizeof(handle));
    CHECK(vfs_file_open(&storage.vfs, PROBE, &handle) == HELIKON_OK);
    char read_back[64];
    size_t got = 0u;
    CHECK(vfs_file_read(&storage.vfs, handle, read_back, sizeof(body) - 1u,
                        &got) == HELIKON_OK);
    CHECK(got == sizeof(body) - 1u);
    CHECK(memcmp(read_back, body, got) == 0);
    CHECK(vfs_file_close(&storage.vfs, handle) == HELIKON_OK);
    /* The flag is still there: a write did not consume the opt-in. */
    CHECK(vfs_stat(&storage.vfs, HELIKON_STORAGE_WRITE_FLAG, &entry) ==
          HELIKON_OK);
    CHECK(entry.kind == VFS_KIND_FILE);

    helikon_storage_unmount(&storage);
    CHECK(storage.writable == 0u);
    CHECK(storage.mounted == 0u);
    return 0;
}

/* --------------------------------------------------------------------------
 * 6: the argument checks
 * ----------------------------------------------------------------------- */

static int check_arguments(void)
{
    CHECK(helikon_storage_enable_write(NULL) == HELIKON_ERR_ARGUMENT);

    /* A storage that never mounted has no volume to open. */
    struct helikon_storage empty;
    memset(&empty, 0, sizeof(empty));
    CHECK(helikon_storage_enable_write(&empty) == HELIKON_ERR_ARGUMENT);
    CHECK(empty.writable == 0u);

    /* The size of the writer the mount carries is what an image pays for the
     * write half even when the flag is absent; a regression that doubles it is
     * worth a failure, not a surprise on the device. */
    if (sizeof(struct vfs_rw) > 272u * 1024u) {
        fprintf(stderr, "  FAIL struct vfs_rw grew to %zu bytes\n",
                sizeof(struct vfs_rw));
        return 1;
    }
    /* Measured, so the memory map in the docs quotes a number and not a guess. */
    printf("  measured: struct helikon_storage %zu B (of which vfs_rw %zu B)\n",
           sizeof(struct helikon_storage), sizeof(struct vfs_rw));
    return 0;
}

int main(void)
{
    if (check_no_flag())
        return 1;
    if (check_flag_is_a_folder())
        return 1;
    if (check_read_only_device())
        return 1;
    if (check_no_journal())
        return 1;
    if (check_write_path())
        return 1;
    if (check_arguments())
        return 1;
    printf("PASS rw mount: the flag decides, a folder is not consent, no journal "
           "is still refused, and a file commits through the mount\n");
    return 0;
}
