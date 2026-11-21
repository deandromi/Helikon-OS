/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Play queue for the Music hub (T-058).
 *
 * The hub can list tracks, but a player needs an order to walk: this queue is
 * the next/previous state machine the Now Playing screen will drive. It stores
 * track ids only, so it knows nothing about HDB records, HFS+ paths or the
 * codec; the caller resolves an id to a file.
 *
 * Everything is fixed-size and allocation-free: one id array plus one order
 * array, both bounded by `HELIKON_QUEUE_CAPACITY`. Shuffle is a permutation of
 * the same ids generated from a caller-supplied seed, so a test (and a session)
 * reproduces the same order, and repeat semantics are explicit enums rather
 * than flags the caller has to remember.
 */
#ifndef HELIKON_PLAYER_QUEUE_H
#define HELIKON_PLAYER_QUEUE_H

#include <stddef.h>
#include <stdint.h>

/* One album's worth of 6G-sized tracks fits many times over. */
#define HELIKON_QUEUE_CAPACITY 512u

enum helikon_queue_repeat {
    HELIKON_QUEUE_REPEAT_OFF = 0,
    HELIKON_QUEUE_REPEAT_ALL = 1,
    HELIKON_QUEUE_REPEAT_ONE = 2
};

/* What `next`/`previous` did, so the caller can decide what to restart. */
enum helikon_queue_result {
    HELIKON_QUEUE_MOVED = 0,   /* a different id is current */
    HELIKON_QUEUE_WRAPPED = 1, /* moved to the other end of the order */
    HELIKON_QUEUE_END = 2,     /* repeat off and no further id: nothing moved */
    HELIKON_QUEUE_RESTART = 3, /* repeat one: the same id, from the top */
    HELIKON_QUEUE_EMPTY = 4
};

struct helikon_queue {
    uint32_t ids[HELIKON_QUEUE_CAPACITY];
    uint32_t order[HELIKON_QUEUE_CAPACITY];
    uint32_t count;
    uint32_t position; /* index into `order` */
    uint8_t repeat;
    uint8_t shuffle;
    uint32_t seed;
};

/*
 * Replaces the contents and rewinds to the first id. `count` of zero empties
 * the queue, which is legal (an empty album is a real case). Refuses
 * `count > HELIKON_QUEUE_CAPACITY` with HELIKON_ERR_ARGUMENT and leaves the
 * queue untouched.
 */
int helikon_queue_set(struct helikon_queue *queue, const uint32_t *ids,
                      uint32_t count);

/* Current track id; HELIKON_ERR_NOT_FOUND on an empty queue. */
int helikon_queue_current(const struct helikon_queue *queue, uint32_t *id);

/* Jumps to `index` in play order (0-based). Out of range is ARGUMENT. */
int helikon_queue_select(struct helikon_queue *queue, uint32_t index);

/*
 * Repeat mode and shuffle. Shuffle regenerates the order from `seed` with the
 * current id first, so enabling it mid-track does not interrupt playback.
 * Turning shuffle off restores the natural order and keeps the current id.
 */
int helikon_queue_set_repeat(struct helikon_queue *queue, uint8_t mode);
int helikon_queue_set_shuffle(struct helikon_queue *queue, int enabled,
                              uint32_t seed);

/* Advance. `NULL` queue or an empty queue returns HELIKON_QUEUE_EMPTY. */
int helikon_queue_next(struct helikon_queue *queue);
int helikon_queue_previous(struct helikon_queue *queue);

#endif
