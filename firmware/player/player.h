/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded PCM player for the first audio path.
 *
 * The player owns a parsed WAV image, a byte cursor and the audio HAL. It
 * plays at most one short DMA-sized chunk per call so the UI can poll input
 * between chunks. It never reads storage and never writes the catalog.
 */
#ifndef HELIKON_PLAYER_PLAYER_H
#define HELIKON_PLAYER_PLAYER_H

#include <stddef.h>
#include <stdint.h>

#include "audio.h"
#include "wav.h"

struct helikon_player {
    struct helikon_wav wav;
    struct helikon_audio audio;
    size_t offset;
    int last_error;
    uint32_t last_stage;
    int opened;
    int started;
};

int helikon_player_open(struct helikon_player *player, const uint8_t *data,
                        size_t size, struct helikon_pmu *pmu,
                        struct helikon_clock *clock, uint32_t timeout_ms);
int helikon_player_play_chunk(struct helikon_player *player,
                              uint32_t max_frames);
int helikon_player_pause(struct helikon_player *player);
int helikon_player_resume(struct helikon_player *player);
void helikon_player_stop(struct helikon_player *player);
uint32_t helikon_player_position_ms(const struct helikon_player *player);
uint32_t helikon_player_duration_ms(const struct helikon_player *player);
int helikon_player_finished(const struct helikon_player *player);
const char *helikon_player_error_text(const struct helikon_player *player);

#endif
