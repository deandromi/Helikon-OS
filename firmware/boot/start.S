/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helikon-OS firmware entry for the ARM926EJ-S.
 *
 * The loader passes r0 = boot-info page, r1 = 0, with IRQ/FIQ disabled and no
 * valid stack. The entry sequence saves r0, sets the mode stacks, zeroes BSS,
 * installs the exception vectors at 0, marks the stack guards and fills the
 * stacks with a high-water pattern, then calls firmware_main. A return is a
 * panic, never a loader recovery path.
 */
    .section .text.start, "ax", %progbits
    .global _start
    .type _start, %function
_start:
    b _start_setup

_start_setup:
    /* Preserve the loader's boot-info pointer in a callee-saved register. */
    mov r4, r0

    /* Supervisor mode, IRQ and FIQ disabled. */
    msr cpsr_c, #0xd3
    ldr sp, =__svc_stack_top

    /* Exception-mode stacks. */
    msr cpsr_c, #0xd2
    ldr sp, =__irq_stack_top
    msr cpsr_c, #0xd1
    ldr sp, =__fiq_stack_top
    msr cpsr_c, #0xd7
    ldr sp, =__abt_stack_top
    msr cpsr_c, #0xdb
    ldr sp, =__und_stack_top
    msr cpsr_c, #0xd3
    ldr sp, =__svc_stack_top

    /*
     * .data is linked at its load address (LMA == VMA) and is part of the raw
     * image, so no copy is required. Only .bss lives outside the image.
     */
    /* Zero .bss from __bss_start to __bss_end. */
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 1b

    /* Install the exception vector table at address 0 (IRAM on the 6G). */
    ldr r0, =0
    ldr r1, =_helikon_vectors_start
    ldr r2, =_helikon_vectors_end
2:
    ldr r3, [r1], #4
    str r3, [r0], #4
    cmp r1, r2
    blo 2b

    /* Fill stacks with 0xa5 so the firmware can report high-water marks. */
    ldr r2, =0xa5a5a5a5
    ldr r0, =__svc_stack_bottom
    ldr r1, =__svc_stack_top
3:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 3b
    ldr r0, =__irq_stack_bottom
    ldr r1, =__irq_stack_top
4:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 4b
    ldr r0, =__fiq_stack_bottom
    ldr r1, =__fiq_stack_top
5:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 5b
    ldr r0, =__abt_stack_bottom
    ldr r1, =__abt_stack_top
6:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 6b
    ldr r0, =__und_stack_bottom
    ldr r1, =__und_stack_top
7:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 7b

    /* Stack guards ("HELK"); a hit is a panic in firmware_main. */
    ldr r1, =0x48454c4b
    ldr r0, =__svc_guard
    str r1, [r0]
    ldr r0, =__irq_guard
    str r1, [r0]
    ldr r0, =__fiq_guard
    str r1, [r0]
    ldr r0, =__abt_guard
    str r1, [r0]
    ldr r0, =__und_guard
    str r1, [r0]

    /* firmware_main(uintptr_t boot_info, uint32_t reserved). */
    mov r0, r4
    mov r1, #0
    bl firmware_main

    /* A return is a firmware panic, not a loader recovery path. */
    mov r0, #10
    mov r1, lr
    bl helikon_panic_vector
8:
    b 8b

    .size _start, .-_start
    .ltorg
