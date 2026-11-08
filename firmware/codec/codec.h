/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded codec registry and PCM read interface.
 *
 * A decoder is a static descriptor with a probe, open, read, seek and close
 * operation. It receives a fixed workspace owned by the caller and emits
 * interleaved stereo 16-bit PCM for the existing audio ring. No decoder uses
 * malloc and no decoder touches storage directly.
 */
#ifndef HELIKON_CODEC_CODEC_H
#define HELIKON_CODEC_CODEC_H

#include <stddef.h>
#include <stdint.h>

/* The accepted codec workspace budget; FLAC uses well under this. */
#define HELIKON_CODEC_WORKSPACE_MAX (128u * 1024u)

#define HELIKON_CODEC_FLAG_SEEK 0x00000001u

struct helikon_codec_info {
    uint32_t sample_rate;
    uint32_t total_frames;
    uint32_t duration_ms;
    uint16_t source_channels;
    uint16_t source_bits;
    uint16_t output_channels;
    uint16_t output_bits;
    uint32_t workspace_bytes;
    uint32_t flags;
};

struct helikon_codec;

struct helikon_codec_ops {
    const char *name;
    int (*probe)(const uint8_t *data, size_t size,
                 struct helikon_codec_info *info);
    int (*open)(struct helikon_codec *codec, void *workspace,
                size_t workspace_size);
    int (*read)(struct helikon_codec *codec, int16_t *pcm, size_t max_frames,
                size_t *frames);
    int (*seek)(struct helikon_codec *codec, uint32_t frame);
    void (*close)(struct helikon_codec *codec);
};

struct helikon_codec {
    const struct helikon_codec_ops *ops;
    struct helikon_codec_info info;
    const uint8_t *data;
    size_t size;
    void *state;
    void *workspace;
    size_t workspace_size;
};

/*
 * Returns HELIKON_OK and the matching descriptor, HELIKON_ERR_UNSUPPORTED if
 * no descriptor recognizes the header, or the specific probe error from a
 * recognized format. `HELIKON_ERR_NOT_FOUND` is internal to the registry.
 */
int helikon_codec_probe(const uint8_t *data, size_t size,
                        const struct helikon_codec_ops **ops,
                        struct helikon_codec_info *info);
int helikon_codec_open(struct helikon_codec *codec,
                       const struct helikon_codec_ops *ops,
                       const uint8_t *data, size_t size, void *workspace,
                       size_t workspace_size);
int helikon_codec_read(struct helikon_codec *codec, int16_t *pcm,
                       size_t max_frames, size_t *frames);
int helikon_codec_seek(struct helikon_codec *codec, uint32_t frame);
void helikon_codec_close(struct helikon_codec *codec);

extern const struct helikon_codec_ops helikon_wav_codec_ops;
extern const struct helikon_codec_ops helikon_flac_codec_ops;

#endif
