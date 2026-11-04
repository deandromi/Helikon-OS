/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded RAM trace ring.
 *
 * Records carry a timestamp, a level, a module, an event code and two
 * arguments. The ring overwrites its oldest record when full and counts the
 * drops. It is safe from a panic or an interrupt only when the caller keeps
 * the writer single and does not re-enter; F1 runs with IRQs disabled.
 *
 * The trace never writes the filesystem. Export is a later, explicit action.
 */
#ifndef HELIKON_DIAG_TRACE_H
#define HELIKON_DIAG_TRACE_H

#include <stddef.h>
#include <stdint.h>

#include "ring.h"

#define HELIKON_TRACE_CAPACITY 128u

enum helikon_trace_level {
    HELIKON_TRACE_ERROR = 0,
    HELIKON_TRACE_WARN = 1,
    HELIKON_TRACE_INFO = 2,
    HELIKON_TRACE_TRACE = 3
};

enum helikon_trace_module {
    HELIKON_TRACE_BOOT = 0,
    HELIKON_TRACE_HAL = 1,
    HELIKON_TRACE_BLOCK = 2,
    HELIKON_TRACE_HFS = 3,
    HELIKON_TRACE_DIAG = 4
};

struct helikon_trace_record {
    uint64_t timestamp_us;
    uint8_t level;
    uint8_t module;
    uint16_t event;
    uint32_t arg0;
    uint32_t arg1;
};

struct helikon_trace {
    struct helikon_ring ring;
    struct helikon_trace_record storage[HELIKON_TRACE_CAPACITY];
    uint64_t (*now_us)(void *context);
    void *context;
    int initialized;
};

void helikon_trace_init(struct helikon_trace *trace,
                        uint64_t (*now_us)(void *context), void *context);
void helikon_trace_add(struct helikon_trace *trace, enum helikon_trace_level level,
                       enum helikon_trace_module module, uint16_t event,
                       uint32_t arg0, uint32_t arg1);
size_t helikon_trace_count(const struct helikon_trace *trace);
uint32_t helikon_trace_dropped(const struct helikon_trace *trace);
int helikon_trace_get(const struct helikon_trace *trace, size_t index,
                      struct helikon_trace_record *record);
void helikon_trace_reset(struct helikon_trace *trace);

#endif
