/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded fixed-element ring buffer.
 *
 * The ring is a view over caller-owned storage. It is single-writer: the
 * caller serializes pushes (the firmware runs with interrupts disabled during
 * the first phases). A push to a full ring overwrites the oldest element and
 * increments the drop counter, so the trace path never fails or allocates.
 */
#ifndef HELIKON_LIB_RING_H
#define HELIKON_LIB_RING_H

#include <stddef.h>
#include <stdint.h>

struct helikon_ring {
    uint8_t *storage;
    size_t element_size;
    size_t capacity;
    size_t head;       /* next write slot */
    size_t count;      /* valid elements, <= capacity */
    uint32_t dropped;  /* elements overwritten by a full push */
};

/*
 * `storage` must hold at least `element_size * capacity` bytes and stay valid
 * for the ring lifetime. Returns HELIKON_OK or HELIKON_ERR_ARGUMENT.
 */
int helikon_ring_init(struct helikon_ring *ring, void *storage,
                      size_t element_size, size_t capacity);

/* Copy one element in. Overwrites the oldest when full. Never blocks. */
int helikon_ring_push(struct helikon_ring *ring, const void *element);

/* Copy the oldest element out and consume it. Returns 0 when empty. */
int helikon_ring_pop(struct helikon_ring *ring, void *element);

/* Copy element 0 (oldest) .. count-1 without consuming. Returns 0 if absent. */
int helikon_ring_peek(const struct helikon_ring *ring, size_t index,
                      void *element);

size_t helikon_ring_count(const struct helikon_ring *ring);
uint32_t helikon_ring_dropped(const struct helikon_ring *ring);
void helikon_ring_reset(struct helikon_ring *ring);

#endif
