/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "error.h"
#include "pool.h"

#include "test_check.h"

int main(void)
{
    uint8_t storage[3 * 16];
    struct helikon_pool pool;
    void *objects[3];

    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_pool_init(NULL, storage, 16, 3));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_pool_init(&pool, NULL, 16, 3));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_pool_init(&pool, storage, 0, 3));
    CHECK_EQ(HELIKON_OK, helikon_pool_init(&pool, storage, 16, 3));

    for (unsigned int i = 0; i < 3; ++i) {
        objects[i] = helikon_pool_alloc(&pool);
        CHECK(objects[i] != NULL);
    }
    CHECK(helikon_pool_alloc(&pool) == NULL);
    CHECK_EQ(3, helikon_pool_used(&pool));
    CHECK_EQ(3, helikon_pool_high_water(&pool));

    CHECK_EQ(HELIKON_OK, helikon_pool_free(&pool, objects[1]));
    CHECK_EQ(2, helikon_pool_used(&pool));
    CHECK_EQ(HELIKON_ERR_BUSY, helikon_pool_free(&pool, objects[1]));
    CHECK_EQ(1, helikon_pool_double_free(&pool));

    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_pool_free(&pool, storage + 1));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_pool_free(&pool, (void *)0x1000));

    void *reused = helikon_pool_alloc(&pool);
    CHECK(reused == objects[1]);
    CHECK_EQ(3, helikon_pool_used(&pool));

    return helikon_test_report("pool");
}
