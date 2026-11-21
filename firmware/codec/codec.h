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

/*
 * Bytes of the stream head handed to a probe. A streaming decoder sees this
 * prefix instead of the whole file, so a probe must decide from the head and
 * report HELIKON_ERR_NEED_MORE when the prefix ends before it can.
 */
#define HELIKON_CODEC_PROBE_BYTES 4096u

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
 * Where a decoder gets its bytes. `read_at` reads up to `length` bytes at an
 * absolute stream offset and reports what it actually returned; it never
 * requires the caller to hold the whole stream. A decoder that only supports
 * whole-buffer parsing says so with `streaming == 0` and is refused a
 * non-memory source instead of silently reading a header prefix as if it were
 * the file. `size` is 0 when the media cannot report a length.
 */
struct helikon_codec_source {
    int (*read_at)(void *context, uint64_t offset, void *buffer,
                   size_t length, size_t *read);
    uint64_t size;
    void *context;
};

/*
 * A decoder is a static descriptor. The probe decides by header, never by
 * file name: `extensions` is an informational NULL-terminated lowercase
 * list used by the library browser only. `capabilities` reports the
 * family-wide contract; a caller still receives a specific error from a
 * seek that a particular file cannot serve. `stack_bytes` is the measured
 * or bounded worst-case decode path stack; `min_clock_hz` and
 * `recommended_clock_hz` are the CPU-clock gates from the framework plan.
 * `streaming` is 1 when the decoder pulls through the source instead of
 * parsing a whole-file buffer.
 *
 * The probe receives the head of the stream (`data`, `size`) plus the true
 * stream length in `total_size` (0 when unknown). It returns HELIKON_OK,
 * HELIKON_ERR_NOT_FOUND when the head is another format's, a specific error
 * for a recognized-but-invalid head, or HELIKON_ERR_NEED_MORE when the head
 * is too short to decide and the stream continues past it.
 */
struct helikon_codec_ops {
    const char *name;
    const char *const *extensions;
    uint32_t capabilities;
    uint8_t streaming;
    uint32_t stack_bytes;
    uint32_t min_clock_hz;
    uint32_t recommended_clock_hz;
    int (*probe)(const uint8_t *data, size_t size, uint64_t total_size,
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
    /* Points at the caller's source, or at `source_storage` below. */
    const struct helikon_codec_source *source;
    /* Backing store for the whole-buffer adapter; never a stack temporary. */
    struct helikon_codec_source source_storage;
    /* Whole stream for a memory source, otherwise the probe prefix. */
    const uint8_t *data;
    size_t size;
    int memory;
    uint8_t prefix[HELIKON_CODEC_PROBE_BYTES];
    void *state;
    void *workspace;
    size_t workspace_size;
};

/*
 * Returns HELIKON_OK and the matching descriptor, HELIKON_ERR_UNSUPPORTED if
 * no descriptor recognizes the header, or the specific probe error from a
 * recognized format. `HELIKON_ERR_NOT_FOUND` is internal to the registry.
 * This form always sees the whole buffer, so it never reports NEED_MORE.
 */
int helikon_codec_probe(const uint8_t *data, size_t size,
                        const struct helikon_codec_ops **ops,
                        struct helikon_codec_info *info);
int helikon_codec_open(struct helikon_codec *codec,
                       const struct helikon_codec_ops *ops,
                       const uint8_t *data, size_t size, void *workspace,
                       size_t workspace_size);
/*
 * Opens over an arbitrary source. A descriptor with `streaming == 0` returns
 * HELIKON_ERR_UNSUPPORTED unless the source exposes the whole stream as one
 * contiguous buffer (see helikon_codec_memory_span). A decoder that reports
 * NEED_MORE fills `codec->info` during open instead of at probe time; `info`
 * is authoritative once open returns HELIKON_OK.
 */
int helikon_codec_open_source(struct helikon_codec *codec,
                              const struct helikon_codec_ops *ops,
                              const struct helikon_codec_source *source,
                              void *workspace, size_t workspace_size);
/*
 * Returns 1 and the whole stream when the open codec's data is one
 * contiguous buffer, 0 for a streaming source. Used by whole-buffer decoders
 * that have not been converted yet.
 */
int helikon_codec_memory_span(const struct helikon_codec *codec,
                              const uint8_t **data, size_t *size);
int helikon_codec_read(struct helikon_codec *codec, int16_t *pcm,
                       size_t max_frames, size_t *frames);
int helikon_codec_seek(struct helikon_codec *codec, uint32_t frame);
void helikon_codec_close(struct helikon_codec *codec);

extern const struct helikon_codec_ops helikon_wav_codec_ops;
extern const struct helikon_codec_ops helikon_flac_codec_ops;
extern const struct helikon_codec_ops helikon_mp3_codec_ops;

/* Descriptor extension and registry queries for the library browser. */

/* Returns 1 if the descriptor's `extensions` list contains `path`'s
 * extension (case-insensitive, after the final dot). */
int helikon_codec_matches_extension(const struct helikon_codec_ops *ops,
                                    const char *path);

/* Walks the registry; returns the count or fills up to `ops` entries. */
unsigned int helikon_codec_registry(const struct helikon_codec_ops **ops,
                                    unsigned int max_ops);

#endif
