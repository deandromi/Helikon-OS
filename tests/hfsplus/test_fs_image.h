/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_TEST_FS_IMAGE_H
#define HELIKON_TEST_FS_IMAGE_H

#include <stdint.h>

#define TEST_FS_NODE_SIZE 512u

struct test_fs_image {
    uint8_t *leaf;
    uint8_t *file_fork;
};

struct test_fs_image test_fs_image_make(void);

#endif
