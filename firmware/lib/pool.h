/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Fixed-size object pool.
 *
 * The pool is a free list over caller-owned storage. There is no growth and
 * no allocation from a heap. A double free is detected by walking the free
 * list (the pools are small) and returns HELIKON_ERR_BUSY while incrementing
 * the counter; the caller may treat that as a panic in debug builds.
 */
#ifndef HELIKON_LIB_POOL_H
#define HELIKON_LIB_POOL_H

#include <stddef.h>
#include <stdint.h>

struct helikon_pool {
    uint8_t *storage;
    size_t object_size;
    size_t capacity;
    void *free_list;
    size_t used;
    size_t high_water;
    uint32_t double_free;
};

/*
 * `storage` must hold at least `capacity * aligned_object_size` bytes and stay
 * valid for the pool lifetime. `object_size` is rounded up to pointer
 * alignment. Returns HELIKON_OK, HELIKON_ERR_ARGUMENT or HELIKON_ERR_BUFFER.
 */
int helikon_pool_init(struct helikon_pool *pool, void *storage,
                      size_t object_size, size_t capacity);

/* Returns NULL when the pool is exhausted. Never blocks. */
void *helikon_pool_alloc(struct helikon_pool *pool);

/*
 * Returns HELIKON_OK, HELIKON_ERR_ARGUMENT for a foreign/misaligned pointer,
 * or HELIKON_ERR_BUSY for a double free.
 */
int helikon_pool_free(struct helikon_pool *pool, void *object);

size_t helikon_pool_used(const struct helikon_pool *pool);
size_t helikon_pool_high_water(const struct helikon_pool *pool);
uint32_t helikon_pool_double_free(const struct helikon_pool *pool);

#endif
