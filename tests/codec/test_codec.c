/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "codec.h"

#include <stdio.h>
#include <string.h>

#include "error.h"
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

static size_t make_wav(uint8_t *data, size_t frames)
{
    size_t data_bytes = frames * 4u;
    size_t riff_size = 36u + data_bytes;
    memset(data, 0, 44u + data_bytes);
    memcpy(data, "RIFF", 4u);
    data[4] = (uint8_t)(riff_size & 0xffu);
    data[5] = (uint8_t)((riff_size >> 8) & 0xffu);
    data[6] = (uint8_t)((riff_size >> 16) & 0xffu);
    data[7] = (uint8_t)(riff_size >> 24);
    memcpy(data + 8u, "WAVE", 4u);
    memcpy(data + 12u, "fmt ", 4u);
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
    memcpy(data + 36u, "data", 4u);
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

static int check_malformed(void)
{
    size_t size = load_file("fixtures/stereo16.flac", file_data,
                            sizeof(file_data));
    if (expect(size > 0u, "malformed load"))
        return 1;

    memcpy(expected_data, file_data, size);
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

    memcpy(expected_data, file_data, size);
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
    if (check_wav())
        return 1;
    if (check_malformed())
        return 1;

    printf("PASS codec\n");
    return 0;
}
