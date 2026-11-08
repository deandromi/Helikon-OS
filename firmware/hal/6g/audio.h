/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 6G audio output: CS42L55 codec, I2S and PL080 DMA.
 *
 * The register sequences are ported from the Rockbox CS42L55 and
 * pcm-s5l8702 drivers (GPL-2.0-or-later) and are hardware sequences, not
 * measurements. The first path is 16-bit stereo at 44.1 or 48 kHz. Playback
 * drains one bounded DMA chunk at a time so the caller can poll input between
 * chunks.
 */
#ifndef HELIKON_HAL_6G_AUDIO_H
#define HELIKON_HAL_6G_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "clock.h"
#include "pmu.h"

/*
 * Stage codes are retained on the audio object after a failure so the UI can
 * name the failing path instead of collapsing every error into one message.
 * They are diagnostic labels, not a second error taxonomy.
 */
enum helikon_audio_stage {
    HELIKON_AUDIO_STAGE_NONE = 0,
    HELIKON_AUDIO_STAGE_CODEC = 1,
    HELIKON_AUDIO_STAGE_RATE = 2,
    HELIKON_AUDIO_STAGE_I2S = 3,
    HELIKON_AUDIO_STAGE_DMA_SETUP = 4,
    HELIKON_AUDIO_STAGE_DMA_WAIT = 5,
    HELIKON_AUDIO_STAGE_DMA_ACTIVE = 6,
    HELIKON_AUDIO_STAGE_CACHE = 7,
    HELIKON_AUDIO_STAGE_BUFFER = 8,
    HELIKON_AUDIO_STAGE_DMA_CLOCK = 9,
    HELIKON_AUDIO_STAGE_DMA_CONFIG = 10,
    HELIKON_AUDIO_STAGE_I2S_FIFO = 11
};

struct helikon_audio {
    struct helikon_pmu *pmu;
    struct helikon_clock *clock;
    uint32_t sample_rate;
    uint32_t timeout_ms;
    uint32_t stage;
    int initialized;
    int running;
};

int helikon_audio_init(struct helikon_audio *audio, struct helikon_pmu *pmu,
                       struct helikon_clock *clock, uint32_t timeout_ms);
int helikon_audio_set_rate(struct helikon_audio *audio, uint32_t sample_rate);
int helikon_audio_play(struct helikon_audio *audio, const int16_t *pcm,
                       size_t frames);
int helikon_audio_pause(struct helikon_audio *audio);
int helikon_audio_resume(struct helikon_audio *audio);
int helikon_audio_stop(struct helikon_audio *audio);

#endif
