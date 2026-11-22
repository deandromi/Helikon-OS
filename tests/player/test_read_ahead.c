/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host tests for the read-ahead window (T-058).
 *
 * A single downstream read must serve many decoder pulls, a window must survive
 * a backward seek, and a request too large to amortise must go straight to the
 * media. The tests read the counters rather than timing anything: the point is
 * the number of storage reads, which is what the device pays for.
 */
#include "read_ahead.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "helikon_error.h"

static unsigned int failures;

static int expect(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        failures += 1u;
    }
    return condition ? 0 : 1;
}

/* Deterministic filler, so a wrong offset is visible as wrong bytes. */
static uint8_t pattern(uint64_t index)
{
    uint32_t value = (uint32_t)index * 2654435761u + 0x9e3779b9u;
    return (uint8_t)(value >> 19);
}

struct fake_downstream {
    uint8_t *data;
    size_t size;
    size_t chunk;      /* 0 = return what was asked */
    size_t fail_from;  /* offset at which to report an I/O error, ~0 = never */
    unsigned int reads;
    uint64_t bytes;
    size_t largest;
};

static int fake_read_at(void *context, uint64_t offset, void *buffer,
                        size_t length, size_t *read)
{
    struct fake_downstream *media = context;
    if (media == NULL || read == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;

    *read = 0u;
    media->reads += 1u;
    if (length > media->largest)
        media->largest = length;
    if (media->fail_from != (size_t)-1 &&
        offset + (uint64_t)length > (uint64_t)media->fail_from)
        return HELIKON_ERR_IO;
    if (offset >= media->size)
        return HELIKON_OK;

    size_t available = media->size - (size_t)offset;
    size_t count = available < length ? available : length;
    if (media->chunk != 0u && count > media->chunk)
        count = media->chunk;
    if (count != 0u)
        memcpy(buffer, media->data + (size_t)offset, count);
    media->bytes += count;
    *read = count;
    return HELIKON_OK;
}

static void fill_media(uint8_t *data, size_t size)
{
    for (size_t index = 0u; index < size; ++index)
        data[index] = pattern(index);
}

/* Reads through the wrapper and checks the bytes really are the file's. */
static int expect_read(struct helikon_codec_source *source, uint64_t offset,
                       size_t length, const char *message)
{
    static uint8_t buffer[65536];
    size_t read = 0u;
    int rc = source->read_at(source->context, offset, buffer, length, &read);
    if (expect(rc == HELIKON_OK, message))
        return 1;
    if (expect(read == length, message))
        return 1;
    for (size_t index = 0u; index < length; ++index) {
        if (buffer[index] != pattern(offset + index))
            return expect(0, message);
    }
    return 0;
}

static int check_sequential(void)
{
    static uint8_t data[65536];
    static uint8_t window[4096];
    struct fake_downstream media;
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;

    fill_media(data, sizeof(data));
    memset(&media, 0, sizeof(media));
    media.data = data;
    media.size = sizeof(data);
    media.fail_from = (size_t)-1;

    struct helikon_codec_source downstream;
    downstream.read_at = fake_read_at;
    downstream.size = media.size;
    downstream.context = &media;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_OK,
               "init accepts a wrapped source"))
        return 1;
    if (expect(source.size == media.size, "size passes through"))
        return 1;

    /*
     * Sixty-four 1 KiB pulls over 64 KiB: sixteen 4 KiB windows, so sixteen
     * storage reads instead of sixty-four, and every pull after a window's
     * first is a hit.
     */
    for (unsigned int index = 0u; index < 64u; ++index) {
        if (expect_read(&source, (uint64_t)index * 1024u, 1024u,
                        "sequential pull returns the file's bytes"))
            return 1;
    }
    if (expect(media.reads == 16u, "64 pulls of 1 KiB cost 16 window reads"))
        return 1;
    if (expect(read_ahead.stats.refills == 16u, "sixteen windows were filled"))
        return 1;
    if (expect(read_ahead.stats.hits == 48u,
               "every pull inside a filled window is a hit"))
        return 1;
    if (expect(read_ahead.stats.partial == 0u,
               "no sequential pull straddles a window"))
        return 1;
    if (expect(media.largest == sizeof(window),
               "a window is filled with one large request"))
        return 1;
    if (expect(media.bytes == sizeof(data),
               "no byte is fetched twice in a forward pass"))
        return 1;
    return 0;
}

static int check_backward_seek(void)
{
    static uint8_t data[32768];
    static uint8_t window[4096];
    struct fake_downstream media;
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;

    fill_media(data, sizeof(data));
    memset(&media, 0, sizeof(media));
    media.data = data;
    media.size = sizeof(data);
    media.fail_from = (size_t)-1;

    struct helikon_codec_source downstream;
    downstream.read_at = fake_read_at;
    downstream.size = media.size;
    downstream.context = &media;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_OK,
               "seek init"))
        return 1;

    if (expect_read(&source, 0u, 512u, "first window fill"))
        return 1;
    unsigned int reads = media.reads;
    if (expect(reads == 1u, "one read for the first window"))
        return 1;

    /* Back into the same window: a decoder's reservoir re-read costs nothing. */
    if (expect_read(&source, 2048u, 1024u,
                    "a backward read inside the window returns the file"))
        return 1;
    if (expect(media.reads == reads,
               "a read inside the window costs no storage access"))
        return 1;
    if (expect(read_ahead.stats.hits == 1u, "a hit is counted"))
        return 1;
    return 0;
}

static int check_bypass(void)
{
    static uint8_t data[32768];
    static uint8_t window[4096];
    struct fake_downstream media;
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;

    fill_media(data, sizeof(data));
    memset(&media, 0, sizeof(media));
    media.data = data;
    media.size = sizeof(data);
    media.fail_from = (size_t)-1;

    struct helikon_codec_source downstream;
    downstream.read_at = fake_read_at;
    downstream.size = media.size;
    downstream.context = &media;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_OK,
               "bypass init"))
        return 1;

    if (expect_read(&source, 0u, 2048u, "warm the window"))
        return 1;
    unsigned int reads = media.reads;

    /* A whole-window request is not worth a copy in and a copy out. */
    if (expect_read(&source, 4096u, sizeof(window),
                    "a window-sized request returns the file"))
        return 1;
    if (expect(media.reads == reads + 1u, "a window-sized request reads once"))
        return 1;
    if (expect(read_ahead.stats.bypasses == 1u, "the bypass is counted"))
        return 1;

    /* The bypass must not have thrown the window away. */
    if (expect_read(&source, 1024u, 512u, "the window survives a bypass"))
        return 1;
    if (expect(media.reads == reads + 1u,
               "the window survives a bypass without a new read"))
        return 1;
    return 0;
}

static int check_straddle(void)
{
    static uint8_t data[16384];
    static uint8_t window[4096];
    struct fake_downstream media;
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;

    fill_media(data, sizeof(data));
    memset(&media, 0, sizeof(media));
    media.data = data;
    media.size = sizeof(data);
    media.fail_from = (size_t)-1;

    struct helikon_codec_source downstream;
    downstream.read_at = fake_read_at;
    downstream.size = media.size;
    downstream.context = &media;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_OK,
               "straddle init"))
        return 1;

    if (expect_read(&source, 0u, 3000u, "fill most of the window"))
        return 1;
    /* 3000..5000 crosses the 4096 boundary: part cached, part refilled. */
    if (expect_read(&source, 3000u, 2000u,
                    "a straddling read returns contiguous bytes"))
        return 1;
    if (expect(read_ahead.stats.partial == 1u,
               "the straddling read is counted as partial"))
        return 1;
    if (expect(media.reads == 2u, "the straddle costs one refill"))
        return 1;
    return 0;
}

static int check_end_of_file(void)
{
    static uint8_t data[5000];
    static uint8_t window[4096];
    struct fake_downstream media;
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;
    static uint8_t buffer[8192];

    fill_media(data, sizeof(data));
    memset(&media, 0, sizeof(media));
    media.data = data;
    media.size = sizeof(data);
    media.fail_from = (size_t)-1;

    struct helikon_codec_source downstream;
    downstream.read_at = fake_read_at;
    downstream.size = media.size;
    downstream.context = &media;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_OK,
               "eof init"))
        return 1;

    size_t read = 0u;
    if (expect(source.read_at(source.context, 4000u, buffer, 4000u, &read) ==
                   HELIKON_OK &&
                   read == 1000u,
               "a read past the end returns what exists"))
        return 1;
    unsigned int reads = media.reads;
    if (expect(source.read_at(source.context, 5000u, buffer, 4095u, &read) ==
                   HELIKON_OK &&
                   read == 0u,
               "a read at the end returns nothing"))
        return 1;
    if (expect(media.reads == reads, "the end is latched, not re-asked"))
        return 1;
    if (expect(source.read_at(source.context, 0u, buffer, 4095u, &read) ==
                   HELIKON_OK &&
                   read == 4095u,
               "a read before the end still works after the latch"))
        return 1;
    return 0;
}

static int check_unknown_size(void)
{
    static uint8_t data[3000];
    static uint8_t window[1024];
    struct fake_downstream media;
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;
    static uint8_t buffer[1024];

    fill_media(data, sizeof(data));
    memset(&media, 0, sizeof(media));
    media.data = data;
    media.size = sizeof(data);
    media.fail_from = (size_t)-1;

    struct helikon_codec_source downstream;
    downstream.read_at = fake_read_at;
    downstream.size = 0u; /* the media cannot report a length */
    downstream.context = &media;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_OK,
               "unknown-size init"))
        return 1;
    if (expect(source.size == 0u, "an unknown size stays unknown"))
        return 1;

    uint64_t offset = 0u;
    size_t total = 0u;
    for (;;) {
        size_t read = 0u;
        if (expect(source.read_at(source.context, offset, buffer,
                                  sizeof(buffer), &read) == HELIKON_OK,
                   "unknown-size pull reports no error"))
            return 1;
        if (read == 0u)
            break;
        for (size_t index = 0u; index < read; ++index) {
            if (buffer[index] != pattern(offset + index))
                return expect(0, "unknown-size pull returns the file");
        }
        offset += read;
        total += read;
        if (total > sizeof(data))
            return expect(0, "unknown-size pull stops at the end");
    }
    if (expect(total == sizeof(data), "the whole file is read once"))
        return 1;
    if (expect(media.reads <= 8u, "the window keeps the read count small"))
        return 1;
    return 0;
}

static int check_refusals(void)
{
    static uint8_t data[4096];
    static uint8_t window[512];
    struct fake_downstream media;
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;
    struct helikon_codec_source downstream;
    static uint8_t buffer[256];

    fill_media(data, sizeof(data));
    memset(&media, 0, sizeof(media));
    media.data = data;
    media.size = sizeof(data);
    media.fail_from = (size_t)-1;

    downstream.read_at = fake_read_at;
    downstream.size = media.size;
    downstream.context = &media;

    if (expect(helikon_read_ahead_init(NULL, window, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_ERR_ARGUMENT,
               "init refuses a null read-ahead"))
        return 1;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       NULL,
                                       &source) == HELIKON_ERR_ARGUMENT,
               "init refuses a null downstream"))
        return 1;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       &downstream,
                                       NULL) == HELIKON_ERR_ARGUMENT,
               "init refuses a null source"))
        return 1;
    if (expect(helikon_read_ahead_init(&read_ahead, NULL, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_ERR_ARGUMENT,
               "init refuses a window with no buffer"))
        return 1;

    /* No buffer is legal: the wrapper becomes a pass-through. */
    if (expect(helikon_read_ahead_init(&read_ahead, NULL, 0u, &downstream,
                                       &source) == HELIKON_OK,
               "init accepts a pass-through"))
        return 1;
    size_t read = 0u;
    if (expect(source.read_at(source.context, 0u, buffer, sizeof(buffer),
                              &read) == HELIKON_OK &&
                   read == sizeof(buffer),
               "a pass-through still returns bytes"))
        return 1;
    if (expect(media.reads == 1u &&
                   read_ahead.stats.bypasses == 1u,
               "a pass-through counts as a bypass"))
        return 1;
    if (expect(source.read_at(source.context, 0u, NULL, 0u, &read) ==
                   HELIKON_OK &&
                   read == 0u,
               "a zero-length read is not an error"))
        return 1;
    if (expect(source.read_at(source.context, 0u, buffer, sizeof(buffer),
                              NULL) == HELIKON_ERR_ARGUMENT,
               "a null length pointer is refused"))
        return 1;
    if (expect(source.read_at(source.context, 0u, NULL, 4u, &read) ==
                   HELIKON_ERR_ARGUMENT,
               "a null buffer with a length is refused"))
        return 1;
    return 0;
}

static int check_error_passthrough(void)
{
    static uint8_t data[8192];
    static uint8_t window[1024];
    struct fake_downstream media;
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;
    static uint8_t buffer[512];

    fill_media(data, sizeof(data));
    memset(&media, 0, sizeof(media));
    media.data = data;
    media.size = sizeof(data);
    media.fail_from = 2048u;

    struct helikon_codec_source downstream;
    downstream.read_at = fake_read_at;
    downstream.size = media.size;
    downstream.context = &media;
    if (expect(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                       &downstream,
                                       &source) == HELIKON_OK,
               "error init"))
        return 1;

    size_t read = 0u;
    /* The refill starts at 1500 and runs past 2048, where the media fails: the
     * error must reach the decoder unchanged, not become a short read that the
     * decoder would treat as a truncated file. */
    if (expect(source.read_at(source.context, 1500u, buffer, sizeof(buffer),
                              &read) == HELIKON_ERR_IO,
               "a media error is not swallowed"))
        return 1;
    if (expect(read_ahead.stats.refills == 0u,
               "a failed fill leaves no partial window behind"))
        return 1;
    return 0;
}

int main(void)
{
    if (check_sequential() || check_backward_seek() || check_bypass() ||
        check_straddle() || check_end_of_file() || check_unknown_size() ||
        check_refusals() || check_error_passthrough())
        return 1;

    if (failures != 0u) {
        printf("FAIL read ahead\n");
        return 1;
    }
    printf("PASS read ahead\n");
    return 0;
}
