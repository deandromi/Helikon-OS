/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Disk Mode in a bootloader image (DFU-only diagnostic).
 *
 * This is not the production bootloader. It is a separate image whose only job
 * is to give a laptop the raw disk as a writable USB LUN, so a 6G whose firmware
 * cannot boot (or whose volume is empty) can still be formatted — including a
 * volume above 128 GiB. The loader images stay read-only; this one is reachable
 * only by flashing it through DFU, and it never loads firmware.
 *
 * It reuses the same code as the firmware's Disk Mode image:
 *   firmware/hal/6g/usb.c        the DesignWare driver, over a seam built here
 *   firmware/usb/usb_*.c         descriptors, control requests, BOT and SCSI
 *   firmware/usb/disk_mode.c     the gate, the window and the phase machine
 *
 * The seam is deliberate: `usb.c` knows nothing about Rockbox, so the same
 * driver is host-tested against a mock and linked here against the donor's
 * hardware primitives (`ata_*`, `udelay`, `PWRCON`, `button_get`). The USB
 * interrupt is polled from the main loop instead of wired into the donor's VIC,
 * which keeps this image out of the donor's interrupt model entirely.
 *
 * NOTHING IN THIS IMAGE HAS RUN ON HARDWARE.
 */
#include "config.h"

#include <stdint.h>
#include <string.h>

#include "ata.h"
#include "button.h"
#include "lcd.h"
#include "power.h"
#include "storage.h"
#include "system.h"

#include "block.h"
#include "disk_mode.h"
#include "helikon_error.h"
#include "usb.h"
#include "usb_hw.h"

/* The window is the whole disk: this image exists to format it. */
#define HELIKON_USB_DISK_TIMEOUT_MS 5000u
#define HELIKON_USB_DISK_CONFIRM_TICKS 300u

static struct helikon_block_device usb_disk_block;
static struct helikon_disk_mode usb_disk_mode;

/* --- block device over the donor ATA path -------------------------------- */

static int usb_disk_read(void *context, uint64_t lba, uint32_t count,
                         void *buffer, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    if (count == 0u)
        return HELIKON_ERR_ARGUMENT;
    /* The donor call takes a sector count in `int`; a single LUN command never
     * asks for more than 128 sectors, so the cast is bounded by the caller. */
    return ata_read_sectors(IF_MD(0,) (sector_t)lba, (int)count, buffer) == 0
               ? HELIKON_OK
               : HELIKON_ERR_IO;
}

static int usb_disk_write(void *context, uint64_t lba, uint32_t count,
                          const void *buffer, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    if (count == 0u)
        return HELIKON_ERR_ARGUMENT;
    return ata_write_sectors(IF_MD(0,) (sector_t)lba, (int)count, buffer) == 0
               ? HELIKON_OK
               : HELIKON_ERR_IO;
}

static int usb_disk_flush(void *context, uint32_t timeout_ms)
{
    (void)context;
    (void)timeout_ms;
    return ata_flush() == 0 ? HELIKON_OK : HELIKON_ERR_IO;
}

static int usb_disk_power(void *context, int on, uint32_t timeout_ms)
{
    (void)context;
    (void)on;
    (void)timeout_ms;
    return HELIKON_OK;
}

static const struct helikon_block_ops usb_disk_block_ops = {
    .read = usb_disk_read,
    .write = usb_disk_write,
    .flush = usb_disk_flush,
    .power = usb_disk_power
};

/* --- the controller seam ------------------------------------------------- */

static uint32_t seam_read(void *context, uint32_t address)
{
    (void)context;
    return *(volatile uint32_t *)(uintptr_t)address;
}

static void seam_write(void *context, uint32_t address, uint32_t value)
{
    (void)context;
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static void seam_fifo_write(void *context, uint32_t address,
                            const void *buffer, uint32_t length)
{
    (void)context;
    const uint8_t *bytes = buffer;
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)address;
    for (uint32_t offset = 0u; offset < length; offset += 4u) {
        uint32_t word = 0u;
        for (uint32_t index = 0u; index < 4u && offset + index < length;
             ++index)
            word |= (uint32_t)bytes[offset + index] << (8u * index);
        *fifo = word;
    }
}

static void seam_fifo_read(void *context, uint32_t address, void *buffer,
                           uint32_t length)
{
    (void)context;
    uint8_t *bytes = buffer;
    volatile uint32_t *fifo = (volatile uint32_t *)(uintptr_t)address;
    for (uint32_t offset = 0u; offset < length; offset += 4u) {
        uint32_t word = *fifo;
        for (uint32_t index = 0u; index < 4u && offset + index < length;
             ++index)
            bytes[offset + index] = (uint8_t)(word >> (8u * index));
    }
}

static void seam_delay(void *context, uint32_t microseconds)
{
    (void)context;
    /* The donor's microsecond delay is calibrated for this target; the PHY
     * sequence's 10/400/3000 us bounds ride on it. */
    udelay(microseconds);
}

static void seam_clock(void *context, unsigned int gate)
{
    (void)context;
    /* PWRCON bits are power-*down* gates: enabling clears the bit. The donor's
     * clockgate_enable() does the same, and setting the bit here would gate the
     * OTG and PHY clocks off before the core is touched. */
    uint32_t bank = gate >> 5;
    uint32_t bit = 1u << (gate & 0x1fu);
    PWRCON(bank) = PWRCON(bank) & ~bit;
}

static void seam_irq(void *context, int enable)
{
    /* Polled: `helikon_usb_irq()` is called from the loop below, so the donor's
     * VIC and its interrupt model are not touched at all. */
    (void)context;
    (void)enable;
}

static struct helikon_usb_hw usb_disk_hw(void)
{
    struct helikon_usb_hw hw;
    hw.context = NULL;
    hw.read = seam_read;
    hw.write = seam_write;
    hw.fifo_read = seam_fifo_read;
    hw.fifo_write = seam_fifo_write;
    hw.delay_us = seam_delay;
    hw.clock_enable = seam_clock;
    hw.irq_enable = seam_irq;
    return hw;
}

/* --- transport ops for the mode machine ---------------------------------- */

static int mode_ep0_send(void *context, const void *data, uint32_t length)
{
    (void)context;
    return helikon_usb_ep0_send(data, length);
}

static int mode_ep0_receive(void *context, void *data, uint32_t length)
{
    (void)context;
    return helikon_usb_ep0_receive(data, length);
}

static int mode_ep0_stall(void *context)
{
    (void)context;
    return helikon_usb_ep0_stall();
}

static int mode_ep1_send(void *context, const void *data, uint32_t length)
{
    (void)context;
    return helikon_usb_ep1_send(data, length);
}

static int mode_ep2_receive(void *context, void *data, uint32_t length)
{
    (void)context;
    return helikon_usb_ep2_receive(data, length);
}

static int mode_set_address(void *context, uint8_t address)
{
    (void)context;
    return helikon_usb_set_address(address);
}

static int mode_configure(void *context, int configured)
{
    (void)context;
    return helikon_usb_configure(configured);
}

/* --- screen --------------------------------------------------------------- */

static void usb_disk_screen(const char *line, const char *state)
{
    lcd_set_foreground(LCD_WHITE);
    lcd_set_background(LCD_BLACK);
    lcd_clear_display();
    lcd_puts(0, 0, "HELIKON DISK MODE (BOOTLOADER)");
    lcd_puts(0, 2, line);
    if (state != NULL)
        lcd_puts(0, 3, state);
    lcd_update();
}

/* SELECT hands the disk over; MENU or 30 s without input powers off. */
static int usb_disk_confirm(void)
{
    usb_disk_screen("SELECT: HAND OVER DISK", "MENU: POWER OFF");
    lcd_puts(0, 5, "THE COMPUTER MAY ERASE IT");
    lcd_update();

    for (unsigned int tick = 0u; tick < HELIKON_USB_DISK_CONFIRM_TICKS;
         ++tick) {
        int button = button_get(false);
        if (button & BUTTON_SELECT)
            return 1;
        if (button & BUTTON_MENU)
            return 0;
        sleep(HZ / 10);
    }
    return 0;
}

/* --- entry ---------------------------------------------------------------- */

/*
 * Noreturn on purpose: every path ends in power_off(). Marking it lets the
 * linker drop the donor's filesystem and firmware-loading code behind this
 * call, so this image cannot mount a volume even if a future edit returns
 * early.
 */
void helikon_usb_disk_main(void) __attribute__((noreturn));

void helikon_usb_disk_main(void)
{
    struct storage_info info;
    memset(&info, 0, sizeof(info));
    /* The donor returns void here and fills the record; a zeroed record is what
     * a failed ATA initialisation leaves behind. */
    storage_get_info(0, &info);
    if (info.num_sectors == 0u || info.sector_size < 512u) {
        usb_disk_screen("NO DISK", NULL);
        sleep(HZ);
        power_off();
        return;
    }

    struct helikon_block_info block_info = {
        .sector_size = info.sector_size,
        .sector_count = (uint64_t)info.num_sectors,
        .flags = 0u /* writable: this image exists to format the disk */
    };
    if (helikon_block_init(&usb_disk_block, &usb_disk_block_ops, NULL,
                           &block_info) != HELIKON_OK) {
        usb_disk_screen("BLOCK DEVICE REFUSED", NULL);
        sleep(HZ);
        power_off();
        return;
    }

    /* No cache maintenance here: the donor's ATA path owns it for its DMA
     * transfers, and this image only moves data through those calls. */

    if (!usb_disk_confirm()) {
        power_off();
        return;
    }

    struct helikon_usb_hw hw = usb_disk_hw();
    int rc = helikon_usb_init(&hw);
    if (rc != HELIKON_OK) {
        usb_disk_screen("USB CORE FAILED", NULL);
        sleep(HZ);
        power_off();
        return;
    }

    struct helikon_usb_device_ops ops = {
        .context = NULL,
        .ep0_send = mode_ep0_send,
        .ep0_receive = mode_ep0_receive,
        .ep0_stall = mode_ep0_stall,
        .ep1_send = mode_ep1_send,
        .ep2_receive = mode_ep2_receive,
        .set_address = mode_set_address,
        .configure = mode_configure
    };
    struct helikon_usb_identity identity = {
        .manufacturer = "Helikon",
        .product = "Helikon Disk Mode",
        .serial = NULL
    };
    struct helikon_disk_mode_options options = {
        .opt_in = 1,
        .restore_mode = 1,
        .timeout_ms = HELIKON_USB_DISK_TIMEOUT_MS
    };

    rc = helikon_disk_mode_start(&usb_disk_mode, &ops, &usb_disk_block,
                                 &identity, &options);
    if (rc != HELIKON_OK) {
        usb_disk_screen("DISK MODE REFUSED", NULL);
        helikon_usb_shutdown();
        sleep(HZ);
        power_off();
        return;
    }

    usb_disk_screen("CONNECT USB", "waiting for host");

    unsigned int shown = 0u;
    for (;;) {
        (void)helikon_usb_irq(); /* polled: no VIC involvement */
        (void)helikon_usb_pump();

        enum helikon_usb_event event;
        uint8_t setup[HELIKON_USB_SETUP_LENGTH];
        unsigned int budget = 8u;
        while (budget-- > 0u && helikon_usb_pop(&event, setup) == 1) {
            uint32_t length = 0u;
            if (event == HELIKON_USB_EVENT_EP2_OUT_DONE)
                (void)helikon_usb_out_received(2u, &length);
            (void)helikon_disk_mode_event(&usb_disk_mode, event, setup, length);
        }

        unsigned int state = (unsigned int)usb_disk_mode.state;
        if (state != shown) {
            shown = state;
            if (usb_disk_mode.state == HELIKON_DISK_MODE_ENUMERATED)
                usb_disk_screen("HOST CONNECTED", "read/write");
            else if (usb_disk_mode.state == HELIKON_DISK_MODE_RELEASED)
                usb_disk_screen("EJECTED - SAFE", NULL);
            else
                usb_disk_screen("CONNECT USB", "waiting for host");
        }
        if (usb_disk_mode.state == HELIKON_DISK_MODE_RELEASED ||
            usb_disk_mode.state == HELIKON_DISK_MODE_ERROR)
            break;

        int button = button_get(false);
        if (button & BUTTON_MENU)
            break;

        sleep(1);
    }

    helikon_disk_mode_stop(&usb_disk_mode);
    helikon_usb_shutdown();
    usb_disk_screen("POWER OFF", NULL);
    sleep(HZ / 2);
    power_off();
}
