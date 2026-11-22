/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * hfs-check: run the read-only structural check over a volume image.
 *
 *   hfs-check disk.img
 *
 * Exit status: 0 clean, 1 findings, 2 the image could not be used at all.
 *
 * This is host tooling. The same code runs in the firmware; the program exists
 * so a real macOS-created image, a recovered crash image or a device dump can
 * be checked without booting anything.
 */
#include "hfs_check.h"
#include "hfs_volume.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct image {
    int fd;
};

static int read_at(void *context, uint64_t offset, void *buffer, size_t length)
{
    struct image *image = context;
    ssize_t done = pread(image->fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : 1;
}

static void print_flags(uint32_t problems)
{
    static const struct {
        uint32_t bit;
        const char *name;
    } names[] = {
        { HFS_CHECK_BAD_BLOCK_SIZE, "bad block size" },
        { HFS_CHECK_BAD_BLOCK_COUNT, "bad block count" },
        { HFS_CHECK_FREE_EXCEEDS_TOTAL, "free blocks exceed the volume" },
        { HFS_CHECK_EXTENT_OUTSIDE, "an extent points outside the volume" },
        { HFS_CHECK_FORK_BLOCK_OVERFLOW, "a fork claims more blocks than it has" },
        { HFS_CHECK_FORK_SIZE_MISMATCH, "a fork's size does not fit its blocks" },
        { HFS_CHECK_ALLOCATION_TOO_SMALL, "the allocation file cannot describe the volume" },
        { HFS_CHECK_FREE_MISMATCH, "the free-block count disagrees with the bitmap" },
        { HFS_CHECK_BTREE_HEADER, "a B-tree header is not valid" },
        { HFS_CHECK_BTREE_SIZE, "a B-tree does not fit its fork" },
        { HFS_CHECK_BTREE_INDEX, "a B-tree index node or child pointer is not valid" },
        { HFS_CHECK_BTREE_CHAIN, "a B-tree leaf chain does not match its header" },
        { HFS_CHECK_CATALOG_ORDER, "catalog keys are out of order" },
        { HFS_CHECK_CATALOG_BODY, "a catalog record body is malformed" },
        { HFS_CHECK_EXTENTS_ORDER, "extents keys are out of order" },
        { HFS_CHECK_CATALOG_COUNT, "the catalog count disagrees with the header" },
        { HFS_CHECK_CATALOG_RECORD, "a catalog record or key is malformed" },
        { HFS_CHECK_SKIPPED_BITMAP, "the bitmap was not counted (overflowed fork)" }
    };

    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if ((problems & names[index].bit) != 0u)
            printf("  finding: %s\n", names[index].name);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s IMAGE\n", argv[0]);
        return 2;
    }

    struct image image;
    image.fd = open(argv[1], O_RDONLY);
    if (image.fd < 0) {
        fprintf(stderr, "%s: cannot open %s\n", argv[0], argv[1]);
        return 2;
    }

    struct hfs_volume volume;
    int rc = hfs_volume_probe(&volume, read_at, &image, 0u, (uint64_t)-1);
    if (rc != HFS_OK) {
        fprintf(stderr, "%s: %s: %s\n", argv[0], argv[1], hfs_strerror(rc));
        close(image.fd);
        return 2;
    }

    struct hfs_check_report report;
    rc = hfs_check_volume(&volume, &report);
    close(image.fd);
    if (rc != HFS_OK) {
        fprintf(stderr, "%s: check failed: %s\n", argv[0], hfs_strerror(rc));
        return 2;
    }

    printf("%s\n", argv[1]);
    printf("  block size %u, blocks %u\n", report.block_size, report.blocks);
    printf("  free blocks: header %u, bitmap %u%s\n", report.free_blocks_claimed,
           report.free_blocks_counted,
           report.bitmap_counted != 0 ? "" : " (not counted)");
    printf("  catalog: %u files, %u folders (header says %u and %u)\n",
           report.catalog_files, report.catalog_folders, report.header_files,
           report.header_folders);
    printf("  trees: catalog depth %u/%u nodes, extents %u/%u, attributes %u/%u\n",
           report.catalog_depth, report.catalog_nodes, report.extents_depth,
           report.extents_nodes, report.attributes_depth,
           report.attributes_nodes);

    if (report.skipped != 0u)
        printf("  not checked: the allocation bitmap (its fork is not inline or is "
               "above the cap)\n");
    if (report.problems != 0u) {
        print_flags(report.problems);
        printf("  problems: 0x%08x\n", report.problems);
        return 1;
    }
    printf("  clean\n");
    return 0;
}
