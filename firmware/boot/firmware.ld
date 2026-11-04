/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helikon-OS 6G firmware linker script.
 *
 * The image is linked at the signed load address and is not relocatable. The
 * accepted map (D-27 / ADR-0003) reserves the top 4 KiB of the 8 MiB signed
 * window for the loader boot-info page. BSS, stacks, pools and the arena live
 * at 0x08800000 in the kernel region; the raw image ends at __image_end and
 * never includes the kernel region.
 */
ENTRY(_start)
OUTPUT_FORMAT(elf32-littlearm)
OUTPUT_ARCH(arm)

MEMORY
{
    IMAGE (rx)  : ORIGIN = 0x08000000, LENGTH = 0x007ff000
    KERNEL (rw) : ORIGIN = 0x08800000, LENGTH = 0x00800000
}

SECTIONS
{
    .text : {
        __image_start = .;
        KEEP(*(.text.start))
        KEEP(*(.text.vectors))
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        . = ALIGN(4);
    } > IMAGE

    .rodata : {
        *(.rodata*)
        . = ALIGN(4);
    } > IMAGE

    .data : {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > IMAGE

    __image_end = .;

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(32);
        __bss_end = .;
    } > KERNEL

    .stacks (NOLOAD) : {
        . = ALIGN(32);
        __stack_region_start = .;

        __svc_guard = .;
        . += 4;
        __svc_stack_bottom = .;
        . += 8188;
        __svc_stack_top = .;

        __irq_guard = .;
        . += 4;
        __irq_stack_bottom = .;
        . += 4092;
        __irq_stack_top = .;

        __fiq_guard = .;
        . += 4;
        __fiq_stack_bottom = .;
        . += 1020;
        __fiq_stack_top = .;

        __abt_guard = .;
        . += 4;
        __abt_stack_bottom = .;
        . += 1020;
        __abt_stack_top = .;

        __und_guard = .;
        . += 4;
        __und_stack_bottom = .;
        . += 1020;
        __und_stack_top = .;

        . = ALIGN(32);
        __stack_region_end = .;
    } > KERNEL

    .arena (NOLOAD) : {
        . = ALIGN(32);
        __arena_start = .;
    } > KERNEL

    __arena_end = 0x09000000;
    __kernel_end = .;

    /* Accepted map assertions (ADR-0003). */
    ASSERT(__image_start == 0x08000000, "image must start at 0x08000000")
    ASSERT(__image_end <= 0x087ff000, "image overlaps the boot-info page")
    ASSERT(__bss_end <= 0x09000000, "BSS exceeds the kernel region")
    ASSERT(__kernel_end <= 0x09000000, "kernel region exceeds 0x09000000")

    /DISCARD/ : {
        *(.eh_frame)
        *(.ARM.exidx*)
        *(.comment)
    }
}
