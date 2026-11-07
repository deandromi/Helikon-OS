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

struct helikon_audio {
    struct helikon_pmu *pmu;
    struct helikon_clock *clock;
    uint32_t sample_rate;
    uint32_t timeout_ms;
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
