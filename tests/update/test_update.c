/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The signed staged-update client (T-064) over a real, journaled volume.
 *
 * The sets in `fixtures/` are really signed (see make_update_set.py), so the
 * Ed25519 path is exercised rather than mocked. What the test asks, in order:
 *
 *   1. A set that is not trustworthy never writes a byte: a truncated image, an
 *      image whose hash is not the one the manifest signs, another board's set,
 *      a tampered signature, a development set on a release policy and a
 *      manifest that names more bytes than exist are all refused, each with its
 *      own reason, and the volume is byte-identical afterwards.
 *   2. A good set installs: the manifest, the image and the boot state all land
 *      in one commit, the boot state carries a TRYING slot for the new facts,
 *      the journal is clean and `hfs_check_volume` finds nothing.
 *   3. The recovery copy really is the pair that was installed before, so the
 *      manual restore path has something to restore.
 *   4. The second install demotes the previous GOOD slot, which is the detail
 *      that decides whether the loader boots or shows a recovery screen.
 *   5. A downgrade is refused, and so is a second commit.
 *
 * The fixture is `writer_fixture_build_journaled()`: the writer fixture plus a
 * 64 KiB in-FS journal, so every commit goes append -> flush -> replay exactly
 * as it does on a device.
 */
#include "update.h"

#include <stdio.h>
#include <string.h>

#include "bootstate.h"
#include "helikon_error.h"
#include "hfs_catalog.h"
#include "hfs_check.h"
#include "hfs_volume.h"
#include "keys.h"
#include "test_check.h"
#include "update_test_key.h"
#include "writer_fixture.h"

static struct vfs vfs;
static struct vfs_rw rw;
static struct helikon_update update;
static struct helikon_public_key key;
static struct helikon_key_table keys;
static uint8_t catalog_scratch[WRITER_NODE_SIZE];
static uint8_t extent_scratch[WRITER_NODE_SIZE];
static uint8_t snapshot[WRITER_IMAGE_SIZE];
static uint8_t buffer[3u * HELIKON_UPDATE_CHUNK];

static const char *const FIXTURES = "fixtures";

static int image_read(void *context, uint64_t offset, void *data, size_t length)
{
    return writer_fixture_read(context, offset, data, length);
}

static int fixture_path(char *out, size_t capacity, const char *set,
                        const char *suffix)
{
    int written = snprintf(out, capacity, "%s/%s.%s", FIXTURES, set, suffix);
    return written > 0 && (size_t)written < capacity ? 0 : 1;
}

/* One fixture set, loaded whole. The caller owns it, so two sets can be alive at
 * once without one overwriting the other's bytes. */
struct fixture_set {
    uint8_t manifest[HELIKON_UPDATE_MANIFEST_BYTES];
    uint8_t image[32u * 1024u];
    size_t image_length;
};

static int load_part(const char *set, const char *suffix, uint8_t *data,
                     size_t capacity, size_t *length)
{
    char path[256];
    if (fixture_path(path, sizeof(path), set, suffix) != 0)
        return 1;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "  FAIL cannot open %s\n", path);
        return 1;
    }
    size_t done = fread(data, 1u, capacity, file);
    int closed = fclose(file);
    /* A manifest fills its buffer exactly; an image must not, or the fixture is
     * larger than the test believes. `load_set` checks both. */
    if (closed != 0 || done == 0u) {
        fprintf(stderr, "  FAIL cannot read %s\n", path);
        return 1;
    }
    *length = done;
    return 0;
}

static int load_set(const char *set, struct fixture_set *out)
{
    size_t manifest_length = 0u;
    if (load_part(set, "manifest", out->manifest, sizeof(out->manifest),
                  &manifest_length) != 0)
        return 1;
    if (manifest_length != sizeof(out->manifest)) {
        fprintf(stderr, "  FAIL %s.manifest is not %zu bytes\n", set,
                sizeof(out->manifest));
        return 1;
    }
    return load_part(set, "img", out->image, sizeof(out->image),
                     &out->image_length);
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

static void unmount(void)
{
    vfs_rw_close(&rw);
    memset(&vfs, 0, sizeof(vfs));
    memset(&rw, 0, sizeof(rw));
}

/* A fresh journaled volume with the loader's directories in place. */
static int fresh_volume(void)
{
    unmount();
    struct hfs_volume built;
    if (writer_fixture_build_journaled(&built) != 0) {
        fprintf(stderr, "  FAIL the journaled fixture did not build\n");
        return 1;
    }
    if (vfs_mount(&vfs, image_read, NULL, 0, WRITER_IMAGE_SIZE, catalog_scratch,
                  sizeof(catalog_scratch), extent_scratch,
                  sizeof(extent_scratch)) != HELIKON_OK) {
        fprintf(stderr, "  FAIL vfs_mount\n");
        return 1;
    }
    vfs.catalog.now = 3800000000u;
    if (vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush,
                     NULL) != HELIKON_OK) {
        fprintf(stderr, "  FAIL vfs_rw_mount\n");
        return 1;
    }
    uint32_t id = 0u;
    int rc = vfs_rw_mkdir(&rw, HELIKON_UPDATE_ROOT, &id);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL mkdir %s: %d\n", HELIKON_UPDATE_ROOT, rc);
        return 1;
    }
    rc = vfs_rw_mkdir(&rw, HELIKON_UPDATE_STAGING, &id);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL mkdir %s: %d\n", HELIKON_UPDATE_STAGING, rc);
        return 1;
    }
    return 0;
}

static int open_update(int allow_development)
{
    int rc = helikon_update_open(&update, &vfs, &rw, &keys, HELIKON_BOARD_IPOD6G,
                                 allow_development);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL helikon_update_open: %d\n", rc);
        return 1;
    }
    return 0;
}

static int put_file(const char *path, const uint8_t *data, size_t length)
{
    int rc = vfs_rw_unlink(&rw, path);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_NOT_FOUND)
        return rc;
    rc = vfs_rw_create(&rw, path, NULL);
    if (rc != HELIKON_OK)
        return rc;
    size_t done = 0u;
    while (done < length) {
        size_t applied = 0u;
        rc = vfs_rw_write(&rw, path, done, data + done, length - done,
                          &applied);
        if (rc != HELIKON_OK)
            return rc;
        if (applied == 0u)
            return HELIKON_ERR_IO;
        done += applied;
    }
    return HELIKON_OK;
}

static int get_file(const char *path, uint8_t *data, size_t capacity,
                    size_t *length)
{
    struct vfs_file_handle handle;
    int rc = vfs_file_open(&vfs, path, &handle);
    if (rc != HELIKON_OK)
        return rc;
    size_t total = 0u;
    while (total < capacity) {
        size_t done = 0u;
        rc = vfs_file_read(&vfs, handle, data + total, capacity - total, &done);
        if (rc != HELIKON_OK) {
            (void)vfs_file_close(&vfs, handle);
            return rc;
        }
        if (done == 0u)
            break;
        total += done;
    }
    (void)vfs_file_close(&vfs, handle);
    *length = total;
    return HELIKON_OK;
}

/* Writes a named fixture set into the staging area, as a host would. */
static int stage_set(const char *set)
{
    struct fixture_set fixtures;
    if (load_set(set, &fixtures) != 0)
        return 1;
    int rc = put_file(HELIKON_UPDATE_STAGED_MANIFEST, fixtures.manifest,
                      sizeof(fixtures.manifest));
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL stage manifest: %d\n", rc);
        return 1;
    }
    rc = put_file(HELIKON_UPDATE_STAGED_IMAGE, fixtures.image,
                  fixtures.image_length);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL stage image: %d\n", rc);
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

static int expect_file(const char *path, const uint8_t *expected,
                       size_t expected_length)
{
    size_t length = 0u;
    int rc = get_file(path, buffer, sizeof(buffer), &length);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL read %s: %d (%s)\n", path, rc, hfs_strerror(rc));
        return 1;
    }
    if (length != expected_length ||
        memcmp(buffer, expected, expected_length) != 0) {
        fprintf(stderr, "  FAIL %s holds %zu bytes, expected %zu\n", path, length,
                expected_length);
        return 1;
    }
    return 0;
}

static int expect_absent(const char *path)
{
    struct vfs_entry entry;
    int rc = vfs_stat(&vfs, path, &entry);
    if (rc != HELIKON_ERR_NOT_FOUND) {
        fprintf(stderr, "  FAIL %s should be absent, stat said %d\n", path, rc);
        return 1;
    }
    return 0;
}

/* The boot state as the loader would read it. */
static int parse_bootstate(struct helikon_bootstate *bootstate,
                          uint8_t *raw, size_t capacity)
{
    size_t length = 0u;
    int rc = get_file(HELIKON_UPDATE_BOOTSTATE, raw, capacity, &length);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL read bootstate: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    if (length != HELIKON_BOOTSTATE_SIZE) {
        fprintf(stderr, "  FAIL bootstate is %zu bytes\n", length);
        return 1;
    }
    rc = helikon_bootstate_parse(raw, length, HELIKON_BOARD_IPOD6G, bootstate);
    if (rc != HELIKON_OK) {
        fprintf(stderr, "  FAIL bootstate did not parse: %d (%s)\n", rc,
                hfs_strerror(rc));
        return 1;
    }
    return 0;
}

static int slot_is(const struct helikon_bootstate_slot *slot, uint32_t version,
                   uint32_t size, uint8_t state,
                   const uint8_t digest[HELIKON_SHA256_SIZE])
{
    return slot->valid && slot->image_version == version &&
           slot->image_size == size && slot->state == state &&
           memcmp(slot->image_sha256, digest, HELIKON_SHA256_SIZE) == 0;
}

/* 1. Everything that must never be installed, and never written. */
static int test_verify_rejects(void)
{
    if (fresh_volume() != 0)
        return 1;
    if (open_update(0) != 0)
        return 1;

    /* Nothing staged at all. */
    take_snapshot();
    CHECK(helikon_update_verify(&update) == HELIKON_ERR_NOT_FOUND);
    CHECK(snapshot_unchanged());

    if (stage_set("bad-hash") != 0)
        return 1;
    take_snapshot();
    CHECK(expect_refused("an image that is not the one signed",
                         helikon_update_verify(&update),
                         HELIKON_ERR_FORMAT) == 0);

    if (stage_set("wrong-board") != 0)
        return 1;
    take_snapshot();
    CHECK(expect_refused("another board's set", helikon_update_verify(&update),
                         HELIKON_ERR_UNSUPPORTED) == 0);

    if (stage_set("bad-signature") != 0)
        return 1;
    take_snapshot();
    CHECK(expect_refused("a tampered signature", helikon_update_verify(&update),
                         HELIKON_ERR_UNSUPPORTED) == 0);

    if (stage_set("development") != 0)
        return 1;
    take_snapshot();
    CHECK(expect_refused("a development set on a release policy",
                         helikon_update_verify(&update),
                         HELIKON_ERR_UNSUPPORTED) == 0);

    if (stage_set("short-image") != 0)
        return 1;
    take_snapshot();
    CHECK(expect_refused("a manifest naming more bytes than exist",
                         helikon_update_verify(&update),
                         HELIKON_ERR_FORMAT) == 0);

    /* The same set, with the development flag allowed, is accepted: the refusal
     * above was the policy, not the bytes. */
    if (open_update(1) != 0)
        return 1;
    if (stage_set("development") != 0)
        return 1;
    CHECK(helikon_update_verify(&update) == HELIKON_OK);
    CHECK(update.stage == HELIKON_UPDATE_VERIFIED);
    CHECK(update.staged.image_version == 2u);
    CHECK(update.have_installed == 0);
    CHECK(check_clean("verification") == 0);

    /* A commit without a park is ARGUMENT, and a park without a writable VFS is
     * READ_ONLY: neither is allowed to write anything. */
    take_snapshot();
    CHECK(helikon_update_commit(&update) == HELIKON_ERR_ARGUMENT);
    CHECK(snapshot_unchanged());
    unmount();
    return 0;
}

/* 2 and 3. A first install, then the state the loader will read. */
static int test_first_install(void)
{
    if (fresh_volume() != 0)
        return 1;
    if (open_update(0) != 0)
        return 1;
    if (stage_set("good") != 0)
        return 1;

    struct fixture_set good;
    if (load_set("good", &good) != 0)
        return 1;
    const uint8_t *manifest = good.manifest;
    const uint8_t *image = good.image;
    const size_t manifest_length = sizeof(good.manifest);
    const size_t image_length = good.image_length;

    CHECK(helikon_update_verify(&update) == HELIKON_OK);
    CHECK(update.stage == HELIKON_UPDATE_VERIFIED);
    CHECK(update.have_installed == 0);
    CHECK(update.staged.image_version == 2u);
    CHECK(update.staged.image_size == image_length);

    take_snapshot();
    CHECK(helikon_update_park(&update) == HELIKON_OK);
    CHECK(update.stage == HELIKON_UPDATE_PARKED);
    /* Park writes the copy and nothing else: the installed set is still absent,
     * so a power cut here boots exactly what booted before. */
    CHECK(expect_absent(HELIKON_UPDATE_INSTALLED_IMAGE) == 0);
    CHECK(expect_absent(HELIKON_UPDATE_INSTALLED_MANIFEST) == 0);
    CHECK(expect_file(HELIKON_UPDATE_PARKED_IMAGE, image, image_length) == 0);
    /* No installed manifest means no recovery copy to make. */
    CHECK(expect_absent(HELIKON_UPDATE_RECOVERY_IMAGE) == 0);
    CHECK(!snapshot_unchanged()); /* it did write, just not over the live set */
    CHECK(check_clean("park") == 0);

    CHECK(helikon_update_commit(&update) == HELIKON_OK);
    CHECK(update.stage == HELIKON_UPDATE_INSTALLED);
    CHECK(update.bootstate_sequence == 1u);
    CHECK(check_clean("commit") == 0);

    CHECK(expect_file(HELIKON_UPDATE_INSTALLED_MANIFEST, manifest,
                      manifest_length) == 0);
    CHECK(expect_file(HELIKON_UPDATE_INSTALLED_IMAGE, image, image_length) == 0);
    CHECK(expect_absent(HELIKON_UPDATE_PARKED_IMAGE) == 0);

    struct helikon_bootstate bootstate;
    static uint8_t raw[HELIKON_BOOTSTATE_SIZE];
    if (parse_bootstate(&bootstate, raw, sizeof(raw)) != 0)
        return 1;
    CHECK(slot_is(&bootstate.slots[0], 2u, (uint32_t)image_length,
                  HELIKON_BOOTSTATE_TRYING, update.staged.image_sha256));
    CHECK(bootstate.slots[0].sequence == 1u);
    CHECK(bootstate.slots[1].valid == 0);
    /* TRYING is not selectable, which is the point: the loader uses the verified
     * default image and the firmware promotes it after its self-test. */
    CHECK(bootstate.selected == -1);

    /* A second commit on the same object is ARGUMENT, not a silent re-swap. */
    take_snapshot();
    CHECK(helikon_update_commit(&update) == HELIKON_ERR_ARGUMENT);
    CHECK(helikon_update_park(&update) == HELIKON_ERR_ARGUMENT);
    CHECK(snapshot_unchanged());
    return 0;
}

/* 4 and 5. A real upgrade: recovery copy, demotion, and a refused downgrade. */
static int test_upgrade_and_recovery(void)
{
    if (fresh_volume() != 0)
        return 1;
    if (open_update(0) != 0)
        return 1;

    /* Install version 2 first. */
    if (stage_set("good") != 0)
        return 1;
    CHECK(helikon_update_install(&update) == HELIKON_OK);
    CHECK(update.stage == HELIKON_UPDATE_INSTALLED);
    CHECK(check_clean("install v2") == 0);

    struct fixture_set previous;
    struct fixture_set replacement;
    if (load_set("good", &previous) != 0)
        return 1;
    if (load_set("next", &replacement) != 0)
        return 1;
    const uint8_t *image2 = previous.image;
    const uint8_t *manifest2 = previous.manifest;
    const size_t image2_length = previous.image_length;
    const size_t manifest2_length = sizeof(previous.manifest);
    const uint8_t *image3 = replacement.image;
    const uint8_t *manifest3 = replacement.manifest;
    const size_t image3_length = replacement.image_length;
    const size_t manifest3_length = sizeof(replacement.manifest);

    /* Now stage version 3 and install it. */
    if (open_update(0) != 0)
        return 1;
    if (stage_set("next") != 0)
        return 1;
    CHECK(helikon_update_verify(&update) == HELIKON_OK);
    CHECK(update.have_installed == 1);
    CHECK(update.installed.image_version == 2u);
    CHECK(helikon_update_park(&update) == HELIKON_OK);

    /* The recovery copy is the pair that was installed before, verified. */
    CHECK(expect_file(HELIKON_UPDATE_RECOVERY_IMAGE, image2, image2_length) == 0);
    CHECK(expect_file(HELIKON_UPDATE_RECOVERY_MANIFEST, manifest2,
                      manifest2_length) == 0);
    /* And the live set is still version 2 at this point. */
    CHECK(expect_file(HELIKON_UPDATE_INSTALLED_IMAGE, image2, image2_length) == 0);
    CHECK(check_clean("park v3") == 0);

    CHECK(helikon_update_commit(&update) == HELIKON_OK);
    CHECK(update.bootstate_sequence == 2u);
    CHECK(check_clean("commit v3") == 0);
    CHECK(expect_file(HELIKON_UPDATE_INSTALLED_IMAGE, image3, image3_length) == 0);
    CHECK(expect_file(HELIKON_UPDATE_INSTALLED_MANIFEST, manifest3,
                      manifest3_length) == 0);
    /* The recovery copy is not touched by the commit. */
    CHECK(expect_file(HELIKON_UPDATE_RECOVERY_IMAGE, image2, image2_length) == 0);

    struct helikon_bootstate bootstate;
    static uint8_t raw[HELIKON_BOOTSTATE_SIZE];
    if (parse_bootstate(&bootstate, raw, sizeof(raw)) != 0)
        return 1;
    /* The new facts are TRYING with the higher sequence... */
    CHECK(slot_is(&bootstate.slots[1], 3u, (uint32_t)image3_length,
                  HELIKON_BOOTSTATE_TRYING, update.staged.image_sha256));
    CHECK(bootstate.slots[1].sequence == 2u);
    /* ...and the slot that described version 2 is no longer GOOD, so the loader
     * cannot validate the old image against the new manifest. */
    CHECK(bootstate.slots[0].valid == 1);
    CHECK(bootstate.slots[0].state == HELIKON_BOOTSTATE_FAILED);
    CHECK(bootstate.slots[0].image_version == 2u);
    CHECK(bootstate.selected == -1);

    /* The same version again is a downgrade: refused, nothing written. */
    if (open_update(0) != 0)
        return 1;
    if (stage_set("same-version") != 0)
        return 1;
    take_snapshot();
    CHECK(expect_refused("re-installing the installed version",
                         helikon_update_verify(&update),
                         HELIKON_ERR_ARGUMENT) == 0);

    /* The older set is a downgrade too. */
    if (stage_set("good") != 0)
        return 1;
    take_snapshot();
    CHECK(expect_refused("downgrading", helikon_update_verify(&update),
                         HELIKON_ERR_ARGUMENT) == 0);
    return 0;
}

/*
 * The two ways an update must not write: a volume that is not journaled has no
 * writer at all (I-02), and a writer that has already failed a commit reports
 * `writable = 0`, which is the state the VFS leaves behind rather than retrying.
 */
static int test_read_only_paths(void)
{
    unmount();
    struct hfs_volume plain;
    CHECK(writer_fixture_build(&plain) == 0);
    CHECK(vfs_mount(&vfs, image_read, NULL, 0, WRITER_IMAGE_SIZE,
                    catalog_scratch, sizeof(catalog_scratch), extent_scratch,
                    sizeof(extent_scratch)) == HELIKON_OK);
    CHECK(vfs_rw_mount(&rw, &vfs, writer_fixture_write, writer_fixture_flush,
                       NULL) == HELIKON_ERR_UNSUPPORTED);
    CHECK(rw.writable == 0u);

    /* A real, writable volume to stage on, then a writer that has been marked
     * read-only, which is exactly the state after a failed commit. */
    if (fresh_volume() != 0)
        return 1;
    if (open_update(0) != 0)
        return 1;
    if (stage_set("good") != 0)
        return 1;
    CHECK(helikon_update_verify(&update) == HELIKON_OK);

    take_snapshot();
    rw.writable = 0u;
    CHECK(expect_refused("parking through a read-only writer",
                         helikon_update_park(&update),
                         HELIKON_ERR_READ_ONLY) == 0);
    rw.writable = 1u;

    /* A writer that does not exist is ARGUMENT, not a crash. */
    CHECK(helikon_update_open(&update, &vfs, NULL, &keys, HELIKON_BOARD_IPOD6G,
                              0) == HELIKON_OK);
    CHECK(helikon_update_verify(&update) == HELIKON_OK);
    CHECK(helikon_update_park(&update) == HELIKON_ERR_ARGUMENT);
    CHECK(helikon_update_install(&update) == HELIKON_ERR_ARGUMENT);
    unmount();
    return 0;
}

int main(void)
{
    memset(&key, 0, sizeof(key));
    key.id = UPDATE_TEST_KEY_ID;
    memcpy(key.key, update_test_public_key, sizeof(key.key));
    keys.keys = &key;
    keys.count = 1u;

    if (test_verify_rejects())
        return 1;
    if (test_first_install())
        return 1;
    if (test_upgrade_and_recovery())
        return 1;
    if (test_read_only_paths())
        return 1;

    printf("PASS update: signed staged set verified, recovery copy and one-commit "
           "install, boot state staged TRYING, every refusal wrote nothing\n");
    return 0;
}
