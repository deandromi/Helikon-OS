/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CRC-32, reflected polynomial 0xEDB88320, initial value 0xFFFFFFFF and final
 * inversion. This is the standard IEEE/zlib CRC used by the boot-info page
 * (ADR-0004) and the GPT scanner.
 */
#ifndef HELIKON_LIB_CRC32_H
#define HELIKON_LIB_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t helikon_crc32_update(uint32_t crc, const void *data, size_t length);
uint32_t helikon_crc32(const void *data, size_t length);

#endif
