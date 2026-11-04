/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "error.h"
#include "ring.h"

#include "test_check.h"

struct element {
    uint32_t value;
};

int main(void)
{
    struct element storage[3];
    struct helikon_ring ring;
    struct element value;

    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_ring_init(NULL, storage, sizeof(storage[0]), 3));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_ring_init(&ring, NULL, sizeof(storage[0]), 3));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_ring_init(&ring, storage, 0, 3));
    CHECK_EQ(HELIKON_OK, helikon_ring_init(&ring, storage,
                                           sizeof(storage[0]), 3));

    for (uint32_t i = 1; i <= 3; ++i) {
        value.value = i;
        CHECK_EQ(HELIKON_OK, helikon_ring_push(&ring, &value));
    }
    CHECK_EQ(3, helikon_ring_count(&ring));
    CHECK(helikon_ring_peek(&ring, 0, &value) == 1);
    CHECK_EQ(1, value.value);
    CHECK(helikon_ring_peek(&ring, 2, &value) == 1);
    CHECK_EQ(3, value.value);

    value.value = 4;
    CHECK_EQ(HELIKON_OK, helikon_ring_push(&ring, &value));
    CHECK_EQ(1, helikon_ring_dropped(&ring));
    CHECK_EQ(3, helikon_ring_count(&ring));
    CHECK(helikon_ring_peek(&ring, 0, &value) == 1);
    CHECK_EQ(2, value.value);
    CHECK(helikon_ring_peek(&ring, 2, &value) == 1);
    CHECK_EQ(4, value.value);

    CHECK(helikon_ring_pop(&ring, &value) == 1);
    CHECK_EQ(2, value.value);
    CHECK_EQ(2, helikon_ring_count(&ring));
    CHECK(helikon_ring_peek(&ring, 3, &value) == 0);

    helikon_ring_reset(&ring);
    CHECK_EQ(0, helikon_ring_count(&ring));
    CHECK_EQ(0, helikon_ring_dropped(&ring));
    CHECK(helikon_ring_pop(&ring, &value) == 0);

    return helikon_test_report("ring");
}
