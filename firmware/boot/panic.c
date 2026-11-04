/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "panic.h"

#include "clock.h"
#include "error.h"

static struct helikon_panic_hooks panic_hooks;
static struct helikon_trace *panic_trace;
static int panic_stage = -1;
static volatile int panic_active;

void helikon_panic_set_hooks(const struct helikon_panic_hooks *hooks)
{
    if (hooks == NULL) {
        panic_hooks.disable_interrupts = NULL;
        panic_hooks.paint = NULL;
        panic_hooks.power_off = NULL;
        panic_hooks.context = NULL;
        return;
    }
    panic_hooks = *hooks;
}

void helikon_panic_set_trace(struct helikon_trace *trace)
{
    panic_trace = trace;
}

void helikon_panic_set_stage(int stage)
{
    panic_stage = stage;
}

static uint32_t read_sp(void)
{
    uint32_t value;
    __asm__ volatile("mov %0, sp" : "=r"(value));
    return value;
}

static uint32_t read_mode(void)
{
    uint32_t value;
    __asm__ volatile("mrs %0, cpsr" : "=r"(value));
    return value & 0x1fu;
}

void helikon_panic(enum helikon_panic_reason reason, uint32_t pc, uint32_t lr)
{
    if (panic_active) {
        /* A fault inside the panic path: stop, do not recurse. */
        uint32_t cpsr;
        __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
        cpsr |= 0xc0u;
        __asm__ volatile("msr cpsr_c, %0" : : "r"(cpsr) : "memory");
        for (;;)
            __asm__ volatile("nop");
    }
    panic_active = 1;

    if (panic_hooks.disable_interrupts != NULL)
        panic_hooks.disable_interrupts(panic_hooks.context);
    else {
        uint32_t cpsr;
        __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
        cpsr |= 0xc0u;
        __asm__ volatile("msr cpsr_c, %0" : : "r"(cpsr) : "memory");
    }

    struct helikon_crash_record record;
    record.magic = HELIKON_CRASH_MAGIC;
    record.reason = (uint32_t)reason;
    record.pc = pc;
    record.lr = lr;
    record.sp = read_sp();
    record.mode = read_mode();
    record.stage = panic_stage;
    record.timestamp_us = 0;
    helikon_crash_set(&record);

    if (panic_trace != NULL) {
        helikon_trace_add(panic_trace, HELIKON_TRACE_ERROR,
                          HELIKON_TRACE_BOOT, (uint16_t)reason, pc, lr);
    }

    if (panic_hooks.paint != NULL)
        panic_hooks.paint(&record, panic_hooks.context);

    if (panic_hooks.power_off != NULL)
        panic_hooks.power_off(panic_hooks.context);

    for (;;)
        __asm__ volatile("nop");
}

void helikon_panic_vector(uint32_t reason, uint32_t lr)
{
    helikon_panic((enum helikon_panic_reason)reason, lr, lr);
}
