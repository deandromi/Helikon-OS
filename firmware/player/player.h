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
#include "codec.h"
#include "read_ahead.h"
#include "wav.h"

struct helikon_player {
    struct helikon_wav wav;
    struct helikon_audio audio;
    struct helikon_codec codec;
    uint8_t codec_workspace[HELIKON_CODEC_WORKSPACE_MAX]
        __attribute__((aligned(8)));
    /*
     * A storage-backed stream pulls through a 16 KiB window: one read per
     * window instead of one per decoder call. The player owns both the policy
     * object and its buffer, so `helikon_player_open_media` needs neither from
     * the caller.
     */
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source media_source;
    uint8_t read_ahead_buffer[HELIKON_READ_AHEAD_BYTES]
        __attribute__((aligned(8)));
    size_t offset;
    uint32_t start_frame; /* first frame of the current stream segment */
    int last_error;
    uint32_t last_stage;
    int opened;
    int started;
    int compressed;
};

int helikon_player_open(struct helikon_player *player, const uint8_t *data,
                        size_t size, struct helikon_pmu *pmu,
                        struct helikon_clock *clock, uint32_t timeout_ms);
/*
 * Opens audio from a pull source (the VFS media adapter, a test media, or any
 * other `read_at`). The source must outlive the player: the open codec keeps
 * the pointer. Unlike the buffer form this needs no resident file, and a
 * descriptor whose probe needs more than a head is resolved by open.
 */
int helikon_player_open_media(struct helikon_player *player,
                              const struct helikon_codec_source *source,
                              struct helikon_pmu *pmu,
                              struct helikon_clock *clock, uint32_t timeout_ms);
int helikon_player_play_chunk(struct helikon_player *player,
                              uint32_t max_frames);
/*
 * Moves playback to `position_ms`. Compressed audio seeks the codec and
 * restarts the PCM stream; the resident WAV path restarts at the matching byte
 * offset. Before the first chunk it only moves the start point, so a caller can
 * open, seek and then play. A position past the end clamps to the last frame,
 * a seek that lands where playback already is does nothing, and a paused
 * player stays paused.
 */
int helikon_player_seek_ms(struct helikon_player *player, uint32_t position_ms);
int helikon_player_pause(struct helikon_player *player);
int helikon_player_resume(struct helikon_player *player);
void helikon_player_stop(struct helikon_player *player);
uint32_t helikon_player_position_ms(const struct helikon_player *player);
uint32_t helikon_player_duration_ms(const struct helikon_player *player);
int helikon_player_finished(const struct helikon_player *player);
const char *helikon_player_error_text(const struct helikon_player *player);

#endif
