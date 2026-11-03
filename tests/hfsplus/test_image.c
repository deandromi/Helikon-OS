/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "test_image.h"

#include <string.h>

uint8_t test_image[TEST_IMAGE_SIZE] __attribute__((aligned(16)));

void test_image_clear(void)
{
    memset(test_image, 0, sizeof(test_image));
}

int test_image_read(void *context, uint64_t offset, void *buffer,
                    size_t length)
{
    (void)context;
    if (offset > TEST_IMAGE_SIZE || length > TEST_IMAGE_SIZE - offset)
        return 1;
    memcpy(buffer, test_image + (size_t)offset, length);
    return 0;
}

void test_image_put_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

void test_image_put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

void test_image_put_be64(uint8_t *p, uint64_t value)
{
    test_image_put_be32(p, (uint32_t)(value >> 32));
    test_image_put_be32(p + 4, (uint32_t)value);
}

void test_image_make_header(uint8_t *header, uint16_t signature,
                            uint16_t version, uint32_t attributes,
                            uint32_t block_size, uint32_t blocks,
                            uint32_t free_blocks)
{
    test_image_put_be16(header, signature);
    test_image_put_be16(header + 2, version);
    test_image_put_be32(header + 4, attributes);
    test_image_put_be32(header + 40, block_size);
    test_image_put_be32(header + 44, blocks);
    test_image_put_be32(header + 48, free_blocks);
    test_image_put_be32(header + 68, 1);
}
