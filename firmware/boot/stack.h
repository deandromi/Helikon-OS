/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Stack guard and high-water reporting.
 *
 * _start fills each mode stack with 0xa5 and writes a "HELK" guard at its
 * lowest address. A guard mismatch is an invariant violation and must panic.
 * The high-water value is a measurement from the running image, not a target.
 */
#ifndef HELIKON_BOOT_STACK_H
#define HELIKON_BOOT_STACK_H

#include <stdint.h>

int helikon_stack_guards_ok(void);
uint32_t helikon_stack_used_bytes(void);
uint32_t helikon_stack_size_bytes(void);

#endif
