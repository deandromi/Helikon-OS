/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "stack.h"

#include <stddef.h>

extern uint8_t __svc_guard[];
extern uint8_t __irq_guard[];
extern uint8_t __fiq_guard[];
extern uint8_t __abt_guard[];
extern uint8_t __und_guard[];
extern uint8_t __svc_stack_bottom[];
extern uint8_t __svc_stack_top[];

#define STACK_GUARD_VALUE 0x48454c4bu

static int guard_ok(const uint8_t *guard)
{
    uint32_t value = (uint32_t)guard[0] | ((uint32_t)guard[1] << 8) |
                     ((uint32_t)guard[2] << 16) |
                     ((uint32_t)guard[3] << 24);
    return value == STACK_GUARD_VALUE;
}

int helikon_stack_guards_ok(void)
{
    return guard_ok(__svc_guard) && guard_ok(__irq_guard) &&
           guard_ok(__fiq_guard) && guard_ok(__abt_guard) &&
           guard_ok(__und_guard);
}

uint32_t helikon_stack_used_bytes(void)
{
    const uint8_t *bottom = __svc_stack_bottom;
    const uint8_t *top = __svc_stack_top;

    while (bottom < top && *bottom == 0xa5u)
        ++bottom;
    return (uint32_t)(top - bottom);
}

uint32_t helikon_stack_size_bytes(void)
{
    return (uint32_t)(__svc_stack_top - __svc_stack_bottom);
}
