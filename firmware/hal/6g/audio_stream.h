/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Pure helpers for the 6G audio stream ring.
 *
 * The hardware layer owns the buffers and LLI array; this header only holds
 * the constants, the LLI word layout and the PCM-to-buffer copy so the host
 * tests can exercise the ring arithmetic and mono expansion without a
 * device.
 */
#ifndef HELIKON_HAL_6G_AUDIO_STREAM_H
#define HELIKON_HAL_6G_AUDIO_STREAM_H

#include <stddef.h>
#include <stdint.h>

#include "audio_dma.h"

#define HELIKON_AUDIO_RING_BUFFERS 4u
#define HELIKON_AUDIO_RING_FRAMES 2047u

struct helikon_audio_lli {
    uint32_t source;
    uint32_t destination;
    uint32_t link;
    uint32_t control;
} __attribute__((aligned(16)));

static inline uint32_t helikon_audio_ring_next(uint32_t index)
{
    return (index + 1u) % HELIKON_AUDIO_RING_BUFFERS;
}

/*
 * Number of buffers completed when the hardware's next-LLI pointer has
 * advanced from `play` to `next`. A full ring wrap is indistinguishable from
 * no progress with a single pointer; the service treats that as an underrun.
 */
static inline uint32_t helikon_audio_ring_distance(uint32_t play,
                                                    uint32_t next)
{
    uint32_t count = 0u;
    while (count < HELIKON_AUDIO_RING_BUFFERS &&
           helikon_audio_ring_next(play) != next) {
        play = helikon_audio_ring_next(play);
        ++count;
    }
    return count;
}

static inline void helikon_audio_lli_build(struct helikon_audio_lli *lli,
                                           uint32_t source,
                                           uint32_t destination,
                                           uint32_t link,
                                           uint32_t samples)
{
    if (lli == NULL)
        return;
    lli->source = source;
    lli->destination = destination;
    lli->link = link;
    lli->control = helikon_audio_dma_control(samples);
}

/*
 * Copy up to `capacity` frames from an interleaved source into the stereo
 * DMA buffer, duplicating mono samples. `cursor` is the source frame index.
 * Returns the number of frames copied and advances the cursor. A zero return
 * leaves the destination untouched.
 */
static inline size_t helikon_audio_stream_fill(int16_t *destination,
                                               size_t capacity,
                                               const int16_t *source,
                                               size_t source_frames,
                                               size_t *cursor,
                                               uint32_t channels)
{
    if (destination == NULL || cursor == NULL || channels == 0u)
        return 0u;
    if (*cursor >= source_frames)
        return 0u;

    size_t available = source_frames - *cursor;
    size_t frames = available < capacity ? available : capacity;
    if (frames == 0u)
        return 0u;

    if (channels == 2u) {
        const int16_t *input = source + *cursor * 2u;
        for (size_t index = 0u; index < frames * 2u; ++index)
            destination[index] = input[index];
    } else {
        const int16_t *input = source + *cursor;
        for (size_t index = 0u; index < frames; ++index) {
            int16_t sample = input[index];
            destination[index * 2u] = sample;
            destination[index * 2u + 1u] = sample;
        }
    }
    *cursor += frames;
    return frames;
}

#endif
