/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM926 CPU primitives.
 *
 * These are the only routines allowed to touch CP15 and CPSR. They do not
 * allocate, block or touch the filesystem. The exact MMU/cache state across
 * the loader jump is an open question; F1 reads the control register and
 * records it instead of guessing.
 */
#ifndef HELIKON_HAL_6G_CPU_H
#define HELIKON_HAL_6G_CPU_H

#include <stdint.h>

uint32_t helikon_cpu_control(void);
uint32_t helikon_cpu_mode(void);
void helikon_cpu_disable_interrupts(void);
void helikon_cpu_data_sync(void);
void helikon_cpu_idle(void);

#endif
