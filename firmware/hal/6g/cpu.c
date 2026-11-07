/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu.h"

uint32_t helikon_cpu_control(void)
{
    uint32_t value;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(value));
    return value;
}

uint32_t helikon_cpu_mode(void)
{
    uint32_t value;
    __asm__ volatile("mrs %0, cpsr" : "=r"(value));
    return value & 0x1fu;
}

void helikon_cpu_disable_interrupts(void)
{
    uint32_t value;
    __asm__ volatile("mrs %0, cpsr" : "=r"(value));
    value |= 0xc0u; /* IRQ and FIQ disable */
    __asm__ volatile("msr cpsr_c, %0" : : "r"(value) : "memory");
}

void helikon_cpu_data_sync(void)
{
    uint32_t zero = 0;
    /* Drain the write buffer (ARM926 TRM, CP15 c7 c10). */
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(zero) : "memory");
}

void helikon_cpu_clean_dcache_range(const void *address, size_t length)
{
    uintptr_t start = (uintptr_t)address & ~(uintptr_t)31u;
    uintptr_t end = ((uintptr_t)address + length + 31u) & ~(uintptr_t)31u;
    for (uintptr_t line = start; line < end; line += 32u) {
        __asm__ volatile("mcr p15, 0, %0, c7, c14, 1"
                         :
                         : "r"(line)
                         : "memory");
    }
    helikon_cpu_data_sync();
}

void helikon_cpu_idle(void)
{
    uint32_t zero = 0;
    /* Wait For Interrupt: CP15 c7 c0,4 on ARM926EJ-S. */
    __asm__ volatile("mcr p15, 0, %0, c7, c0, 4" : : "r"(zero) : "memory");
}
