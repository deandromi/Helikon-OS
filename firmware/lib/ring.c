/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ring.h"

#include "error.h"

static uint8_t *slot(const struct helikon_ring *ring, size_t index)
{
    size_t position = (ring->head + ring->capacity - ring->count + index) %
                      ring->capacity;
    return ring->storage + position * ring->element_size;
}

int helikon_ring_init(struct helikon_ring *ring, void *storage,
                      size_t element_size, size_t capacity)
{
    if (ring == NULL || storage == NULL || element_size == 0u ||
        capacity == 0u)
        return HELIKON_ERR_ARGUMENT;

    ring->storage = storage;
    ring->element_size = element_size;
    ring->capacity = capacity;
    ring->head = 0;
    ring->count = 0;
    ring->dropped = 0;
    return HELIKON_OK;
}

int helikon_ring_push(struct helikon_ring *ring, const void *element)
{
    if (ring == NULL || element == NULL)
        return HELIKON_ERR_ARGUMENT;

    uint8_t *destination = ring->storage + ring->head * ring->element_size;
    const uint8_t *source = element;
    for (size_t i = 0; i < ring->element_size; ++i)
        destination[i] = source[i];

    ring->head = (ring->head + 1u) % ring->capacity;
    if (ring->count < ring->capacity) {
        ++ring->count;
    } else {
        ++ring->dropped;
    }
    return HELIKON_OK;
}

int helikon_ring_pop(struct helikon_ring *ring, void *element)
{
    if (ring == NULL || element == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (ring->count == 0u)
        return 0;

    const uint8_t *source = slot(ring, 0);
    uint8_t *destination = element;
    for (size_t i = 0; i < ring->element_size; ++i)
        destination[i] = source[i];
    --ring->count;
    return 1;
}

int helikon_ring_peek(const struct helikon_ring *ring, size_t index,
                      void *element)
{
    if (ring == NULL || element == NULL || index >= ring->count)
        return 0;

    const uint8_t *source = slot(ring, index);
    uint8_t *destination = element;
    for (size_t i = 0; i < ring->element_size; ++i)
        destination[i] = source[i];
    return 1;
}

size_t helikon_ring_count(const struct helikon_ring *ring)
{
    return ring == NULL ? 0u : ring->count;
}

uint32_t helikon_ring_dropped(const struct helikon_ring *ring)
{
    return ring == NULL ? 0u : ring->dropped;
}

void helikon_ring_reset(struct helikon_ring *ring)
{
    if (ring == NULL)
        return;
    ring->head = 0;
    ring->count = 0;
    ring->dropped = 0;
}
