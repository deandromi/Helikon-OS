/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "pool.h"

#include "error.h"

static size_t aligned_size(size_t size)
{
    size_t alignment = sizeof(void *);
    return (size + alignment - 1u) & ~(alignment - 1u);
}

int helikon_pool_init(struct helikon_pool *pool, void *storage,
                      size_t object_size, size_t capacity)
{
    if (pool == NULL || storage == NULL || object_size == 0u ||
        capacity == 0u)
        return HELIKON_ERR_ARGUMENT;

    size_t stride = aligned_size(object_size);
    if (capacity > SIZE_MAX / stride)
        return HELIKON_ERR_BUFFER;

    pool->storage = storage;
    pool->object_size = stride;
    pool->capacity = capacity;
    pool->free_list = NULL;
    pool->used = 0;
    pool->high_water = 0;
    pool->double_free = 0;

    /* Build the list high-to-low so alloc returns the lowest address first. */
    for (size_t i = capacity; i != 0u; --i) {
        uint8_t *object = pool->storage + (i - 1u) * stride;
        void **link = (void **)(void *)object;
        *link = pool->free_list;
        pool->free_list = object;
    }
    return HELIKON_OK;
}

void *helikon_pool_alloc(struct helikon_pool *pool)
{
    if (pool == NULL || pool->free_list == NULL)
        return NULL;

    void *object = pool->free_list;
    void **link = (void **)object;
    pool->free_list = *link;

    ++pool->used;
    if (pool->used > pool->high_water)
        pool->high_water = pool->used;
    return object;
}

static int object_in_pool(const struct helikon_pool *pool, const void *object)
{
    const uint8_t *bytes = object;
    uintptr_t start = (uintptr_t)pool->storage;
    uintptr_t address = (uintptr_t)bytes;

    if (address < start)
        return 0;
    size_t offset = (size_t)(address - start);
    if (offset % pool->object_size != 0u)
        return 0;
    return offset / pool->object_size < pool->capacity;
}

int helikon_pool_free(struct helikon_pool *pool, void *object)
{
    if (pool == NULL || object == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (!object_in_pool(pool, object))
        return HELIKON_ERR_ARGUMENT;

    for (void *cursor = pool->free_list; cursor != NULL;
         cursor = *(void **)cursor) {
        if (cursor == object) {
            ++pool->double_free;
            return HELIKON_ERR_BUSY;
        }
    }

    void **link = (void **)object;
    *link = pool->free_list;
    pool->free_list = object;
    if (pool->used != 0u)
        --pool->used;
    return HELIKON_OK;
}

size_t helikon_pool_used(const struct helikon_pool *pool)
{
    return pool == NULL ? 0u : pool->used;
}

size_t helikon_pool_high_water(const struct helikon_pool *pool)
{
    return pool == NULL ? 0u : pool->high_water;
}

uint32_t helikon_pool_double_free(const struct helikon_pool *pool)
{
    return pool == NULL ? 0u : pool->double_free;
}
