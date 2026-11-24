/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The Music hub's boot path (T-126 caller).
 *
 * `storage/music_boot.c` is the object a booting image owns: it mounts the
 * volume, tries the D-57 write opt-in, opens `/.helikon/music.hdb` and answers
 * one question -- is there a catalog to draw from? This suite proves the four
 * answers and, more importantly, the rule behind them:
 *
 *   1. A volume with no catalog is `NOT_FOUND`, `db` is NULL and the mount is
 *      *gone*: nothing half-open is left for the UI to draw from.
 *   2. A device that is not HFS+ fails at the mount, with `db` still NULL.
 *   3. A volume with the fixture catalog opens: `db` walks the real records, the
 *      track count matches the header, and `writable` is 0 because the volume did
 *      not carry `/.helikon/write-enabled`.
 *   4. With the opt-in file present the same volume reports `writable`, so the
 *      caller can offer saving -- and a refused gate never stops reading.
 *
 * The catalog bytes are the committed 57,276-byte fixture
 * `tests/vfs/fixtures/music.hdb`, built by `tools/hdb_build.py fixture` (the
 * same fixture `tests/ui` links as a blob). Writing it into the volume goes
 * through `vfs_rw`, so the file is created the way the firmware creates one.
 */
#include "music_boot.h"

#include <stdio.h>
#include <string.h>

#include "hfs_check.h"
#include "helikon_error.h"
#include "test_check.h"
#include "vfs_bookmark.h"
#include "writer_fixture.h"

#define CATALOG_PATH "/.helikon/music.hdb"
#define FLAG_PATH "/.helikon/write-enabled"

static struct helikon_music_boot boot;
static struct vfs vfs;
static struct vfs_rw rw;
static uint8_t catalog_scratch[WRITER_NODE_SIZE];
static uint8_t extent_scratch[WRITER_NODE_SIZE];
static uint8_t fixture_bytes[65536];

/* --------------------------------------------------------------------------
 * The disk, over the writer fixture image
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

/* A mount of the same image that writes directly: the shape a host tool would
 * have when it prepares a card, and the only way this suite creates files. */
static int direct_mount(void)
{
    memset(&vfs, 0, sizeof(vfs));
    int rc = vfs_mount(&vfs, writer_fixture_read, NULL, 0, WRITER_IMAGE_SIZE,
                       catalog_scratch, sizeof(catalog_scratch),
                       extent_scratch, sizeof(extent_scratch));
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL direct mount: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    vfs.catalog.now = 3800000000u;
    rc = vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush,
                      NULL);
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
    if (vfs_stat(&vfs, path, &entry) == HELIKON_OK)
        return 0;
    uint32_t id = 0u;
    int rc = vfs_rw_mkdir(&rw, path, &id);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_BUSY) {
        fprintf(stderr, "  FAIL mkdir %s: %d (%s)\n", path, rc, hfs_strerror(rc));
        return 1;
    }
    return 0;
}

static int copy_in(const char *path, const uint8_t *bytes, size_t length)
{
    uint32_t id = 0u;
    int rc = vfs_rw_create(&rw, path, &id);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_BUSY) {
        fprintf(stderr, "  FAIL create %s: %d (%s)\n", path, rc, hfs_strerror(rc));
        return 1;
    }
    size_t written = 0u;
    size_t done = 0u;
    while (done < length) {
        rc = vfs_rw_write(&rw, path, done, bytes + done, length - done, &written);
        if (rc != HELIKON_OK) {
            fprintf(stderr, "  FAIL write %s: %d (%s)\n", path, rc,
                    hfs_strerror(rc));
            return 1;
        }
        if (written == 0u)
            break;
        done += written;
    }
    if (done != length) {
        fprintf(stderr, "  FAIL %s holds %zu of %zu bytes\n", path, done, length);
        return 1;
    }
    return 0;
}

/* Loads the committed catalog fixture from disk. */
static int load_fixture(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "  FAIL cannot open the catalog fixture %s\n", path);
        return 1;
    }
    size_t read = fread(fixture_bytes, 1u, sizeof(fixture_bytes), file);
    int rc = ferror(file);
    fclose(file);
    if (rc != 0 || read == 0u) {
        fprintf(stderr, "  FAIL cannot read the catalog fixture\n");
        return 1;
    }
    *size = read;
    return 0;
}

/* --------------------------------------------------------------------------
 * 1 and 2: the refusals
 * ----------------------------------------------------------------------- */

static int check_no_catalog(void)
{
    if (build_volume())
        return 1;
    open_device();
    memset(&boot, 0, sizeof(boot));
    int rc = helikon_music_boot_open(&boot, &device, 0u);
    if (rc != HELIKON_ERR_NOT_FOUND) {
        fprintf(stderr, "  FAIL a volume with no catalog said %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    CHECK(boot.db == NULL);
    CHECK(boot.writable == 0u);
    CHECK(boot.error == HELIKON_ERR_NOT_FOUND);
    /* Rule 1: nothing half-open is left behind. */
    CHECK(boot.storage.mounted == 0u);
    CHECK(boot.storage.writable == 0u);
    helikon_music_boot_close(&boot);
    return 0;
}

static int check_not_hfs(void)
{
    /* A zeroed image has no volume header anywhere the probe looks. */
    memset(writer_fixture_image(), 0, WRITER_IMAGE_SIZE);
    open_device();
    memset(&boot, 0, sizeof(boot));
    int rc = helikon_music_boot_open(&boot, &device, 0u);
    if (rc == HELIKON_OK) {
        fprintf(stderr, "  FAIL a zeroed image mounted\n");
        return 1;
    }
    CHECK(boot.db == NULL);
    CHECK(boot.storage.mounted == 0u);
    /* The argument checks are refusals, not crashes. */
    CHECK(helikon_music_boot_open(NULL, &device, 0u) == HELIKON_ERR_ARGUMENT);
    CHECK(helikon_music_boot_open(&boot, NULL, 0u) == HELIKON_ERR_ARGUMENT);
    helikon_music_boot_close(NULL);
    return 0;
}

/* --------------------------------------------------------------------------
 * 3 and 4: a real catalog, read-only and then writable
 * ----------------------------------------------------------------------- */

static int check_catalog(const char *fixture)
{
    size_t size = 0u;
    if (load_fixture(fixture, &size))
        return 1;
    if (build_volume())
        return 1;
    if (direct_mount())
        return 1;
    if (direct_mkdir(HELIKON_STORAGE_DIR))
        return 1;
    if (copy_in(CATALOG_PATH, fixture_bytes, size))
        return 1;

    /* Read-only first: the volume has no opt-in file. */
    open_device();
    memset(&boot, 0, sizeof(boot));
    int rc = helikon_music_boot_open(&boot, &device, 0u);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL a volume with a catalog said %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    CHECK(boot.db != NULL);
    CHECK(boot.writable == 0u);
    CHECK(boot.storage.mounted == 1u);
    CHECK(boot.db->track_count > 0u);
    CHECK(boot.db->file_size == (uint32_t)size);
    {
        /* The reader walks a real record, not just a header. */
        struct hdb_track track;
        CHECK(hdb_track(boot.db, 1u, &track) == HELIKON_OK);
        CHECK(track.duration_ms > 0u);
        CHECK(track.path_off < boot.db->file_size);
    }
    helikon_music_boot_close(&boot);
    CHECK(boot.db == NULL);
    CHECK(boot.storage.mounted == 0u);

    /* Now the opt-in: the same volume opens and says it can be written. */
    static const char flag[] = "helikon-write-enabled 1\n";
    if (copy_in(FLAG_PATH, (const uint8_t *)flag, sizeof(flag) - 1u))
        return 1;
    memset(&boot, 0, sizeof(boot));
    rc = helikon_music_boot_open(&boot, &device, 0u);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL the opted-in volume said %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    CHECK(boot.db != NULL);
    CHECK(boot.writable == 1u);
    CHECK(boot.error == HELIKON_OK);
    helikon_music_boot_close(&boot);
    return 0;
}

int main(int argc, char **argv)
{
    const char *fixture = argc > 1 ? argv[1] : "fixtures/music.hdb";
    if (check_no_catalog())
        return 1;
    if (check_not_hfs())
        return 1;
    if (check_catalog(fixture))
        return 1;
    printf("PASS music boot: a missing catalog leaves nothing open, a real one "
           "opens, and the opt-in is what makes it writable\n");
    return 0;
}
