/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "trace.h"

#include "error.h"
#include "mem.h"

void helikon_trace_init(struct helikon_trace *trace,
                        uint64_t (*now_us)(void *context), void *context)
{
    if (trace == NULL)
        return;
    helikon_ring_init(&trace->ring, trace->storage,
                      sizeof(trace->storage[0]), HELIKON_TRACE_CAPACITY);
    trace->now_us = now_us;
    trace->context = context;
    trace->initialized = 1;
}

void helikon_trace_add(struct helikon_trace *trace, enum helikon_trace_level level,
                       enum helikon_trace_module module, uint16_t event,
                       uint32_t arg0, uint32_t arg1)
{
    if (trace == NULL || !trace->initialized)
        return;

    struct helikon_trace_record record;
    record.timestamp_us =
        trace->now_us != NULL ? trace->now_us(trace->context) : 0u;
    record.level = (uint8_t)level;
    record.module = (uint8_t)module;
    record.event = event;
    record.arg0 = arg0;
    record.arg1 = arg1;
    helikon_ring_push(&trace->ring, &record);
}

size_t helikon_trace_count(const struct helikon_trace *trace)
{
    return trace == NULL ? 0u : helikon_ring_count(&trace->ring);
}

uint32_t helikon_trace_dropped(const struct helikon_trace *trace)
{
    return trace == NULL ? 0u : helikon_ring_dropped(&trace->ring);
}

int helikon_trace_get(const struct helikon_trace *trace, size_t index,
                      struct helikon_trace_record *record)
{
    if (trace == NULL || record == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_ring_peek(&trace->ring, index, record);
}

void helikon_trace_reset(struct helikon_trace *trace)
{
    if (trace != NULL)
        helikon_ring_reset(&trace->ring);
}
