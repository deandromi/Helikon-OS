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

/* Descriptor capability flags (per family, from the opening header). */
#define HELIKON_CODEC_CAP_SEEK    0x00000001u
#define HELIKON_CODEC_CAP_GAPLESS 0x00000002u
#define HELIKON_CODEC_CAP_RESUME  0x00000004u

/* Per-file information flags. */
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

/*
 * A decoder is a static descriptor. The probe decides by header, never by
 * file name: `extensions` is an informational NULL-terminated lowercase
 * list used by the library browser only. `capabilities` reports the
 * family-wide contract; a caller still receives a specific error from a
 * seek that a particular file cannot serve. `stack_bytes` is the measured
 * or bounded worst-case decode path stack; `min_clock_hz` and
 * `recommended_clock_hz` are the CPU-clock gates from the framework plan.
 */
struct helikon_codec_ops {
    const char *name;
    const char *const *extensions;
    uint32_t capabilities;
    uint32_t stack_bytes;
    uint32_t min_clock_hz;
    uint32_t recommended_clock_hz;
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

/* Descriptor extensions helpers: registry and browser queries. */

/* Returns 1 if the descriptor's `extensions` list contains `path`'s
 * extension (case-insensitive, after the final dot). */
int helikon_codec_matches_extension(const struct helikon_codec_ops *ops,
                                    const char *path);

/* Walks the registry; returns the count or fills up to `ops` entries. */
unsigned int helikon_codec_registry(const struct helikon_codec_ops **ops,
                                    unsigned int max_ops);
