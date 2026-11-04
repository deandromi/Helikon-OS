/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "crc32.h"

uint32_t helikon_crc32_update(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *bytes = data;

    while (length-- != 0u) {
        crc ^= *bytes++;
        for (unsigned int bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

uint32_t helikon_crc32(const void *data, size_t length)
{
    return ~helikon_crc32_update(0xffffffffu, data, length);
}
