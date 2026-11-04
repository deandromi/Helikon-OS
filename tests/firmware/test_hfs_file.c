/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hfs_file.h"

#include "test_check.h"
#include "test_fs_image.h"
#include "test_image.h"

#include <string.h>

int main(void)
{
    test_fs_image_make();

    struct hfs_volume volume;
    CHECK_EQ(HFS_OK, hfs_volume_probe(&volume, test_image_read, NULL, 0,
                                      TEST_IMAGE_SIZE));
    uint8_t scratch[512];
    struct hfs_catalog catalog;
    CHECK_EQ(HFS_OK, hfs_catalog_open(&catalog, &volume, scratch,
                                      sizeof(scratch)));

    uint8_t data[8];
    size_t done = 0;
    CHECK_EQ(HFS_OK, helikon_hfs_read_file(&catalog, NULL,
                                           "/.firmware/current.img", 0, data,
                                           4, &done));
    CHECK_EQ(4, done);
    CHECK(memcmp(data, "HELO", 4) == 0);

    CHECK_EQ(HFS_OK, helikon_hfs_read_file(&catalog, NULL,
                                           "/.firmware/current.img", 2, data,
                                           2, &done));
    CHECK_EQ(2, done);
    CHECK(memcmp(data, "LO", 2) == 0);

    CHECK_EQ(HFS_ERR_NOT_FOUND,
             helikon_hfs_read_file(&catalog, NULL, "/.firmware/missing.img",
                                   0, data, sizeof(data), &done));
    CHECK_EQ(HFS_ERR_ARGUMENT,
             helikon_hfs_read_file(&catalog, NULL,
                                   "/.firmware/current.img", 3, data,
                                   sizeof(data), &done));
    CHECK_EQ(HFS_ERR_UNSUPPORTED,
             helikon_hfs_read_file(&catalog, NULL, "/.firmware", 0, data,
                                   sizeof(data), &done));

    return helikon_test_report("hfs-file");
}
