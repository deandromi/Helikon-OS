/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "queue.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"

/* Deterministic shuffle: xorshift32, seeded by the caller. */
static uint32_t queue_random(struct helikon_queue *queue)
{
    uint32_t value = queue->seed;
    if (value == 0u)
        value = 0x2545f491u; /* a zero seed must not freeze the sequence */
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    queue->seed = value;
    return value;
}

/* Natural order: order[i] = i. */
static void queue_reset_order(struct helikon_queue *queue)
{
    for (uint32_t index = 0u; index < queue->count; ++index)
        queue->order[index] = index;
    queue->position = 0u;
}

static uint32_t queue_current_index(const struct helikon_queue *queue)
{
    if (queue->position >= queue->count)
        return 0u;
    return queue->order[queue->position];
}

/*
 * Fisher-Yates over the order array, but the id that was current stays first so
 * enabling shuffle mid-track does not jump to another song.
 */
static void queue_shuffle(struct helikon_queue *queue, uint32_t keep)
{
    queue_reset_order(queue);
    for (uint32_t index = queue->count; index > 1u; --index) {
        uint32_t pick = queue_random(queue) % index;
        uint32_t swap = queue->order[index - 1u];
        queue->order[index - 1u] = queue->order[pick];
        queue->order[pick] = swap;
    }

    if (queue->count == 0u)
        return;
    /* Move the kept id to the front, leaving the rest of the permutation. */
    for (uint32_t index = 0u; index < queue->count; ++index) {
        if (queue->ids[queue->order[index]] != keep)
            continue;
        uint32_t first = queue->order[0];
        queue->order[0] = queue->order[index];
        queue->order[index] = first;
        break;
    }
    queue->position = 0u;
}

/* Position of `id` in the current order, or count when it is absent. */
static uint32_t queue_position_of(const struct helikon_queue *queue,
                                  uint32_t id)
{
    for (uint32_t index = 0u; index < queue->count; ++index) {
        if (queue->ids[queue->order[index]] == id)
            return index;
    }
    return queue->count;
}

int helikon_queue_set(struct helikon_queue *queue, const uint32_t *ids,
                      uint32_t count)
{
    if (queue == NULL || (ids == NULL && count != 0u))
        return HELIKON_ERR_ARGUMENT;
    if (count > HELIKON_QUEUE_CAPACITY)
        return HELIKON_ERR_ARGUMENT;

    for (uint32_t index = 0u; index < count; ++index)
        queue->ids[index] = ids[index];
    queue->count = count;
    if (queue->shuffle != 0u)
        queue_shuffle(queue, count == 0u ? 0u : ids[0]);
    else
        queue_reset_order(queue);
    return HELIKON_OK;
}

int helikon_queue_current(const struct helikon_queue *queue, uint32_t *id)
{
    if (queue == NULL || id == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (queue->count == 0u)
        return HELIKON_ERR_NOT_FOUND;
    *id = queue->ids[queue_current_index(queue)];
    return HELIKON_OK;
}

int helikon_queue_select(struct helikon_queue *queue, uint32_t index)
{
    if (queue == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (index >= queue->count)
        return HELIKON_ERR_ARGUMENT;
    queue->position = index;
    return HELIKON_OK;
}

int helikon_queue_set_repeat(struct helikon_queue *queue, uint8_t mode)
{
    if (queue == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (mode != HELIKON_QUEUE_REPEAT_OFF &&
        mode != HELIKON_QUEUE_REPEAT_ALL &&
        mode != HELIKON_QUEUE_REPEAT_ONE)
        return HELIKON_ERR_ARGUMENT;
    queue->repeat = mode;
    return HELIKON_OK;
}

int helikon_queue_set_shuffle(struct helikon_queue *queue, int enabled,
                              uint32_t seed)
{
    if (queue == NULL)
        return HELIKON_ERR_ARGUMENT;

    uint32_t keep = queue->count == 0u ? 0u : queue->ids[queue_current_index(queue)];
    queue->shuffle = enabled != 0 ? 1u : 0u;
    queue->seed = seed;
    if (queue->shuffle != 0u)
        queue_shuffle(queue, keep);
    else {
        queue_reset_order(queue);
        queue->position = queue_position_of(queue, keep);
        if (queue->position >= queue->count)
            queue->position = 0u;
    }
    return HELIKON_OK;
}

/*
 * One step along the order. With repeat off, the ends are hard stops: the
 * position and the current id do not change, so a caller that ignores the
 * result does not silently wrap. Repeat one answers RESTART and stays put; the
 * caller restarts the track rather than seeking to the next one.
 */
static int queue_step(struct helikon_queue *queue, int forward)
{
    if (queue == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (queue->count == 0u)
        return HELIKON_QUEUE_EMPTY;
    if (queue->repeat == HELIKON_QUEUE_REPEAT_ONE)
        return HELIKON_QUEUE_RESTART;

    if (forward) {
        if (queue->position + 1u < queue->count) {
            queue->position += 1u;
            return HELIKON_QUEUE_MOVED;
        }
        if (queue->repeat == HELIKON_QUEUE_REPEAT_ALL) {
            queue->position = 0u;
            return HELIKON_QUEUE_WRAPPED;
        }
        return HELIKON_QUEUE_END;
    }

    if (queue->position > 0u) {
        queue->position -= 1u;
        return HELIKON_QUEUE_MOVED;
    }
    if (queue->repeat == HELIKON_QUEUE_REPEAT_ALL) {
        queue->position = queue->count - 1u;
        return HELIKON_QUEUE_WRAPPED;
    }
    return HELIKON_QUEUE_END;
}

int helikon_queue_next(struct helikon_queue *queue)
{
    return queue_step(queue, 1);
}

int helikon_queue_previous(struct helikon_queue *queue)
{
    return queue_step(queue, 0);
}
