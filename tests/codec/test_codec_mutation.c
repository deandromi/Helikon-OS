/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Mutation sweep over the decoders.
 *
 * The codec tests decode known-good fixtures and compare PCM. This one does the
 * opposite: it corrupts a valid stream at random and drives the whole decode
 * path over it -
 *
 *   helikon_codec_probe            (memory)   -> open -> read -> seek -> close
 *   helikon_codec_probe_source     (streaming) -> open_source -> read -> seek
 *
 * Nothing is asserted about the *audio*: a corrupted stream may decode to noise,
 * stop early or be refused. What must hold is that the decoder stays inside its
 * workspace and its PCM buffer, terminates, and does not trip the address or
 * undefined-behaviour sanitizer. A decoder is the one place in the firmware that
 * turns bytes from a stranger's file into arithmetic, so a read past the end of
 * a buffer here is the bug class worth the most.
 *
 * The sweep is deterministic. A failure prints the fixture, the iteration, the
 * seed and every byte that was changed, which is enough to replay it:
 *
 *   ./test_codec_mutation             # the committed sweep
 *   ./test_codec_mutation 7 20000     # another seed, more iterations
 *
 * FLAC and MP3 are both swept, because they fail differently: FLAC parses its
 * own bitstream, MP3 runs through libmad's fixed-point synthesis.
 */
#include "codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helikon_error.h"
#include "mem.h"

#define FILE_MAX 65536u
#define CHUNK_FRAMES 257u
/* A mutated header may claim a huge stream; the sweep stops after this many. */
#define READ_BUDGET 256u
#define MAX_MUTATIONS 8u
#define DEFAULT_ITERATIONS 600u

struct mutation {
    uint32_t offset;
    uint32_t width;
    uint32_t value;
};

struct memory_media {
    const uint8_t *data;
    uint64_t size;
};

static uint8_t pristine[FILE_MAX];
static uint8_t work[FILE_MAX];
static uint8_t workspace[HELIKON_CODEC_WORKSPACE_MAX];
static int16_t pcm[CHUNK_FRAMES * 2u];

static struct mutation mutations[MAX_MUTATIONS];
static uint32_t mutation_count;
static uint32_t rng_state;
/* How often a pass got as far as an open decoder: a sweep that only ever trips
 * the probe proves nothing, so these are printed and bounded at the end. */
static uint32_t opened_count;
static uint32_t source_opened_count;

/* Same generator as the HFS+ mutation sweep: portable and deterministic. */
static uint32_t next_random(void)
{
    uint32_t value = rng_state;
    if (value == 0u)
        value = 0x2545f491u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    rng_state = value;
    return value;
}

static const uint32_t widths[] = { 1u, 1u, 2u, 4u };

static const uint32_t extreme_values[] = {
    0x00u, 0x01u, 0x7fu, 0x80u, 0xffu,
    0x0000u, 0x0001u, 0x7fffu, 0x8000u, 0xfffeu, 0xffffu,
    0x0000ffffu, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu,
};

/*
 * With MUTATION_TRACE=1 every pass prints its mutation first. A sanitizer abort
 * kills the process before it can report anything, so the last marker is the
 * reproducer: it names the fixture, the seed and every byte that was changed.
 */
static void trace(const char *fixture, uint32_t seed, uint32_t iteration)
{
    static int enabled = -1;
    if (enabled < 0)
        enabled = getenv("MUTATION_TRACE") != NULL ? 1 : 0;
    if (enabled == 0)
        return;
    fprintf(stderr, "%s iteration %u seed %u:", fixture, iteration, seed);
    for (uint32_t i = 0; i < mutation_count; ++i)
        fprintf(stderr, " %u@%u=0x%0*x", mutations[i].width,
                mutations[i].offset, (int)(mutations[i].width * 2u),
                mutations[i].value);
    fputc('\n', stderr);
}

static size_t load(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "  FAIL cannot open %s\n", path);
        return 0u;
    }
    size_t size = fread(pristine, 1u, sizeof(pristine), file);
    (void)fclose(file);
    return size;
}

/*
 * A media whose bytes come from the work buffer. An honest source: it reports
 * only what it has, so the decoder's short-read handling is exercised rather
 * than its error handling.
 */
static int memory_read_at(void *context, uint64_t offset, void *buffer,
                          size_t length, size_t *read)
{
    const struct memory_media *media = context;
    if (media == NULL || read == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    *read = 0u;
    if (offset > media->size)
        return HELIKON_ERR_FORMAT;
    uint64_t available = media->size - offset;
    size_t count = available < length ? (size_t)available : length;
    if (count != 0u)
        helikon_memcpy(buffer, media->data + (size_t)offset, count);
    *read = count;
    return HELIKON_OK;
}

/* Read the whole stream in chunks and a few seeks, within a hard budget. */
static int drain(struct helikon_codec *codec, const struct helikon_codec_info *info)
{
    size_t frames = 0;
    uint32_t budget = READ_BUDGET;
    while (budget-- != 0u) {
        int rc = helikon_codec_read(codec, pcm, CHUNK_FRAMES, &frames);
        if (rc != HELIKON_OK || frames == 0u)
            break;
    }

    static const uint32_t targets[] = { 0u, 1u, 2u };
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        uint32_t frame = targets[i];
        if (info->total_frames > targets[i])
            frame = info->total_frames - targets[i];
        (void)helikon_codec_seek(codec, frame);
        (void)helikon_codec_read(codec, pcm, CHUNK_FRAMES, &frames);
    }
    /* One past the end: a seek there must fail or clamp, never run wild. */
    (void)helikon_codec_seek(codec, info->total_frames + 1u);
    (void)helikon_codec_read(codec, pcm, CHUNK_FRAMES, &frames);
    return 0;
}

static void exercise(const char *fixture, size_t size)
{
    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;

    if (helikon_codec_probe(work, size, &ops, &info) == HELIKON_OK &&
        ops != NULL) {
        struct helikon_codec codec;
        if (helikon_codec_open(&codec, ops, work, size, workspace,
                               sizeof(workspace)) == HELIKON_OK) {
            ++opened_count;
            if (info.total_frames == 0u)
                info = codec.info;
            (void)drain(&codec, &info);
            helikon_codec_close(&codec);
        }
    }

    /*
     * The streaming path: the same bytes behind read_at, so the decoder sees
     * only the probe prefix plus whatever it pulls. That is what the player
     * actually uses.
     */
    struct memory_media media = { work, size };
    struct helikon_codec_source source;
    source.read_at = memory_read_at;
    source.size = size;
    source.context = &media;

    ops = NULL;
    struct helikon_codec_info source_info;
    if (helikon_codec_probe_source(&source, &ops, &source_info) == HELIKON_OK &&
        ops != NULL) {
        struct helikon_codec codec;
        if (helikon_codec_open_source(&codec, ops, &source, workspace,
                                      sizeof(workspace)) == HELIKON_OK) {
            ++source_opened_count;
            (void)drain(&codec, &codec.info);
            helikon_codec_close(&codec);
        }
    }
    (void)fixture;
}

/*
 * A canonical 16-bit stereo WAV, built in memory: RIFF/WAVE, one `fmt ` chunk
 * and one `data` chunk. The WAV walker is a parser like the others and gets the
 * same treatment.
 */
static size_t make_wav(uint8_t *data, size_t frames)
{
    size_t data_bytes = frames * 4u;
    size_t riff_size = 36u + data_bytes;
    helikon_memset(data, 0, 44u + data_bytes);
    helikon_memcpy(data, "RIFF", 4u);
    data[4] = (uint8_t)(riff_size & 0xffu);
    data[5] = (uint8_t)((riff_size >> 8) & 0xffu);
    data[6] = (uint8_t)((riff_size >> 16) & 0xffu);
    data[7] = (uint8_t)(riff_size >> 24);
    helikon_memcpy(data + 8u, "WAVE", 4u);
    helikon_memcpy(data + 12u, "fmt ", 4u);
    data[16] = 16u;
    data[20] = 1u;
    data[22] = 2u;
    data[24] = 0x44u;
    data[25] = 0xacu;
    data[28] = 0x10u;
    data[29] = 0xb1u;
    data[30] = 0x02u;
    data[32] = 4u;
    data[34] = 16u;
    helikon_memcpy(data + 36u, "data", 4u);
    data[40] = (uint8_t)(data_bytes & 0xffu);
    data[41] = (uint8_t)((data_bytes >> 8) & 0xffu);
    data[42] = (uint8_t)((data_bytes >> 16) & 0xffu);
    data[43] = (uint8_t)(data_bytes >> 24);
    for (size_t frame = 0u; frame < frames; ++frame) {
        int16_t left = (int16_t)(frame * 3u + 1u);
        int16_t right = (int16_t)(frame * 5u + 2u);
        data[44u + frame * 4u] = (uint8_t)(left & 0xff);
        data[45u + frame * 4u] = (uint8_t)((uint16_t)left >> 8);
        data[46u + frame * 4u] = (uint8_t)(right & 0xff);
        data[47u + frame * 4u] = (uint8_t)((uint16_t)right >> 8);
    }
    return 44u + data_bytes;
}

static int sweep_buffer(const char *name, size_t figure_size,
                        uint32_t iterations, uint32_t seed)
{

    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
        helikon_memcpy(work, pristine, figure_size);
        mutation_count = 1u + next_random() % MAX_MUTATIONS;
        for (uint32_t i = 0; i < mutation_count; ++i) {
            struct mutation *m = &mutations[i];
            m->width = widths[next_random() % 4u];
            /*
             * Half the hits land in the first 256 bytes, where every format
             * keeps its header: sample rate, block size, channel count and the
             * frame sizes a decoder trusts.
             */
            if ((next_random() % 2u) == 0u)
                m->offset = next_random() % 256u;
            else
                m->offset = next_random() % (uint32_t)figure_size;
            m->offset &= ~(m->width - 1u);
            if (m->offset > figure_size - m->width)
                m->offset = (uint32_t)(figure_size - m->width);
            if ((next_random() % 2u) == 0u)
                m->value = extreme_values[next_random() %
                                          (sizeof(extreme_values) /
                                           sizeof(extreme_values[0]))];
            else
                m->value = next_random();
            for (uint32_t b = 0; b < m->width; ++b)
                work[m->offset + b] =
                    (uint8_t)(m->value >> (8u * (m->width - 1u - b)));
        }

        /*
         * A truncated file is the other half of the story: the last frame of a
         * stream is usually cut short, and that is where a decoder reads past
         * its input.
         */
        size_t size = figure_size;
        if ((next_random() % 4u) == 0u)
            size = 1u + next_random() % (uint32_t)figure_size;

        trace(name, seed, iteration);
        exercise(name, size);
    }
    return 0;
}

static int sweep(const char *path, uint32_t iterations, uint32_t seed)
{
    size_t figure_size = load(path);
    if (figure_size == 0u)
        return 1;
    return sweep_buffer(path, figure_size, iterations, seed);
}

static int sweep_wav(uint32_t iterations, uint32_t seed)
{
    size_t figure_size = make_wav(pristine, 512u);
    helikon_memcpy(work, pristine, figure_size);
    return sweep_buffer("built-in WAV", figure_size, iterations, seed);
}

int main(int argc, char **argv)
{
    uint32_t seed = 0x434f4445u; /* "CODE" */
    uint32_t iterations = DEFAULT_ITERATIONS;
    if (argc > 1)
        seed = (uint32_t)strtoul(argv[1], NULL, 0);
    if (argc > 2)
        iterations = (uint32_t)strtoul(argv[2], NULL, 0);

    static const char *const fixtures[] = {
        "fixtures/stereo16.flac",
        "fixtures/constant16.flac",
        "fixtures/mp3_cbr_stereo16.mp3",
        "fixtures/mp3_vbr_stereo16.mp3",
        "fixtures/mp3_id3_stereo16.mp3",
    };

    rng_state = seed;
    for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i) {
        if (sweep(fixtures[i], iterations, seed) != 0)
            return 1;
    }
    if (sweep_wav(iterations, seed) != 0)
        return 1;

    printf("  codec mutation: %u iterations over %u fixtures and a built-in "
           "WAV, %u opened, %u streaming opens\n",
           iterations, (unsigned int)(sizeof(fixtures) / sizeof(fixtures[0])),
           opened_count, source_opened_count);
    if (opened_count == 0u || source_opened_count == 0u) {
        fprintf(stderr, "  FAIL the sweep never reached an open decoder\n");
        return 1;
    }
    return 0;
}
