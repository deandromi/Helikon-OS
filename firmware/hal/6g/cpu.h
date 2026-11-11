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

#include <stddef.h>
#include <stdint.h>

uint32_t helikon_cpu_control(void);
uint32_t helikon_cpu_mode(void);
void helikon_cpu_disable_interrupts(void);
/* Clear the CPSR I bit only; the F bit is left unchanged. */
void helikon_cpu_enable_interrupts(void);
/* 1 when the CPSR I bit is clear, 0 when IRQs are masked. */
int helikon_cpu_irq_enabled(void);
void helikon_cpu_data_sync(void);
void helikon_cpu_clean_dcache_range(const void *address, size_t length);
void helikon_cpu_idle(void);

#endif
