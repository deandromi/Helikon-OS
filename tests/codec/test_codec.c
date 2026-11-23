/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "codec.h"
#include "flac_decoder.h"
#include "id3v2.h"
#include "vorbis.h"
#include "mp3.h"

#include <stdio.h>
#include <string.h>

#include "helikon_error.h"
#include "mem.h"

#define FILE_MAX 65536u
#define FRAME_MAX 8192u
#define CHUNK_FRAMES 257u

static uint8_t file_data[FILE_MAX];
static uint8_t expected_data[FILE_MAX];
static uint8_t workspace[HELIKON_CODEC_WORKSPACE_MAX];
static int16_t pcm[FRAME_MAX * 2u];

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", message);
        return 1;
    }
    return 0;
}

static size_t load_file(const char *path, uint8_t *buffer, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        return 0u;
    }
    size_t size = fread(buffer, 1u, capacity, file);
    (void)fclose(file);
    return size;
}

static int read_all(struct helikon_codec *codec, size_t total_frames,
                    size_t *frames)
{
    size_t out = 0u;
    while (out < total_frames) {
        size_t count = 0u;
        int rc = helikon_codec_read(codec, pcm + out * 2u, CHUNK_FRAMES,
                                    &count);
        if (rc != HELIKON_OK)
            return rc;
        if (count == 0u)
            break;
        out += count;
        if (out > total_frames)
            return HELIKON_ERR_FORMAT;
    }
    *frames = out;
    return HELIKON_OK;
}

static int16_t expected_sample(const uint8_t *pcm_data, size_t frame,
                               uint16_t channels, uint16_t bits,
                               uint16_t channel)
{
    size_t index = frame * channels + channel;
    if (bits == 16u) {
        uint16_t value = (uint16_t)(pcm_data[index * 2u] |
                                    ((uint16_t)pcm_data[index * 2u + 1u] << 8));
        return (int16_t)value;
    }
    uint32_t value = (uint32_t)pcm_data[index * 3u] |
                     ((uint32_t)pcm_data[index * 3u + 1u] << 8) |
                     ((uint32_t)pcm_data[index * 3u + 2u] << 16);
    if ((value & 0x800000u) != 0u)
        value |= 0xff000000u;
    return (int16_t)((int32_t)value >> 8);
}

/* Decodes up to `limit` frames into the shared chunk buffer and counts them. */
static int count_frames(struct helikon_codec *codec, size_t limit, size_t *frames)
{
    size_t total = 0u;
    while (total < limit) {
        size_t got = 0u;
        int rc = helikon_codec_read(codec, pcm, CHUNK_FRAMES, &got);
        if (rc != HELIKON_OK)
            return rc;
        if (got == 0u)
            break;
        total += got;
    }
    *frames = total;
    return HELIKON_OK;
}

static int check_flac(const char *name, uint16_t channels, uint16_t bits,
                      uint32_t rate, uint32_t total_frames)
{
    char path[256];
    (void)snprintf(path, sizeof(path), "fixtures/%s.flac", name);
    size_t size = load_file(path, file_data, sizeof(file_data));
    if (expect(size > 0u, "FLAC fixture load"))
        return 1;
    (void)snprintf(path, sizeof(path), "fixtures/%s.pcm", name);
    size_t expected_size = load_file(path, expected_data, sizeof(expected_data));
    if (expect(expected_size > 0u, "PCM fixture load"))
        return 1;

    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK,
               "FLAC probe"))
        return 1;
    if (expect(ops == &helikon_flac_codec_ops, "FLAC descriptor"))
        return 1;
    if (expect(info.sample_rate == rate && info.source_channels == channels &&
                   info.source_bits == bits &&
                   info.output_channels == 2u && info.output_bits == 16u &&
                   info.total_frames == total_frames &&
                   info.workspace_bytes <= sizeof(workspace) &&
                   (info.flags & HELIKON_CODEC_FLAG_SEEK) != 0u,
               "FLAC stream info"))
        return 1;

    struct helikon_codec codec;
    if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "FLAC open"))
        return 1;

    size_t frames = 0u;
    if (expect(read_all(&codec, total_frames, &frames) == HELIKON_OK &&
                   frames == total_frames,
               "FLAC read length"))
        return 1;

    size_t expected_frames = expected_size / (channels * (bits / 8u));
    if (expect(expected_frames == total_frames, "PCM fixture length"))
        return 1;
    for (size_t frame = 0u; frame < total_frames; ++frame) {
        int16_t left = expected_sample(expected_data, frame, channels, bits,
                                       0u);
        int16_t right = expected_sample(expected_data, frame, channels, bits,
                                        channels == 1u ? 0u : 1u);
        if (expect(pcm[frame * 2u] == left &&
                       pcm[frame * 2u + 1u] == right,
                   "FLAC sample data"))
            return 1;
    }

    if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                   HELIKON_OK &&
                   frames == 0u,
               "FLAC end of stream"))
        return 1;

    size_t seek_frame = total_frames / 2u;
    size_t seek_count = total_frames - seek_frame;
    if (seek_count > 100u)
        seek_count = 100u;
    if (expect(helikon_codec_seek(&codec, (uint32_t)seek_frame) ==
                   HELIKON_OK,
               "FLAC seek"))
        return 1;
    if (expect(helikon_codec_read(&codec, pcm, seek_count, &frames) ==
                   HELIKON_OK &&
                   frames == seek_count,
               "FLAC seek read"))
        return 1;
    for (size_t index = 0u; index < seek_count; ++index) {
        size_t frame = seek_frame + index;
        int16_t left = expected_sample(expected_data, frame, channels, bits,
                                       0u);
        int16_t right = expected_sample(expected_data, frame, channels, bits,
                                        channels == 1u ? 0u : 1u);
        if (expect(pcm[index * 2u] == left &&
                       pcm[index * 2u + 1u] == right,
                   "FLAC seek data"))
            return 1;
    }
    if (expect(helikon_codec_seek(&codec, total_frames) == HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, 1u, &frames) == HELIKON_OK &&
                   frames == 0u,
               "FLAC seek to end"))
        return 1;
    if (expect(helikon_codec_seek(&codec, total_frames + 1u) ==
                   HELIKON_ERR_ARGUMENT,
               "FLAC seek beyond end"))
        return 1;

    helikon_codec_close(&codec);
    return 0;
}

/*
 * AIFF/AIFF-C. The reference `.pcm` is interleaved stereo 16-bit little-endian,
 * whatever the source width was, so one comparison covers the byte swap, the
 * mono duplication and the 24/32-bit truncation.
 */
static int check_aiff(const char *name, const char *extension, uint32_t rate,
                      uint16_t source_channels, uint16_t source_bits,
                      uint32_t total_frames)
{
    char path[256];
    (void)snprintf(path, sizeof(path), "fixtures/%s.%s", name, extension);
    size_t size = load_file(path, file_data, sizeof(file_data));
    if (expect(size > 0u, "AIFF fixture load"))
        return 1;
    (void)snprintf(path, sizeof(path), "fixtures/%s.pcm", name);
    size_t expected_size = load_file(path, expected_data,
                                     sizeof(expected_data));
    if (expect(expected_size > 0u, "AIFF PCM fixture load"))
        return 1;

    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK,
               "AIFF probe"))
        return 1;
    if (expect(ops == &helikon_aiff_codec_ops, "AIFF descriptor"))
        return 1;
    if (expect(info.sample_rate == rate &&
                   info.source_channels == source_channels &&
                   info.source_bits == source_bits &&
                   info.output_channels == 2u && info.output_bits == 16u &&
                   info.total_frames == total_frames &&
                   info.workspace_bytes <= sizeof(workspace) &&
                   (info.flags & HELIKON_CODEC_FLAG_SEEK) != 0u,
               "AIFF stream info"))
        return 1;

    struct helikon_codec codec;
    if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "AIFF open"))
        return 1;

    size_t frames = 0u;
    if (expect(read_all(&codec, total_frames, &frames) == HELIKON_OK &&
                   frames == total_frames,
               "AIFF read length"))
        return 1;

    size_t expected_frames = expected_size / (2u * sizeof(int16_t));
    if (expect(expected_frames == total_frames, "AIFF PCM fixture length"))
        return 1;
    for (size_t frame = 0u; frame < total_frames; ++frame) {
        int16_t left = expected_sample(expected_data, frame, 2u, 16u, 0u);
        int16_t right = expected_sample(expected_data, frame, 2u, 16u, 1u);
        if (expect(pcm[frame * 2u] == left &&
                       pcm[frame * 2u + 1u] == right,
                   "AIFF sample data"))
            return 1;
    }

    if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                   HELIKON_OK &&
                   frames == 0u,
               "AIFF end of stream"))
        return 1;

    size_t seek_frame = total_frames / 2u;
    size_t seek_count = total_frames - seek_frame;
    if (seek_count > 100u)
        seek_count = 100u;
    if (expect(helikon_codec_seek(&codec, (uint32_t)seek_frame) == HELIKON_OK,
               "AIFF seek"))
        return 1;
    if (expect(helikon_codec_read(&codec, pcm, seek_count, &frames) ==
                   HELIKON_OK &&
                   frames == seek_count,
               "AIFF seek read"))
        return 1;
    for (size_t index = 0u; index < seek_count; ++index) {
        size_t frame = seek_frame + index;
        int16_t left = expected_sample(expected_data, frame, 2u, 16u, 0u);
        int16_t right = expected_sample(expected_data, frame, 2u, 16u, 1u);
        if (expect(pcm[index * 2u] == left &&
                       pcm[index * 2u + 1u] == right,
                   "AIFF seek data"))
            return 1;
    }
    if (expect(helikon_codec_seek(&codec, total_frames) == HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, 1u, &frames) ==
                       HELIKON_OK &&
                   frames == 0u,
               "AIFF seek to end"))
        return 1;
    if (expect(helikon_codec_seek(&codec, total_frames + 1u) ==
                   HELIKON_ERR_ARGUMENT,
               "AIFF seek beyond end"))
        return 1;

    helikon_codec_close(&codec);
    return 0;
}

/* A compression type Helikon does not decode is UNSUPPORTED, not NOT_FOUND. */
static int check_aiff_unsupported(void)
{
    size_t size = load_file("fixtures/aiff_alaw.aifc", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "AIFC alaw fixture load"))
        return 1;
    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_ERR_UNSUPPORTED,
               "AIFC alaw is unsupported"))
        return 1;
    return 0;
}

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

static int check_wav(void)
{
    size_t size = make_wav(file_data, 100u);
    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK,
               "WAV probe"))
        return 1;
    if (expect(ops == &helikon_wav_codec_ops && info.total_frames == 100u &&
                   info.source_channels == 2u && info.source_bits == 16u,
               "WAV descriptor"))
        return 1;
    struct helikon_codec codec;
    if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "WAV open"))
        return 1;
    size_t frames = 0u;
    if (expect(helikon_codec_read(&codec, pcm, 100u, &frames) == HELIKON_OK &&
                   frames == 100u && pcm[0] == 1 && pcm[1] == 2 &&
                   pcm[198] == (int16_t)(99u * 3u + 1u) &&
                   pcm[199] == (int16_t)(99u * 5u + 2u),
               "WAV read"))
        return 1;
    if (expect(helikon_codec_seek(&codec, 10u) == HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, 1u, &frames) == HELIKON_OK &&
                   frames == 1u && pcm[0] == (int16_t)(10u * 3u + 1u),
               "WAV seek"))
        return 1;
    helikon_codec_close(&codec);
    return 0;
}

static size_t flac_first_frame(const uint8_t *data, size_t size)
{
    if (size < 8u)
        return 0u;
    size_t offset = 4u;
    for (;;) {
        if (offset > size || size - offset < 4u)
            return 0u;
        uint8_t header = data[offset];
        uint32_t length = ((uint32_t)data[offset + 1u] << 16) |
                          ((uint32_t)data[offset + 2u] << 8) |
                          data[offset + 3u];
        offset += 4u;
        if ((size_t)length > size - offset)
            return 0u;
        offset += length;
        if ((header & 0x80u) != 0u)
            return offset;
    }
}

static int check_mp3_cbr_stereo(void)
{
    size_t size = load_file("fixtures/mp3_cbr_stereo16.mp3", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "MP3 fixture load"))
        return 1;

    /* The probe decides by header, not by extension. */
    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK,
               "MP3 probe"))
        return 1;
    if (expect(ops == &helikon_mp3_codec_ops, "MP3 descriptor"))
        return 1;
    /* The measured workspace (30,976 bytes) must keep the descriptor's
     * claim inside the shared workspace ceiling. */
    if (expect(info.workspace_bytes > 0u &&
                   info.workspace_bytes <= HELIKON_CODEC_WORKSPACE_MAX,
               "MP3 workspace bound"))
        return 1;
    if (expect(info.sample_rate == 44100u && info.source_channels == 2u &&
                   info.source_bits == 16u && info.output_bits == 16u &&
                   info.total_frames >= 4096u &&
                   info.total_frames <= 4096u + 2304u,
               "MP3 stream info"))
        return 1;

    struct helikon_codec codec;
    if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "MP3 open"))
        return 1;
    size_t frames = 0u;
    if (expect(helikon_codec_read(&codec, NULL, 4u, &frames) ==
                   HELIKON_ERR_ARGUMENT,
               "MP3 read null buffer"))
        return 1;
    if (expect(helikon_codec_read(&codec, pcm, 0u, &frames) == HELIKON_OK &&
                   frames == 0u,
               "MP3 zero-length read"))
        return 1;
    size_t out = 0u;
    while (out < info.total_frames + 1152u) {
        if (expect(helikon_codec_read(&codec, pcm + out * 2u, CHUNK_FRAMES,
                                      &frames) == HELIKON_OK,
                   "MP3 read"))
            return 1;
        if (frames == 0u)
            break;
        out += frames;
        if (out > info.total_frames + 1152u)
            break;
    }
    if (expect(out >= 4096u && out <= 4096u + 2304u, "MP3 decoded length"))
        return 1;
    if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                   HELIKON_OK && frames == 0u,
               "MP3 end of stream"))
        return 1;

    /* A 12 kHz-amplitude sine must decode to a clearly non-silent signal. */
    int16_t peak = 0;
    for (size_t i = 0u; i < out; ++i) {
        if (pcm[i * 2u] > peak)
            peak = pcm[i * 2u];
    }
    if (expect(peak > 9000, "MP3 sine peak"))
        return 1;

    const uint32_t mid = info.total_frames / 2u;
    if (expect(helikon_codec_seek(&codec, mid) == HELIKON_OK,
               "MP3 seek"))
        return 1;
    size_t after = 0u;
    while (after < 1u) {
        if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                       HELIKON_OK,
                   "MP3 seek read"))
            return 1;
        if (frames == 0u)
            break;
        after += frames;
    }
    if (expect(after > 0u, "MP3 seek read count"))
        return 1;
    if (expect(helikon_codec_seek(&codec, info.total_frames + 1u) ==
                   HELIKON_ERR_ARGUMENT,
               "MP3 seek beyond end"))
        return 1;
    helikon_codec_close(&codec);
    return 0;
}

static int check_mp3_mono_vbr_id3(void)
{
    /* Mono: the wrapper must duplicate the single channel to stereo. */
    size_t size = load_file("fixtures/mp3_cbr_mono16.mp3", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "MP3 mono load"))
        return 1;
    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK && ops == &helikon_mp3_codec_ops &&
                   info.source_channels == 1u && info.sample_rate == 44100u,
               "MP3 mono probe"))
        return 1;
    struct helikon_codec codec;
    if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "MP3 mono open"))
        return 1;
    size_t frames = 0u;
    if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                   HELIKON_OK && frames > 0u,
               "MP3 mono read"))
        return 1;
    if (expect(pcm[0] == pcm[1], "MP3 mono duplicate"))
        return 1;
    helikon_codec_close(&codec);

    /* VBR: Xing frames with a TOC; seek must serve a mid-file position. */
    size = load_file("fixtures/mp3_vbr_stereo16.mp3", file_data,
                     sizeof(file_data));
    if (expect(size > 0u, "MP3 vbr load"))
        return 1;
    ops = NULL;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK && ops == &helikon_mp3_codec_ops &&
                   info.sample_rate == 44100u,
               "MP3 vbr probe"))
        return 1;
    if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "MP3 vbr open"))
        return 1;
    if (expect(helikon_codec_seek(&codec, info.total_frames / 2u) ==
                   HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                       HELIKON_OK && frames > 0u,
               "MP3 vbr seek read"))
        return 1;
    /*
     * A seek to the last frame, and to the end of the stream, are legal player
     * actions (the last one is what "jump to the end" does). Both land in the
     * TOC bucket 99..100; the sanitize target turns an off-by-one there into a
     * finding, so this is the regression test for it.
     */
    if (expect(info.total_frames > 2u, "MP3 vbr end-seek room"))
        return 1;
    if (expect(helikon_codec_seek(&codec, info.total_frames) == HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                       HELIKON_OK,
               "MP3 vbr seek end"))
        return 1;
    if (expect(helikon_codec_seek(&codec, info.total_frames - 1u) ==
                   HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                       HELIKON_OK,
               "MP3 vbr seek last frame"))
        return 1;
    helikon_codec_close(&codec);

    /* ID3v2 front matter is skipped, then the header probe finds MPEG sync. */
    size = load_file("fixtures/mp3_id3_stereo16.mp3", file_data,
                     sizeof(file_data));
    if (expect(size > 0u, "MP3 id3 load"))
        return 1;
    ops = NULL;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK && ops == &helikon_mp3_codec_ops &&
                   info.sample_rate == 44100u,
               "MP3 id3 probe"))
        return 1;

    /* Truncated mid-frame decodes to a clean end of stream, not a hang. */
    if (expect(size > 64u, "MP3 id3 truncation room"))
        return 1;
    size_t truncated = size - 64u;
    ops = NULL;
    if (expect(helikon_codec_probe(file_data, truncated, &ops, &info) ==
                   HELIKON_OK,
               "MP3 truncated probe"))
        return 1;
    if (expect(helikon_codec_open(&codec, ops, file_data, truncated,
                                  workspace, sizeof(workspace)) ==
                   HELIKON_OK,
               "MP3 truncated open"))
        return 1;
    if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                   HELIKON_OK,
               "MP3 truncated read"))
        return 1;
    helikon_codec_close(&codec);

    /* Garbage that starts with MPEG sync but has an invalid header is a
     * recognized-format FORMAT, not a silent "not ours". */
    file_data[0] = 0xFFu;
    file_data[1] = 0xFBu;
    file_data[2] = 0xF0u; /* bitrate index 15: illegal */
    file_data[3] = 0x00u;
    for (size_t i = 4u; i < 4096u; ++i)
        file_data[i] = 0x11u;
    ops = NULL;
    if (expect(helikon_codec_probe(file_data, 4096u, &ops, &info) ==
                   HELIKON_ERR_FORMAT,
               "MP3 invalid header probe"))
        return 1;
    return 0;
}

/* ---------- streaming source model ---------- */

/*
 * A fake media that exposes only `read_at`. It records the largest single
 * request and the total bytes pulled, so the tests can prove a decoder never
 * asks for the whole file and never reads ahead of what it decodes.
 */
struct fake_media {
    const uint8_t *data;
    size_t size;
    uint64_t reported; /* 0 = the media cannot report a length */
    size_t chunk;      /* 0 = deliver everything requested */
    size_t bytes_read;
    size_t largest_request;
    unsigned int reads;
};

static int fake_read_at(void *context, uint64_t offset, void *buffer,
                        size_t length, size_t *read)
{
    struct fake_media *media = context;
    if (media == NULL || read == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    *read = 0u;
    media->reads += 1u;
    if (length > media->largest_request)
        media->largest_request = length;
    if (offset > media->size)
        return HELIKON_ERR_FORMAT;
    size_t available = media->size - (size_t)offset;
    size_t count = available < length ? available : length;
    if (media->chunk != 0u && count > media->chunk)
        count = media->chunk;
    if (count != 0u) {
        helikon_memcpy(buffer, media->data + (size_t)offset, count);
        media->bytes_read += count;
    }
    *read = count;
    return HELIKON_OK;
}

static void fake_media_init(struct fake_media *media, const uint8_t *data,
                            size_t size)
{
    helikon_memset(media, 0, sizeof(*media));
    media->data = data;
    media->size = size;
    media->reported = size;
}

/* A WAV with a `JUNK` chunk big enough to push `data` past the probe prefix. */
static size_t make_wav_junk(uint8_t *data, size_t frames, size_t junk)
{
    size_t data_bytes = frames * 4u;
    size_t base = make_wav(data, frames);
    if (junk == 0u)
        return base;

    size_t total = 44u + junk + 8u + data_bytes;

    /* Shift the 8-byte `data` header and the payload past the junk chunk. */
    helikon_memcpy(data + 44u + junk, data + 36u, 8u + data_bytes);
    helikon_memcpy(data + 36u, "JUNK", 4u);
    data[40] = (uint8_t)(junk & 0xffu);
    data[41] = (uint8_t)((junk >> 8) & 0xffu);
    data[42] = (uint8_t)((junk >> 16) & 0xffu);
    data[43] = (uint8_t)(junk >> 24);
    for (size_t index = 44u; index < 44u + junk; ++index)
        data[index] = (uint8_t)index;

    size_t riff_size = 36u + junk + 8u + data_bytes;
    data[4] = (uint8_t)(riff_size & 0xffu);
    data[5] = (uint8_t)((riff_size >> 8) & 0xffu);
    data[6] = (uint8_t)((riff_size >> 16) & 0xffu);
    data[7] = (uint8_t)(riff_size >> 24);
    return total;
}

static int check_wav_source(void)
{
    size_t frames_total = 5000u;
    size_t size = make_wav(file_data, frames_total);
    struct fake_media media;
    struct helikon_codec_source source;
    struct helikon_codec codec;

    fake_media_init(&media, file_data, size);
    source.read_at = fake_read_at;
    source.size = media.reported;
    source.context = &media;

    if (expect(helikon_codec_open_source(&codec, &helikon_wav_codec_ops, &source,
                                         workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "WAV source open"))
        return 1;
    const uint8_t *span = NULL;
    size_t span_size = 0u;
    if (expect(!helikon_codec_memory_span(&codec, &span, &span_size),
               "source is not a memory span"))
        return 1;
    if (expect(codec.info.total_frames == frames_total &&
                   codec.info.sample_rate == 44100u,
               "WAV source info"))
        return 1;

    size_t got = 0u;
    if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &got) ==
                       HELIKON_OK &&
                   got == CHUNK_FRAMES && pcm[0] == 1 && pcm[1] == 2,
               "WAV source first frame"))
        return 1;

    /* Bounded decode-path requests: only the probe prefix may be larger. */
    if (expect(media.largest_request <= HELIKON_CODEC_PROBE_BYTES,
               "WAV source bounded request"))
        return 1;

    if (expect(helikon_codec_seek(&codec, 4000u) == HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, 1u, &got) == HELIKON_OK &&
                   got == 1u && pcm[0] == (int16_t)(4000u * 3u + 1u) &&
                   pcm[1] == (int16_t)(4000u * 5u + 2u),
               "WAV source seek"))
        return 1;

    /* Never the whole file: header + the chunks actually decoded. */
    if (expect(media.bytes_read < size, "WAV source avoids whole file"))
        return 1;
    helikon_codec_close(&codec);

    /* One decode chunk pulls exactly one chunk's worth of bytes. */
    fake_media_init(&media, file_data, size);
    source.size = media.reported;
    source.context = &media;
    if (expect(helikon_codec_open_source(&codec, &helikon_wav_codec_ops, &source,
                                         workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "WAV request-size reopen"))
        return 1;
    media.bytes_read = 0u;
    media.largest_request = 0u;
    if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &got) ==
                       HELIKON_OK &&
                   got == CHUNK_FRAMES,
               "WAV request-size read"))
        return 1;
    if (expect(media.largest_request == CHUNK_FRAMES * 4u &&
                   media.bytes_read == CHUNK_FRAMES * 4u,
               "WAV request matches chunk"))
        return 1;
    helikon_codec_close(&codec);

    /* Partial reads must not lose or duplicate frames. */
    fake_media_init(&media, file_data, size);
    media.chunk = 37u;
    source.size = media.reported;
    source.context = &media;
    if (expect(helikon_codec_open_source(&codec, &helikon_wav_codec_ops, &source,
                                         workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "WAV short-read open"))
        return 1;
    size_t out = 0u;
    size_t total = 0u;
    while (total < frames_total) {
        int rc = helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &out);
        if (rc != HELIKON_OK || out == 0u)
            break;
        if (pcm[0] != (int16_t)(total * 3u + 1u))
            break;
        total += out;
    }
    if (expect(total == frames_total, "WAV short-read frame count"))
        return 1;
    helikon_codec_close(&codec);

    /* A media with no length: the RIFF header is the only bound. */
    fake_media_init(&media, file_data, size);
    media.reported = 0u;
    source.size = 0u;
    source.context = &media;
    if (expect(helikon_codec_open_source(&codec, &helikon_wav_codec_ops, &source,
                                         workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "WAV unknown-size open"))
        return 1;
    if (expect(codec.info.total_frames == frames_total,
               "WAV unknown-size frames"))
        return 1;
    helikon_codec_close(&codec);
    return 0;
}

static int check_wav_prefix(void)
{
    /* `data` sits past the probe prefix; only open can resolve the head. */
    size_t junk = 6000u;
    size_t size = make_wav_junk(file_data, 200u, junk);
    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;

    if (expect(size > HELIKON_CODEC_PROBE_BYTES, "prefix fixture size"))
        return 1;
    if (expect(helikon_wav_codec_ops.probe(file_data,
                                           HELIKON_CODEC_PROBE_BYTES, size,
                                           &info) == HELIKON_ERR_NEED_MORE,
               "prefix probe needs more"))
        return 1;
    /* The whole-buffer probe still decides immediately. */
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) == HELIKON_OK &&
                   ops == &helikon_wav_codec_ops && info.total_frames == 200u,
               "prefix probe via memory"))
        return 1;

    struct fake_media media;
    fake_media_init(&media, file_data, size);
    struct helikon_codec_source source;
    source.read_at = fake_read_at;
    source.size = media.reported;
    source.context = &media;

    struct helikon_codec codec;
    if (expect(helikon_codec_open_source(&codec, &helikon_wav_codec_ops, &source,
                                         workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "prefix source open"))
        return 1;
    if (expect(codec.info.total_frames == 200u &&
                   codec.info.workspace_bytes >= sizeof(codec.info),
               "prefix source info from open"))
        return 1;
    size_t got = 0u;
    if (expect(helikon_codec_seek(&codec, 199u) == HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, 1u, &got) == HELIKON_OK &&
                   got == 1u && pcm[0] == (int16_t)(199u * 3u + 1u),
               "prefix source tail frame"))
        return 1;
    helikon_codec_close(&codec);
    return 0;
}


static int check_flac_source(void)
{
    size_t size = load_file("fixtures/stereo16.flac", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "FLAC source fixture load"))
        return 1;

    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK,
               "FLAC source probe"))
        return 1;

    /* Reference: the same fixture through the resident-buffer path. */
    struct helikon_codec codec;
    if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "FLAC source reference open"))
        return 1;
    size_t total = info.total_frames;
    size_t ref_frames = 0u;
    if (expect(read_all(&codec, total, &ref_frames) == HELIKON_OK &&
                   ref_frames == total,
               "FLAC source reference decode"))
        return 1;
    helikon_memcpy(expected_data, pcm, total * 2u * sizeof(int16_t));
    helikon_codec_close(&codec);

    struct fake_media media;
    fake_media_init(&media, file_data, size);
    struct helikon_codec_source source;
    source.read_at = fake_read_at;
    source.size = media.reported;
    source.context = &media;

    if (expect(helikon_codec_open_source(&codec, ops, &source, workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "FLAC source open"))
        return 1;
    if (expect(codec.info.total_frames == total, "FLAC source info"))
        return 1;

    size_t frames = 0u;
    if (expect(read_all(&codec, total, &frames) == HELIKON_OK &&
                   frames == total,
               "FLAC source decode length"))
        return 1;
    if (expect(helikon_memcmp(expected_data, pcm,
                              total * 2u * sizeof(int16_t)) == 0,
               "FLAC source decode matches buffer"))
        return 1;

    /* The window is the only buffer: requests are bounded by it. */
    if (expect(media.largest_request <= HELIKON_FLAC_WINDOW,
               "FLAC source bounded request"))
        return 1;
    /*
     * Each frame is walked twice (decode, then CRC verification), so the pull
     * total is bounded by ~2x the stream and never by a whole-file request.
     */
    if (expect(media.bytes_read <= size * 2u + 1024u,
               "FLAC source bounded total"))
        return 1;

    if (expect(helikon_codec_seek(&codec, 1000u) == HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, 16u, &frames) == HELIKON_OK &&
                   frames == 16u &&
                   helikon_memcmp(expected_data + 1000u * 2u * sizeof(int16_t),
                                  pcm, 16u * 2u * sizeof(int16_t)) == 0,
               "FLAC source seek"))
        return 1;
    helikon_codec_close(&codec);

    /* Partial reads across a window refill must not shift the bitstream. */
    fake_media_init(&media, file_data, size);
    media.chunk = 61u;
    source.size = media.reported;
    source.context = &media;
    if (expect(helikon_codec_open_source(&codec, ops, &source, workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "FLAC short-read open"))
        return 1;
    if (expect(read_all(&codec, total, &frames) == HELIKON_OK &&
                   frames == total &&
                   helikon_memcmp(expected_data, pcm,
                                  total * 2u * sizeof(int16_t)) == 0,
               "FLAC short-read decode"))
        return 1;
    helikon_codec_close(&codec);
    return 0;
}


static int check_mp3_source(void)
{
    size_t size = load_file("fixtures/mp3_cbr_stereo16.mp3", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "MP3 source fixture load"))
        return 1;

    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK,
               "MP3 source probe"))
        return 1;

    /* Reference decode through the resident-buffer path. */
    struct helikon_codec codec;
    if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "MP3 source reference open"))
        return 1;
    size_t total = info.total_frames;
    size_t ref_frames = 0u;
    if (expect(read_all(&codec, total, &ref_frames) == HELIKON_OK &&
                   ref_frames > 0u,
               "MP3 source reference decode"))
        return 1;
    const size_t ref_bytes = ref_frames * 2u * sizeof(int16_t);
    helikon_memcpy(expected_data, pcm, ref_bytes);
    helikon_codec_close(&codec);

    struct fake_media media;
    fake_media_init(&media, file_data, size);
    struct helikon_codec_source source;
    source.read_at = fake_read_at;
    source.size = media.reported;
    source.context = &media;

    if (expect(helikon_codec_open_source(&codec, ops, &source, workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "MP3 source open"))
        return 1;
    size_t frames = 0u;
    if (expect(read_all(&codec, ref_frames, &frames) == HELIKON_OK &&
                   frames >= ref_frames,
               "MP3 source decode length"))
        return 1;
    if (expect(helikon_memcmp(expected_data, pcm, ref_bytes) == 0,
               "MP3 source decode matches buffer"))
        return 1;
    if (expect(media.largest_request <= 32768u,
               "MP3 source bounded request"))
        return 1;

    /* Frame-boundary seek: the same request must land on the same PCM. */
    if (expect(helikon_codec_seek(&codec, 576u) == HELIKON_OK &&
                   helikon_codec_read(&codec, pcm, 32u, &frames) == HELIKON_OK &&
                   frames != 0u,
               "MP3 source seek decodes"))
        return 1;
    helikon_codec_close(&codec);

    /* Partial reads must not disturb the libmad buffer boundaries. */
    fake_media_init(&media, file_data, size);
    media.chunk = 37u;
    source.size = media.reported;
    source.context = &media;
    if (expect(helikon_codec_open_source(&codec, ops, &source, workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "MP3 short-read open"))
        return 1;
    if (expect(read_all(&codec, ref_frames, &frames) == HELIKON_OK &&
                   frames == ref_frames &&
                   helikon_memcmp(expected_data, pcm, ref_bytes) == 0,
               "MP3 short-read decode"))
        return 1;
    helikon_codec_close(&codec);

    /* The VBR fixture with an unknown media length still opens and decodes. */
    size = load_file("fixtures/mp3_vbr_stereo16.mp3", file_data,
                     sizeof(file_data));
    if (expect(size > 0u, "MP3 VBR source load"))
        return 1;
    media.reported = 0u;
    source.size = 0u;
    source.context = &media;
    media.data = file_data;
    media.size = size;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) == HELIKON_OK,
               "MP3 VBR probe"))
        return 1;
    if (expect(helikon_codec_open_source(&codec, ops, &source, workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "MP3 unknown-size open"))
        return 1;
    if (expect(codec.info.sample_rate == 44100u, "MP3 unknown-size info"))
        return 1;
    helikon_codec_close(&codec);
    return 0;
}


/* ---------- ID3v2 tags ---------- */

static size_t put_syncsafe(uint8_t *out, size_t value)
{
    out[0] = (uint8_t)((value >> 21) & 0x7fu);
    out[1] = (uint8_t)((value >> 14) & 0x7fu);
    out[2] = (uint8_t)((value >> 7) & 0x7fu);
    out[3] = (uint8_t)(value & 0x7fu);
    return 4u;
}

/* Appends a v2.3/v2.4 text frame; returns the new length. */
static size_t put_text_frame(uint8_t *data, size_t offset, const char *id,
                             uint8_t encoding, const uint8_t *body,
                             size_t body_length, int v4)
{
    helikon_memcpy(data + offset, id, 4u);
    if (v4) {
        put_syncsafe(data + offset + 4u, body_length + 1u);
    } else {
        const size_t length = body_length + 1u;
        data[offset + 4] = (uint8_t)(length >> 24);
        data[offset + 5] = (uint8_t)(length >> 16);
        data[offset + 6] = (uint8_t)(length >> 8);
        data[offset + 7] = (uint8_t)length;
    }
    data[offset + 8] = 0u;
    data[offset + 9] = 0u;
    data[offset + 10] = encoding;
    helikon_memcpy(data + offset + 11u, body, body_length);
    return offset + 11u + body_length;
}

/* Wraps a frame area in an ID3v2 header (version 3 or 4). */
static size_t wrap_tag(uint8_t *data, size_t frame_bytes, uint8_t version,
                       uint8_t flags)
{
    helikon_memcpy(data, "ID3", 3u);
    data[3] = version;
    data[4] = 0u;
    data[5] = flags;
    put_syncsafe(data + 6u, frame_bytes);
    return 10u + frame_bytes;
}


static int check_mp3_gapless(void)
{
    static const char *const names[] = { "fixtures/mp3_cbr_stereo16.mp3",
                                         "fixtures/mp3_vbr_stereo16.mp3" };
    for (size_t file = 0u; file < 2u; ++file) {
        size_t size = load_file(names[file], file_data, sizeof(file_data));
        if (expect(size > 0u, "gapless fixture load"))
            return 1;

        const struct helikon_codec_ops *ops = NULL;
        struct helikon_codec_info info;
        if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                       HELIKON_OK,
                   "gapless probe"))
            return 1;
        /* LAME writes 576 samples of encoder delay for 44.1 kHz; the exact
         * value pins the LAME-tag offset arithmetic, not just "non-zero". */
        if (expect((info.flags & HELIKON_CODEC_FLAG_GAPLESS) != 0u &&
                       info.encoder_delay == 576u && info.padding > 0u,
                   "gapless declared"))
            return 1;
        if (expect(info.total_frames > info.encoder_delay + info.padding,
                   "gapless trim fits"))
            return 1;

        struct helikon_codec codec;
        if (expect(helikon_codec_open(&codec, ops, file_data, size, workspace,
                                      sizeof(workspace)) == HELIKON_OK,
                   "gapless open"))
            return 1;
        size_t frames = 0u;
        if (expect(count_frames(&codec, info.total_frames + CHUNK_FRAMES,
                                &frames) == HELIKON_OK,
                   "gapless decode"))
            return 1;
        if (expect(frames == info.total_frames - info.encoder_delay -
                                info.padding,
                   "gapless trimmed length"))
            return 1;

        /* Seeking past the delay must still reach the same end. */
        if (expect(helikon_codec_seek(&codec, 0u) == HELIKON_OK,
                   "gapless seek zero"))
            return 1;
        if (expect(count_frames(&codec, info.total_frames + CHUNK_FRAMES,
                                &frames) == HELIKON_OK &&
                       frames == info.total_frames - info.encoder_delay -
                                      info.padding,
                   "gapless seek restart"))
            return 1;
        helikon_codec_close(&codec);
    }
    return 0;
}

static size_t count_frames_all(struct helikon_codec *codec, size_t limit,
                               int16_t *peak_out)
{
    size_t total = 0u;
    int16_t peak = 0;
    for (;;) {
        size_t frames = 0u;
        if (helikon_codec_read(codec, pcm, CHUNK_FRAMES, &frames) !=
            HELIKON_OK)
            break;
        if (frames == 0u)
            break;
        for (size_t i = 0u; i < frames; ++i) {
            int16_t value = pcm[i * 2u] < 0 ? (int16_t)-pcm[i * 2u]
                                            : pcm[i * 2u];
            if (value > peak)
                peak = value;
        }
        total += frames;
        if (total > limit)
            break;
    }
    if (peak_out != NULL)
        *peak_out = peak;
    return total;
}

/* The registry claims the whole MPEG audio family, but every fixture was
 * MPEG-1 Layer III at 44.1 kHz until now. MPEG-2 and 2.5 carry 576 samples per
 * frame instead of 1152, so any arithmetic that assumes the MPEG-1 count is
 * wrong by a factor of two: total duration, the Xing frame count, the gapless
 * trim and seek by frame index all depend on it. These fixtures execute the
 * other branches and pin the numbers against the encoder. */
static int check_mp3_family(void)
{
    struct family_case {
        const char *path;
        uint32_t rate;
        uint32_t total_frames;
        uint32_t delay;
        uint32_t padding;
        uint32_t decoded;
        uint8_t gapless;
        const char *label;
    };
    static const struct family_case cases[] = {
        /* LAME at 22.05 kHz: 8 frames of 576, 576+576 trimmed, 6 frames out. */
        { "fixtures/mp3_mpeg2_stereo16.mp3", 22050u, 4608u, 576u, 576u, 3456u,
          1u, "MPEG-2" },
        /* LAME at 11.025 kHz: 6 frames of 576, the same trim, 4 frames out. */
        { "fixtures/mp3_mpeg25_stereo16.mp3", 11025u, 3456u, 576u, 576u, 2304u,
          1u, "MPEG-2.5" },
        /* ffmpeg MP2 at 44.1 kHz: no Xing frame, so no trim and no claim. */
        { "fixtures/mp3_layer2_stereo44.mp3", 44100u, 2304u, 0u, 0u, 2304u, 0u,
          "Layer II" },
    };
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]);
         ++index) {
        const struct family_case *item = &cases[index];
        size_t size = load_file(item->path, file_data, sizeof(file_data));
        if (expect(size > 0u, "MP3 family load"))
            return 1;

        const struct helikon_codec_ops *ops = NULL;
        struct helikon_codec_info info;
        if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                       HELIKON_OK &&
                       ops == &helikon_mp3_codec_ops,
                   "MP3 family probe"))
            return 1;
        if (expect(info.sample_rate == item->rate &&
                       info.source_channels == 2u &&
                       info.total_frames == item->total_frames &&
                       info.encoder_delay == item->delay &&
                       info.padding == item->padding,
                   "MP3 family info"))
            return 1;
        if (expect(((info.flags & HELIKON_CODEC_FLAG_GAPLESS) != 0u) ==
                       (item->gapless != 0u),
                   "MP3 family gapless flag"))
            return 1;

        struct helikon_codec codec;
        if (expect(helikon_codec_open(&codec, ops, file_data, size,
                                      workspace, sizeof(workspace)) ==
                       HELIKON_OK,
                   "MP3 family open"))
            return 1;
        int16_t peak = 0;
        size_t decoded = count_frames_all(&codec, info.total_frames + 4096u,
                                          &peak);
        if (expect(decoded == item->decoded, "MP3 family decoded length"))
            return 1;
        if (expect(peak > 2000, "MP3 family decodes audio"))
            return 1;

        /* Frame-index seek has to divide by the family's frame size: seeking
         * half way with 1152 assumed lands at a quarter of the file. */
        if (expect(helikon_codec_seek(&codec, item->decoded / 2u) ==
                       HELIKON_OK,
                   "MP3 family seek"))
            return 1;
        size_t after = 0u;
        while (after == 0u) {
            size_t frames = 0u;
            if (expect(helikon_codec_read(&codec, pcm, CHUNK_FRAMES, &frames) ==
                           HELIKON_OK,
                       "MP3 family seek read"))
                return 1;
            if (frames == 0u)
                break;
            after = frames;
        }
        if (expect(after > 0u, "MP3 family seek resumes"))
            return 1;
        helikon_codec_close(&codec);
        printf("MP3 family %s: %u frames at %u Hz, %u out\n", item->label,
               item->total_frames, item->rate, item->decoded);
    }
    return 0;
}

static size_t text_len(const char *text)
{
    size_t length = 0u;
    while (text[length] != '\0')
        ++length;
    return length;
}

static int text_is(const char *text, const char *wanted)
{
    size_t index = 0u;
    for (;;) {
        if (text[index] != wanted[index])
            return 0;
        if (text[index] == '\0')
            return 1;
        ++index;
    }
}

/* Builds `fLaC` + a STREAMINFO block + a VORBIS_COMMENT block from `body`. */
static size_t make_flac_comment(uint8_t *data, const uint8_t *body,
                                size_t body_size, int last)
{
    size_t offset = 0u;
    helikon_memcpy(data + offset, "fLaC", 4u);
    offset += 4u;
    data[offset] = 0x00u; /* STREAMINFO, not last */
    data[offset + 1u] = 0u;
    data[offset + 2u] = 0u;
    data[offset + 3u] = 34u;
    offset += 4u;
    helikon_memset(data + offset, 0, 34u);
    offset += 34u;
    if (body == NULL)
        return offset;
    data[offset] = (uint8_t)(last ? 0x84u : 0x04u);
    data[offset + 1u] = (uint8_t)((body_size >> 16) & 0xffu);
    data[offset + 2u] = (uint8_t)((body_size >> 8) & 0xffu);
    data[offset + 3u] = (uint8_t)(body_size & 0xffu);
    offset += 4u;
    helikon_memcpy(data + offset, body, body_size);
    return offset + body_size;
}

/* Vendor "test", two complete comments and the given raw comment list. */
static size_t make_comment_body(uint8_t *body, const char *const *comments,
                                size_t count, const uint8_t *raw,
                                size_t raw_size)
{
    uint32_t vendor = 4u;
    size_t offset = 0u;
    helikon_memcpy(body + offset, &vendor, 4u);
    offset += 4u;
    helikon_memcpy(body + offset, "test", 4u);
    offset += 4u;
    uint32_t total = (uint32_t)(count + (raw != NULL ? 1u : 0u));
    helikon_memcpy(body + offset, &total, 4u);
    offset += 4u;
    for (size_t index = 0u; index < count; ++index) {
        uint32_t length = (uint32_t)text_len(comments[index]);
        helikon_memcpy(body + offset, &length, 4u);
        offset += 4u;
        helikon_memcpy(body + offset, comments[index], length);
        offset += length;
    }
    if (raw != NULL) {
        uint32_t length = (uint32_t)raw_size;
        helikon_memcpy(body + offset, &length, 4u);
        offset += 4u;
        helikon_memcpy(body + offset, raw, raw_size);
        offset += raw_size;
    }
    return offset;
}

static int check_vorbis_tags(void)
{
    static uint8_t body[1024];
    static uint8_t tag_file[2048];
    struct helikon_vorbis_tags tags;
    const char *const comments[] = {
        "TITLE=Hello World",
        "ARTIST=\xc3\x84rtist",
        "ALBUM=An Album",
        "ALBUMARTIST=Various",
        "GENRE=Test",
        "DATE=1999-12-31",
        "TRACKNUMBER=7/12",
        "DISCNUMBER=2/2",
    };
    size_t body_size = make_comment_body(body, comments, 8u, NULL, 0u);
    size_t size = make_flac_comment(tag_file, body, body_size, 1);

    if (expect(helikon_flac_tags_buffer(tag_file, size, &tags) == HELIKON_OK,
               "vorbis buffer parse"))
        return 1;
    if (expect(text_is(tags.title, "Hello World") &&
                   text_is(tags.artist, "\xc3\x84rtist") &&
                   text_is(tags.album, "An Album") &&
                   text_is(tags.album_artist, "Various") &&
                   text_is(tags.genre, "Test") &&
                   text_is(tags.year, "1999") && tags.track == 7u &&
                   tags.disc == 2u,
               "vorbis buffer fields"))
        return 1;

    /* First value wins for a repeated field. */
    const char *const repeated[] = {"TITLE=First", "TITLE=Second"};
    body_size = make_comment_body(body, repeated, 2u, NULL, 0u);
    size = make_flac_comment(tag_file, body, body_size, 1);
    if (expect(helikon_flac_tags_buffer(tag_file, size, &tags) == HELIKON_OK &&
                   text_is(tags.title, "First"),
               "vorbis first value wins"))
        return 1;

    /* Invalid UTF-8 drops the value instead of storing half a character. */
    const uint8_t bad[] = {'T', 'I', 'T', 'L', 'E', '=', 0xffu, 'x'};
    body_size = make_comment_body(body, NULL, 0u, bad, sizeof(bad));
    size = make_flac_comment(tag_file, body, body_size, 1);
    if (expect(helikon_flac_tags_buffer(tag_file, size, &tags) == HELIKON_OK &&
                   tags.title[0] == '\0',
               "vorbis invalid utf8"))
        return 1;

    /* A comment with no '=' is malformed. */
    const uint8_t no_equal[] = {'T', 'I', 'T', 'L', 'E'};
    body_size = make_comment_body(body, NULL, 0u, no_equal, sizeof(no_equal));
    size = make_flac_comment(tag_file, body, body_size, 0);
    if (expect(helikon_flac_tags_buffer(tag_file, size, &tags) ==
                   HELIKON_ERR_FORMAT,
               "vorbis missing equals"))
        return 1;

    /* A declared comment length past the block is malformed. */
    body_size = make_comment_body(body, NULL, 0u, NULL, 0u);
    body[body_size - 4u] = 0xffu; /* count = 255 with no data after it */
    size = make_flac_comment(tag_file, body, body_size, 0);
    if (expect(helikon_flac_tags_buffer(tag_file, size, &tags) ==
                   HELIKON_ERR_FORMAT,
               "vorbis short block"))
        return 1;

    /* No comment block at all. */
    size = make_flac_comment(tag_file, NULL, 0u, 0);
    tag_file[4] = 0x80u; /* STREAMINFO is the last block */
    if (expect(helikon_flac_tags_buffer(tag_file, size, &tags) ==
                   HELIKON_ERR_NOT_FOUND,
               "vorbis no block"))
        return 1;

    /* Not a FLAC file. */
    helikon_memcpy(tag_file, "RIFF", 4u);
    if (expect(helikon_flac_tags_buffer(tag_file, 64u, &tags) ==
                   HELIKON_ERR_NOT_FOUND,
               "vorbis not flac"))
        return 1;

    /* The same bytes through a pull source with short reads. */
    body_size = make_comment_body(body, comments, 8u, NULL, 0u);
    size = make_flac_comment(tag_file, body, body_size, 1);
    struct fake_media media;
    fake_media_init(&media, tag_file, size);
    media.chunk = 61u;
    struct helikon_codec_source source;
    source.read_at = fake_read_at;
    source.size = media.reported;
    source.context = &media;
    if (expect(helikon_flac_tags_source(&source, &tags) == HELIKON_OK &&
                   text_is(tags.title, "Hello World") &&
                   tags.track == 7u && tags.disc == 2u,
               "vorbis source parse"))
        return 1;
    source.size = 0u;
    if (expect(helikon_flac_tags_source(&source, &tags) ==
                   HELIKON_ERR_UNSUPPORTED,
               "vorbis source without size"))
        return 1;
    if (expect(helikon_flac_tags_source(NULL, &tags) == HELIKON_ERR_ARGUMENT &&
                   helikon_vorbis_comment_parse(NULL, 0u, &tags) ==
                       HELIKON_ERR_ARGUMENT &&
                   helikon_vorbis_comment_parse(body, body_size, NULL) ==
                       HELIKON_ERR_ARGUMENT,
               "vorbis bad arguments"))
        return 1;

    /* A real encoder file: stereo16 has an empty comment block, tagged16 has
     * the fixture tags. */
    size_t tagged = load_file("fixtures/tagged16.flac", tag_file,
                              sizeof(tag_file));
    if (expect(tagged > 0u, "vorbis tagged fixture load"))
        return 1;
    if (expect(helikon_flac_tags_buffer(tag_file, tagged, &tags) ==
                   HELIKON_OK &&
                   text_is(tags.title, "Tagged Track") &&
                   text_is(tags.artist, "Test Artist") &&
                   text_is(tags.album, "Test Album") &&
                   text_is(tags.album_artist, "Various Artists") &&
                   text_is(tags.genre, "Test Genre") &&
                   text_is(tags.year, "2021") && tags.track == 3u &&
                   tags.disc == 2u,
               "vorbis tagged fixture"))
        return 1;
    fake_media_init(&media, tag_file, tagged);
    media.chunk = 173u;
    source.read_at = fake_read_at;
    source.size = media.reported;
    source.context = &media;
    if (expect(helikon_flac_tags_source(&source, &tags) == HELIKON_OK &&
                   text_is(tags.title, "Tagged Track"),
               "vorbis tagged fixture source"))
        return 1;

    size_t plain = load_file("fixtures/stereo16.flac", tag_file,
                             sizeof(tag_file));
    if (expect(plain > 0u, "vorbis plain fixture load"))
        return 1;
    if (expect(helikon_flac_tags_buffer(tag_file, plain, &tags) == HELIKON_OK &&
                   tags.title[0] == '\0' && tags.track == 0u,
               "vorbis plain fixture"))
        return 1;
    return 0;
}

static int check_id3v2(void)
{
    struct helikon_id3v2_tags tags;

    /* v2.3, Latin-1 text and a "3/12" track pair. */
    size_t offset = 10u;
    const uint8_t latin1_cafe[] = { 'C', 'a', 'f', 0xe9 };
    offset = put_text_frame(file_data, offset, "TIT2", 0u, latin1_cafe,
                            sizeof(latin1_cafe), 0);
    const uint8_t artist[] = { 'A', 'r', 't', 'i', 's', 't' };
    offset = put_text_frame(file_data, offset, "TPE1", 0u, artist,
                            sizeof(artist), 0);
    const uint8_t album[] = { 'A', 'l', 'b', 'u', 'm' };
    offset = put_text_frame(file_data, offset, "TALB", 0u, album,
                            sizeof(album), 0);
    const uint8_t track[] = { '3', '/', '1', '2' };
    offset = put_text_frame(file_data, offset, "TRCK", 0u, track,
                            sizeof(track), 0);
    const uint8_t year[] = { '2', '0', '0', '4' };
    offset = put_text_frame(file_data, offset, "TYER", 0u, year,
                            sizeof(year), 0);
    size_t size = wrap_tag(file_data, offset - 10u, 3u, 0u);

    if (expect(helikon_id3v2_tag_size(file_data, size) == size,
               "ID3v2 tag size"))
        return 1;
    if (expect(helikon_id3v2_parse(file_data, size, &tags) == HELIKON_OK,
               "ID3v2 v2.3 parse"))
        return 1;
    if (expect(tags.version == 3u && tags.track == 3u && tags.disc == 0u &&
                   tags.title[0] == 'C' && tags.title[1] == 'a' &&
                   tags.title[2] == 'f' &&
                   (uint8_t)tags.title[3] == 0xc3u &&
                   (uint8_t)tags.title[4] == 0xa9u &&
                   tags.title[5] == '\0',
               "ID3v2 Latin-1 title"))
        return 1;
    if (expect(helikon_memcmp(tags.artist, "Artist", 6u) == 0 &&
                   helikon_memcmp(tags.album, "Album", 5u) == 0 &&
                   helikon_memcmp(tags.year, "2004", 4u) == 0,
               "ID3v2 v2.3 fields"))
        return 1;

    /* v2.4, UTF-8 and UTF-16LE-with-BOM, sync-safe frame sizes. */
    offset = 10u;
    const uint8_t utf8_title[] = { 'T', 0xc3, 0xaf, 't', 'l', 'e' };
    offset = put_text_frame(file_data, offset, "TIT2", 3u, utf8_title,
                            sizeof(utf8_title), 1);
    const uint8_t utf16_artist[] = { 0xff, 0xfe, 'U', 0x00, 0xdc, 0x00,
                                     'n', 0x00, 'i', 0x00 };
    offset = put_text_frame(file_data, offset, "TPE1", 1u, utf16_artist,
                            sizeof(utf16_artist), 1);
    size = wrap_tag(file_data, offset - 10u, 4u, 0u);

    if (expect(helikon_id3v2_parse(file_data, size, &tags) == HELIKON_OK,
               "ID3v2 v2.4 parse"))
        return 1;
    if (expect(tags.version == 4u &&
                   helikon_memcmp(tags.title, "T\xc3\xaftle", 6u) == 0,
               "ID3v2 UTF-8 title"))
        return 1;
    /* LE BOM, U+0055 U+00DC U+006E U+0069 -> "U" + C3 9C + "ni". */
    if (expect(helikon_memcmp(tags.artist, "U\xc3\x9cni", 5u) == 0,
               "ID3v2 UTF-16 title"))
        return 1;

    /* No tag, a truncated tag and an unsupported flag are distinguished. */
    if (expect(helikon_id3v2_parse((const uint8_t *)"RIFF", 4u, &tags) ==
                   HELIKON_ERR_NOT_FOUND,
               "ID3v2 missing"))
        return 1;
    if (expect(helikon_id3v2_tag_size(NULL, 0u) == 0u, "ID3v2 null"))
        return 1;

    const uint8_t unsync[] = { 'I', 'D', '3', 3, 0, 0x80, 0, 0, 0, 0 };
    if (expect(helikon_id3v2_parse(unsync, sizeof(unsync), &tags) ==
                   HELIKON_ERR_UNSUPPORTED,
               "ID3v2 unsynchronization"))
        return 1;

    /* A frame whose declared length runs past the tag is FORMAT, not a read. */
    offset = 10u;
    const uint8_t one[] = { 'x' };
    offset = put_text_frame(file_data, offset, "TIT2", 0u, one, sizeof(one), 0);
    size = wrap_tag(file_data, offset - 10u, 3u, 0u);
    file_data[14] = 0x00u;
    file_data[15] = 0x00u;
    file_data[16] = 0x7fu;
    file_data[17] = 0xffu;
    if (expect(helikon_id3v2_parse(file_data, size, &tags) ==
                   HELIKON_ERR_FORMAT,
               "ID3v2 frame overrun"))
        return 1;
    return 0;
}

static int check_source_contract(void)
{
    struct fake_media media;
    fake_media_init(&media, file_data, 16u);
    struct helikon_codec_source source;
    source.read_at = fake_read_at;
    source.size = media.reported;
    source.context = &media;
    struct helikon_codec codec;

    /* Every decoder now pulls; a source without read_at is refused. */
    struct helikon_codec_source broken = source;
    broken.read_at = NULL;
    if (expect(helikon_codec_open_source(&codec, &helikon_mp3_codec_ops,
                                         &broken, workspace,
                                         sizeof(workspace)) ==
                   HELIKON_ERR_ARGUMENT,
               "source without read_at"))
        return 1;
    if (expect(helikon_codec_open_source(NULL, &helikon_wav_codec_ops, &source,
                                         workspace,
                                         sizeof(workspace)) ==
                   HELIKON_ERR_ARGUMENT,
               "source null codec"))
        return 1;
    if (expect(helikon_codec_open_source(&codec, &helikon_wav_codec_ops, NULL,
                                         workspace,
                                         sizeof(workspace)) ==
                   HELIKON_ERR_ARGUMENT,
               "source null media"))
        return 1;

    /* The streaming flag must match what each descriptor really does. */
    if (expect(helikon_wav_codec_ops.streaming == 1u &&
                   helikon_flac_codec_ops.streaming == 1u &&
                   helikon_mp3_codec_ops.streaming == 1u,
               "streaming flags"))
        return 1;
    return 0;
}

/* Same file through a source that answers short reads, like the VFS media. */
static int check_aiff_source(void)
{
    size_t size = load_file("fixtures/aiff_stereo16.aiff", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "AIFF source fixture load"))
        return 1;
    size_t expected_size = load_file("fixtures/aiff_stereo16.pcm",
                                     expected_data, sizeof(expected_data));
    if (expect(expected_size > 0u, "AIFF source PCM load"))
        return 1;

    struct fake_media media;
    fake_media_init(&media, file_data, size);
    struct helikon_codec_source source;
    source.read_at = fake_read_at;
    source.size = media.reported;
    source.context = &media;

    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe_source(&source, &ops, &info) ==
                       HELIKON_OK &&
                   ops == &helikon_aiff_codec_ops,
               "AIFF source probe"))
        return 1;

    struct helikon_codec codec;
    if (expect(helikon_codec_open_source(&codec, ops, &source, workspace,
                                         sizeof(workspace)) == HELIKON_OK,
               "AIFF source open"))
        return 1;
    size_t frames = 0u;
    size_t total = codec.info.total_frames;
    if (expect(total == 1024u, "AIFF source frame count"))
        return 1;
    if (expect(read_all(&codec, total, &frames) == HELIKON_OK &&
                   frames == total,
               "AIFF source decode"))
        return 1;
    for (size_t frame = 0u; frame < total; ++frame) {
        int16_t left = expected_sample(expected_data, frame, 2u, 16u, 0u);
        int16_t right = expected_sample(expected_data, frame, 2u, 16u, 1u);
        if (expect(pcm[frame * 2u] == left &&
                       pcm[frame * 2u + 1u] == right,
                   "AIFF source sample data"))
            return 1;
    }
    helikon_codec_close(&codec);
    return 0;
}

static int check_registry(void)
{
    const struct helikon_codec_ops *entries[8];
    unsigned int count = helikon_codec_registry(entries, 8u);
    if (expect(count == 4u, "registry count"))
        return 1;
    if (expect(entries[0] == &helikon_wav_codec_ops &&
                   entries[1] == &helikon_aiff_codec_ops &&
                   entries[2] == &helikon_flac_codec_ops &&
                   entries[3] == &helikon_mp3_codec_ops,
               "registry order"))
        return 1;

    if (expect(helikon_codec_matches_extension(entries[0], "a/B.WAV") &&
                   helikon_codec_matches_extension(entries[0], "a.wav") &&
                   helikon_codec_matches_extension(entries[1], "x/y.aiff") &&
                   helikon_codec_matches_extension(entries[1], "take.AIFC") &&
                   helikon_codec_matches_extension(entries[2], "x/y.FLAC") &&
                   helikon_codec_matches_extension(entries[3], "song.mp3"),
               "extension matches"))
        return 1;
    if (expect(!helikon_codec_matches_extension(entries[3], "song.mp4") &&
                   !helikon_codec_matches_extension(entries[3], "mp3") &&
                   !helikon_codec_matches_extension(entries[2], "flac.fl") &&
                   !helikon_codec_matches_extension(entries[1], "song.aif "),
               "extension rejects"))
        return 1;

    /* Every descriptor must declare its family contract. */
    for (unsigned int i = 0u; i < count; ++i) {
        if (expect(entries[i]->probe != NULL && entries[i]->open != NULL &&
                       entries[i]->read != NULL && entries[i]->close != NULL &&
                       entries[i]->name != NULL &&
                       entries[i]->extensions != NULL &&
                       entries[i]->stack_bytes > 0u &&
                       entries[i]->min_clock_hz > 0u &&
                       entries[i]->recommended_clock_hz >=
                           entries[i]->min_clock_hz &&
                       (entries[i]->capabilities &
                        (HELIKON_CODEC_CAP_SEEK | HELIKON_CODEC_CAP_GAPLESS |
                         HELIKON_CODEC_CAP_RESUME)) != 0u,
                   "descriptor contract"))
            return 1;
    }

    /* Wrong extension still probes by header. */
    size_t size = load_file("fixtures/mp3_cbr_stereo16.mp3", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "MP3 header-decision load"))
        return 1;
    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(file_data, size, &ops, &info) ==
                   HELIKON_OK && ops == &helikon_mp3_codec_ops,
               "probe ignores extension"))
        return 1;

    /* An empty buffer refuses: no probe can claim it. */
    if (expect(helikon_codec_probe(file_data, 0u, &ops, &info) ==
                   HELIKON_ERR_ARGUMENT,
               "zero buffer probe"))
        return 1;
    return 0;
}

static int check_malformed(void)
{
    size_t size = load_file("fixtures/stereo16.flac", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "malformed load"))
        return 1;

    helikon_memcpy(expected_data, file_data, size);
    expected_data[0] = 'X';
    const struct helikon_codec_ops *ops = NULL;
    struct helikon_codec_info info;
    if (expect(helikon_codec_probe(expected_data, size, &ops, &info) ==
                   HELIKON_ERR_UNSUPPORTED,
               "unknown magic"))
        return 1;

    if (expect(helikon_codec_probe(file_data, 20u, &ops, &info) ==
                   HELIKON_ERR_FORMAT,
               "truncated metadata"))
        return 1;

    helikon_memcpy(expected_data, file_data, size);
    size_t first_frame = flac_first_frame(expected_data, size);
    if (expect(first_frame > 0u && first_frame + 20u < size,
               "first frame offset"))
        return 1;
    expected_data[first_frame + 10u] ^= 0xffu;
    if (expect(helikon_codec_probe(expected_data, size, &ops, &info) ==
                   HELIKON_OK,
               "corrupt probe"))
        return 1;
    struct helikon_codec codec;
    if (expect(helikon_codec_open(&codec, ops, expected_data, size, workspace,
                                  sizeof(workspace)) == HELIKON_OK,
               "corrupt open"))
        return 1;
    size_t frames = 0u;
    int rc = read_all(&codec, info.total_frames, &frames);
    if (expect(rc == HELIKON_ERR_FORMAT, "corrupt frame error"))
        return 1;
    helikon_codec_close(&codec);
    return 0;
}

int main(void)
{
    if (check_flac("stereo16", 2u, 16u, 44100u, 2048u))
        return 1;
    if (check_flac("mono16", 1u, 16u, 44100u, 1024u))
        return 1;
    if (check_flac("stereo24", 2u, 24u, 48000u, 1200u))
        return 1;
    if (check_flac("mono24", 1u, 24u, 48000u, 800u))
        return 1;
    if (check_flac("constant16", 2u, 16u, 44100u, 512u))
        return 1;
    if (check_flac("verbatim16", 2u, 16u, 44100u, 512u))
        return 1;
    if (check_flac("stereo16_48", 2u, 16u, 48000u, 1024u))
        return 1;
    if (check_flac("left_side16", 2u, 16u, 44100u, 1024u))
        return 1;
    if (check_aiff("aiff_stereo16", "aiff", 44100u, 2u, 16u, 1024u))
        return 1;
    if (check_aiff("aiff_mono16", "aiff", 44100u, 1u, 16u, 512u))
        return 1;
    if (check_aiff("aiff_stereo24", "aiff", 48000u, 2u, 24u, 512u))
        return 1;
    if (check_aiff("aiff_sowt16", "aifc", 44100u, 2u, 16u, 1024u))
        return 1;
    if (check_aiff("aiff_fl32", "aifc", 44100u, 2u, 32u, 256u))
        return 1;
    if (check_aiff("aiff_8bit", "aiff", 22050u, 1u, 8u, 256u))
        return 1;
    if (check_aiff("aiff_ssnd_first", "aiff", 44100u, 2u, 16u, 256u))
        return 1;
    /* COMM claims 1024 frames; the sound chunk carries 512. */
    if (check_aiff("aiff_short_data", "aiff", 44100u, 2u, 16u, 512u))
        return 1;
    /* NaN, both infinities and values past full scale: clamped, not UB. */
    if (check_aiff("aiff_fl32_range", "aifc", 44100u, 2u, 32u, 4u))
        return 1;
    if (check_aiff_unsupported())
        return 1;
    if (check_aiff_source())
        return 1;
    if (check_wav())
        return 1;
    if (check_wav_source())
        return 1;
    if (check_wav_prefix())
        return 1;
    if (check_flac_source())
        return 1;
    if (check_mp3_source())
        return 1;
    if (check_vorbis_tags())
        return 1;
    if (check_id3v2())
        return 1;
    if (check_mp3_gapless())
        return 1;
    if (check_source_contract())
        return 1;
    if (check_mp3_cbr_stereo())
        return 1;
    if (check_mp3_mono_vbr_id3())
        return 1;
    if (check_mp3_family())
        return 1;
    if (check_registry())
        return 1;
    if (check_malformed())
        return 1;

    printf("PASS codec\n");
    return 0;
}
