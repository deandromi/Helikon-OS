/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * S5L8702 VIC and IRQ handler table.
 *
 * The VIC has two 32-source banks. Bank 0 owns the sources used by the 6G
 * firmware (USB is source 19); bank 1 is initialised and cleared here but has
 * no handler table yet, so an active bank-1 source is counted as spurious and
 * returned instead of panicking.
 *
 * The handler table is fixed at 32 entries (VIC bank 0). One handler per IRQ,
 * no allocation and no nesting. Callers register before enabling the line.
 * helikon_vic_dispatch() is called from the assembly IRQ vector and must stay
 * short. It reads the VIC address registers for acknowledgement, dispatches
 * from the masked IRQ status, clears a software-interrupt source and writes
 * the address registers back to signal EOI.
 */
#ifndef HELIKON_HAL_6G_VIC_H
#define HELIKON_HAL_6G_VIC_H

#include <stdint.h>

#define HELIKON_VIC_BANK_COUNT 2u
#define HELIKON_VIC_IRQS_PER_BANK 32u
#define HELIKON_VIC_IRQ_COUNT \
    (HELIKON_VIC_BANK_COUNT * HELIKON_VIC_IRQS_PER_BANK)
#define HELIKON_VIC_HANDLER_COUNT 32u /* VIC bank 0 handler table */

#define HELIKON_VIC_IRQ_USB 19u
/* Diagnostic self-test line: VIC bank 0 software interrupt, source 0. */
#define HELIKON_VIC_IRQ_SOFT 0u

/* Register offsets inside one 0x1000-byte VIC bank. */
#define HELIKON_VIC_REG_IRQ_STATUS 0x000u
#define HELIKON_VIC_REG_FIQ_STATUS 0x004u
#define HELIKON_VIC_REG_RAW_INTR 0x008u
#define HELIKON_VIC_REG_INT_SELECT 0x00cu
#define HELIKON_VIC_REG_INT_ENABLE 0x010u
#define HELIKON_VIC_REG_INT_ENABLE_CLEAR 0x014u
#define HELIKON_VIC_REG_SOFT_INT 0x018u
#define HELIKON_VIC_REG_SOFT_INT_CLEAR 0x01cu
#define HELIKON_VIC_REG_ADDRESS 0xf00u

typedef void (*helikon_vic_handler)(void *context);

/*
 * Disable and clear both banks, select IRQ (not FIQ), clear all software
 * interrupts and empty the handler table. Call with CPU IRQs disabled, before
 * registering or enabling any line.
 */
int helikon_vic_init(void);

/* Enable/disable one VIC source 0..63. Returns HELIKON_ERR_ARGUMENT if out of
 * range. */
int helikon_vic_enable(uint32_t irq);
int helikon_vic_disable(uint32_t irq);

/* Masked pending state of one VIC source 0..63: 1 pending, 0 clear, or a
 * negative error for an invalid line. */
int helikon_vic_pending(uint32_t irq);

/*
 * Register or remove a bank-0 handler (IRQ 0..31). A second registration
 * replaces the first. Unregistering a line that has no handler is not an
 * error; en/disable state and pending bits are untouched.
 */
int helikon_vic_register(uint32_t irq, helikon_vic_handler handler,
                         void *context);
int helikon_vic_unregister(uint32_t irq);

/* Raise or clear a VIC software-interrupt source 0..63. */
int helikon_vic_software_trigger(uint32_t irq);
int helikon_vic_software_clear(uint32_t irq);

/* Write both VIC address registers to signal end-of-interrupt. */
void helikon_vic_acknowledge(void);

/* IRQ-context dispatcher called by firmware/boot/vectors.S. */
void helikon_vic_dispatch(void);

/* Saturating count of active sources with no registered handler. */
uint32_t helikon_vic_spurious_count(void);

#ifdef HELIKON_HOST_TEST
/* Host-test register backing. Offsets are the HELIKON_VIC_REG_* values. */
void helikon_vic_test_reset(void);
uint32_t helikon_vic_test_read(uint32_t bank, uint32_t offset);
void helikon_vic_test_write(uint32_t bank, uint32_t offset, uint32_t value);
/* Emulate a device source: set/clear the masked status and soft-int bit. */
void helikon_vic_test_set_pending(uint32_t irq, int pending);
#endif

#endif
