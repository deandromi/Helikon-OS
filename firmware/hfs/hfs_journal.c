/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Apple HFS+ journal access.
 *
 * The journal is the safety-critical part of the writer. This module only
 * understands the on-disk journal format: it validates the journal header and
 * block-list headers, appends one committed transaction, and replays committed
 * transactions idempotently. Catalog/bitmap/extents mutation lives elsewhere.
 */
#include "hfs_journal.h"

#include <string.h>

#define HFS_JOURNAL_MAGIC 0x4a4e4c78u
#define HFS_JOURNAL_ENDIAN 0x12345678u
#define HFS_BLOCK_INFO_SIZE 16u
#define HFS_BLOCK_LIST_HEADER 16u
#define HFS_BLOCK_FIRST_HEADER 0x0002u

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint16_t get16(const uint8_t *p, int little)
{
    return little ? (uint16_t)(((uint16_t)p[1] << 8) | p[0]) : be16(p);
}

static void put16(uint8_t *p, uint16_t value, int little)
{
    if (little) {
        p[0] = (uint8_t)value;
        p[1] = (uint8_t)(value >> 8);
    } else {
        p[0] = (uint8_t)(value >> 8);
        p[1] = (uint8_t)value;
    }
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

static uint32_t le32(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) | p[0];
}

static uint64_t get64(const uint8_t *p, int little)
{
    return little ? ((uint64_t)le32(p + 4) << 32) | le32(p)
                  : be64(p);
}

static uint32_t get32(const uint8_t *p, int little)
{
    return little ? le32(p) : be32(p);
}

static void put32(uint8_t *p, uint32_t value, int little)
{
    if (little) {
        p[0] = (uint8_t)value;
        p[1] = (uint8_t)(value >> 8);
        p[2] = (uint8_t)(value >> 16);
        p[3] = (uint8_t)(value >> 24);
    } else {
        p[0] = (uint8_t)(value >> 24);
        p[1] = (uint8_t)(value >> 16);
        p[2] = (uint8_t)(value >> 8);
        p[3] = (uint8_t)value;
    }
}

static void put64(uint8_t *p, uint64_t value, int little)
{
    if (little) {
        put32(p, (uint32_t)value, 1);
        put32(p + 4, (uint32_t)(value >> 32), 1);
    } else {
        put32(p, (uint32_t)(value >> 32), 0);
        put32(p + 4, (uint32_t)value, 0);
    }
}

static uint32_t journal_checksum(const uint8_t *p, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < length; ++i)
        sum = (sum << 8) ^ (sum + p[i]);
    return ~sum;
}

static int power2(uint32_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

static uint64_t journal_wrap(const struct hfs_journal *journal, uint64_t offset)
{
    uint64_t span = journal->journal_size - journal->sector_size;
    uint64_t relative = offset - journal->sector_size;
    return journal->sector_size + relative % span;
}

static int journal_read(struct hfs_journal *journal, uint64_t offset,
                        void *buffer, size_t length)
{
    uint8_t *dest = buffer;
    if (journal->read == NULL)
        return HFS_ERR_ARGUMENT;
    if (offset < journal->sector_size || offset >= journal->journal_size ||
        length > journal->journal_size - journal->sector_size)
        return HFS_ERR_FORMAT;

    if (offset + length <= journal->journal_size)
        return journal->read(journal->context,
                             journal->journal_base + offset, dest, length)
                   ? HFS_ERR_IO : HFS_OK;

    size_t first = (size_t)(journal->journal_size - offset);
    if (journal->read(journal->context, journal->journal_base + offset,
                      dest, first))
        return HFS_ERR_IO;
    if (journal->read(journal->context,
                      journal->journal_base + journal->sector_size,
                      dest + first, length - first))
        return HFS_ERR_IO;
    return HFS_OK;
}

static int journal_write(struct hfs_journal *journal, uint64_t offset,
                         const void *buffer, size_t length)
{
    const uint8_t *src = buffer;
    if (journal->write == NULL)
        return HFS_ERR_READ_ONLY;
    if (offset < journal->sector_size || offset >= journal->journal_size ||
        length > journal->journal_size - journal->sector_size)
        return HFS_ERR_FORMAT;

    if (offset + length <= journal->journal_size)
        return journal->write(journal->context,
                              journal->journal_base + offset, src, length)
                   ? HFS_ERR_IO : HFS_OK;

    size_t first = (size_t)(journal->journal_size - offset);
    if (journal->write(journal->context, journal->journal_base + offset,
                       src, first))
        return HFS_ERR_IO;
    if (journal->write(journal->context,
                       journal->journal_base + journal->sector_size,
                       src + first, length - first))
        return HFS_ERR_IO;
    return HFS_OK;
}

static int write_header(struct hfs_journal *journal)
{
    static uint8_t header[HFS_JOURNAL_MAX_SECTOR];
    if (journal->sector_size > sizeof(header))
        return HFS_ERR_UNSUPPORTED;

    memset(header, 0, journal->sector_size);
    put32(header, HFS_JOURNAL_MAGIC, journal->little_endian);
    put32(header + 4, HFS_JOURNAL_ENDIAN, journal->little_endian);
    put64(header + 8, journal->start, journal->little_endian);
    put64(header + 16, journal->end, journal->little_endian);
    put64(header + 24, journal->journal_size, journal->little_endian);
    put32(header + 32, journal->block_list_size, journal->little_endian);
    put32(header + 36, 0, journal->little_endian);
    put32(header + 40, journal->sector_size, journal->little_endian);

    uint32_t checksum = journal_checksum(header, HFS_JOURNAL_HEADER_SIZE);
    put32(header + 36, checksum, journal->little_endian);

    if (journal->write == NULL)
        return HFS_ERR_READ_ONLY;
    if (journal->write(journal->context, journal->journal_base, header,
                       journal->sector_size))
        return HFS_ERR_IO;
    if (journal->flush != NULL && journal->flush(journal->context))
        return HFS_ERR_IO;
    return HFS_OK;
}

int hfs_journal_open(struct hfs_journal *journal,
                     const struct hfs_volume *volume,
                     hfs_write_fn write, hfs_flush_fn flush)
{
    static uint8_t header[HFS_JOURNAL_MAX_SECTOR];
    if (journal == NULL || volume == NULL || volume->read == NULL)
        return HFS_ERR_ARGUMENT;
    if (volume->journal_uninitialized != 0u)
        return HFS_ERR_UNSUPPORTED;
    if (volume->journal_offset < 1536u || volume->journal_size < 1024u ||
        volume->journal_offset > volume->length ||
        volume->journal_size > volume->length - volume->journal_offset)
        return HFS_ERR_NOT_HFS;

    memset(journal, 0, sizeof(*journal));
    journal->read = volume->read;
    journal->write = write;
    journal->flush = flush;
    journal->context = volume->context;
    journal->volume_base = volume->base;
    journal->volume_length = volume->length;
    journal->journal_offset = volume->journal_offset;
    journal->journal_size = volume->journal_size;
    journal->journal_base = volume->base + volume->journal_offset;

    if (journal->read(journal->context, journal->journal_base, header,
                      HFS_JOURNAL_HEADER_SIZE))
        return HFS_ERR_IO;

    uint32_t endian = be32(header + 4);
    if (endian == HFS_JOURNAL_ENDIAN) {
        journal->little_endian = 0;
    } else if (endian == 0x78563412u) {
        journal->little_endian = 1;
    } else {
        return HFS_ERR_FORMAT;
    }

    if (get32(header, journal->little_endian) != HFS_JOURNAL_MAGIC ||
        get32(header + 4, journal->little_endian) != HFS_JOURNAL_ENDIAN)
        return HFS_ERR_FORMAT;

    uint64_t start = get64(header + 8, journal->little_endian);
    uint64_t end = get64(header + 16, journal->little_endian);
    uint64_t size = get64(header + 24, journal->little_endian);
    uint32_t blhdr = get32(header + 32, journal->little_endian);
    uint32_t expected = get32(header + 36, journal->little_endian);
    uint32_t jhdr = get32(header + 40, journal->little_endian);

    if (!power2(jhdr) || jhdr < 512u || jhdr > HFS_JOURNAL_MAX_SECTOR ||
        size != journal->journal_size || size <= jhdr ||
        size % jhdr != 0 || blhdr < 32u ||
        blhdr > HFS_JOURNAL_MAX_BLOCK_LIST || blhdr > size - jhdr ||
        start < jhdr || start >= size || end < jhdr || end >= size ||
        start % jhdr != 0 || end % jhdr != 0)
        return HFS_ERR_FORMAT;

    header[36] = 0;
    header[37] = 0;
    header[38] = 0;
    header[39] = 0;
    if (journal_checksum(header, HFS_JOURNAL_HEADER_SIZE) != expected)
        return HFS_ERR_FORMAT;

    journal->sector_size = jhdr;
    journal->block_list_size = blhdr;
    journal->start = start;
    journal->end = end;
    journal->dirty = start != end;
    return HFS_OK;
}

int hfs_journal_is_clean(const struct hfs_journal *journal)
{
    return journal != NULL && !journal->dirty && journal->start == journal->end;
}

static int replay_one_block_list(struct hfs_journal *journal, uint64_t *offset,
                                 uint64_t *next_offset, int *last)
{
    static uint8_t header[HFS_JOURNAL_MAX_BLOCK_LIST];
    uint8_t bounce[512];
    int rc = journal_read(journal, *offset, header, journal->block_list_size);
    if (rc != HFS_OK)
        return rc;

    uint16_t num_blocks = get16(header + 2, journal->little_endian);
    uint32_t bytes_used = get32(header + 4, journal->little_endian);
    uint32_t expected = get32(header + 8, journal->little_endian);
    if (num_blocks < 1u ||
        HFS_BLOCK_LIST_HEADER + (size_t)num_blocks * HFS_BLOCK_INFO_SIZE >
            journal->block_list_size ||
        bytes_used < journal->block_list_size ||
        bytes_used > journal->journal_size - journal->sector_size)
        return HFS_ERR_FORMAT;

    put32(header + 8, 0, 0);
    if (journal_checksum(header, 32) != expected)
        return HFS_ERR_FORMAT;

    uint64_t data_offset = journal_wrap(journal,
                                        *offset + journal->block_list_size);

    uint32_t copied = 0;
    for (uint16_t i = 1; i < num_blocks; ++i) {
        const uint8_t *info = header + HFS_BLOCK_LIST_HEADER +
                              (size_t)i * HFS_BLOCK_INFO_SIZE;
        uint64_t bnum = get64(info, journal->little_endian);
        uint32_t bsize = get32(info + 8, journal->little_endian);

        if (bsize == 0 || bsize % journal->sector_size != 0)
            return HFS_ERR_FORMAT;
        if (bnum != UINT64_MAX) {
            uint64_t target = bnum * journal->sector_size;
            if (bnum > journal->volume_length / journal->sector_size ||
                (uint64_t)bsize > journal->volume_length - target)
                return HFS_ERR_FORMAT;
        }
        if (bsize > UINT32_MAX - copied)
            return HFS_ERR_FORMAT;

        uint32_t done = 0;
        while (done < bsize) {
            uint32_t chunk = bsize - done;
            if (chunk > sizeof(bounce))
                chunk = sizeof(bounce);
            if (journal_read(journal, data_offset, bounce, chunk) != HFS_OK)
                return HFS_ERR_IO;
            if (bnum != UINT64_MAX) {
                uint64_t target = journal->volume_base + bnum *
                                  journal->sector_size + done;
                if (journal->write == NULL)
                    return HFS_ERR_READ_ONLY;
                if (journal->write(journal->context, target, bounce, chunk))
                    return HFS_ERR_IO;
            }
            done += chunk;
            data_offset = journal_wrap(journal, data_offset + chunk);
        }
        copied += bsize;
    }

    if (bytes_used != journal->block_list_size + copied)
        return HFS_ERR_FORMAT;

    *last = get32(header + 28, journal->little_endian) == 0;
    *next_offset = data_offset;
    *offset = data_offset;
    return HFS_OK;
}

int hfs_journal_replay(struct hfs_journal *journal)
{
    if (journal == NULL || journal->read == NULL)
        return HFS_ERR_ARGUMENT;
    if (journal->start == journal->end) {
        journal->dirty = 0;
        return HFS_OK;
    }
    if (journal->write == NULL)
        return HFS_ERR_READ_ONLY;

    uint64_t offset = journal->start;
    size_t guard = (size_t)(journal->journal_size / journal->sector_size) + 1u;
    int last = 0;
    while (offset != journal->end) {
        if (guard-- == 0)
            return HFS_ERR_FORMAT;
        uint64_t next = offset;
        int rc = replay_one_block_list(journal, &offset, &next, &last);
        if (rc != HFS_OK)
            return rc;
        (void)last;
    }

    if (journal->flush != NULL && journal->flush(journal->context))
        return HFS_ERR_IO;

    uint64_t old_start = journal->start;
    journal->start = journal->end;
    journal->dirty = 0;
    int rc = write_header(journal);
    if (rc != HFS_OK) {
        journal->start = old_start;
        journal->dirty = 1;
    }
    return rc;
}

int hfs_journal_append(struct hfs_journal *journal,
                       const struct hfs_journal_entry *entries, size_t count)
{
    static uint8_t header[HFS_JOURNAL_MAX_BLOCK_LIST];
    if (journal == NULL || entries == NULL || count == 0)
        return HFS_ERR_ARGUMENT;
    if (journal->write == NULL || journal->flush == NULL)
        return HFS_ERR_READ_ONLY;
    if (journal->dirty || journal->start != journal->end)
        return HFS_ERR_DIRTY;

    if (HFS_BLOCK_LIST_HEADER + (count + 1u) * HFS_BLOCK_INFO_SIZE >
        journal->block_list_size)
        return HFS_ERR_NO_SPACE;

    uint32_t data_bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        const struct hfs_journal_entry *entry = &entries[i];
        if (entry->data == NULL || entry->size == 0 ||
            entry->size % journal->sector_size != 0)
            return HFS_ERR_ARGUMENT;
        uint64_t sectors = entry->size / journal->sector_size;
        if (entry->sector > journal->volume_length / journal->sector_size ||
            sectors > journal->volume_length / journal->sector_size -
                          entry->sector)
            return HFS_ERR_ARGUMENT;
        if (entry->size > UINT32_MAX - data_bytes)
            return HFS_ERR_NO_SPACE;
        data_bytes += entry->size;
    }

    uint64_t bytes_used = journal->block_list_size + (uint64_t)data_bytes;
    if (bytes_used > journal->journal_size - journal->sector_size)
        return HFS_ERR_NO_SPACE;

    uint64_t new_end = journal_wrap(journal, journal->end + bytes_used);
    if (new_end == journal->start)
        return HFS_ERR_NO_SPACE;

    uint32_t max_blocks = journal->block_list_size / HFS_BLOCK_INFO_SIZE - 1u;
    if (max_blocks == 0 || max_blocks > UINT16_MAX ||
        count + 1u > max_blocks)
        return HFS_ERR_NO_SPACE;

    memset(header, 0, journal->block_list_size);
    put16(header, (uint16_t)max_blocks, journal->little_endian);
    put16(header + 2, (uint16_t)(count + 1u), journal->little_endian);
    put32(header + 4, (uint32_t)bytes_used, journal->little_endian);
    put32(header + 8, 0, journal->little_endian);
    put32(header + 12, HFS_BLOCK_FIRST_HEADER, journal->little_endian);
    /* First binfo is the continuation marker; next = 0 ends the transaction. */
    put64(header + 16, 0, journal->little_endian);
    put32(header + 24, 0, journal->little_endian);
    put32(header + 28, 0, journal->little_endian);
    for (size_t i = 0; i < count; ++i) {
        uint8_t *info = header + HFS_BLOCK_LIST_HEADER +
                        (i + 1u) * HFS_BLOCK_INFO_SIZE;
        put64(info, entries[i].sector, journal->little_endian);
        put32(info + 8, entries[i].size, journal->little_endian);
        put32(info + 12, 0, journal->little_endian);
    }
    put32(header + 8, journal_checksum(header, 32),
          journal->little_endian);

    int rc = journal_write(journal, journal->end, header,
                           journal->block_list_size);
    if (rc != HFS_OK)
        return rc;

    uint64_t data_offset = journal_wrap(journal,
                                        journal->end +
                                        journal->block_list_size);
    for (size_t i = 0; i < count; ++i) {
        rc = journal_write(journal, data_offset, entries[i].data,
                           entries[i].size);
        if (rc != HFS_OK)
            return rc;
        data_offset = journal_wrap(journal, data_offset + entries[i].size);
    }

    if (journal->flush(journal->context))
        return HFS_ERR_IO;

    uint64_t old_end = journal->end;
    journal->end = new_end;
    journal->dirty = 1;
    rc = write_header(journal);
    if (rc != HFS_OK) {
        journal->end = old_end;
        journal->dirty = 0;
    }
    return rc;
}
