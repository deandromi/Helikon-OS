/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "test_fs_image.h"
#include "test_image.h"

#include <stdint.h>
#include <string.h>

#define CATALOG_BLOCK 8u
#define CATALOG_NODE_SIZE 512u
#define CATALOG_TOTAL_NODES 4u
#define FILE_DATA_BLOCK 16u
/*
 * The extents overflow tree: one leaf record for the overflow.bin fixture, whose
 * second block lives at OVERFLOW_DATA_BLOCK. Its fork therefore has more blocks
 * than inline extents, which is exactly the read path a real fragmented file
 * takes through the VFS.
 */
#define EXTENTS_BLOCK 24u
#define EXTENTS_NODE_SIZE 512u
#define EXTENTS_TOTAL_NODES 2u
#define OVERFLOW_FILE_ID 6u
#define OVERFLOW_DATA_BLOCK 20u

static size_t put_key(uint8_t *record, uint32_t parent, const char *name)
{
    size_t units = strlen(name);
    test_image_put_be16(record, (uint16_t)(6u + units * 2u));
    test_image_put_be32(record + 2, parent);
    test_image_put_be16(record + 6, (uint16_t)units);
    for (size_t i = 0; i < units; ++i)
        test_image_put_be16(record + 8 + i * 2, (uint8_t)name[i]);
    return 6u + units * 2u;
}

static size_t put_key_raw(uint8_t *record, uint32_t parent,
                          const uint8_t *name, size_t units)
{
    test_image_put_be16(record, (uint16_t)(6u + units * 2u));
    test_image_put_be32(record + 2, parent);
    test_image_put_be16(record + 6, (uint16_t)units);
    if (units != 0)
        memcpy(record + 8, name, units * 2u);
    return 6u + units * 2u;
}

static size_t put_folder_raw(uint8_t *record, uint32_t parent,
                             const uint8_t *name, size_t units, uint32_t id)
{
    size_t key = put_key_raw(record, parent, name, units);
    uint8_t *data = record + key + 2;
    memset(data, 0, 88);
    test_image_put_be16(data, 1);
    test_image_put_be32(data + 8, id);
    test_image_put_be16(data + 42, 0040000);
    return key + 2u + 88u;
}

static size_t put_folder(uint8_t *record, uint32_t parent, const char *name,
                         uint32_t id)
{
    size_t units = strlen(name);
    uint8_t encoded[255 * 2];
    for (size_t i = 0; i < units; ++i)
        test_image_put_be16(encoded + i * 2, (uint8_t)name[i]);
    return put_folder_raw(record, parent, encoded, units, id);
}

static size_t put_file_with_size(uint8_t *record, uint32_t parent,
                                 const char *name, uint32_t id,
                                 uint64_t size, uint32_t blocks,
                                 uint32_t first_block, uint32_t inline_blocks,
                                 uint8_t **fork_out)
{
    size_t key = put_key(record, parent, name);
    uint8_t *data = record + key + 2;
    memset(data, 0, 248);
    test_image_put_be16(data, 2);
    test_image_put_be32(data + 8, id);
    test_image_put_be16(data + 42, 0100000);
    test_image_put_be64(data + 88, size);
    test_image_put_be32(data + 88 + 12, blocks);
    test_image_put_be32(data + 88 + 16, first_block);
    test_image_put_be32(data + 88 + 20, inline_blocks);
    if (fork_out != NULL)
        *fork_out = data + 88;
    return key + 2u + 248u;
}

static size_t put_file(uint8_t *record, uint32_t parent, const char *name,
                       uint32_t id, uint32_t blocks, uint8_t **fork_out)
{
    return put_file_with_size(record, parent, name, id, 4, blocks,
                              FILE_DATA_BLOCK, 1, fork_out);
}

struct test_fs_image test_fs_image_make(void)
{
    test_image_clear();
    test_image_make_header(test_image + 1024, 0x482bu, 4, TEST_ATTR_UNMOUNTED,
                           512, 128, 64);

    uint8_t *fork = test_image + 1024 + 272;
    test_image_put_be64(fork, CATALOG_TOTAL_NODES * CATALOG_NODE_SIZE);
    test_image_put_be32(fork + 12, CATALOG_TOTAL_NODES);
    test_image_put_be32(fork + 16, CATALOG_BLOCK);
    test_image_put_be32(fork + 20, CATALOG_TOTAL_NODES);

    uint8_t *header_node = test_image + CATALOG_BLOCK * CATALOG_NODE_SIZE;
    memset(header_node, 0, CATALOG_NODE_SIZE);
    header_node[8] = 1;
    header_node[9] = 0;
    test_image_put_be16(header_node + 10, 3);

    uint8_t *header = header_node + 14;
    test_image_put_be16(header, 1);
    test_image_put_be32(header + 2, 1);
    test_image_put_be32(header + 6, 2);
    test_image_put_be32(header + 10, 1);
    test_image_put_be32(header + 14, 1);
    test_image_put_be16(header + 18, CATALOG_NODE_SIZE);
    test_image_put_be16(header + 20, 516);
    test_image_put_be32(header + 22, CATALOG_TOTAL_NODES);
    test_image_put_be32(header + 26, 1);
    test_image_put_be32(header + 38, 6);
    header[36] = 0;
    header[37] = 0xcf;

    test_image_put_be16(header_node + 504, 504);
    test_image_put_be16(header_node + 506, 248);
    test_image_put_be16(header_node + 508, 120);
    test_image_put_be16(header_node + 510, 14);

    uint8_t *leaf = test_image + 9 * CATALOG_NODE_SIZE;
    memset(leaf, 0, CATALOG_NODE_SIZE);
    leaf[8] = 0xff;
    leaf[9] = 1;
    test_image_put_be16(leaf + 10, 3);

    size_t folder_size = put_folder(leaf + 14, 2, ".firmware", 3);
    uint8_t *file_fork = NULL;
    size_t file_size = put_file(leaf + 14 + folder_size, 3, "current.img", 4,
                                1, &file_fork);
    static const uint8_t private_name[] = { 0, 0 };
    size_t private_size = put_folder_raw(
        leaf + 14 + folder_size + file_size, 3, private_name, 1, 5);
    size_t end = 14 + folder_size + file_size + private_size;

    test_image_put_be16(leaf + 510, 14);
    test_image_put_be16(leaf + 508, (uint16_t)(14 + folder_size));
    test_image_put_be16(leaf + 506,
                        (uint16_t)(14 + folder_size + file_size));
    test_image_put_be16(leaf + 504, (uint16_t)end);

    memcpy(test_image + FILE_DATA_BLOCK * 512, "HELO", 4);

    struct test_fs_image fixture = { leaf, file_fork };
    (void)private_size;
    return fixture;
}

/*
 * The same catalog, but `current.img` has a second block that lives in the
 * extents-overflow tree: a two-block fork with one inline extent and one
 * overflow extent. A read has to splice both halves, which is the path a
 * fragmented file on a real volume takes.
 */
struct test_fs_image test_fs_image_make_overflow(void)
{
    struct test_fs_image fixture = test_fs_image_make();
    uint8_t *fork = fixture.file_fork;
    test_image_put_be64(fork, 2u * 512u);
    test_image_put_be32(fork + 12, 2);
    test_image_put_be32(fork + 16, FILE_DATA_BLOCK);
    test_image_put_be32(fork + 20, 1);
    memset(test_image + OVERFLOW_DATA_BLOCK * 512, 'B', 512);

    uint8_t *extents_fork = test_image + 1024 + 192;
    test_image_put_be64(extents_fork, EXTENTS_TOTAL_NODES * EXTENTS_NODE_SIZE);
    test_image_put_be32(extents_fork + 12, EXTENTS_TOTAL_NODES);
    test_image_put_be32(extents_fork + 16, EXTENTS_BLOCK);
    test_image_put_be32(extents_fork + 20, EXTENTS_TOTAL_NODES);

    uint8_t *extents_header = test_image + EXTENTS_BLOCK * EXTENTS_NODE_SIZE;
    memset(extents_header, 0, EXTENTS_NODE_SIZE);
    extents_header[8] = 1;
    test_image_put_be16(extents_header + 10, 3);
    uint8_t *eh = extents_header + 14;
    test_image_put_be16(eh, 1);
    test_image_put_be32(eh + 2, 1);
    test_image_put_be32(eh + 6, 1);
    test_image_put_be32(eh + 10, 1);
    test_image_put_be32(eh + 14, 1);
    test_image_put_be16(eh + 18, EXTENTS_NODE_SIZE);
    test_image_put_be16(eh + 20, 10);
    test_image_put_be32(eh + 22, EXTENTS_TOTAL_NODES);
    test_image_put_be32(eh + 26, 0);
    test_image_put_be32(eh + 38, 6);
    eh[36] = 0;
    eh[37] = 0;
    test_image_put_be16(extents_header + 504, 504);
    test_image_put_be16(extents_header + 506, 248);
    test_image_put_be16(extents_header + 508, 120);
    test_image_put_be16(extents_header + 510, 14);

    uint8_t *extents_leaf =
        test_image + (EXTENTS_BLOCK + 1u) * EXTENTS_NODE_SIZE;
    memset(extents_leaf, 0, EXTENTS_NODE_SIZE);
    extents_leaf[8] = 0xff;
    extents_leaf[9] = 1;
    test_image_put_be16(extents_leaf + 10, 1);
    uint8_t *record = extents_leaf + 14;
    test_image_put_be16(record, 10);          /* key length */
    test_image_put_be16(record + 2, 0);       /* data fork */
    test_image_put_be32(record + 4, 4);       /* file id: current.img */
    test_image_put_be32(record + 8, 1);       /* logical start block */
    test_image_put_be32(record + 12, OVERFLOW_DATA_BLOCK);
    test_image_put_be32(record + 16, 1);      /* extent count */
    test_image_put_be16(extents_leaf + 510, 14);
    /* 2-byte key length, 10-byte key, eight 8-byte extent entries. */
    test_image_put_be16(extents_leaf + 508, 14 + 2 + 10 + 64);
    return fixture;
}
