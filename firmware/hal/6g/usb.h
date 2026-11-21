/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * S5L8702 USB device controller, stack-facing interface.
 *
 * The layer below `firmware/usb/` (descriptors, control requests, MSC). It
 * knows packets, endpoints and events, nothing about SCSI.
 *
 * Usage:
 *   helikon_usb_init(&helikon_usb_hw_s5l8702(...));   once, from the mode entry
 *   loop:  helikon_usb_irq()   from the VIC handler for IRQ 19
 *          helikon_usb_pump()  from the main loop
 *          helikon_usb_pop(&event, setup)
 *
 * One transfer per endpoint at a time, one producer (the IRQ) and one consumer
 * (the main loop). No allocation, no threads, no nested interrupts.
 *
 * NOTHING HERE HAS RUN ON A 6G. See `usb_hw.h` for the evidence status of every
 * register value.
 */
#ifndef HELIKON_HAL_6G_USB_H
#define HELIKON_HAL_6G_USB_H

#include <stdint.h>

#include "usb_hw.h"

/* Event queue depth. One SETUP plus a few completions is the working set. */
#define HELIKON_USB_QUEUE_DEPTH 16u

enum helikon_usb_event {
    HELIKON_USB_EVENT_NONE = 0,
    /* A SETUP packet arrived; the 8 bytes are in the `setup` output. */
    HELIKON_USB_EVENT_SETUP,
    /* EP0 IN data stage finished (or the zero-length status went out). */
    HELIKON_USB_EVENT_EP0_IN_DONE,
    /* EP0 OUT data stage finished. */
    HELIKON_USB_EVENT_EP0_OUT_DONE,
    /* Bulk IN (EP1) transfer finished. */
    HELIKON_USB_EVENT_EP1_IN_DONE,
    /* Bulk OUT (EP2) transfer finished. */
    HELIKON_USB_EVENT_EP2_OUT_DONE,
    /* Bus reset: address 0, endpoints disabled. */
    HELIKON_USB_EVENT_BUS_RESET,
    /* Cable removed (or VBUS gone): the host no longer owns the LUN. */
    HELIKON_USB_EVENT_DISCONNECT,
    /* An internal bound was hit (FIFO stuck, oversize OUT, queue full). */
    HELIKON_USB_EVENT_ERROR
};

/* Runs the PHY, clock and core sequence and connects. Returns HELIKON_OK,
 * HELIKON_ERR_ARGUMENT for an incomplete seam, HELIKON_ERR_IO if the core never
 * acknowledges a reset. */
int helikon_usb_init(const struct helikon_usb_hw *hw);

/* Disconnects, masks everything and powers the core down. Safe when not
 * initialised. */
void helikon_usb_shutdown(void);

/* VIC IRQ 19 body. Returns the number of status groups handled; a call with
 * nothing pending counts a spurious interrupt and returns 0 (never panics). */
int helikon_usb_irq(void);

/* Main-loop service: continues an IN transfer when the host drained the FIFO. */
int helikon_usb_pump(void);

/* Pops one event. Returns 1 when an event was written, 0 when the queue is
 * empty. `setup` may be NULL; it receives the 8 SETUP bytes for a SETUP event. */
int helikon_usb_pop(enum helikon_usb_event *event, uint8_t setup[8]);

/* Applies a SET_ADDRESS. The core wants the address before the status stage, so
 * the stack must call this before answering the request. 0..127. */
int helikon_usb_set_address(uint8_t address);

/* Configures (1) or releases (0) EP1 IN and EP2 OUT at the negotiated speed. */
int helikon_usb_configure(int configured);

uint8_t helikon_usb_address(void);
int helikon_usb_high_speed(void);
int helikon_usb_is_configured(void);

/* Starts an EP0 IN data stage. `length` 0 sends the zero-length status. */
int helikon_usb_ep0_send(const void *data, uint32_t length);
/* Prepares an EP0 OUT data stage; completion is EP0_OUT_DONE. */
int helikon_usb_ep0_receive(void *buffer, uint32_t length);
/* Stalls both directions of EP0 (an unsupported request). */
int helikon_usb_ep0_stall(void);

/* Bulk EP1 IN and EP2 OUT. Both require a configured device. */
int helikon_usb_ep1_send(const void *data, uint32_t length);
int helikon_usb_ep2_receive(void *buffer, uint32_t length);

/* Stalls one endpoint direction; the host clears it with CLEAR_FEATURE. */
void helikon_usb_ep_stall(uint32_t ep, int in);

/* Bytes the current OUT buffer holds, for the main loop after an OUT event. */
int helikon_usb_out_received(uint32_t ep, uint32_t *length);

/* Diagnostics, for the Disk Mode screen and the host tests. */
uint32_t helikon_usb_irq_count(void);
uint32_t helikon_usb_spurious_count(void);
uint32_t helikon_usb_error_count(void);

#endif
