/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "error.h"
#include "trace.h"

#include "test_check.h"

static uint64_t fake_now;

static uint64_t now_us(void *context)
{
    (void)context;
    return fake_now;
}

int main(void)
{
    struct helikon_trace trace;
    struct helikon_trace_record record;

    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_trace_get(NULL, 0, &record));
    helikon_trace_init(&trace, now_us, NULL);
    CHECK_EQ(0, helikon_trace_count(&trace));

    fake_now = 10;
    helikon_trace_add(&trace, HELIKON_TRACE_INFO, HELIKON_TRACE_BOOT,
                      0x1000u, 1u, 2u);
    fake_now = 20;
    helikon_trace_add(&trace, HELIKON_TRACE_WARN, HELIKON_TRACE_HAL,
                      0x2000u, 3u, 4u);
    CHECK_EQ(2, helikon_trace_count(&trace));
    CHECK(helikon_trace_get(&trace, 0, &record) == 1);
    CHECK_EQ(10, record.timestamp_us);
    CHECK_EQ(HELIKON_TRACE_INFO, record.level);
    CHECK_EQ(0x1000, record.event);
    CHECK(helikon_trace_get(&trace, 1, &record) == 1);
    CHECK_EQ(20, record.timestamp_us);

    for (unsigned int i = 0; i < HELIKON_TRACE_CAPACITY + 2u; ++i) {
        fake_now = 100u + i;
        helikon_trace_add(&trace, HELIKON_TRACE_TRACE, HELIKON_TRACE_DIAG,
                          (uint16_t)i, i, 0u);
    }
    CHECK_EQ(HELIKON_TRACE_CAPACITY, helikon_trace_count(&trace));
    CHECK_EQ(4, helikon_trace_dropped(&trace));
    CHECK(helikon_trace_get(&trace, 0, &record) == 1);
    CHECK_EQ(2, record.event);
    CHECK(helikon_trace_get(&trace, HELIKON_TRACE_CAPACITY, &record) == 0);

    helikon_trace_reset(&trace);
    CHECK_EQ(0, helikon_trace_count(&trace));

    return helikon_test_report("trace");
}
