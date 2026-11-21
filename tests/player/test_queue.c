/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host tests for the play queue (T-058).
 *
 * The queue decides what "next" means, so the tests pin the four answers
 * (moved, wrapped, end, restart) and the two orders (natural, shuffled) rather
 * than eyeballing a list.
 */
#include "queue.h"

#include <stdio.h>
#include <string.h>

#include "error.h"

static unsigned int failures;

static int expect(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        failures += 1u;
        return 1;
    }
    return 0;
}

static int expect_current(const struct helikon_queue *queue, uint32_t want,
                          const char *message)
{
    uint32_t id = 0xffffffffu;
    if (expect(helikon_queue_current(queue, &id) == HELIKON_OK, message))
        return 1;
    if (expect(id == want, message))
        return 1;
    return 0;
}

static int check_natural_order(void)
{
    struct helikon_queue queue;
    memset(&queue, 0, sizeof(queue));
    const uint32_t ids[4] = { 10u, 11u, 12u, 13u };
    if (expect(helikon_queue_set(&queue, ids, 4u) == HELIKON_OK, "set"))
        return 1;
    if (expect(queue.count == 4u && queue.shuffle == 0u, "count and mode"))
        return 1;
    if (expect_current(&queue, 10u, "the first id is current"))
        return 1;

    if (expect(helikon_queue_next(&queue) == HELIKON_QUEUE_MOVED, "next moves"))
        return 1;
    if (expect_current(&queue, 11u, "next selects the second id"))
        return 1;
    if (expect(helikon_queue_previous(&queue) == HELIKON_QUEUE_MOVED,
               "previous moves back"))
        return 1;
    if (expect_current(&queue, 10u, "previous selects the first id"))
        return 1;

    /* The start is a hard stop with repeat off: nothing moves. */
    if (expect(helikon_queue_previous(&queue) == HELIKON_QUEUE_END,
               "previous at the start ends"))
        return 1;
    if (expect_current(&queue, 10u, "a stopped previous does not move"))
        return 1;

    if (expect(helikon_queue_select(&queue, 3u) == HELIKON_OK, "select"))
        return 1;
    if (expect_current(&queue, 13u, "select jumps"))
        return 1;
    if (expect(helikon_queue_next(&queue) == HELIKON_QUEUE_END,
               "next at the end ends"))
        return 1;
    if (expect_current(&queue, 13u, "a stopped next does not move"))
        return 1;

    if (expect(helikon_queue_select(&queue, 4u) == HELIKON_ERR_ARGUMENT,
               "select past the end is refused"))
        return 1;
    return 0;
}

static int check_repeat(void)
{
    struct helikon_queue queue;
    memset(&queue, 0, sizeof(queue));
    const uint32_t ids[3] = { 1u, 2u, 3u };
    if (expect(helikon_queue_set(&queue, ids, 3u) == HELIKON_OK, "repeat set"))
        return 1;

    if (expect(helikon_queue_set_repeat(&queue, HELIKON_QUEUE_REPEAT_ALL) ==
                   HELIKON_OK,
               "repeat all"))
        return 1;
    if (expect(helikon_queue_select(&queue, 2u) == HELIKON_OK, "select last"))
        return 1;
    if (expect(helikon_queue_next(&queue) == HELIKON_QUEUE_WRAPPED,
               "repeat all wraps"))
        return 1;
    if (expect_current(&queue, 1u, "repeat all wraps to the first id"))
        return 1;
    if (expect(helikon_queue_previous(&queue) == HELIKON_QUEUE_WRAPPED,
               "repeat all wraps backwards"))
        return 1;
    if (expect_current(&queue, 3u, "a backward wrap lands on the last id"))
        return 1;

    /* Repeat one: the caller restarts, the position does not move. */
    if (expect(helikon_queue_set_repeat(&queue, HELIKON_QUEUE_REPEAT_ONE) ==
                   HELIKON_OK,
               "repeat one"))
        return 1;
    if (expect(helikon_queue_next(&queue) == HELIKON_QUEUE_RESTART,
               "repeat one asks for a restart"))
        return 1;
    if (expect_current(&queue, 3u, "repeat one keeps the id"))
        return 1;
    if (expect(helikon_queue_previous(&queue) == HELIKON_QUEUE_RESTART,
               "repeat one also restarts backwards"))
        return 1;

    if (expect(helikon_queue_set_repeat(&queue, 3u) == HELIKON_ERR_ARGUMENT,
               "an unknown repeat mode is refused"))
        return 1;
    if (expect(helikon_queue_set_repeat(&queue,
                                        HELIKON_QUEUE_REPEAT_OFF) == HELIKON_OK,
               "repeat off"))
        return 1;
    return 0;
}

static int check_shuffle(void)
{
    struct helikon_queue queue;
    memset(&queue, 0, sizeof(queue));
    uint32_t ids[8];
    for (uint32_t index = 0u; index < 8u; ++index)
        ids[index] = 100u + index;
    if (expect(helikon_queue_set(&queue, ids, 8u) == HELIKON_OK, "shuffle set"))
        return 1;

    /* Select the third id, then shuffle: the current id must survive. */
    if (expect(helikon_queue_select(&queue, 2u) == HELIKON_OK, "preselect"))
        return 1;
    if (expect(helikon_queue_set_shuffle(&queue, 1, 12345u) == HELIKON_OK,
               "shuffle on"))
        return 1;
    if (expect_current(&queue, 102u, "shuffle keeps the playing id"))
        return 1;

    /* Walk the whole order: every id exactly once, no repeats. */
    int seen[8];
    memset(seen, 0, sizeof(seen));
    uint32_t id = 0u;
    for (uint32_t step = 0u; step < 8u; ++step) {
        if (expect(helikon_queue_current(&queue, &id) == HELIKON_OK,
                   "shuffle current"))
            return 1;
        if (expect(id >= 100u && id < 108u, "shuffle id in range"))
            return 1;
        if (expect(seen[id - 100u] == 0, "shuffle visits each id once"))
            return 1;
        seen[id - 100u] = 1;
        (void)helikon_queue_next(&queue);
    }
    if (expect(helikon_queue_next(&queue) == HELIKON_QUEUE_END,
               "a shuffled walk ends with repeat off"))
        return 1;

    /*
     * The same seed produces the same order. The first queue has already been
     * walked to the end, so it is rebuilt from scratch: this compares two fresh
     * permutations, not a permutation with a finished walk.
     */
    struct helikon_queue again;
    memset(&again, 0, sizeof(again));
    if (expect(helikon_queue_set(&queue, ids, 8u) == HELIKON_OK, "rebuild"))
        return 1;
    if (expect(helikon_queue_set_shuffle(&queue, 1, 12345u) == HELIKON_OK,
               "rebuild shuffle"))
        return 1;
    if (expect(helikon_queue_set(&again, ids, 8u) == HELIKON_OK, "again set"))
        return 1;
    if (expect(helikon_queue_set_shuffle(&again, 1, 12345u) == HELIKON_OK,
               "again shuffle"))
        return 1;
    for (uint32_t step = 0u; step < 8u; ++step) {
        uint32_t left = 0u;
        uint32_t right = 0u;
        if (expect(helikon_queue_current(&queue, &left) == HELIKON_OK &&
                       helikon_queue_current(&again, &right) == HELIKON_OK,
                   "seed current"))
            return 1;
        if (expect(left == right, "a seed reproduces the order"))
            return 1;
        (void)helikon_queue_next(&queue);
        (void)helikon_queue_next(&again);
    }

    /* Shuffle off restores the natural order around the current id. */
    uint32_t current = 0u;
    if (expect(helikon_queue_current(&queue, &current) == HELIKON_OK,
               "current before unshuffle"))
        return 1;
    if (expect(helikon_queue_set_shuffle(&queue, 0, 0u) == HELIKON_OK,
               "shuffle off"))
        return 1;
    if (expect_current(&queue, current, "unshuffle keeps the current id"))
        return 1;
    if (expect(helikon_queue_next(&queue) == HELIKON_QUEUE_END ||
                   helikon_queue_next(&queue) == HELIKON_QUEUE_MOVED,
               "the natural order walks again"))
        return 1;
    return 0;
}

static int check_edge_cases(void)
{
    struct helikon_queue queue;
    memset(&queue, 0, sizeof(queue));
    uint32_t id = 0u;

    if (expect(helikon_queue_current(&queue, &id) == HELIKON_ERR_NOT_FOUND,
               "an empty queue has no current id"))
        return 1;
    if (expect(helikon_queue_next(&queue) == HELIKON_QUEUE_EMPTY,
               "next on an empty queue"))
        return 1;
    if (expect(helikon_queue_previous(&queue) == HELIKON_QUEUE_EMPTY,
               "previous on an empty queue"))
        return 1;

    /* An empty set is legal and clears a previous list. */
    const uint32_t ids[2] = { 7u, 8u };
    if (expect(helikon_queue_set(&queue, ids, 2u) == HELIKON_OK, "fill"))
        return 1;
    if (expect(helikon_queue_set(&queue, NULL, 0u) == HELIKON_OK, "empty set"))
        return 1;
    if (expect(queue.count == 0u, "the empty set clears the queue"))
        return 1;

    static uint32_t too_many[HELIKON_QUEUE_CAPACITY + 1u];
    for (uint32_t index = 0u; index < HELIKON_QUEUE_CAPACITY + 1u; ++index)
        too_many[index] = index;
    if (expect(helikon_queue_set(&queue, too_many, HELIKON_QUEUE_CAPACITY + 1u) ==
                   HELIKON_ERR_ARGUMENT,
               "a queue past capacity is refused"))
        return 1;
    if (expect(helikon_queue_set(&queue, NULL, 1u) == HELIKON_ERR_ARGUMENT,
               "a count without ids is refused"))
        return 1;

    /* The full capacity is usable, and "next" walks it. */
    if (expect(helikon_queue_set(&queue, too_many, HELIKON_QUEUE_CAPACITY) ==
                   HELIKON_OK,
               "the full capacity is accepted"))
        return 1;
    if (expect(helikon_queue_select(&queue,
                                    HELIKON_QUEUE_CAPACITY - 1u) == HELIKON_OK,
               "select the last slot"))
        return 1;
    if (expect_current(&queue, HELIKON_QUEUE_CAPACITY - 1u,
                       "the last slot holds the last id"))
        return 1;
    if (expect(helikon_queue_next(&queue) == HELIKON_QUEUE_END,
               "the last slot ends the walk"))
        return 1;

    if (expect(helikon_queue_next(NULL) == HELIKON_ERR_ARGUMENT,
               "a null queue is refused"))
        return 1;
    if (expect(helikon_queue_set(NULL, ids, 1u) == HELIKON_ERR_ARGUMENT,
               "a null queue cannot be filled"))
        return 1;
    if (expect(helikon_queue_current(NULL, &id) == HELIKON_ERR_ARGUMENT,
               "a null queue has no current id"))
        return 1;
    return 0;
}

int main(void)
{
    if (check_natural_order() || check_repeat() || check_shuffle() ||
        check_edge_cases())
        return 1;
    if (failures != 0u) {
        printf("FAIL play queue\n");
        return 1;
    }
    printf("PASS play queue\n");
    return 0;
}
