/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Endian helpers for on-disk and register values.
 *
 * These are byte-access readers/writers; they do not assume alignment. HFS+
 * values are big-endian, the manifest, boot-info and ATA identify data are
 * little-endian. Both directions are provided explicitly.
 */
#ifndef HELIKON_LIB_ENDIAN_H
#define HELIKON_LIB_ENDIAN_H

#include <stdint.h>

static inline uint16_t helikon_be16(const void *data)
{
    const uint8_t *p = data;
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t helikon_be32(const void *data)
{
    const uint8_t *p = data;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint64_t helikon_be64(const void *data)
{
    const uint8_t *p = data;
    return ((uint64_t)helikon_be32(p) << 32) | helikon_be32(p + 4);
}

static inline uint16_t helikon_le16(const void *data)
{
    const uint8_t *p = data;
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t helikon_le32(const void *data)
{
    const uint8_t *p = data;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t helikon_le64(const void *data)
{
    const uint8_t *p = data;
    return (uint64_t)helikon_le32(p) |
           ((uint64_t)helikon_le32(p + 4) << 32);
}

static inline void helikon_put_be16(void *data, uint16_t value)
{
    uint8_t *p = data;
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static inline void helikon_put_be32(void *data, uint32_t value)
{
    uint8_t *p = data;
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static inline void helikon_put_be64(void *data, uint64_t value)
{
    uint8_t *p = data;
    helikon_put_be32(p, (uint32_t)(value >> 32));
    helikon_put_be32(p + 4, (uint32_t)value);
}

static inline void helikon_put_le16(void *data, uint16_t value)
{
    uint8_t *p = data;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static inline void helikon_put_le32(void *data, uint32_t value)
{
    uint8_t *p = data;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static inline void helikon_put_le64(void *data, uint64_t value)
{
    uint8_t *p = data;
    helikon_put_le32(p, (uint32_t)value);
    helikon_put_le32(p + 4, (uint32_t)(value >> 32));
}

#endif
