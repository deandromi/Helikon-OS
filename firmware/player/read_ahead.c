/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "read_ahead.h"

#include <stddef.h>
#include <stdint.h>

#include "error.h"

#define READ_AHEAD_UNKNOWN_END UINT64_MAX

/*
 * One window fill. Returns the number of bytes now valid at buffer[0], or a
 * negative error. A short read is not end of stream: the downstream may return
 * fewer bytes than the window (fork boundary, bounce limit), so only a zero
 * read latches the end.
 */
static int read_ahead_fill(struct helikon_read_ahead *read_ahead,
                           uint64_t offset, size_t *filled)
{
    size_t want = read_ahead->capacity;
    if (read_ahead->end != READ_AHEAD_UNKNOWN_END) {
        if (offset >= read_ahead->end) {
            *filled = 0u;
            return HELIKON_OK;
        }
        const uint64_t available = read_ahead->end - offset;
        if (available < (uint64_t)want)
            want = (size_t)available;
    }

    size_t got = 0u;
    int rc = read_ahead->downstream->read_at(
        read_ahead->downstream->context, offset, read_ahead->buffer, want, &got);
    if (rc != HELIKON_OK)
        return rc;
    if (got > want)
        return HELIKON_ERR_FORMAT;
    if (got == 0u) {
        /* End of stream, latched: no later request asks again. */
        read_ahead->end = offset;
        *filled = 0u;
        return HELIKON_OK;
    }

    read_ahead->start = offset;
    read_ahead->length = got;
    read_ahead->stats.reads++;
    read_ahead->stats.refills++;
    read_ahead->stats.bytes_read += got;
    *filled = got;
    return HELIKON_OK;
}

/* Bytes the window can answer at `offset`, or 0 when the offset is outside it. */
static size_t read_ahead_available(const struct helikon_read_ahead *read_ahead,
                                   uint64_t offset)
{
    if (read_ahead->length == 0u)
        return 0u;
    if (offset < read_ahead->start)
        return 0u;
    const uint64_t delta = offset - read_ahead->start;
    if (delta >= (uint64_t)read_ahead->length)
        return 0u;
    return read_ahead->length - (size_t)delta;
}

static int read_ahead_direct(struct helikon_read_ahead *read_ahead,
                             uint64_t offset, void *buffer, size_t length,
                             size_t *read)
{
    if (read_ahead->end != READ_AHEAD_UNKNOWN_END) {
        if (offset >= read_ahead->end) {
            *read = 0u;
            return HELIKON_OK;
        }
        const uint64_t available = read_ahead->end - offset;
        if (available < (uint64_t)length)
            length = (size_t)available;
    }
    if (length == 0u) {
        *read = 0u;
        return HELIKON_OK;
    }

    size_t got = 0u;
    int rc = read_ahead->downstream->read_at(
        read_ahead->downstream->context, offset, buffer, length, &got);
    if (rc != HELIKON_OK)
        return rc;
    if (got > length)
        return HELIKON_ERR_FORMAT;
    if (got == 0u)
        read_ahead->end = offset;
    read_ahead->stats.reads++;
    read_ahead->stats.bytes_read += got;
    *read = got;
    return HELIKON_OK;
}

static int read_ahead_read_at(void *context, uint64_t offset, void *buffer,
                              size_t length, size_t *read)
{
    struct helikon_read_ahead *read_ahead = context;
    if (read_ahead == NULL || read == NULL ||
        (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;

    *read = 0u;
    if (length == 0u)
        return HELIKON_OK;

    /*
     * A request that fills or overruns the window pays for the window twice
     * (once into the buffer, once out of it), so it goes straight through. The
     * window survives: its bytes are still true.
     */
    if (read_ahead->capacity == 0u || length >= read_ahead->capacity) {
        read_ahead->stats.bypasses++;
        return read_ahead_direct(read_ahead, offset, buffer, length, read);
    }

    size_t filled = 0u;
    size_t available = read_ahead_available(read_ahead, offset);
    const int hit = available != 0u;
    const uint32_t refills_before = read_ahead->stats.refills;
    int rc = HELIKON_OK;

    while (filled < length) {
        if (available != 0u) {
            size_t take = length - filled;
            if (take > available)
                take = available;
            uint8_t *source = read_ahead->buffer +
                              (size_t)(offset + filled - read_ahead->start);
            for (size_t i = 0u; i < take; ++i)
                ((uint8_t *)buffer)[filled + i] = source[i];
            filled += take;
            available = 0u;
            continue;
        }

        size_t got = 0u;
        rc = read_ahead_fill(read_ahead, offset + filled, &got);
        if (rc != HELIKON_OK)
            return rc;
        if (got == 0u)
            break; /* end of stream */
        available = got;
    }

    /*
     * A hit is a request the window already held; a straddle is a request that
     * the window started and a refill finished. The refill counter tells them
     * apart without inferring anything from the byte counts.
     */
    if (hit) {
        if (read_ahead->stats.refills == refills_before)
            read_ahead->stats.hits++;
        else
            read_ahead->stats.partial++;
    }
    *read = filled;
    return HELIKON_OK;
}

int helikon_read_ahead_init(struct helikon_read_ahead *read_ahead,
                            uint8_t *buffer, size_t capacity,
                            const struct helikon_codec_source *downstream,
                            struct helikon_codec_source *source)
{
    if (read_ahead == NULL || downstream == NULL || source == NULL ||
        downstream->read_at == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (capacity != 0u && buffer == NULL)
        return HELIKON_ERR_ARGUMENT;

    read_ahead->downstream = downstream;
    read_ahead->buffer = buffer;
    read_ahead->capacity = capacity;
    read_ahead->start = 0u;
    read_ahead->length = 0u;
    read_ahead->end = downstream->size != 0u ? downstream->size
                                             : READ_AHEAD_UNKNOWN_END;
    read_ahead->stats.reads = 0u;
    read_ahead->stats.bytes_read = 0u;
    read_ahead->stats.hits = 0u;
    read_ahead->stats.partial = 0u;
    read_ahead->stats.bypasses = 0u;
    read_ahead->stats.refills = 0u;

    source->read_at = read_ahead_read_at;
    source->size = downstream->size;
    source->context = read_ahead;
    return HELIKON_OK;
}
