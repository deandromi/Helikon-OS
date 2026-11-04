/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Panic path.
 *
 * A panic disables interrupts, records a crash record and a trace event,
 * paints one screen through a registered hook and powers off or parks. It never
 * writes the filesystem, never replays a journal and never reboots in a loop.
 */
#ifndef HELIKON_BOOT_PANIC_H
#define HELIKON_BOOT_PANIC_H

#include <stdint.h>

#include "crash.h"
#include "trace.h"

struct helikon_panic_hooks {
    void (*disable_interrupts)(void *context);
    void (*paint)(const struct helikon_crash_record *record, void *context);
    void (*power_off)(void *context);
    void *context;
};

void helikon_panic_set_hooks(const struct helikon_panic_hooks *hooks);
void helikon_panic_set_trace(struct helikon_trace *trace);
void helikon_panic_set_stage(int stage);

void helikon_panic(enum helikon_panic_reason reason, uint32_t pc, uint32_t lr)
    __attribute__((noreturn));
void helikon_panic_vector(uint32_t reason, uint32_t lr)
    __attribute__((noreturn));

#endif
