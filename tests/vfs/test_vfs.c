/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host tests for the read-only VFS (T-056) over the fabric catalog fixture
 * (root -> .firmware -> current.img with the "HELO" payload) and the shared
 * test-image helpers from tests/hfsplus.
 */
#include "codec.h"
#include "test_check.h"
#include "test_fs_image.h"
#include "test_image.h"
#include "vfs.h"
#include "vfs_media.h"

#include <stdint.h>
#include <string.h>

static int mount_fixture(struct vfs *vfs)
{
    static uint8_t catalog_scratch[TEST_FS_NODE_SIZE];
    static uint8_t extents_scratch[TEST_FS_NODE_SIZE];
    test_fs_image_make();
    return vfs_mount(vfs, test_image_read, NULL, 0, TEST_IMAGE_SIZE,
                     catalog_scratch, sizeof(catalog_scratch),
                     extents_scratch, sizeof(extents_scratch));
}

/* The same catalog, with current.img's second block in the extents tree. */
static int mount_overflow_fixture(struct vfs *vfs)
{
    static uint8_t catalog_scratch[TEST_FS_NODE_SIZE];
    static uint8_t extents_scratch[TEST_FS_NODE_SIZE];
    test_fs_image_make_overflow();
    return vfs_mount(vfs, test_image_read, NULL, 0, TEST_IMAGE_SIZE,
                     catalog_scratch, sizeof(catalog_scratch),
                     extents_scratch, sizeof(extents_scratch));
}

static int test_mount_stat(void)
{
    struct vfs vfs;
    struct vfs_entry entry;

    CHECK(mount_fixture(&vfs) == HELIKON_OK);
    CHECK(vfs.journal_dirty == 0u);

    CHECK(vfs_stat(&vfs, "/.firmware", &entry) == HELIKON_OK);
    CHECK(entry.kind == VFS_KIND_DIR);
    CHECK(entry.id == 3u);
    CHECK(entry.unsupported == 0u);

    CHECK(vfs_stat(&vfs, "/.firmware/current.img", &entry) == HELIKON_OK);
    CHECK(entry.kind == VFS_KIND_FILE);
    CHECK(entry.id == 4u);
    CHECK(entry.size == 4u); /* the fixture payload is "HELO" */

    CHECK(vfs_stat(&vfs, "/.firmware/missing", &entry) ==
          HELIKON_ERR_NOT_FOUND);
    CHECK(vfs_stat(&vfs, "relative", &entry) == HELIKON_ERR_NAME_INVALID);
    CHECK(vfs_stat(&vfs, "/../escape", &entry) == HELIKON_ERR_NAME_INVALID);
    CHECK(vfs_stat(&vfs, "/./here", &entry) == HELIKON_ERR_NAME_INVALID);
    CHECK(vfs_stat(&vfs, "//double", &entry) == HELIKON_ERR_NAME_INVALID);
    CHECK(vfs_stat(&vfs, "/trailing/", &entry) == HELIKON_ERR_NAME_INVALID);
    CHECK(vfs_stat(&vfs, "", &entry) == HELIKON_ERR_NAME_INVALID);
    CHECK(vfs_stat(&vfs, NULL, &entry) == HELIKON_ERR_NAME_INVALID);
    return 0;
}

static int test_mount_rejects_non_hfs(void)
{
    struct vfs vfs;
    static uint8_t scratch[TEST_FS_NODE_SIZE];

    CHECK(vfs_mount(&vfs, NULL, NULL, 0, TEST_IMAGE_SIZE, scratch,
                    sizeof(scratch), scratch, sizeof(scratch)) ==
          HELIKON_ERR_ARGUMENT);
    CHECK(vfs_mount(NULL, test_image_read, NULL, 0, TEST_IMAGE_SIZE, scratch,
                    sizeof(scratch), scratch, sizeof(scratch)) ==
          HELIKON_ERR_ARGUMENT);
    return 0;
}

static int test_open_read_eof(void)
{
    struct vfs vfs;
    struct vfs_file_handle handle = { 0u, 0u };
    uint8_t data[16];
    size_t done = 0;
    int rc = 0;

    CHECK(mount_fixture(&vfs) == HELIKON_OK);

    rc = vfs_file_open(&vfs, "/.firmware/current.img", &handle);
    CHECK(rc == HELIKON_OK);

    memset(data, 0, sizeof(data));
    rc = vfs_file_read(&vfs, handle, data, 4u, &done);
    CHECK(rc == HELIKON_OK);
    CHECK(done == 4u);
    CHECK(memcmp(data, "HELO", 4u) == 0);
    CHECK(vfs_file_position(&vfs, handle) == 4u);

    /* Reading at end of file reports done == 0, not an error. */
    rc = vfs_file_read(&vfs, handle, data, sizeof(data), &done);
    CHECK(rc == HELIKON_OK);
    CHECK(done == 0u);
    CHECK(vfs_file_position(&vfs, handle) == 4u);

    /* Reopen, seek into the middle, read to the end. */
    struct vfs_file_handle again = { 0u, 0u };
    CHECK(vfs_file_open(&vfs, "/.firmware/current.img", &again) ==
          HELIKON_OK);
    uint64_t position = 0u;
    CHECK(vfs_file_seek(&vfs, again, VFS_SEEK_SET, 2u, &position) ==
          HELIKON_OK);
    CHECK(position == 2u);
    CHECK(vfs_file_read(&vfs, again, data, sizeof(data), &done) ==
          HELIKON_OK);
    CHECK(done == 2u);
    CHECK(memcmp(data, "LO", 2u) == 0);
    CHECK(vfs_file_position(&vfs, again) == 4u);

    /* Seeking past the 4-byte size refuses. */
    CHECK(vfs_file_seek(&vfs, again, VFS_SEEK_SET, 16u, &position) ==
          HELIKON_ERR_ARGUMENT);

    CHECK(vfs_file_close(&vfs, again) == HELIKON_OK);
    CHECK(vfs_file_close(&vfs, handle) == HELIKON_OK);
    return 0;
}

static int test_seek_semantics(void)
{
    struct vfs vfs;
    struct vfs_file_handle handle = { 0u, 0u };
    uint64_t position = 0u;

    CHECK(mount_fixture(&vfs) == HELIKON_OK);
    CHECK(vfs_file_open(&vfs, "/.firmware/current.img", &handle) ==
          HELIKON_OK);

    CHECK(vfs_file_seek(&vfs, handle, VFS_SEEK_SET, 2u, &position) ==
          HELIKON_OK);
    CHECK(position == 2u);
    CHECK(vfs_file_position(&vfs, handle) == 2u);

    CHECK(vfs_file_seek(&vfs, handle, VFS_SEEK_CUR, 2u, &position) ==
          HELIKON_OK);
    CHECK(position == 4u);

    CHECK(vfs_file_seek(&vfs, handle, VFS_SEEK_END, 0, &position) ==
          HELIKON_OK);
    CHECK(position == 4u);

    CHECK(vfs_file_seek(&vfs, handle, VFS_SEEK_END, 1u, &position) ==
          HELIKON_ERR_ARGUMENT);

    CHECK(vfs_file_seek(&vfs, handle, VFS_SEEK_SET, 16u, &position) ==
          HELIKON_ERR_ARGUMENT);

    /* Read at EOF reports done == 0, not an error. */
    uint8_t data[4];
    size_t done = 0;
    CHECK(vfs_file_read(&vfs, handle, data, sizeof(data), &done) ==
          HELIKON_OK);
    CHECK(done == 0u);

    CHECK(vfs_file_close(&vfs, handle) == HELIKON_OK);
    return 0;
}

static int test_handle_generation(void)
{
    struct vfs vfs;
    struct vfs_file_handle handle = { 0u, 0u };
    struct vfs_file_handle copy;
    uint8_t data[4];
    size_t done = 0;

    CHECK(mount_fixture(&vfs) == HELIKON_OK);
    CHECK(vfs_file_open(&vfs, "/.firmware/current.img", &handle) ==
          HELIKON_OK);
    copy = handle;

    CHECK(vfs_file_close(&vfs, handle) == HELIKON_OK);

    /* The closed handle (and a stale copy) cannot read or re-close. */
    CHECK(vfs_file_read(&vfs, handle, data, sizeof(data), &done) ==
          HELIKON_ERR_ARGUMENT);
    CHECK(vfs_file_close(&vfs, copy) == HELIKON_ERR_ARGUMENT);
    CHECK(vfs_file_position(&vfs, copy) == 0u);

    /* A fresh handle in the same slot must not be confused with the old one. */
    struct vfs_file_handle second = { 0u, 0u };
    CHECK(vfs_file_open(&vfs, "/.firmware/current.img", &second) ==
          HELIKON_OK);
    CHECK(second.generation != handle.generation);
    CHECK(second.slot == handle.slot);
    CHECK(vfs_file_read(&vfs, second, data, sizeof(data), &done) ==
          HELIKON_OK);
    CHECK(done == 4u);
    CHECK(vfs_file_close(&vfs, second) == HELIKON_OK);
    return 0;
}

static int test_dir_semantics(void)
{
    struct vfs vfs;
    struct vfs_dir_handle dir = { 0u, 0u };
    struct vfs_file_handle file = { 0u, 0u };
    struct vfs_entry entry;
    int saw_firmware = 0;
    int rc = 0;

    CHECK(mount_fixture(&vfs) == HELIKON_OK);

    rc = vfs_dir_open(&vfs, "/", &dir);
    CHECK(rc == HELIKON_OK);
    while ((rc = vfs_dir_next(&vfs, dir, &entry)) == 1)
        if (strcmp(entry.name, ".firmware") == 0) {
            CHECK(entry.kind == VFS_KIND_DIR);
            saw_firmware = 1;
        }
    CHECK(rc == 0);
    CHECK(saw_firmware == 1);
    CHECK(vfs_dir_close(&vfs, dir) == HELIKON_OK);
    CHECK(vfs_dir_next(&vfs, dir, &entry) == HELIKON_ERR_ARGUMENT);

    /* Opening a file as a directory, and a directory as a file, refuse. */
    CHECK(vfs_dir_open(&vfs, "/.firmware/current.img", &dir) ==
          HELIKON_ERR_NOT_DIR);
    CHECK(vfs_file_open(&vfs, "/.firmware", &file) == HELIKON_ERR_IS_DIR);

    /* A directory can list its own children; unsupported records surface. */
    CHECK(mount_fixture(&vfs) == HELIKON_OK);
    CHECK(vfs_dir_open(&vfs, "/.firmware", &dir) == HELIKON_OK);
    int saw_current = 0;
    while ((rc = vfs_dir_next(&vfs, dir, &entry)) == 1)
        if (strcmp(entry.name, "current.img") == 0) {
            CHECK(entry.size == 4u);
            saw_current = 1;
        }
    CHECK(rc == 0);
    CHECK(saw_current == 1);
    CHECK(vfs_dir_close(&vfs, dir) == HELIKON_OK);
    return 0;
}

static int test_read_only_boundary(void)
{
    struct vfs vfs;
    struct vfs_file_handle handle = { 0u, 0u };
    size_t done = 0;

    CHECK(mount_fixture(&vfs) == HELIKON_OK);
    CHECK(vfs_file_open(&vfs, "/.firmware/current.img", &handle) ==
          HELIKON_OK);
    CHECK(vfs_file_write(&vfs, handle, "", 1u, &done) ==
          HELIKON_ERR_READ_ONLY);
    CHECK(done == 0u);
    CHECK(vfs_file_close(&vfs, handle) == HELIKON_OK);
    return 0;
}

static int test_not_hfs_refused(void)
{
    struct vfs vfs;
    static uint8_t scratch[TEST_FS_NODE_SIZE];

    test_image_clear();
    CHECK(vfs_mount(&vfs, test_image_read, NULL, 0, TEST_IMAGE_SIZE, scratch,
                    sizeof(scratch), scratch, sizeof(scratch)) ==
          HELIKON_ERR_NOT_HFS);
    return 0;
}


/*
 * T-058 read seam: an open VFS file becomes a codec source. The adapter must
 * return exactly the file's bytes at any absolute offset, report end of file
 * as a clean zero-byte read, and compose with the registry (a file that is
 * not audio is refused, not misread).
 */
static int test_vfs_media(void)
{
    struct vfs vfs;
    struct vfs_entry entry;
    struct vfs_file_handle handle;
    static uint8_t catalog_scratch[TEST_FS_NODE_SIZE];
    static uint8_t extents_scratch[TEST_FS_NODE_SIZE];

    test_fs_image_make();
    CHECK(vfs_mount(&vfs, test_image_read, NULL, 0, TEST_IMAGE_SIZE,
                    catalog_scratch, sizeof(catalog_scratch), extents_scratch,
                    sizeof(extents_scratch)) == HELIKON_OK);
    CHECK(vfs_stat(&vfs, "/.firmware/current.img", &entry) == HELIKON_OK);
    CHECK(entry.size == 4u);
    CHECK(vfs_file_open(&vfs, "/.firmware/current.img", &handle) ==
          HELIKON_OK);

    struct helikon_vfs_media media;
    struct helikon_codec_source source;
    CHECK(helikon_vfs_media_init(&media, &vfs, handle, entry.size, &source) ==
          HELIKON_OK);
    CHECK(source.size == 4u && source.read_at != NULL && source.context == &media);

    uint8_t buffer[8];
    size_t got = 0u;

    /* Absolute reads do not depend on each other's order. */
    CHECK(source.read_at(source.context, 2u, buffer, 2u, &got) == HELIKON_OK &&
          got == 2u && memcmp(buffer, "LO", 2u) == 0);
    CHECK(source.read_at(source.context, 0u, buffer, 2u, &got) == HELIKON_OK &&
          got == 2u && memcmp(buffer, "HE", 2u) == 0);
    CHECK(source.read_at(source.context, 3u, buffer, 8u, &got) == HELIKON_OK &&
          got == 1u && buffer[0] == 'O');
    CHECK(source.read_at(source.context, 4u, buffer, 8u, &got) == HELIKON_OK &&
          got == 0u);
    CHECK(source.read_at(source.context, 4096u, buffer, 8u, &got) ==
          HELIKON_OK && got == 0u);
    CHECK(source.read_at(NULL, 0u, buffer, 8u, &got) == HELIKON_ERR_ARGUMENT);

    /* Compose with the registry: "HELO" is not audio, and saying so must not
     * read past the file or crash. */
    memset(buffer, 0, sizeof(buffer));
    CHECK(source.read_at(source.context, 0u, buffer, entry.size, &got) ==
          HELIKON_OK && got == 4u);

    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    CHECK(helikon_codec_probe(buffer, got, &ops, &info) ==
          HELIKON_ERR_UNSUPPORTED);

    static uint8_t workspace[HELIKON_CODEC_WORKSPACE_MAX];
    struct helikon_codec codec;
    CHECK(helikon_codec_open_source(&codec, &helikon_wav_codec_ops, &source,
                                   workspace, sizeof(workspace)) ==
          HELIKON_ERR_UNSUPPORTED);

    CHECK(vfs_file_close(&vfs, handle) == HELIKON_OK);
    return 0;
}


/*
 * A file whose second block lives in the extents-overflow tree. The read has to
 * splice both halves, report the byte count and advance the handle; the old
 * code returned the raw reader result, so done stayed 0 and the position never
 * moved — a caller would loop on the same bytes forever.
 */
static int test_overflow_file_read(void)
{
    struct vfs vfs;
    struct vfs_entry entry;
    struct vfs_file_handle handle = { 0u, 0u };
    uint8_t data[1024];
    size_t done = 0;

    CHECK(mount_overflow_fixture(&vfs) == HELIKON_OK);
    CHECK(vfs_stat(&vfs, "/.firmware/current.img", &entry) == HELIKON_OK);
    CHECK(entry.kind == VFS_KIND_FILE);
    CHECK(entry.size == 1024u);
    CHECK(vfs_file_open(&vfs, "/.firmware/current.img", &handle) ==
          HELIKON_OK);

    /* Inline half first: "HELO" at the front of the first block. */
    memset(data, 0, sizeof(data));
    CHECK(vfs_file_read(&vfs, handle, data, 512u, &done) == HELIKON_OK);
    CHECK(done == 512u);
    CHECK(vfs_file_position(&vfs, handle) == 512u);
    CHECK(memcmp(data, "HELO", 4u) == 0);

    /* Overflow half: the second block comes from the extents tree. */
    memset(data, 0, sizeof(data));
    CHECK(vfs_file_read(&vfs, handle, data, sizeof(data), &done) ==
          HELIKON_OK);
    CHECK(done == 512u);
    CHECK(vfs_file_position(&vfs, handle) == 1024u);
    for (size_t i = 0; i < 512u; ++i) {
        if (data[i] != 'B') {
            fprintf(stderr, "overflow read byte %zu is 0x%02x\n", i, data[i]);
            CHECK(data[i] == 'B');
            break;
        }
    }

    /* End of file reports done == 0, not an error. */
    CHECK(vfs_file_read(&vfs, handle, data, sizeof(data), &done) ==
          HELIKON_OK);
    CHECK(done == 0u);

    /* A seek across the inline/overflow boundary reads both halves. */
    uint64_t position = 0;
    CHECK(vfs_file_seek(&vfs, handle, VFS_SEEK_SET, 1020u, &position) ==
          HELIKON_OK);
    CHECK(vfs_file_read(&vfs, handle, data, 8u, &done) == HELIKON_OK);
    CHECK(done == 4u);
    CHECK(memcmp(data, "BBBB", 4u) == 0);

    CHECK(vfs_file_close(&vfs, handle) == HELIKON_OK);
    return 0;
}

int main(void)
{
    int failures = 0;
    failures += test_mount_stat();
    failures += test_mount_rejects_non_hfs();
    failures += test_open_read_eof();
    failures += test_seek_semantics();
    failures += test_handle_generation();
    failures += test_dir_semantics();
    failures += test_read_only_boundary();
    failures += test_not_hfs_refused();
    failures += test_vfs_media();
    failures += test_overflow_file_read();
    if (failures != 0) {
        fprintf(stderr, "test_vfs: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stderr, "test_vfs: all tests passed\n");
    return 0;
}
