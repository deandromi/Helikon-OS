/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "inner_image.h"

#include "hfs_catalog.h"
#include "hfs_extents.h"

#include <string.h>

static int overlaps(uint64_t start, uint64_t end, uint64_t other_start,
                    uint64_t other_end)
{
    return start < other_end && other_start < end;
}

static int guard_range(struct inner_image *image, uint64_t start, uint64_t end)
{
    for (size_t i = 0; i < image->extent_count; ++i) {
        uint64_t extent_start = (uint64_t)image->extents[i].start *
                                image->outer_block_size;
        uint64_t extent_end = extent_start +
                              (uint64_t)image->extents[i].count *
                              image->outer_block_size;
        if (overlaps(extent_start, extent_end, start, end))
            return HFS_ERR_FORMAT;
    }
    return HFS_OK;
}

static int fork_inline_complete(const struct hfs_fork *fork)
{
    uint64_t blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i)
        blocks += fork->extents[i].count;
    return blocks == fork->blocks;
}

static int guard_fork(struct inner_image *image, const struct hfs_fork *fork)
{
    if (fork->blocks != 0 && !fork_inline_complete(fork))
        return HFS_ERR_UNSUPPORTED;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        if (fork->extents[i].count == 0)
            continue;
        uint64_t start = (uint64_t)fork->extents[i].start *
                         image->outer_block_size;
        uint64_t end = start + (uint64_t)fork->extents[i].count *
                       image->outer_block_size;
        int rc = guard_range(image, start, end);
        if (rc != HFS_OK)
            return rc;
    }
    return HFS_OK;
}

int inner_image_guard_metadata(struct inner_image *image,
                               const struct hfs_volume *outer)
{
    if (image == NULL || outer == NULL || image->extent_count == 0)
        return HFS_ERR_ARGUMENT;

    int rc = guard_range(image, 0, 2048u);
    if (rc != HFS_OK)
        return rc;
    rc = guard_range(image, outer->length - 1024u, outer->length);
    if (rc != HFS_OK)
        return rc;
    if (outer->journal_info_size != 0) {
        rc = guard_range(image, outer->journal_info_offset,
                         outer->journal_info_offset +
                             outer->journal_info_size);
        if (rc != HFS_OK)
            return rc;
    }
    if (outer->journal_size != 0) {
        rc = guard_range(image, outer->journal_offset,
                         outer->journal_offset + outer->journal_size);
        if (rc != HFS_OK)
            return rc;
    }

    const struct hfs_fork *forks[] = {
        &outer->catalog, &outer->extents, &outer->allocation,
        &outer->attributes_file, &outer->startup
    };
    for (size_t i = 0; i < sizeof(forks) / sizeof(forks[0]); ++i) {
        rc = guard_fork(image, forks[i]);
        if (rc != HFS_OK)
            return rc;
    }

    for (size_t i = 0; i < image->extent_count; ++i) {
        uint64_t start = (uint64_t)image->extents[i].start *
                         image->outer_block_size;
        uint64_t end = start + (uint64_t)image->extents[i].count *
                       image->outer_block_size;
        for (size_t j = i + 1u; j < image->extent_count; ++j) {
            uint64_t other_start = (uint64_t)image->extents[j].start *
                                   image->outer_block_size;
            uint64_t other_end = other_start +
                                 (uint64_t)image->extents[j].count *
                                 image->outer_block_size;
            if (overlaps(start, end, other_start, other_end))
                return HFS_ERR_FORMAT;
        }
    }
    return HFS_OK;
}

static int append_extent(struct inner_image *image, uint32_t start,
                         uint32_t count)
{
    if (start == 0 || count == 0 || count > UINT32_MAX - start)
        return HFS_ERR_FORMAT;
    if (image->extent_count >= HFS_INNER_EXTENT_MAX)
        return HFS_ERR_NO_SPACE;
    image->extents[image->extent_count].start = start;
    image->extents[image->extent_count].count = count;
    ++image->extent_count;
    return HFS_OK;
}

int inner_image_open(struct inner_image *image, struct hfs_volume *outer,
                     struct hfs_catalog *outer_catalog, const char *name,
                     hfs_write_fn outer_write, hfs_flush_fn outer_flush,
                     void *extents_scratch, size_t extents_scratch_size,
                     uint64_t minimum_size)
{
    if (image == NULL || outer == NULL || outer_catalog == NULL ||
        name == NULL || outer_write == NULL || extents_scratch == NULL)
        return HFS_ERR_ARGUMENT;

    memset(image, 0, sizeof(*image));
    image->outer_read = outer->read;
    image->outer_write = outer_write;
    image->outer_flush = outer_flush;
    image->outer_context = outer->context;
    image->outer_base = outer->base;
    image->outer_block_size = outer->block_size;

    struct hfs_entry entry;
    int rc = hfs_catalog_lookup_ascii(outer_catalog, 2, name, &entry);
    if (rc != HFS_OK)
        return rc;
    if (entry.kind != 2 || entry.unsupported)
        return HFS_ERR_UNSUPPORTED;
    if (entry.data.size < minimum_size || entry.data.blocks == 0)
        return HFS_ERR_BUFFER;

    uint32_t inline_blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        uint32_t start = entry.data.extents[i].start;
        uint32_t count = entry.data.extents[i].count;
        if (count == 0)
            continue;
        rc = append_extent(image, start, count);
        if (rc != HFS_OK)
            return rc;
        inline_blocks += count;
    }
    if (inline_blocks > entry.data.blocks)
        return HFS_ERR_FORMAT;

    if (entry.data.blocks > inline_blocks) {
        struct hfs_extents extents;
        rc = hfs_extents_open(&extents, outer, extents_scratch,
                              extents_scratch_size);
        if (rc != HFS_OK)
            return rc;
        struct hfs_extent overflow[HFS_INNER_EXTENT_MAX];
        size_t overflow_count = 0;
        rc = hfs_extents_collect(&extents, entry.id, inline_blocks,
                                 entry.data.blocks, overflow,
                                 HFS_INNER_EXTENT_MAX, &overflow_count);
        if (rc != HFS_OK)
            return rc;
        for (size_t i = 0; i < overflow_count; ++i) {
            rc = append_extent(image, overflow[i].start, overflow[i].count);
            if (rc != HFS_OK)
                return rc;
        }
    }

    uint64_t blocks = 0;
    uint64_t logical = 0;
    for (size_t i = 0; i < image->extent_count; ++i) {
        struct hfs_extent *extent = &image->extents[i];
        if (extent->start >= outer->blocks ||
            extent->count > outer->blocks - extent->start)
            return HFS_ERR_FORMAT;
        blocks += extent->count;
        image->logical[i] = logical;
        logical += (uint64_t)extent->count * outer->block_size;
    }
    if (blocks != entry.data.blocks || logical < entry.data.size)
        return HFS_ERR_FORMAT;

    image->size = entry.data.size;
    return HFS_OK;
}

static int translate(struct inner_image *image, uint64_t offset,
                     size_t length, uint64_t *physical, size_t *chunk)
{
    if (image == NULL || physical == NULL || chunk == NULL || length == 0)
        return HFS_ERR_ARGUMENT;
    if (offset >= image->size)
        return HFS_ERR_FORMAT;
    if (length > image->size - offset)
        length = (size_t)(image->size - offset);

    for (size_t i = 0; i < image->extent_count; ++i) {
        uint64_t bytes = (uint64_t)image->extents[i].count *
                         image->outer_block_size;
        if (offset >= image->logical[i] &&
            offset < image->logical[i] + bytes) {
            uint64_t within = offset - image->logical[i];
            uint64_t available = bytes - within;
            *chunk = available < length ? (size_t)available : length;
            *physical = image->outer_base +
                        (uint64_t)image->extents[i].start *
                            image->outer_block_size + within;
            return HFS_OK;
        }
    }
    return HFS_ERR_FORMAT;
}

int inner_image_read(void *context, uint64_t offset, void *buffer,
                     size_t length)
{
    struct inner_image *image = context;
    if (image == NULL || image->outer_read == NULL ||
        (buffer == NULL && length != 0))
        return HFS_ERR_ARGUMENT;
    uint8_t *out = buffer;
    while (length != 0) {
        uint64_t physical = 0;
        size_t chunk = 0;
        int rc = translate(image, offset, length, &physical, &chunk);
        if (rc != HFS_OK)
            return rc;
        if (image->outer_read(image->outer_context, physical, out, chunk))
            return HFS_ERR_IO;
        out += chunk;
        offset += chunk;
        length -= chunk;
    }
    return HFS_OK;
}

int inner_image_write(void *context, uint64_t offset, const void *buffer,
                      size_t length)
{
    struct inner_image *image = context;
    if (image == NULL || image->outer_write == NULL ||
        (buffer == NULL && length != 0))
        return HFS_ERR_READ_ONLY;
    const uint8_t *in = buffer;
    while (length != 0) {
        uint64_t physical = 0;
        size_t chunk = 0;
        int rc = translate(image, offset, length, &physical, &chunk);
        if (rc != HFS_OK)
            return rc;
        if (image->outer_write(image->outer_context, physical, in, chunk))
            return HFS_ERR_IO;
        ++image->writes;
        in += chunk;
        offset += chunk;
        length -= chunk;
    }
    return HFS_OK;
}

int inner_image_flush(void *context)
{
    struct inner_image *image = context;
    if (image == NULL)
        return HFS_ERR_ARGUMENT;
    if (image->outer_flush == NULL)
        return HFS_OK;
    return image->outer_flush(image->outer_context) ? HFS_ERR_IO : HFS_OK;
}
