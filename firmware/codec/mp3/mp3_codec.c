/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MP3 codec wrapper over vendored libmad (see codec/libmad/README.md).
 * Helikon-owned code; libmad stays untouched behind its own headers.
 */

#include "codec.h"
#include "mp3.h"

#include <stddef.h>
#include <stdint.h>

/* Include the port shim first: it provides the FPM setting and the port
 * attribute macros the libmad headers assume (see codec/libmad/README.md). */
#include "../libmad/libmad_port.h"
#include "../libmad/mad.h"
#include "../libmad/stream.h"
#include "../libmad/frame.h"
#include "../libmad/synth.h"

#include "error.h"
#include "mem.h"

/* Bound on consecutive recoverable errors before a file is declared bad. */
#define MP3_MAX_RECOVERABLE 64u

/*
 * Pull-source window. libmad needs whole frames plus guard bytes and keeps
 * its bit reservoir in `main_data` (a copy), so a refill only has to preserve
 * the bytes from `stream.next_frame` onward.
 */
#define HELIKON_MP3_WINDOW 32768u



/* ---------- header parsing (probe side) ---------- */

struct mp3_header {
    uint16_t sample_rate;
    uint16_t channels;
    uint8_t layer;       /* 1, 2 or 3 */
    uint8_t version;     /* 3 = MPEG-1, 2 = MPEG-2, 0 = MPEG-2.5 */
    uint8_t channel_mode;
    uint32_t bitrate;    /* bits per second */
    uint32_t frame_bytes;
    uint32_t frame_samples;
    uint16_t offset;     /* header byte position in the stream */
    uint8_t crc;         /* protection bit (1 = no CRC) */
};

static const uint16_t mp3_samplerates[3][3] = {
    { 44100u, 48000u, 32000u }, /* MPEG-1 */
    { 22050u, 24000u, 16000u }, /* MPEG-2 */
    { 11025u, 12000u,  8000u }, /* MPEG-2.5 */
};

static const uint16_t mp3_bitrates[5][15] = {
    { 0u,  32u,  64u,  96u, 128u, 160u, 192u, 224u, 256u, 288u, 320u,
      352u, 384u, 416u, 448u },                     /* MPEG-1 Layer I    */
    { 0u,  32u,  48u,  56u,  64u,  80u,  96u, 112u, 128u, 160u, 192u,
      224u, 256u, 320u, 384u },                     /* MPEG-1 Layer II   */
    { 0u,  32u,  40u,  48u,  56u,  64u,  80u,  96u, 112u, 128u, 160u,
      192u, 224u, 256u, 320u },                     /* MPEG-1 Layer III  */
    { 0u,  32u,  48u,  56u,  64u,  80u,  96u, 112u, 128u, 144u, 160u,
      176u, 192u, 224u, 256u },                     /* MPEG-2 LSF I      */
    { 0u,   8u,  16u,  24u,  32u,  40u,  48u,  56u,  64u,  80u,  96u,
      112u, 128u, 144u, 160u },                     /* MPEG-2/2.5 II/III */
};

static int mp3_probe_header(const uint8_t *data, struct mp3_header *head)
{
    const uint8_t version = (uint8_t)((data[1] >> 3) & 0x3u);
    if (version == 1u) /* reserved encoding */
        return HELIKON_ERR_FORMAT;
    const uint8_t layer = (uint8_t)((data[1] >> 1) & 0x3u);
    if (layer == 0u)
        return HELIKON_ERR_FORMAT; /* reserved */
    const uint8_t bitrate_index = (uint8_t)((data[2] >> 4) & 0xFu);
    const uint8_t rate_index = (uint8_t)((data[2] >> 2) & 0x3u);
    if (bitrate_index == 0u || bitrate_index == 15u || rate_index == 3u)
        return HELIKON_ERR_FORMAT;

    head->version = version;
    head->layer = (uint8_t)(4u - layer);
    head->sample_rate = mp3_samplerates[version == 3u ? 0u
                        : version == 2u ? 1u : 2u][rate_index];
    if (head->sample_rate == 0u)
        return HELIKON_ERR_FORMAT;
    head->channel_mode = (uint8_t)((data[3] >> 6) & 0x3u);
    head->channels = (head->channel_mode == 3u) ? 1u : 2u;
    head->crc = (uint8_t)(data[1] & 1u);
    head->offset = 0u;

    uint8_t rate_row;
    if (version == 3u)
        rate_row = (uint8_t)(head->layer - 1u);
    else if (head->layer == 1u)
        rate_row = 3u;
    else
        rate_row = 4u;
    head->bitrate = (uint32_t)mp3_bitrates[rate_row][bitrate_index] * 1000u;
    if (head->bitrate == 0u)
        return HELIKON_ERR_FORMAT;

    if (head->layer == 1u)
        head->frame_samples = 384u;
    else if (head->layer == 2u)
        head->frame_samples = 1152u;
    else
        head->frame_samples = (version == 3u) ? 1152u : 576u;

    const uint32_t padding = (uint32_t)((data[2] >> 1) & 1u);
    if (head->layer == 1u)
        head->frame_bytes =
            (head->bitrate / head->sample_rate + padding) * 4u;
    else
        head->frame_bytes = head->bitrate * head->frame_samples /
                            head->sample_rate / 8u + padding;
    return HELIKON_OK;
}

static int mp3_next_sync(const uint8_t *data, size_t size, size_t start,
                         struct mp3_header *head, size_t window,
                         const struct mp3_header *filter)
{
    if (size < 4u)
        return HELIKON_ERR_NOT_FOUND;
    size_t limit = size - 4u;
    if (window != 0u && limit > start + window)
        limit = start + window;
    int saw_invalid = 0;
    for (size_t i = start; i < limit; ++i) {
        if (data[i] != 0xFFu || (data[i + 1u] & 0xE0u) != 0xE0u)
            continue;
        struct mp3_header candidate;
        int rc = mp3_probe_header(data + i, &candidate);
        if (rc != HELIKON_OK) {
            saw_invalid = 1;
            continue;
        }
        /* Main data frequently contains bytes that look like a legal sync
         * with a plausible header of the WRONG layer/version; aligning to
         * one makes libmad jump past real frames. Seek resync passes a
         * filter with the file's MPEG version and layer. */
        if (filter != NULL && (candidate.version != filter->version ||
                               candidate.layer != filter->layer))
            continue;
        candidate.offset = (uint16_t)i;
        *head = candidate;
        return HELIKON_OK;
    }
    /* MPEG sync bits with an illegal header field are a recognized but
     * malformed stream; without any sync at all the format is not ours. */
    if (saw_invalid != 0)
        return HELIKON_ERR_FORMAT;
    return HELIKON_ERR_NOT_FOUND;
}

/* ---------- Xing / Info frame ---------- */

struct mp3_xing {
    uint8_t valid;
    uint32_t frames;
    uint8_t toc[100];
};

static void mp3_toc_from_be32(const uint8_t *p, uint8_t *toc)
{
    for (unsigned int i = 0u; i < 100u; ++i)
        toc[i] = p[i];
}

/*
 * Inspects the frame at `frame` for a Xing/Info tag. Returns HELIKON_OK
 * when a tag is present (even without useful fields), HELIKON_ERR_NOT_FOUND
 * otherwise.
 */
static int mp3_xing(const uint8_t *data, size_t size,
                    const struct mp3_header *frame, struct mp3_xing *xing)
{
    helikon_memset(xing, 0, sizeof(*xing));
    if (frame->layer != 3u)
        return HELIKON_ERR_NOT_FOUND;
    if ((size_t)frame->offset >= size || size - (size_t)frame->offset < 4u)
        return HELIKON_ERR_NOT_FOUND;

    size_t p = (size_t)frame->offset + 4u;
    if (frame->crc == 0u)
        p += 2u;
    /* Side information length. */
    const uint8_t side = frame->version == 3u
        ? (frame->channels == 1u ? 17u : 32u)
        : (frame->channels == 1u ? 9u : 17u);
    p += side;
    if (p + 16u > size)
        return HELIKON_ERR_NOT_FOUND;

    const uint8_t *tag = data + p;
    const uint8_t have_tag = (tag[0] == 'X' && tag[1] == 'i' && tag[2] == 'n' &&
                              tag[3] == 'g') ||
        (tag[0] == 'I' && tag[1] == 'n' && tag[2] == 'f' && tag[3] == 'o');
    if (!have_tag)
        return HELIKON_ERR_NOT_FOUND;

    const uint32_t flags =
        ((uint32_t)tag[4] << 24) | ((uint32_t)tag[5] << 16) |
        ((uint32_t)tag[6] << 8) | (uint32_t)tag[7];
    size_t q = p + 8u;
    if ((flags & 0x01u) != 0u) {
        if (q + 4u > size)
            return HELIKON_OK;
        xing->frames = ((uint32_t)data[q + 0] << 24) |
                       ((uint32_t)data[q + 1] << 16) |
                       ((uint32_t)data[q + 2] << 8) |
                       (uint32_t)data[q + 3];
        q += 4u;
    }
    if ((flags & 0x02u) != 0u)
        q += 4u;    /* bytes: not needed for the whole-buffer model */
    if ((flags & 0x04u) != 0u) {
        if (q + 100u <= size) {
            mp3_toc_from_be32(data + q, xing->toc);
        }
        q += 100u;
    }
    xing->valid = 1u;
    return HELIKON_OK;
}


struct mp3_state {
    /* Rockbox's libmad dropped malloc: big buffers are wired by the
     * wrapper (mad_frame_init expects sbsample/sbsample_prev/overlap and
     * mad_stream_buffer expects stream.main_data to point at user arrays).
     * sbsample and sbsample_prev must point at the SAME buffer: the layer
     * decoders write frame->sbsample while the Rockbox-modified synth reads
     * frame->sbsample_prev (see codec/libmad/README.md). */
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    mad_fixed_t sbsample[2][36][32];
    mad_fixed_t overlap[2][32][18];
    unsigned char main_data[MAD_BUFFER_MDLEN];
    /* Pull source and its window. The final frame cannot decode without
     * MAD_BUFFER_GUARD spare bytes past its end, so the first end-of-file
     * refill pads the window once and sets `tail_active`. */
    const struct helikon_codec_source *source;
    unsigned char window[HELIKON_MP3_WINDOW];
    uint64_t window_base;   /* absolute stream offset of window[0] */
    size_t window_len;
    uint64_t audio_start;   /* absolute offset of the first frame */
    struct mp3_header head;
    struct mp3_xing xing;
    uint8_t window_eof;
    uint8_t tail_active;
    unsigned int synth_part;      /* next synth.pcm block to emit */
    uint32_t frame_index;         /* PCM frame position of next output */
    uint32_t consecutive_errors;
    uint32_t skip_frames_left;    /* frames decoded but not emitted */
};

/*
 * Refills the window from the source, keeping the bytes from
 * `stream.next_frame` onward: libmad reports the first byte it has not
 * consumed and every frame it still needs. At end of file the first refill
 * that reads nothing pads the tail with MAD_BUFFER_GUARD zero bytes so the
 * final frame decodes, then `tail_active` stops further refills.
 */
static int mp3_window_refill(struct mp3_state *state)
{
    size_t keep = 0u;
    if (state->window_len != 0u && state->stream.next_frame != NULL) {
        const unsigned char *base = state->window;
        const unsigned char *next = state->stream.next_frame;
        if (next >= base && next <= base + state->window_len)
            keep = state->window_len - (size_t)(next - base);
    }
    size_t shift = state->window_len - keep;

    if (shift != 0u) {
        if (keep != 0u)
            helikon_memmove(state->window, state->window + shift, keep);
        state->window_base += shift;
        state->window_len = keep;
    }

    /*
     * Fill what the window can take. A media may return short reads (extent
     * boundaries, small transfers), and a nearly empty window would hide the
     * Xing/Info frame from the parser.
     */
    size_t got = 0u;
    for (;;) {
        size_t space = sizeof(state->window) - (keep + got);
        if (space == 0u)
            break;
        size_t chunk = 0u;
        int rc = state->source->read_at(state->source->context,
                                        (uint64_t)(state->window_base + keep + got),
                                        state->window + keep + got, space,
                                        &chunk);
        if (rc != HELIKON_OK)
            return rc;
        got += chunk;
        if (chunk == 0u)
            break;
    }
    state->window_len = keep + got;

    if (got == 0u) {
        state->window_eof = 1u;
        if (keep != 0u && state->tail_active == 0u &&
            sizeof(state->window) - keep >= MAD_BUFFER_GUARD) {
            helikon_memset(state->window + keep, 0, MAD_BUFFER_GUARD);
            state->window_len = keep + MAD_BUFFER_GUARD;
            state->tail_active = 1u;
        }
    } else if (shift == 0u && keep + got == sizeof(state->window) &&
               state->window_eof == 0u) {
        /* A full window that libmad cannot consume means no progress. */
        return HELIKON_ERR_FORMAT;
    }

    if (state->window_len != 0u)
        mad_stream_buffer(&state->stream, state->window,
                          (unsigned long)state->window_len);
    state->stream.error = MAD_ERROR_NONE;
    return HELIKON_OK;
}

/* Drops the window and refills it from `offset`. */
static int mp3_window_seek(struct mp3_state *state, uint64_t offset)
{
    state->window_base = offset;
    state->window_len = 0u;
    state->window_eof = 0u;
    state->tail_active = 0u;
    state->stream.next_frame = NULL;
    return mp3_window_refill(state);
}

/* The workspace travels in the player's fixed buffer; fail the build rather
 * than overflow it. Measured at 30,976 bytes on both hosts (arm-eabi-gcc 9,
 * -mcpu=arm926ej-s) and clang (arm64 host tests). */
_Static_assert(sizeof(struct mp3_state) <= HELIKON_CODEC_WORKSPACE_MAX,
               "MP3 workspace exceeds HELIKON_CODEC_WORKSPACE_MAX");

/* ---------- descriptor ---------- */

static int mp3_parse(const uint8_t *data, size_t size,
                     struct mp3_header *head, struct mp3_xing *xing)
{
    if (data == NULL)
        return HELIKON_ERR_ARGUMENT;
    size_t start = 0u;
    /* Skip an ID3v2 tag: 10-byte header, sync-safe size, optional footer. */
    if (size >= 10u && data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        const uint32_t sz = ((uint32_t)(data[6] & 0x7Fu) << 21) |
                            ((uint32_t)(data[7] & 0x7Fu) << 14) |
                            ((uint32_t)(data[8] & 0x7Fu) << 7) |
                            (uint32_t)(data[9] & 0x7Fu);
        start = 10u + sz;
        if ((data[5] & 0x10u) != 0u)
            start += 10u;
        if (start >= size)
            return HELIKON_ERR_FORMAT;
    }
    int rc = mp3_next_sync(data, size, start, head, 16u * 1024u, NULL);
    if (rc != HELIKON_OK)
        return rc;
    if (xing != NULL)
        (void)mp3_xing(data, size, head, xing);
    return HELIKON_OK;
}

/*
 * Corroborates a candidate frame: a real stream frame's declared successor
 * position holds another header of the same version and layer. A lone
 * plausible header can appear inside other formats' audio data (a FLAC
 * stream, for instance), but such bytes only rarely chain this way.
 */
static int mp3_chain_ok(const uint8_t *data, size_t size,
                        const struct mp3_header *head)
{
    const size_t expect = (size_t)head->offset + head->frame_bytes;
    if (expect + 4u > size)
        return 1; /* nothing left to corroborate; accept */
    struct mp3_header next;
    return mp3_next_sync(data, size, expect, &next, 4u, head) == HELIKON_OK;
}

static int mp3_probe(const uint8_t *data, size_t size, uint64_t total_size,
                     struct helikon_codec_info *info)
{
    if (info == NULL)
        return HELIKON_ERR_ARGUMENT;
    struct mp3_header head;
    struct mp3_xing xing;
    int rc = mp3_parse(data, size, &head, &xing);
    if (rc != HELIKON_OK) {
        /* A head ending inside a large ID3v2 tag: open skips it exactly. */
        if (total_size != size)
            return HELIKON_ERR_NEED_MORE;
        return rc;
    }
    struct mp3_header witness = head;
    for (;;) {
        if (mp3_chain_ok(data, size, &witness))
            break;
        rc = mp3_next_sync(data, size, (size_t)witness.offset + 1u, &witness,
                           16u * 1024u, NULL);
        if (rc != HELIKON_OK)
            return rc;
    }

    helikon_memset(info, 0, sizeof(*info));
    info->sample_rate = head.sample_rate;
    info->source_channels = head.channels;
    info->source_bits = 16u;
    info->output_channels = 2u;
    info->output_bits = 16u;
    info->flags = HELIKON_CODEC_FLAG_SEEK;
    if (xing.valid && xing.frames != 0u) {
        info->total_frames = xing.frames * head.frame_samples;
    } else if (head.frame_bytes != 0u) {
        const uint64_t bytes = (uint64_t)size - head.offset;
        info->total_frames =
            (uint32_t)(bytes / head.frame_bytes * head.frame_samples);
    } else {
        info->total_frames = 0u;
    }
    info->duration_ms = head.sample_rate != 0u
        ? (uint32_t)((uint64_t)info->total_frames * 1000u /
                     head.sample_rate)
        : 0u;
    info->workspace_bytes = sizeof(struct mp3_state);
    return HELIKON_OK;
}

/* ---------- libmad wrappers ---------- */

static int16_t mp3_sample16(mad_fixed_t fixed)
{
    if (fixed >= MAD_F_ONE)
        return 32767;
    if (fixed <= -MAD_F_ONE)
        return -32768;
    return (int16_t)(fixed >> (MAD_F_FRACBITS - 15u));
}

/* Fills codec info from the located stream; open is authoritative. */
static void mp3_fill_info(struct helikon_codec *codec,
                          const struct mp3_state *state)
{
    helikon_memset(&codec->info, 0, sizeof(codec->info));
    codec->info.sample_rate = state->head.sample_rate;
    codec->info.source_channels = state->head.channels;
    codec->info.source_bits = 16u;
    codec->info.output_channels = 2u;
    codec->info.output_bits = 16u;
    codec->info.workspace_bytes = sizeof(struct mp3_state);
    codec->info.flags = HELIKON_CODEC_FLAG_SEEK;

    const uint64_t stream_bytes =
        state->source->size > state->audio_start
            ? state->source->size - state->audio_start
            : 0u;
    if (state->xing.valid && state->xing.frames != 0u) {
        codec->info.total_frames = state->xing.frames * state->head.frame_samples;
    } else if (state->head.frame_bytes != 0u) {
        codec->info.total_frames =
            (uint32_t)(stream_bytes / state->head.frame_bytes *
                       state->head.frame_samples);
    }
    codec->info.duration_ms = state->head.sample_rate != 0u
        ? (uint32_t)((uint64_t)codec->info.total_frames * 1000u /
                     state->head.sample_rate)
        : 0u;
}

/* Skips an ID3v2 tag, then locates the first frame in the window. */
static int mp3_locate(struct mp3_state *state)
{
    uint8_t id3[10];
    size_t got = 0u;
    int rc = state->source->read_at(state->source->context, 0u, id3,
                                    sizeof(id3), &got);
    if (rc != HELIKON_OK)
        return rc;

    uint64_t start = 0u;
    if (got >= sizeof(id3) && id3[0] == 'I' && id3[1] == 'D' &&
        id3[2] == '3') {
        const uint64_t tag = ((uint64_t)(id3[6] & 0x7Fu) << 21) |
                             ((uint64_t)(id3[7] & 0x7Fu) << 14) |
                             ((uint64_t)(id3[8] & 0x7Fu) << 7) |
                             (uint64_t)(id3[9] & 0x7Fu);
        start = 10u + tag;
        if ((id3[5] & 0x10u) != 0u)
            start += 10u;
        if (state->source->size != 0u && start >= state->source->size)
            return HELIKON_ERR_FORMAT;
    }

    rc = mp3_window_seek(state, start);
    if (rc != HELIKON_OK)
        return rc;
    rc = mp3_parse(state->window, state->window_len, &state->head,
                   &state->xing);
    if (rc != HELIKON_OK)
        return rc;
    state->audio_start = state->window_base + state->head.offset;
    return HELIKON_OK;
}

static int mp3_open(struct helikon_codec *codec, void *workspace,
                    size_t workspace_size)
{
    if (codec == NULL || workspace == NULL ||
        workspace_size < sizeof(struct mp3_state))
        return HELIKON_ERR_ARGUMENT;

    struct mp3_state *state = workspace;
    helikon_memset(state, 0, sizeof(*state));
    state->source = codec->source;
    if (state->source == NULL || state->source->read_at == NULL)
        return HELIKON_ERR_ARGUMENT;

    /* Wire the buffers first: Rockbox's mad_frame_init calls
     * mad_frame_mute, which clears through these pointers. */
    state->frame.sbsample = &state->sbsample;
    state->frame.sbsample_prev = &state->sbsample;
    state->frame.overlap = &state->overlap;
    state->stream.main_data = &state->main_data;
    mad_stream_init(&state->stream);
    mad_frame_init(&state->frame);
    mad_synth_init(&state->synth);

    int rc = mp3_locate(state);
    if (rc != HELIKON_OK)
        return rc;

    /* Start libmad at the first frame, not at the window head. */
    const size_t within = (size_t)(state->audio_start - state->window_base);
    mad_stream_buffer(&state->stream, state->window + within,
                      (unsigned long)(state->window_len - within));
    state->stream.error = MAD_ERROR_NONE;

    /* Skip the Xing/Info frame's PCM when present to avoid a silent block. */
    if (state->xing.valid)
        state->skip_frames_left = 1u;

    state->frame_index = 0u;
    mp3_fill_info(codec, state);
    codec->state = state;
    return HELIKON_OK;
}

/* Emits up to max_frames interleaved stereo frames from the current synth
 * block; returns the number of frames taken. */
static size_t mp3_emit(struct mp3_state *state, int16_t *pcm,
                       size_t max_frames)
{
    struct mad_synth *synth = &state->synth;
    const struct mad_pcm *pcm_in = &synth->pcm;
    const uint32_t take_avail =
        pcm_in->length - (uint32_t)state->synth_part;
    const size_t take = (size_t)take_avail < max_frames
        ? (size_t)take_avail
        : max_frames;
    const mad_fixed_t *left = pcm_in->samples[0] + state->synth_part;
    const mad_fixed_t *right = pcm_in->channels == 2u
        ? pcm_in->samples[1] + state->synth_part
        : left;
    for (size_t i = 0u; i < take; ++i) {
        pcm[i * 2u] = mp3_sample16(left[i]);
        pcm[i * 2u + 1u] = mp3_sample16(right[i]);
    }
    state->synth_part += (unsigned int)take;
    state->frame_index += (uint32_t)take;
    return take;
}

static int mp3_read(struct helikon_codec *codec, int16_t *pcm,
                    size_t max_frames, size_t *frames)
{
    if (codec == NULL || codec->state == NULL || frames == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (pcm == NULL && max_frames != 0u)
        return HELIKON_ERR_ARGUMENT;

    struct mp3_state *state = codec->state;
    *frames = 0u;
    for (;;) {
        if (max_frames != 0u && state->synth_part < state->synth.pcm.length) {
            if (state->skip_frames_left == 0u) {
                *frames = mp3_emit(state, pcm, max_frames);
                return HELIKON_OK;
            }
            state->skip_frames_left = 0u;
            state->synth_part = (unsigned int)state->synth.pcm.length;
            /* Falling through counts the skipped frame's samples. */
            state->frame_index += state->synth.pcm.length;
        }
        if (state->synth_part >= state->synth.pcm.length) {
            int rc = mad_frame_decode(&state->frame, &state->stream);
            if (rc != 0) {
                if (state->stream.error == MAD_ERROR_BUFLEN) {
                    /* The padded tail was already handed to libmad. */
                    if (state->tail_active != 0u)
                        return HELIKON_OK; /* end of stream */
                    rc = mp3_window_refill(state);
                    if (rc != HELIKON_OK)
                        return rc;
                    if (state->window_len == 0u)
                        return HELIKON_OK; /* media ended before a frame */
                    continue;
                }
                if (!MAD_RECOVERABLE(state->stream.error))
                    return HELIKON_ERR_FORMAT;
                if (++state->consecutive_errors > MP3_MAX_RECOVERABLE)
                    return HELIKON_ERR_FORMAT;
                continue;
            }
            state->consecutive_errors = 0u;
            mad_synth_frame(&state->synth, &state->frame);
            state->synth_part = 0u;
            if (max_frames == 0u)
                return HELIKON_OK;
        }
    }
}

static int mp3_seek(struct helikon_codec *codec, uint32_t frame)
{
    if (codec == NULL || codec->state == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (frame > codec->info.total_frames)
        return HELIKON_ERR_ARGUMENT;

    struct mp3_state *state = codec->state;
    const uint64_t span = state->source->size;

    uint64_t offset;
    if (state->xing.valid && codec->info.total_frames != 0u && span != 0u) {
        const unsigned int bucket = frame * 100u / codec->info.total_frames;
        offset = state->audio_start + (uint64_t)state->xing.toc[bucket] *
                                          span / 256u;
    } else {
        offset = state->audio_start +
                 (uint64_t)(frame / state->head.frame_samples) *
                     state->head.frame_bytes;
    }
    if (span != 0u && offset >= span)
        offset = span - 1u;

    /* Align the stream to a real frame header at or after the offset. */
    int rc = mp3_window_seek(state, offset);
    if (rc != HELIKON_OK)
        return rc;
    struct mp3_header target;
    rc = mp3_next_sync(state->window, state->window_len, 0u, &target,
                       (size_t)state->head.frame_bytes + 4u, &state->head);
    if (rc != HELIKON_OK)
        return rc;

    mad_stream_buffer(&state->stream, state->window + target.offset,
                      (unsigned long)(state->window_len - target.offset));
    state->stream.error = MAD_ERROR_NONE;
    mad_frame_mute(&state->frame);
    mad_synth_mute(&state->synth);
    /* mad_synth_mute does not clear pcm; leave no stale block to emit. */
    state->synth.pcm.length = 0u;
    state->synth_part = 0u;
    state->consecutive_errors = 0u;
    state->skip_frames_left = 0u;
    state->tail_active = 0u;
    state->frame_index = frame;
    return HELIKON_OK;
}

static void mp3_close(struct helikon_codec *codec)
{
    if (codec == NULL)
        return;
    codec->state = NULL; /* all libmad state lives in the static workspace */
}

static const char *const mp3_extensions[] = { "mp3", NULL };

const struct helikon_codec_ops helikon_mp3_codec_ops = {
    .name = "mp3",
    .extensions = mp3_extensions,
    .capabilities = HELIKON_CODEC_CAP_SEEK,
    .streaming = 1u,
    .stack_bytes = 8192u,
    .min_clock_hz = 54000000u,
    .recommended_clock_hz = 216000000u,
    .probe = mp3_probe,
    .open = mp3_open,
    .read = mp3_read,
    .seek = mp3_seek,
    .close = mp3_close,
};
