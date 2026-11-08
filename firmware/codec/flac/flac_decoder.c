/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Native FLAC subset decoder for the Helikon-OS embedded proof path.
 *
 * The frame and subframe rules are the published FLAC format: MSB-first
 * bitstream, Rice-coded residuals, fixed predictors 0-4 and LPC 1-32, with
 * left/right/mid-side stereo decorrelation. The arithmetic is integer-only.
 */

#include "flac_decoder.h"

#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include "mem.h"

#define FLAC_BLOCK_MAX HELIKON_FLAC_MAX_BLOCK
#define FLAC_CHANNEL_MAX HELIKON_FLAC_MAX_CHANNELS

enum flac_channel_mode {
    FLAC_CH_INDEPENDENT = 0,
    FLAC_CH_LEFT_SIDE = 1,
    FLAC_CH_RIGHT_SIDE = 2,
    FLAC_CH_MID_SIDE = 3
};

struct flac_bits {
    const uint8_t *data;
    size_t size;
    size_t bit;
    int error;
};

struct flac_streaminfo {
    uint32_t sample_rate;
    uint32_t total_frames;
    uint32_t min_block;
    uint32_t max_block;
    uint16_t channels;
    uint16_t bits;
    size_t first_frame;
};

static void flac_bits_init(struct flac_bits *bits, const uint8_t *data,
                           size_t size, size_t byte_offset)
{
    bits->data = data;
    bits->size = size;
    bits->bit = byte_offset * 8u;
    bits->error = 0;
}

static uint32_t flac_bits_read(struct flac_bits *bits, unsigned count)
{
    if (bits->error != 0 || count > 32u)
        return 0;
    if (bits->size > (SIZE_MAX / 8u) ||
        bits->bit > bits->size * 8u ||
        count > bits->size * 8u - bits->bit) {
        bits->error = 1;
        return 0;
    }

    uint32_t value = 0;
    for (unsigned index = 0u; index < count; ++index) {
        size_t position = bits->bit + index;
        uint8_t byte = bits->data[position >> 3];
        unsigned shift = 7u - (unsigned)(position & 7u);
        value = (value << 1) | (((uint32_t)byte >> shift) & 1u);
    }
    bits->bit += count;
    return value;
}

static int32_t flac_bits_read_signed(struct flac_bits *bits, unsigned count)
{
    uint32_t value = flac_bits_read(bits, count);
    if (bits->error != 0 || count == 0u)
        return 0;
    if (count < 32u && (value & (1u << (count - 1u))) != 0u)
        value |= ~((1u << count) - 1u);
    return (int32_t)value;
}

static void flac_bits_align(struct flac_bits *bits)
{
    if (bits->error != 0)
        return;
    if (bits->bit > bits->size * 8u ||
        (bits->bit & 7u) > bits->size * 8u - bits->bit) {
        bits->error = 1;
        return;
    }
    bits->bit = (bits->bit + 7u) & ~(size_t)7u;
}

static uint16_t flac_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t flac_be24(const uint8_t *data)
{
    return ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
}

static uint8_t flac_crc8_byte(uint8_t crc, uint8_t value)
{
    crc ^= value;
    for (unsigned bit = 0u; bit < 8u; ++bit) {
        if ((crc & 0x80u) != 0u)
            crc = (uint8_t)(((uint32_t)crc << 1) ^ 0x07u);
        else
            crc = (uint8_t)(crc << 1);
    }
    return crc;
}

static uint16_t flac_crc16_byte(uint16_t crc, uint8_t value)
{
    crc ^= (uint16_t)((uint16_t)value << 8);
    for (unsigned bit = 0u; bit < 8u; ++bit) {
        if ((crc & 0x8000u) != 0u)
            crc = (uint16_t)(((uint32_t)crc << 1) ^ 0x8005u);
        else
            crc = (uint16_t)(crc << 1);
    }
    return crc;
}

static int flac_skip_utf8(struct flac_bits *bits)
{
    uint8_t first = (uint8_t)flac_bits_read(bits, 8u);
    if (bits->error != 0)
        return HELIKON_ERR_FORMAT;
    if ((first & 0x80u) == 0u)
        return HELIKON_OK;

    unsigned extra = 0u;
    uint8_t mask = 0x40u;
    while ((first & mask) != 0u) {
        ++extra;
        mask >>= 1;
        if (extra > 6u)
            return HELIKON_ERR_FORMAT;
    }
    if (extra == 0u)
        return HELIKON_ERR_FORMAT;

    for (unsigned index = 0u; index < extra; ++index) {
        uint8_t value = (uint8_t)flac_bits_read(bits, 8u);
        if (bits->error != 0 || (value & 0xc0u) != 0x80u)
            return HELIKON_ERR_FORMAT;
    }
    return HELIKON_OK;
}

static int flac_parse_metadata(const uint8_t *data, size_t size,
                               struct flac_streaminfo *info)
{
    if (data == NULL || info == NULL || size < 8u)
        return HELIKON_ERR_ARGUMENT;
    if (data[0] != 'f' || data[1] != 'L' || data[2] != 'a' ||
        data[3] != 'C')
        return HELIKON_ERR_NOT_FOUND;

    helikon_memset(info, 0, sizeof(*info));
    int have_streaminfo = 0;
    size_t offset = 4u;
    for (;;) {
        if (offset > size || size - offset < 4u)
            return HELIKON_ERR_FORMAT;

        uint8_t header = data[offset];
        int last = (header & 0x80u) != 0u;
        uint8_t type = (uint8_t)(header & 0x7fu);
        uint32_t length = flac_be24(data + offset + 1u);
        offset += 4u;
        if ((size_t)length > size - offset)
            return HELIKON_ERR_FORMAT;

        if (type == 0u) {
            if (have_streaminfo || length != 34u)
                return HELIKON_ERR_FORMAT;
            const uint8_t *stream = data + offset;
            info->min_block = flac_be16(stream);
            info->max_block = flac_be16(stream + 2u);
            info->sample_rate = ((uint32_t)stream[10] << 12) |
                                ((uint32_t)stream[11] << 4) |
                                ((uint32_t)stream[12] >> 4);
            info->channels =
                (uint16_t)(((stream[12] & 0x0eu) >> 1) + 1u);
            uint16_t bits_value =
                (uint16_t)((uint16_t)(stream[12] & 1u) << 4);
            bits_value |=
                (uint16_t)(((uint16_t)stream[13] & 0xf0u) >> 4);
            info->bits = (uint16_t)(bits_value + 1u);
            uint64_t total = ((uint64_t)(stream[13] & 0x0fu) << 32) |
                             ((uint64_t)stream[14] << 24) |
                             ((uint64_t)stream[15] << 16) |
                             ((uint64_t)stream[16] << 8) | stream[17];
            if (total > 0xffffffffu)
                return HELIKON_ERR_UNSUPPORTED;
            info->total_frames = (uint32_t)total;
            have_streaminfo = 1;
        }
        offset += length;
        if (last)
            break;
    }

    if (!have_streaminfo)
        return HELIKON_ERR_FORMAT;
    if (info->min_block == 0u || info->max_block == 0u ||
        info->max_block > FLAC_BLOCK_MAX || info->min_block > info->max_block)
        return HELIKON_ERR_FORMAT;
    if (info->sample_rate != 44100u && info->sample_rate != 48000u)
        return HELIKON_ERR_UNSUPPORTED;
    if (info->channels != 1u && info->channels != 2u)
        return HELIKON_ERR_UNSUPPORTED;
    if (info->bits != 16u && info->bits != 24u)
        return HELIKON_ERR_UNSUPPORTED;
    if (info->total_frames == 0u)
        return HELIKON_ERR_UNSUPPORTED;
    info->first_frame = offset;
    return HELIKON_OK;
}

int helikon_flac_probe(const uint8_t *data, size_t size,
                       struct helikon_codec_info *info)
{
    if (data == NULL || info == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (size < 4u || data[0] != 'f' || data[1] != 'L' || data[2] != 'a' ||
        data[3] != 'C')
        return HELIKON_ERR_NOT_FOUND;

    struct flac_streaminfo stream;
    int rc = flac_parse_metadata(data, size, &stream);
    if (rc != HELIKON_OK)
        return rc;

    helikon_memset(info, 0, sizeof(*info));
    info->sample_rate = stream.sample_rate;
    info->total_frames = stream.total_frames;
    info->duration_ms =
        (uint32_t)(((uint64_t)stream.total_frames * 1000u) /
                   stream.sample_rate);
    info->source_channels = stream.channels;
    info->source_bits = stream.bits;
    info->output_channels = 2u;
    info->output_bits = 16u;
    info->workspace_bytes = sizeof(struct helikon_flac_decoder);
    info->flags = HELIKON_CODEC_FLAG_SEEK;
    return HELIKON_OK;
}

int helikon_flac_open(struct helikon_flac_decoder *decoder,
                      const uint8_t *data, size_t size)
{
    if (decoder == NULL || data == NULL)
        return HELIKON_ERR_ARGUMENT;

    struct flac_streaminfo stream;
    int rc = flac_parse_metadata(data, size, &stream);
    if (rc != HELIKON_OK)
        return rc;

    helikon_memset(decoder, 0, sizeof(*decoder));
    decoder->data = data;
    decoder->size = size;
    decoder->first_frame = stream.first_frame;
    decoder->next_frame = stream.first_frame;
    decoder->sample_rate = stream.sample_rate;
    decoder->total_frames = stream.total_frames;
    decoder->min_block = stream.min_block;
    decoder->max_block = stream.max_block;
    decoder->channels = stream.channels;
    decoder->bits = stream.bits;
    decoder->have_frame = 0;
    return HELIKON_OK;
}

static int flac_decode_residual(struct helikon_flac_decoder *decoder,
                                struct flac_bits *bits, int32_t *decoded,
                                uint32_t blocksize, uint32_t pred_order)
{
    (void)decoder;
    uint32_t method = flac_bits_read(bits, 2u);
    uint32_t rice_order = flac_bits_read(bits, 4u);
    if (bits->error != 0 || method > 1u)
        return HELIKON_ERR_FORMAT;

    uint32_t partitions = 1u << rice_order;
    if (partitions > blocksize || (blocksize % partitions) != 0u)
        return HELIKON_ERR_FORMAT;
    uint32_t partition_samples = blocksize >> rice_order;
    if (pred_order > partition_samples)
        return HELIKON_ERR_FORMAT;

    unsigned rice_bits = 4u + method;
    uint32_t escape = (1u << rice_bits) - 1u;
    uint32_t index = pred_order;
    for (uint32_t partition = 0u; partition < partitions; ++partition) {
        uint32_t count = partition_samples;
        if (partition == 0u)
            count -= pred_order;
        uint32_t rice = flac_bits_read(bits, rice_bits);
        if (bits->error != 0)
            return HELIKON_ERR_FORMAT;

        if (rice == escape) {
            uint32_t raw_bits = flac_bits_read(bits, 5u);
            if (bits->error != 0 || raw_bits > 32u)
                return HELIKON_ERR_FORMAT;
            for (uint32_t sample = 0u; sample < count; ++sample) {
                decoded[index++] = flac_bits_read_signed(bits, raw_bits);
                if (bits->error != 0)
                    return HELIKON_ERR_FORMAT;
            }
        } else {
            for (uint32_t sample = 0u; sample < count; ++sample) {
                uint32_t quotient = 0u;
                for (;;) {
                    uint32_t bit = flac_bits_read(bits, 1u);
                    if (bits->error != 0 || bit != 0u)
                        break;
                    if (++quotient > (1u << 24))
                        return HELIKON_ERR_FORMAT;
                }
                uint32_t remainder =
                    rice == 0u ? 0u : flac_bits_read(bits, rice);
                if (bits->error != 0)
                    return HELIKON_ERR_FORMAT;
                uint64_t value = ((uint64_t)quotient << rice) | remainder;
                if (value > 0xffffffffu)
                    return HELIKON_ERR_FORMAT;
                uint32_t folded = (uint32_t)value;
                decoded[index++] =
                    (int32_t)(folded >> 1) ^ -(int32_t)(folded & 1u);
            }
        }
    }
    return HELIKON_OK;
}

static int flac_decode_fixed(struct helikon_flac_decoder *decoder,
                             struct flac_bits *bits, int32_t *decoded,
                             uint32_t blocksize, uint32_t order,
                             unsigned bps)
{
    for (uint32_t index = 0u; index < order; ++index) {
        decoded[index] = flac_bits_read_signed(bits, bps);
        if (bits->error != 0)
            return HELIKON_ERR_FORMAT;
    }
    int rc = flac_decode_residual(decoder, bits, decoded, blocksize, order);
    if (rc != HELIKON_OK)
        return rc;

    if (order == 0u)
        return HELIKON_OK;

    int32_t a = decoded[order - 1u];
    int32_t b = order >= 2u ? a - decoded[order - 2u] : 0;
    int32_t c = order >= 3u ? b - decoded[order - 2u] +
                                  decoded[order - 3u]
                            : 0;
    int32_t d = order >= 4u ? c - decoded[order - 2u] +
                                  2 * decoded[order - 3u] -
                                  decoded[order - 4u]
                            : 0;
    for (uint32_t index = order; index < blocksize; ++index) {
        if (order == 1u) {
            a += decoded[index];
            decoded[index] = a;
        } else if (order == 2u) {
            b += decoded[index];
            a += b;
            decoded[index] = a;
        } else if (order == 3u) {
            c += decoded[index];
            b += c;
            a += b;
            decoded[index] = a;
        } else {
            d += decoded[index];
            c += d;
            b += c;
            a += b;
            decoded[index] = a;
        }
    }
    return HELIKON_OK;
}

static int flac_decode_lpc(struct helikon_flac_decoder *decoder,
                           struct flac_bits *bits, int32_t *decoded,
                           uint32_t blocksize, uint32_t order, unsigned bps)
{
    for (uint32_t index = 0u; index < order; ++index) {
        decoded[index] = flac_bits_read_signed(bits, bps);
        if (bits->error != 0)
            return HELIKON_ERR_FORMAT;
    }

    uint32_t precision = flac_bits_read(bits, 4u) + 1u;
    int32_t shift = flac_bits_read_signed(bits, 5u);
    if (bits->error != 0 || precision == 16u || shift < 0)
        return HELIKON_ERR_FORMAT;

    int32_t coeffs[32];
    for (uint32_t index = 0u; index < order; ++index) {
        coeffs[index] = flac_bits_read_signed(bits, precision);
        if (bits->error != 0)
            return HELIKON_ERR_FORMAT;
    }

    int rc = flac_decode_residual(decoder, bits, decoded, blocksize, order);
    if (rc != HELIKON_OK)
        return rc;

    for (uint32_t index = order; index < blocksize; ++index) {
        int64_t sum = 0;
        for (uint32_t coeff = 0u; coeff < order; ++coeff)
            sum += (int64_t)coeffs[coeff] * decoded[index - coeff - 1u];
        decoded[index] += (int32_t)(sum >> shift);
    }
    return HELIKON_OK;
}

static int flac_decode_subframe(struct helikon_flac_decoder *decoder,
                                struct flac_bits *bits, uint32_t channel,
                                uint32_t blocksize, unsigned bps)
{
    if (flac_bits_read(bits, 1u) != 0u || bits->error != 0)
        return HELIKON_ERR_FORMAT;

    uint32_t type = flac_bits_read(bits, 6u);
    uint32_t wasted = 0u;
    if (flac_bits_read(bits, 1u) != 0u) {
        wasted = 1u;
        while (flac_bits_read(bits, 1u) == 0u) {
            ++wasted;
            if (bits->error != 0 || wasted >= bps || wasted > 32u)
                return HELIKON_ERR_FORMAT;
        }
    }
    if (bits->error != 0 || wasted >= bps)
        return HELIKON_ERR_FORMAT;
    bps -= wasted;

    int32_t *decoded = decoder->decoded[channel];
    int rc;
    if (type == 0u) {
        int32_t value = flac_bits_read_signed(bits, bps);
        if (bits->error != 0)
            return HELIKON_ERR_FORMAT;
        for (uint32_t index = 0u; index < blocksize; ++index)
            decoded[index] = value;
    } else if (type == 1u) {
        for (uint32_t index = 0u; index < blocksize; ++index) {
            decoded[index] = flac_bits_read_signed(bits, bps);
            if (bits->error != 0)
                return HELIKON_ERR_FORMAT;
        }
    } else if (type >= 8u && type <= 12u) {
        rc = flac_decode_fixed(decoder, bits, decoded, blocksize, type - 8u,
                               bps);
        if (rc != HELIKON_OK)
            return rc;
    } else if (type >= 32u) {
        uint32_t order = (type & 0x1fu) + 1u;
        if (order > 32u)
            return HELIKON_ERR_FORMAT;
        rc = flac_decode_lpc(decoder, bits, decoded, blocksize, order, bps);
        if (rc != HELIKON_OK)
            return rc;
    } else {
        return HELIKON_ERR_FORMAT;
    }

    if (wasted != 0u) {
        for (uint32_t index = 0u; index < blocksize; ++index)
            decoded[index] = (int32_t)((uint32_t)decoded[index] << wasted);
    }
    return HELIKON_OK;
}

static int flac_decode_frame(struct helikon_flac_decoder *decoder)
{
    static const uint16_t block_table[16] = {
        0u, 192u, 576u, 1152u, 2304u, 4608u, 0u, 0u,
        256u, 512u, 1024u, 2048u, 4096u, 8192u, 16384u, 32768u
    };
    static const uint32_t rate_table[12] = {
        0u, 88200u, 176400u, 192000u, 8000u, 16000u, 22050u, 24000u,
        32000u, 44100u, 48000u, 96000u
    };
    static const uint16_t size_table[8] = {
        0u, 8u, 12u, 0u, 16u, 20u, 24u, 0u
    };

    size_t frame_start = decoder->next_frame;
    if (frame_start >= decoder->size)
        return HELIKON_ERR_FORMAT;

    struct flac_bits bits;
    flac_bits_init(&bits, decoder->data, decoder->size, frame_start);
    uint32_t sync = flac_bits_read(&bits, 16u);
    if (bits.error != 0 || (sync & 0xfffeu) != 0xfff8u)
        return HELIKON_ERR_FORMAT;

    uint32_t block_code = flac_bits_read(&bits, 4u);
    uint32_t rate_code = flac_bits_read(&bits, 4u);
    uint32_t channel_code = flac_bits_read(&bits, 4u);
    uint32_t size_code = flac_bits_read(&bits, 3u);
    uint32_t reserved = flac_bits_read(&bits, 1u);
    if (bits.error != 0 || reserved != 0u)
        return HELIKON_ERR_FORMAT;
    if (flac_skip_utf8(&bits) != HELIKON_OK)
        return HELIKON_ERR_FORMAT;

    uint32_t blocksize;
    if (block_code == 0u) {
        blocksize = decoder->min_block;
    } else if (block_code == 6u) {
        blocksize = flac_bits_read(&bits, 8u) + 1u;
    } else if (block_code == 7u) {
        blocksize = flac_bits_read(&bits, 16u) + 1u;
    } else {
        blocksize = block_table[block_code];
    }
    if (bits.error != 0 || blocksize == 0u ||
        blocksize > decoder->max_block || blocksize > FLAC_BLOCK_MAX)
        return HELIKON_ERR_FORMAT;

    uint32_t sample_rate;
    if (rate_code == 0u) {
        sample_rate = decoder->sample_rate;
    } else if (rate_code <= 11u) {
        sample_rate = rate_table[rate_code];
    } else if (rate_code == 12u) {
        sample_rate = flac_bits_read(&bits, 8u) * 1000u;
    } else if (rate_code == 13u) {
        sample_rate = flac_bits_read(&bits, 16u);
    } else if (rate_code == 14u) {
        sample_rate = flac_bits_read(&bits, 16u) * 10u;
    } else {
        return HELIKON_ERR_FORMAT;
    }
    if (bits.error != 0 || sample_rate != decoder->sample_rate)
        return HELIKON_ERR_UNSUPPORTED;

    uint32_t bits_per_sample;
    if (size_code == 0u) {
        bits_per_sample = decoder->bits;
    } else if (size_code == 1u || size_code == 2u ||
               size_code == 4u || size_code == 5u || size_code == 6u) {
        bits_per_sample = size_table[size_code];
    } else {
        return HELIKON_ERR_FORMAT;
    }
    if (bits_per_sample != decoder->bits ||
        (bits_per_sample != 16u && bits_per_sample != 24u))
        return HELIKON_ERR_UNSUPPORTED;

    if ((bits.bit & 7u) != 0u)
        return HELIKON_ERR_FORMAT;
    size_t crc_offset = bits.bit >> 3;
    if (crc_offset >= decoder->size)
        return HELIKON_ERR_FORMAT;
    uint8_t crc = 0u;
    for (size_t index = frame_start; index <= crc_offset; ++index)
        crc = flac_crc8_byte(crc, decoder->data[index]);
    (void)flac_bits_read(&bits, 8u);
    if (bits.error != 0 || crc != 0u)
        return HELIKON_ERR_FORMAT;

    uint32_t mode;
    if (channel_code < 8u) {
        if (channel_code + 1u != decoder->channels)
            return HELIKON_ERR_FORMAT;
        mode = FLAC_CH_INDEPENDENT;
    } else if (channel_code <= 10u) {
        if (decoder->channels != 2u)
            return HELIKON_ERR_FORMAT;
        mode = channel_code - 7u;
    } else {
        return HELIKON_ERR_FORMAT;
    }

    for (uint32_t channel = 0u; channel < decoder->channels; ++channel) {
        unsigned sub_bps = decoder->bits;
        if (channel == 0u && mode == FLAC_CH_RIGHT_SIDE)
            ++sub_bps;
        if (channel == 1u &&
            (mode == FLAC_CH_LEFT_SIDE || mode == FLAC_CH_MID_SIDE))
            ++sub_bps;
        int rc = flac_decode_subframe(decoder, &bits, channel, blocksize,
                                      sub_bps);
        if (rc != HELIKON_OK)
            return rc;
    }

    flac_bits_align(&bits);
    if (bits.error != 0)
        return HELIKON_ERR_FORMAT;

    size_t footer_offset = bits.bit >> 3;
    uint16_t footer = (uint16_t)flac_bits_read(&bits, 16u);
    if (bits.error != 0 || footer_offset >= decoder->size)
        return HELIKON_ERR_FORMAT;
    uint16_t frame_crc = 0u;
    for (size_t index = frame_start; index < footer_offset; ++index)
        frame_crc = flac_crc16_byte(frame_crc, decoder->data[index]);
    if (frame_crc != footer)
        return HELIKON_ERR_FORMAT;

    size_t frame_end = (bits.bit + 7u) >> 3;
    if (frame_end <= frame_start || frame_end > decoder->size)
        return HELIKON_ERR_FORMAT;

    if (mode == FLAC_CH_LEFT_SIDE) {
        for (uint32_t index = 0u; index < blocksize; ++index)
            decoder->decoded[1][index] =
                decoder->decoded[0][index] - decoder->decoded[1][index];
    } else if (mode == FLAC_CH_RIGHT_SIDE) {
        for (uint32_t index = 0u; index < blocksize; ++index)
            decoder->decoded[0][index] =
                decoder->decoded[0][index] + decoder->decoded[1][index];
    } else if (mode == FLAC_CH_MID_SIDE) {
        for (uint32_t index = 0u; index < blocksize; ++index) {
            int32_t mid = decoder->decoded[0][index];
            int32_t side = decoder->decoded[1][index];
            mid = (mid << 1) | (side & 1);
            decoder->decoded[0][index] = (mid + side) >> 1;
            decoder->decoded[1][index] = (mid - side) >> 1;
        }
    }

    decoder->next_frame = frame_end;
    decoder->frame_frames = blocksize;
    decoder->frame_pos = 0u;
    decoder->have_frame = 1u;
    return HELIKON_OK;
}

int helikon_flac_read(struct helikon_flac_decoder *decoder, int16_t *pcm,
                      size_t max_frames, size_t *frames)
{
    if (decoder == NULL || pcm == NULL || frames == NULL)
        return HELIKON_ERR_ARGUMENT;

    size_t out = 0u;
    while (out < max_frames) {
        if (!decoder->have_frame) {
            if (decoder->stream_frames >= decoder->total_frames)
                break;
            int rc = flac_decode_frame(decoder);
            if (rc != HELIKON_OK)
                return rc;
        }

        uint32_t available = decoder->frame_frames - decoder->frame_pos;
        uint32_t remaining = decoder->total_frames - decoder->stream_frames;
        size_t count = available < remaining ? available : remaining;
        if (count > max_frames - out)
            count = max_frames - out;
        if (count == 0u)
            break;

        for (size_t index = 0u; index < count; ++index) {
            int32_t left = decoder->decoded[0][decoder->frame_pos + index];
            int32_t right = decoder->channels == 2u
                                ? decoder->decoded[1][decoder->frame_pos +
                                                      index]
                                : left;
            if (decoder->bits == 24u) {
                left >>= 8;
                right >>= 8;
            }
            pcm[(out + index) * 2u] = (int16_t)left;
            pcm[(out + index) * 2u + 1u] = (int16_t)right;
        }

        decoder->frame_pos += (uint32_t)count;
        decoder->stream_frames += (uint32_t)count;
        out += count;
        if (decoder->frame_pos >= decoder->frame_frames) {
            decoder->have_frame = 0;
            decoder->frame_pos = 0u;
        }
    }

    *frames = out;
    return HELIKON_OK;
}

int helikon_flac_seek(struct helikon_flac_decoder *decoder, uint32_t frame)
{
    if (decoder == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (frame > decoder->total_frames)
        return HELIKON_ERR_ARGUMENT;

    decoder->next_frame = decoder->first_frame;
    decoder->stream_frames = 0u;
    decoder->frame_frames = 0u;
    decoder->frame_pos = 0u;
    decoder->have_frame = 0u;

    while (decoder->stream_frames < frame) {
        if (!decoder->have_frame) {
            int rc = flac_decode_frame(decoder);
            if (rc != HELIKON_OK)
                return rc;
        }
        uint32_t available = decoder->frame_frames - decoder->frame_pos;
        uint32_t remaining = frame - decoder->stream_frames;
        if (remaining >= available) {
            decoder->stream_frames += available;
            decoder->frame_pos = 0u;
            decoder->have_frame = 0u;
        } else {
            decoder->frame_pos += remaining;
            decoder->stream_frames = frame;
            break;
        }
    }
    return HELIKON_OK;
}

void helikon_flac_close(struct helikon_flac_decoder *decoder)
{
    if (decoder == NULL)
        return;
    decoder->next_frame = decoder->first_frame;
    decoder->stream_frames = 0u;
    decoder->frame_frames = 0u;
    decoder->frame_pos = 0u;
    decoder->have_frame = 0u;
    decoder->error = 0;
}
