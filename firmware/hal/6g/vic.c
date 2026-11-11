/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "vic.h"

#include <stddef.h>

#include "error.h"

#ifndef HELIKON_HOST_TEST
#include "s5l8702_regs.h"
#endif

struct vic_handler_entry {
    helikon_vic_handler handler;
    void *context;
};

static struct vic_handler_entry vic_handlers[HELIKON_VIC_HANDLER_COUNT];
static uint32_t vic_spurious;

#ifdef HELIKON_HOST_TEST
#define VIC_MOCK_WORDS (0x1000u / 4u)
static uint32_t vic_mock[HELIKON_VIC_BANK_COUNT][VIC_MOCK_WORDS];

static int vic_mock_valid(uint32_t bank, uint32_t offset)
{
    return bank < HELIKON_VIC_BANK_COUNT && (offset & 3u) == 0u &&
           offset < 0x1000u;
}

uint32_t helikon_vic_test_read(uint32_t bank, uint32_t offset)
{
    if (!vic_mock_valid(bank, offset))
        return 0u;
    return vic_mock[bank][offset >> 2];
}

void helikon_vic_test_write(uint32_t bank, uint32_t offset, uint32_t value)
{
    if (!vic_mock_valid(bank, offset))
        return;
    vic_mock[bank][offset >> 2] = value;
}

void helikon_vic_test_reset(void)
{
    for (uint32_t bank = 0; bank < HELIKON_VIC_BANK_COUNT; ++bank) {
        for (uint32_t word = 0; word < VIC_MOCK_WORDS; ++word)
            vic_mock[bank][word] = 0u;
    }
}

void helikon_vic_test_set_pending(uint32_t irq, int pending)
{
    if (irq >= HELIKON_VIC_IRQ_COUNT)
        return;
    uint32_t bank = irq >> 5;
    uint32_t bit = 1u << (irq & 31u);
    uint32_t status = vic_mock[bank][HELIKON_VIC_REG_IRQ_STATUS >> 2];
    uint32_t soft = vic_mock[bank][HELIKON_VIC_REG_SOFT_INT >> 2];
    if (pending) {
        status |= bit;
        soft |= bit;
    } else {
        status &= ~bit;
        soft &= ~bit;
    }
    vic_mock[bank][HELIKON_VIC_REG_IRQ_STATUS >> 2] = status;
    vic_mock[bank][HELIKON_VIC_REG_SOFT_INT >> 2] = soft;
}

static uint32_t vic_read(uint32_t bank, uint32_t offset)
{
    if (!vic_mock_valid(bank, offset))
        return 0u;
    return vic_mock[bank][offset >> 2];
}

static void vic_write(uint32_t bank, uint32_t offset, uint32_t value)
{
    if (!vic_mock_valid(bank, offset))
        return;
    vic_mock[bank][offset >> 2] = value;
    /*
     * The mock reports the masked status and models the write-only clear
     * registers. Clearing a mask or a software source clears the matching
     * enable/soft-int bit and its status bit, mirroring the VIC.
     */
    if (offset == HELIKON_VIC_REG_INT_ENABLE_CLEAR) {
        vic_mock[bank][HELIKON_VIC_REG_INT_ENABLE >> 2] &= ~value;
        vic_mock[bank][HELIKON_VIC_REG_IRQ_STATUS >> 2] &= ~value;
    } else if (offset == HELIKON_VIC_REG_SOFT_INT_CLEAR) {
        vic_mock[bank][HELIKON_VIC_REG_SOFT_INT >> 2] &= ~value;
        vic_mock[bank][HELIKON_VIC_REG_IRQ_STATUS >> 2] &= ~value;
    }
}
#else
static uint32_t vic_read(uint32_t bank, uint32_t offset)
{
    return HELIKON_REG32(S5L_VIC_BASE + bank * S5L_VIC_BANK_STRIDE + offset);
}

static void vic_write(uint32_t bank, uint32_t offset, uint32_t value)
{
    HELIKON_REG32(S5L_VIC_BASE + bank * S5L_VIC_BANK_STRIDE + offset) = value;
}
#endif

static void vic_clear_state(void)
{
    vic_spurious = 0u;
    for (uint32_t i = 0; i < HELIKON_VIC_HANDLER_COUNT; ++i) {
        vic_handlers[i].handler = NULL;
        vic_handlers[i].context = NULL;
    }
}

static void vic_note_spurious(void)
{
    if (vic_spurious != 0xffffffffu)
        ++vic_spurious;
}

int helikon_vic_init(void)
{
    for (uint32_t bank = 0; bank < HELIKON_VIC_BANK_COUNT; ++bank) {
        vic_write(bank, HELIKON_VIC_REG_INT_ENABLE_CLEAR, 0xffffffffu);
        vic_write(bank, HELIKON_VIC_REG_SOFT_INT_CLEAR, 0xffffffffu);
        vic_write(bank, HELIKON_VIC_REG_INT_SELECT, 0u); /* IRQ, not FIQ */
        vic_write(bank, HELIKON_VIC_REG_ADDRESS, 0u);
    }
    vic_clear_state();
    return HELIKON_OK;
}

static int vic_line_valid(uint32_t irq)
{
    return irq < HELIKON_VIC_IRQ_COUNT;
}

static uint32_t vic_line_bit(uint32_t irq)
{
    return 1u << (irq & (HELIKON_VIC_IRQS_PER_BANK - 1u));
}

int helikon_vic_enable(uint32_t irq)
{
    if (!vic_line_valid(irq))
        return HELIKON_ERR_ARGUMENT;
    vic_write(irq >> 5, HELIKON_VIC_REG_INT_ENABLE, vic_line_bit(irq));
    return HELIKON_OK;
}

int helikon_vic_disable(uint32_t irq)
{
    if (!vic_line_valid(irq))
        return HELIKON_ERR_ARGUMENT;
    vic_write(irq >> 5, HELIKON_VIC_REG_INT_ENABLE_CLEAR, vic_line_bit(irq));
    return HELIKON_OK;
}

int helikon_vic_pending(uint32_t irq)
{
    if (!vic_line_valid(irq))
        return HELIKON_ERR_ARGUMENT;
    uint32_t status = vic_read(irq >> 5, HELIKON_VIC_REG_IRQ_STATUS);
    return (status & vic_line_bit(irq)) != 0u;
}

int helikon_vic_register(uint32_t irq, helikon_vic_handler handler,
                         void *context)
{
    if (irq >= HELIKON_VIC_HANDLER_COUNT || handler == NULL)
        return HELIKON_ERR_ARGUMENT;
    vic_handlers[irq].handler = handler;
    vic_handlers[irq].context = context;
    return HELIKON_OK;
}

int helikon_vic_unregister(uint32_t irq)
{
    if (irq >= HELIKON_VIC_HANDLER_COUNT)
        return HELIKON_ERR_ARGUMENT;
    vic_handlers[irq].handler = NULL;
    vic_handlers[irq].context = NULL;
    return HELIKON_OK;
}

int helikon_vic_software_trigger(uint32_t irq)
{
    if (!vic_line_valid(irq))
        return HELIKON_ERR_ARGUMENT;
    vic_write(irq >> 5, HELIKON_VIC_REG_SOFT_INT, vic_line_bit(irq));
    return HELIKON_OK;
}

int helikon_vic_software_clear(uint32_t irq)
{
    if (!vic_line_valid(irq))
        return HELIKON_ERR_ARGUMENT;
    vic_write(irq >> 5, HELIKON_VIC_REG_SOFT_INT_CLEAR, vic_line_bit(irq));
    return HELIKON_OK;
}

void helikon_vic_acknowledge(void)
{
    for (uint32_t bank = 0; bank < HELIKON_VIC_BANK_COUNT; ++bank)
        vic_write(bank, HELIKON_VIC_REG_ADDRESS, 0u);
}

static void vic_dispatch_bank(uint32_t bank, uint32_t status, uint32_t soft)
{
    for (uint32_t index = 0; index < HELIKON_VIC_IRQS_PER_BANK; ++index) {
        uint32_t bit = 1u << index;
        if ((status & bit) == 0u)
            continue;

        if (bank == 0u && vic_handlers[index].handler != NULL) {
            vic_handlers[index].handler(vic_handlers[index].context);
        } else {
            vic_note_spurious();
        }

        if ((soft & bit) != 0u)
            vic_write(bank, HELIKON_VIC_REG_SOFT_INT_CLEAR, bit);
    }
}

void helikon_vic_dispatch(void)
{
    /*
     * VICADDRESS is the EOI/vectored-address register; the donor reads the
     * masked status to identify the source. Read both address registers first
     * so a stale active address is visible even when the status is clear.
     */
    uint32_t address0 = vic_read(0u, HELIKON_VIC_REG_ADDRESS);
    uint32_t address1 = vic_read(1u, HELIKON_VIC_REG_ADDRESS);
    uint32_t soft0 = vic_read(0u, HELIKON_VIC_REG_SOFT_INT);
    uint32_t soft1 = vic_read(1u, HELIKON_VIC_REG_SOFT_INT);
    uint32_t status0 = vic_read(0u, HELIKON_VIC_REG_IRQ_STATUS);
    uint32_t status1 = vic_read(1u, HELIKON_VIC_REG_IRQ_STATUS);

    vic_dispatch_bank(0u, status0, soft0);
    vic_dispatch_bank(1u, status1, soft1);

    if (status0 == 0u && address0 != 0u)
        vic_note_spurious();
    if (status1 == 0u && address1 != 0u)
        vic_note_spurious();

    helikon_vic_acknowledge();
}

uint32_t helikon_vic_spurious_count(void)
{
    return vic_spurious;
}
