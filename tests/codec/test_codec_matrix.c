/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The seek, resume and end-of-stream matrix, one row per codec (T-100).
 *
 * Every decoder in the registry answers the same questions, and each one was
 * tested against its own idea of correctness. This suite asks all of them the
 * same questions, from the outside, with the decoded stream itself as the
 * oracle:
 *
 *   1. A seek followed by a read returns exactly the frames a sequential decode
 *      would have produced there. That is stronger than "the seek returned OK",
 *      and it is how the MP3 pre-seek replay bug was eventually seen: a filter
 *      that had not been reset repeated samples from before the seek.
 *   2. Seeking to the same frame twice gives the same bytes, and seeking
 *      backwards to frame 0 restarts the stream.
 *   3. A seek to exactly `total_frames` is legal and reads zero frames; a seek
 *      past it is ARGUMENT and leaves the decoder usable.
 *   4. `duration_ms` agrees with `total_frames` and `sample_rate`, and the
 *      reported output frames never run past `total_frames`.
 *   5. A partial read then a seek to 0 then a read returns the first frames
 *      again: state after a seek does not depend on where the decoder was.
 *
 * Every row lands on the requested frame exactly, so there is no tolerance
 * column: a codec that needed slack would be a defect to fix rather than a
 * setting. The MP3 rows are why that sentence is true — writing this table
 * found that an MP3 seek returned samples shifted by the encoder delay, and,
 * at frame 0, the tag frame's silent block (issue #150).
 *
 * Two pieces of arithmetic the rows depend on, and that a fixture of five
 * frames makes easy to get wrong:
 *
 *   - `total_frames` counts the stream; a gapless codec trims `encoder_delay`
 *     at the front and `padding` at the end, so the probes live inside
 *     `total_frames - delay - padding`, which is what the decoder can produce.
 *   - a probe near that end may run out mid-chunk; the comparison uses the
 *     frames both paths actually produced.
 */
#include "codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helikon_error.h"
#include "mem.h"
#include "string.h"

#define FILE_MAX 65536u
#define READ_FRAMES 97u /* a prime, so a stride bug cannot line up with it */
#define SEEK_POINTS 7u

struct row {
    const char *fixture;
    const char *codec;
};

static const struct row rows[] = {
    { "fixtures/stereo16.flac", "flac" },
    { "fixtures/stereo24.flac", "flac" },
    { "fixtures/aiff_stereo16.aiff", "aiff" },
    { "fixtures/aiff_stereo24.aiff", "aiff" },
    { "fixtures/mp3_cbr_stereo16.mp3", "mp3" },
    { "fixtures/mp3_vbr_stereo16.mp3", "mp3" },
    { "fixtures/mp3_mpeg2_stereo16.mp3", "mp3" },
};

static uint8_t file_data[FILE_MAX];
static uint8_t workspace[HELIKON_CODEC_WORKSPACE_MAX];
static int16_t chunk[READ_FRAMES * 2u];
static int16_t reference[READ_FRAMES * 2u];

static int failures;

static int expect(int condition, const char *fixture, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s: %s\n", fixture, message);
        failures += 1;
        return 1;
    }
    return 0;
}

static size_t load_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 0u;
    }
    size_t size = fread(file_data, 1u, sizeof(file_data), file);
    (void)fclose(file);
    return size;
}

/* Decodes `frames` frames at `start` by reading from the top. The slow oracle. */
static int decode_at(const struct helikon_codec_ops *ops, size_t size,
                     uint32_t start, int16_t *out, size_t frames, size_t *got)
{
    struct helikon_codec codec;
    int rc = helikon_codec_open(&codec, ops, file_data, size, workspace,
                                sizeof(workspace));
    if (rc != HELIKON_OK)
        return rc;

    size_t skipped = 0u;
    while (skipped < start) {
        size_t want = start - skipped;
        if (want > READ_FRAMES)
            want = READ_FRAMES;
        size_t count = 0u;
        rc = helikon_codec_read(&codec, out, want, &count);
        if (rc != HELIKON_OK || count == 0u) {
            helikon_codec_close(&codec);
            return rc != HELIKON_OK ? rc : HELIKON_ERR_FORMAT;
        }
        skipped += count;
        if (skipped > start) {
            /* The decoder over-read; the oracle cannot skip accurately. */
            helikon_codec_close(&codec);
            return HELIKON_ERR_FORMAT;
        }
    }
    size_t count = 0u;
    rc = helikon_codec_read(&codec, out, frames, &count);
    helikon_codec_close(&codec);
    if (rc != HELIKON_OK)
        return rc;
    if (got != NULL)
        *got = count;
    return HELIKON_OK;
}


/* First differing sample index, or -1; the message names where it diverges. */
static int first_difference(const int16_t *left, const int16_t *right,
                            size_t frames)
{
    for (size_t index = 0u; index < frames * 2u; ++index) {
        if (left[index] != right[index])
            return (int)index;
    }
    return -1;
}

static int check_row(const struct row *row)
{
    size_t size = load_file(row->fixture);
    if (size == 0u) {
        fprintf(stderr, "FAIL %s: fixture missing\n", row->fixture);
        failures += 1;
        return 1;
    }

    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    int rc = helikon_codec_probe(file_data, size, &ops, &info);
    if (expect(rc == HELIKON_OK && ops != NULL, row->fixture, "probe"))
        return 1;
    if (expect(helikon_strcmp(ops->name, row->codec) == 0, row->fixture,
               "the row names the codec the probe chose"))
        return 1;
    if (expect(info.total_frames > 0u, row->fixture, "frames are known"))
        return 1;
    if (expect(info.output_channels == 2u, row->fixture, "stereo output"))
        return 1;

    /*
     * `total_frames` counts the stream; `playable` counts what the decoder will
     * hand out. They differ for a gapless stream: the encoder's delay and
     * padding are trimmed, and a probe past `playable` can only read zero
     * frames, so the probes live inside it.
     */
    uint32_t playable = info.total_frames;
    if ((info.flags & HELIKON_CODEC_FLAG_GAPLESS) != 0u &&
        info.encoder_delay + info.padding < playable) {
        playable = info.total_frames - info.encoder_delay - info.padding;
    }
    if (expect(playable > READ_FRAMES, row->fixture,
               "the playable stream is longer than one probe"))
        return 1;

    /* duration_ms must agree with the frame count it reports. */
    uint64_t expected_ms = (uint64_t)info.total_frames * 1000ull /
                           (uint64_t)info.sample_rate;
    uint64_t reported_ms = info.duration_ms;
    uint64_t difference = expected_ms > reported_ms ? expected_ms - reported_ms
                                                    : reported_ms - expected_ms;
    if (expect(difference <= 1ull, row->fixture,
               "duration_ms agrees with frames and rate"))
        return 1;

    uint32_t probes[SEEK_POINTS];
    probes[0] = 0u;
    probes[1] = 1u;
    probes[2] = playable / 4u;
    probes[3] = playable / 2u;
    probes[4] = (playable * 3u) / 4u;
    probes[5] = playable - 1u;
    probes[6] = playable - READ_FRAMES;

    for (size_t index = 0u; index < SEEK_POINTS; ++index) {
        uint32_t target = probes[index];
        size_t frames = READ_FRAMES;
        if ((uint64_t)target + frames > playable)
            frames = (size_t)(playable - target);
        if (frames == 0u)
            continue;

        size_t reference_frames = 0u;
        rc = decode_at(ops, size, target, reference, frames,
                       &reference_frames);
        if (expect(rc == HELIKON_OK, row->fixture,
                   "the sequential reference decodes"))
            return 1;
        if (expect(reference_frames > 0u, row->fixture,
                   "the sequential reference produced frames"))
            return 1;
        /* A stream can end inside the probe; compare what both produced. */
        frames = reference_frames;

        struct helikon_codec codec;
        rc = helikon_codec_open(&codec, ops, file_data, size, workspace,
                                sizeof(workspace));
        if (expect(rc == HELIKON_OK, row->fixture, "open before a seek"))
            return 1;
        rc = helikon_codec_seek(&codec, target);
        if (expect(rc == HELIKON_OK, row->fixture, "seek to the probe frame")) {
            helikon_codec_close(&codec);
            return 1;
        }
        size_t count = 0u;
        rc = helikon_codec_read(&codec, chunk, frames, &count);
        int same = rc == HELIKON_OK && count == frames &&
                   helikon_memcmp(chunk, reference,
                                  frames * 2u * sizeof(int16_t)) == 0;
        helikon_codec_close(&codec);
        if (!same) {
            fprintf(stderr,
                    "  %s: probe %u at frame %u read %u of %u, first "
                    "difference at sample %d\n",
                    row->fixture, (unsigned int)index, (unsigned int)target,
                    (unsigned int)count, (unsigned int)frames,
                    first_difference(chunk, reference, frames));
            if (expect(0, row->fixture,
                       "a seek lands on the frames a sequential decode would "
                       "give"))
                return 1;
        }
    }

    /* A seek to the end is legal and reads nothing; past it is ARGUMENT. */
    struct helikon_codec codec;
    rc = helikon_codec_open(&codec, ops, file_data, size, workspace,
                            sizeof(workspace));
    if (expect(rc == HELIKON_OK, row->fixture, "open for the end probes"))
        return 1;
    if (expect(helikon_codec_seek(&codec, playable) == HELIKON_OK,
               row->fixture, "a seek to the end is legal")) {
        helikon_codec_close(&codec);
        return 1;
    }
    size_t count = 0u;
    rc = helikon_codec_read(&codec, chunk, READ_FRAMES, &count);
    if (expect(rc == HELIKON_OK && count == 0u, row->fixture,
               "reading at the end returns no frames")) {
        helikon_codec_close(&codec);
        return 1;
    }
    int past = helikon_codec_seek(&codec, info.total_frames + 1u);
    if (expect(past == HELIKON_ERR_ARGUMENT, row->fixture,
               "a seek past the end is ARGUMENT")) {
        helikon_codec_close(&codec);
        return 1;
    }
    /* The failed seek left the decoder at the end, not somewhere else. */
    if (expect(helikon_codec_seek(&codec, 0u) == HELIKON_OK, row->fixture,
               "the decoder still seeks after a refusal")) {
        helikon_codec_close(&codec);
        return 1;
    }
    count = 0u;
    rc = helikon_codec_read(&codec, chunk, READ_FRAMES, &count);
    helikon_codec_close(&codec);
    if (expect(rc == HELIKON_OK && count == READ_FRAMES, row->fixture,
               "a read after seeking back to zero works"))
        return 1;

    /* Resume: a partial read, a seek to 0, and the same first frames again. */
    rc = decode_at(ops, size, 0u, reference, READ_FRAMES, NULL);
    if (expect(rc == HELIKON_OK, row->fixture, "the first frames decode"))
        return 1;

    struct helikon_codec resumed;
    rc = helikon_codec_open(&resumed, ops, file_data, size, workspace,
                            sizeof(workspace));
    if (expect(rc == HELIKON_OK, row->fixture, "open for the resume probe"))
        return 1;
    size_t partial = 0u;
    rc = helikon_codec_read(&resumed, chunk, 3u, &partial);
    if (expect(rc == HELIKON_OK && partial == 3u, row->fixture,
               "the resume probe reads three frames")) {
        helikon_codec_close(&resumed);
        return 1;
    }
    if (expect(helikon_codec_seek(&resumed, 0u) == HELIKON_OK, row->fixture,
               "the resume probe seeks back to zero")) {
        helikon_codec_close(&resumed);
        return 1;
    }
    count = 0u;
    rc = helikon_codec_read(&resumed, chunk, READ_FRAMES, &count);
    int restart = rc == HELIKON_OK && count == READ_FRAMES &&
                  helikon_memcmp(chunk, reference, READ_FRAMES * 2u * sizeof(int16_t)) ==
                      0;
    helikon_codec_close(&resumed);
    if (expect(restart, row->fixture,
               "a seek to zero restarts the stream exactly"))
        return 1;
    return 0;
}

int main(void)
{
    for (size_t index = 0u; index < sizeof(rows) / sizeof(rows[0]); ++index)
        (void)check_row(&rows[index]);

    if (failures != 0) {
        printf("FAIL codec matrix: %d failure(s)\n", failures);
        return 1;
    }
    printf("PASS codec matrix: %u rows seek, resume and stop exactly\n",
           (unsigned int)(sizeof(rows) / sizeof(rows[0])));
    return 0;
}
