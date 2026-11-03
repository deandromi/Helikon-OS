/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_TEST_IMAGE_H
#define HELIKON_TEST_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define TEST_IMAGE_SIZE 65536u

#define TEST_ATTR_UNMOUNTED (1u << 8)
#define TEST_ATTR_INCONSISTENT (1u << 11)
#define TEST_ATTR_JOURNALED (1u << 13)

extern uint8_t test_image[TEST_IMAGE_SIZE];

void test_image_clear(void);
int test_image_read(void *context, uint64_t offset, void *buffer,
                    size_t length);
void test_image_put_be16(uint8_t *p, uint16_t value);
void test_image_put_be32(uint8_t *p, uint32_t value);
void test_image_put_be64(uint8_t *p, uint64_t value);
void test_image_make_header(uint8_t *header, uint16_t signature,
                            uint16_t version, uint32_t attributes,
                            uint32_t block_size, uint32_t blocks,
                            uint32_t free_blocks);

#endif
