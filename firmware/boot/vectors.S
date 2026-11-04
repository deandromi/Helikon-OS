/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ARM exception vector table and fault stubs.
 *
 * The table is copied to address 0 by _start. Each entry uses a PC-relative
 * literal so the handler address can be anywhere in the image. The handlers
 * save the original LR in r1, set a reason in r0 and enter the panic path.
 */
    .section .text.vectors, "ax", %progbits
    .global _helikon_vectors_start
_helikon_vectors_start:
    ldr pc, vector_reset
    ldr pc, vector_undef
    ldr pc, vector_swi
    ldr pc, vector_prefetch
    ldr pc, vector_data
    ldr pc, vector_reserved
    ldr pc, vector_irq
    ldr pc, vector_fiq

vector_reset:     .word _start
vector_undef:     .word helikon_vector_undef
vector_swi:       .word helikon_vector_swi
vector_prefetch:  .word helikon_vector_prefetch
vector_data:      .word helikon_vector_data
vector_reserved:  .word helikon_vector_reserved
vector_irq:       .word helikon_vector_irq
vector_fiq:       .word helikon_vector_fiq
    .global _helikon_vectors_end
_helikon_vectors_end:

    .text
    .global helikon_vector_undef
    .type helikon_vector_undef, %function
helikon_vector_undef:
    mov r1, lr
    mov r0, #1
    bl helikon_panic_vector
1:
    b 1b

    .global helikon_vector_swi
    .type helikon_vector_swi, %function
helikon_vector_swi:
    mov r1, lr
    mov r0, #2
    bl helikon_panic_vector
1:
    b 1b

    .global helikon_vector_prefetch
    .type helikon_vector_prefetch, %function
helikon_vector_prefetch:
    mov r1, lr
    mov r0, #3
    bl helikon_panic_vector
1:
    b 1b

    .global helikon_vector_data
    .type helikon_vector_data, %function
helikon_vector_data:
    mov r1, lr
    mov r0, #4
    bl helikon_panic_vector
1:
    b 1b

    .global helikon_vector_reserved
    .type helikon_vector_reserved, %function
helikon_vector_reserved:
    mov r1, lr
    mov r0, #5
    bl helikon_panic_vector
1:
    b 1b

    .global helikon_vector_irq
    .type helikon_vector_irq, %function
helikon_vector_irq:
    mov r1, lr
    mov r0, #6
    bl helikon_panic_vector
1:
    b 1b

    .global helikon_vector_fiq
    .type helikon_vector_fiq, %function
helikon_vector_fiq:
    mov r1, lr
    mov r0, #7
    bl helikon_panic_vector
1:
    b 1b

    .ltorg
