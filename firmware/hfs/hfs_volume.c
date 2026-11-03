/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HFS+ volume header and journal probe.
 *
 * Derived from the read-only HFS+ parser in HFSPlus Rockboxed iPod
 * (firmware/hfsplus/hfsplus_ro.c, GPL-2.0-or-later). This module intentionally
 * stops before catalog B-tree parsing; it is the first host-testable slice of
 * the Helikon-OS read core.
 */
#include "hfs_volume.h"

#include <string.h>

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static uint32_t j32(const uint8_t *p, int little)
{
    if (!little)
        return be32(p);
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | p[0];
}

static uint64_t j64(const uint8_t *p, int little)
{
    if (!little)
        return be64(p);
    return ((uint64_t)j32(p + 4, 1) << 32) | j32(p, 1);
}

static int power2(uint32_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

static int read_volume(const struct hfs_volume *volume, uint64_t offset,
                       void *buffer, size_t length)
{
    if (offset > volume->length || length > volume->length - offset)
        return HFS_ERR_FORMAT;
    return volume->read(volume->context, volume->base + offset, buffer, length)
               ? HFS_ERR_IO
               : HFS_OK;
}

int hfs_fork_parse(const struct hfs_volume *volume, const uint8_t *p,
                   struct hfs_fork *fork)
{
    uint64_t sum = 0;
    int ended = 0;

    fork->size = be64(p);
    fork->blocks = be32(p + 12);
    if (fork->blocks > volume->blocks ||
        fork->size > (uint64_t)fork->blocks * volume->block_size)
        return HFS_ERR_FORMAT;

    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        struct hfs_extent *extent = &fork->extents[i];
        extent->start = be32(p + 16 + i * 8);
        extent->count = be32(p + 20 + i * 8);

        if (extent->count == 0) {
            if (extent->start != 0)
                return HFS_ERR_FORMAT;
            ended = 1;
            continue;
        }
        if (ended || extent->start == 0 || extent->start >= volume->blocks ||
            extent->count > volume->blocks - extent->start)
            return HFS_ERR_FORMAT;

        for (unsigned int j = 0; j < i; ++j) {
            const struct hfs_extent *old = &fork->extents[j];
            if (extent->start < (uint64_t)old->start + old->count &&
                old->start < (uint64_t)extent->start + extent->count)
                return HFS_ERR_FORMAT;
        }
        sum += extent->count;
    }

    /* A data fork may continue in the extents-overflow tree. The volume
     * metadata forks themselves must stay inline, which later mount checks
     * enforce. */
    if (sum > fork->blocks)
        return HFS_ERR_FORMAT;
    return HFS_OK;
}

static int fork_inline(const struct hfs_fork *fork)
{
    uint64_t sum = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        sum += fork->extents[i].count;
    return sum == fork->blocks;
}

int hfs_fork_read(const struct hfs_volume *volume,
                  const struct hfs_fork *fork, uint64_t offset,
                  void *buffer, size_t length)
{
    if (volume == NULL || fork == NULL ||
        (buffer == NULL && length != 0))
        return HFS_ERR_ARGUMENT;
    if (offset > fork->size || length > fork->size - offset)
        return HFS_ERR_FORMAT;
    if (!fork_inline(fork))
        return HFS_ERR_UNSUPPORTED;

    uint8_t *out = buffer;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT && length != 0; ++i) {
        const struct hfs_extent *extent = &fork->extents[i];
        uint64_t extent_bytes = (uint64_t)extent->count * volume->block_size;
        if (offset >= extent_bytes) {
            offset -= extent_bytes;
            continue;
        }

        size_t chunk = length;
        if (extent_bytes - offset < chunk)
            chunk = (size_t)(extent_bytes - offset);
        int rc = read_volume(volume,
                             (uint64_t)extent->start * volume->block_size +
                                 offset,
                             out, chunk);
        if (rc != HFS_OK)
            return rc;
        out += chunk;
        length -= chunk;
        offset = 0;
    }

    return length == 0 ? HFS_OK : HFS_ERR_FORMAT;
}

static int check_journal(struct hfs_volume *volume, uint32_t info_block)
{
    uint8_t info[52];
    uint8_t header[44];

    if (info_block == 0 || info_block >= volume->blocks)
        return HFS_ERR_FORMAT;

    int rc = read_volume(volume, (uint64_t)info_block * volume->block_size,
                         info, sizeof(info));
    if (rc != HFS_OK)
        return rc;

    uint32_t flags = be32(info);
    /* kJIJournalInFSMask=1, kJIJournalOnOtherDeviceMask=2,
     * kJIJournalNeedInitMask=4. */
    if ((flags & 2u) != 0 || (flags & 1u) == 0)
        return HFS_ERR_UNSUPPORTED;
    if ((flags & 4u) != 0)
        return HFS_ERR_DIRTY;

    uint64_t offset = be64(info + 36);
    uint64_t size = be64(info + 44);
    if (size < 1024 || offset < 1536 || offset % 512 != 0 ||
        size % 512 != 0 || offset > volume->length ||
        size > volume->length - offset)
        return HFS_ERR_FORMAT;

    rc = read_volume(volume, offset, header, sizeof(header));
    if (rc != HFS_OK)
        return rc;

    int little = be32(header + 4) == 0x78563412u;
    if (j32(header + 4, little) != 0x12345678u ||
        j32(header, little) != 0x4a4e4c78u)
        return HFS_ERR_FORMAT;

    uint64_t start = j64(header + 8, little);
    uint64_t end = j64(header + 16, little);
    uint32_t header_size = j32(header + 40, little);
    uint32_t list_size = j32(header + 32, little);
    if (!power2(header_size) || header_size < 512 ||
        header_size > HFS_MAX_NODE_SIZE || size <= header_size ||
        j64(header + 24, little) != size || list_size < 32 ||
        list_size > size - header_size || start < header_size ||
        start >= size || end < header_size || end >= size ||
        start % header_size != 0 || end % header_size != 0)
        return HFS_ERR_FORMAT;

    uint32_t expected = j32(header + 36, little);
    uint32_t sum = 0;
    memset(header + 36, 0, 4);
    for (size_t i = 0; i < sizeof(header); ++i)
        sum = (sum << 8) ^ (sum + header[i]);
    if (~sum != expected)
        return HFS_ERR_FORMAT;

    volume->journal_info_offset = (uint64_t)info_block * volume->block_size;
    volume->journal_info_size = volume->block_size;
    volume->journal_offset = offset;
    volume->journal_size = size;
    return start == end ? HFS_OK : HFS_ERR_DIRTY;
}

static void volume_reset(struct hfs_volume *volume, hfs_read_fn read,
                         void *context, uint64_t base, uint64_t length)
{
    memset(volume, 0, sizeof(*volume));
    volume->read = read;
    volume->context = context;
    volume->base = base;
    volume->length = length;
}

static int parse_header(struct hfs_volume *volume, const uint8_t *header,
                        uint64_t partition_length, int recovery)
{
    uint16_t signature = be16(header);

    if (signature == 0x4858u) /* HFSX */
        return HFS_ERR_UNSUPPORTED;
    if (signature != 0x482bu || be16(header + 2) != 4)
        return HFS_ERR_NOT_HFS;

    volume->attributes = be32(header + 4);
    volume->modify_date = be32(header + 20);
    volume->write_count = be32(header + 68);
    volume->block_size = be32(header + 40);
    volume->blocks = be32(header + 44);
    volume->free_blocks = be32(header + 48);

    if (!power2(volume->block_size) || volume->block_size < 512 ||
        volume->blocks == 0 || volume->free_blocks > volume->blocks ||
        (uint64_t)volume->blocks * volume->block_size > partition_length)
        return HFS_ERR_FORMAT;

    volume->length = (uint64_t)volume->blocks * volume->block_size;
    volume->recovery = recovery ? 1 : 0;

    if ((volume->attributes & (1u << 8)) == 0 ||
        (volume->attributes & (1u << 11)) != 0)
        return HFS_ERR_DIRTY;

    if ((volume->attributes & (1u << 13)) != 0) {
        if (be32(header + 8) != 0x4846534au) /* HFSJ */
            return HFS_ERR_UNSUPPORTED;
        volume->journaled = 1;
        int rc = check_journal(volume, be32(header + 12));
        if (rc != HFS_OK)
            return rc;
    }

    static const size_t fork_offsets[] = {
        112, 192, 272, 352, 432
    };
    struct hfs_fork *forks[] = {
        &volume->allocation, &volume->extents, &volume->catalog,
        &volume->attributes_file, &volume->startup
    };

    for (size_t i = 0; i < sizeof(forks) / sizeof(forks[0]); ++i) {
        int rc = hfs_fork_parse(volume, header + fork_offsets[i], forks[i]);
        if (rc != HFS_OK)
            return rc;
    }

    return HFS_OK;
}

int hfs_volume_probe(struct hfs_volume *volume, hfs_read_fn read,
                     void *context, uint64_t base, uint64_t length)
{
    uint8_t primary[HFS_VOLUME_HEADER_SIZE];
    uint8_t alternate[HFS_VOLUME_HEADER_SIZE];

    if (volume == NULL || read == NULL || length < 2048 ||
        base > UINT64_MAX - length)
        return HFS_ERR_ARGUMENT;

    volume_reset(volume, read, context, base, length);

    int primary_rc = read_volume(volume, HFS_VOLUME_HEADER_OFFSET, primary,
                                 sizeof(primary));
    int rc = primary_rc;

    if (primary_rc == HFS_OK) {
        uint16_t signature = be16(primary);
        if (signature == 0x4858u)
            return HFS_ERR_UNSUPPORTED;
        if (signature == 0x482bu && be16(primary + 2) == 4) {
            rc = parse_header(volume, primary, length, 0);
            if (rc == HFS_OK)
                return HFS_OK;
            if (rc != HFS_ERR_NOT_HFS && rc != HFS_ERR_FORMAT &&
                rc != HFS_ERR_IO)
                return rc;
        } else {
            rc = HFS_ERR_NOT_HFS;
        }
    }

    volume_reset(volume, read, context, base, length);

    int alternate_rc = read_volume(volume, length - 1024, alternate,
                                   sizeof(alternate));
    if (alternate_rc == HFS_OK && be16(alternate) == 0x482bu &&
        be16(alternate + 2) == 4) {
        int alt = parse_header(volume, alternate, length, 1);
        if (alt == HFS_OK)
            return HFS_OK;
        if (alt != HFS_ERR_NOT_HFS)
            rc = alt;
    }

    volume->length = 0;
    return rc;
}

const char *hfs_strerror(int error)
{
    switch (error) {
    case HFS_OK: return "ok";
    case HFS_ERR_IO: return "I/O error";
    case HFS_ERR_NOT_HFS: return "not an HFS+ volume";
    case HFS_ERR_FORMAT: return "invalid HFS+ structure";
    case HFS_ERR_UNSUPPORTED: return "unsupported HFS+ feature";
    case HFS_ERR_DIRTY: return "volume is dirty or needs journal replay";
    case HFS_ERR_ARGUMENT: return "invalid argument";
    case HFS_ERR_BUFFER: return "buffer too small";
    case HFS_ERR_NOT_FOUND: return "entry not found";
    case HFS_ERR_NOT_DIR: return "not a directory";
    case HFS_ERR_IS_DIR: return "is a directory";
    case HFS_ERR_NAME_INVALID: return "invalid HFS+ name";
    case HFS_ERR_NO_SPACE: return "no space or transaction too large";
    case HFS_ERR_READ_ONLY: return "volume is read-only";
    case HFS_ERR_BUSY: return "writer is busy";
    case HFS_ERR_POWER: return "below the write power gate";
    case HFS_ERR_NOT_EMPTY: return "directory is not empty";
    default: return "unknown HFS+ error";
    }
}
