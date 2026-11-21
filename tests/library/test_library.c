/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host test: HelikonDB read from a real HFS+ volume through the read-only VFS.
 *
 * The image is created by `run-macos.sh` with `hdiutil` (journaled HFS+, the
 * same on-disk shape as the device) and holds `/.helikon/music.hdb` built by
 * `tools/hdb_build.py fixture`. This proves the storage path end to end on the
 * host: mount -> path lookup -> open -> seek/read -> HDB records. It is not a
 * device run and makes no claim about the 6G.
 */
#include "hdb_vfs.h"

#include <stdio.h>
#include <string.h>

#include "error.h"
#include "vfs.h"

static FILE *image;
static long image_size;

static int failures;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", message);
        failures += 1;
    }
}

/* hfs_read_fn over the image file: absolute offsets, exact lengths. */
static int image_read(void *context, uint64_t offset, void *buffer,
                      size_t length)
{
    (void)context;
    if (image == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (offset > (uint64_t)image_size ||
        length > (size_t)((uint64_t)image_size - offset))
        return HELIKON_ERR_IO;
    if (fseek(image, (long)offset, SEEK_SET) != 0)
        return HELIKON_ERR_IO;
    if (fread(buffer, 1u, length, image) != length)
        return HELIKON_ERR_IO;
    return HELIKON_OK;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <hfs-image>\n", argv[0]);
        return 2;
    }

    image = fopen(argv[1], "rb");
    if (image == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    if (fseek(image, 0, SEEK_END) != 0) {
        fprintf(stderr, "cannot size %s\n", argv[1]);
        return 2;
    }
    image_size = ftell(image);
    if (image_size <= 0) {
        fprintf(stderr, "empty image %s\n", argv[1]);
        return 2;
    }

    /* macOS uses 4 KiB or 8 KiB catalog nodes; the scratch holds one node. */
    static uint8_t catalog_scratch[8192];
    static uint8_t extents_scratch[8192];

    struct vfs vfs;
    int rc = vfs_mount(&vfs, image_read, NULL, 0u, (uint64_t)image_size,
                       catalog_scratch, sizeof(catalog_scratch),
                       extents_scratch, sizeof(extents_scratch));
    check(rc == HELIKON_OK, "mount the HFS+ image read-only");
    if (rc != HELIKON_OK) {
        fprintf(stderr, "mount failed: %d\n", rc);
        struct hfs_volume volume;
        int probe = hfs_volume_probe_readonly(&volume, image_read, NULL, 0u,
                                              (uint64_t)image_size);
        fprintf(stderr,
                "probe=%d block=%u blocks=%u journaled=%u dirty=%u "
                "catalog=%u blocks size=%llu\n",
                probe, volume.block_size, volume.blocks, volume.journaled,
                volume.journal_dirty, volume.catalog.blocks,
                (unsigned long long)volume.catalog.size);
        fprintf(stderr,
                "probe=%d block=%u blocks=%u journaled=%u dirty=%u "
                "catalog=%u blocks extents=%u blocks\n",
                probe, volume.block_size, volume.blocks, volume.journaled,
                volume.journal_dirty, volume.catalog.blocks,
                volume.extents.blocks);
        return 1;
    }
    check(vfs.journal_dirty == 0u, "journal is clean after a proper unmount");

    struct vfs_entry entry;
    check(vfs_stat(&vfs, "/.helikon/music.hdb", &entry) == HELIKON_OK,
          "stat /.helikon/music.hdb");
    check(entry.kind == VFS_KIND_FILE, "the catalog is a file");
    check(entry.size > 128u, "the catalog is larger than its header");

    struct hdb_vfs_io io;
    check(hdb_vfs_open(&io, &vfs, "/.helikon/music.hdb") == HELIKON_OK,
          "open the catalog through the VFS");
    check(io.size == (uint32_t)entry.size, "adapter reports the file size");

    struct hdb_io hdb_io_vfs;
    hdb_vfs_io_init(&io, &hdb_io_vfs);

    struct hdb db;
    check(hdb_open(&db, &hdb_io_vfs) == HELIKON_OK, "hdb_open over the VFS");
    check(db.track_count == 6u, "six fixture tracks");
    check(db.album_count == 3u, "three fixture albums");
    check(db.artist_count == 3u, "three fixture artists");

    struct hdb_track track;
    char text[128];
    uint32_t length = 0u;
    check(hdb_track(&db, 0u, &track) == HELIKON_OK, "track 0 reads");
    check(hdb_string(&db, track.title_off, text, sizeof(text), &length) ==
              HELIKON_OK,
          "track 0 title reads");
    check(strcmp(text, "First Light") == 0, "track 0 title");

    struct hdb_album album;
    check(hdb_album(&db, 0u, &album) == HELIKON_OK, "album 0 reads");
    check(hdb_string(&db, album.title_off, text, sizeof(text), &length) ==
              HELIKON_OK,
          "album 0 title reads");
    check(strcmp(text, "Night Harbour") == 0, "album 0 title");

    uint32_t count = 0u;
    uint32_t index_offset = 0u;
    check(hdb_index(&db, HDB_INDEX_TITLE, &count, &index_offset) == HELIKON_OK,
          "title index reads");
    check(count == 6u, "title index count");

    /* CRC over the whole catalog read through the VFS: every byte of the file
     * has to arrive correctly, not just the record we happened to look at. */
    check(hdb_verify_crc(&db) == HELIKON_OK, "catalog CRC over the VFS");

    struct hdb_vfs_io missing;
    check(hdb_vfs_open(&missing, &vfs, "/.helikon/missing.hdb") ==
              HELIKON_ERR_NOT_FOUND,
          "a missing catalog is NOT_FOUND");
    struct hdb_vfs_io folder;
    check(hdb_vfs_open(&folder, &vfs, "/.helikon") == HELIKON_ERR_IS_DIR,
          "a folder is not a catalog");

    hdb_vfs_close(&io);
    hdb_vfs_close(&io);
    check(io.open == 0, "close is idempotent");
    const struct hdb_io *closed = &hdb_io_vfs;
    check(closed->read(&io, 0u, text, 4u) == HELIKON_ERR_ARGUMENT,
          "a closed adapter refuses reads");

    if (fclose(image) != 0)
        return 2;
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("PASS library: HFS+ -> VFS -> HDB (%u tracks, %u albums, CRC OK)\n",
           db.track_count, db.album_count);
    return 0;
}
