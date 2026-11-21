/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The transport-to-Disk-Mode event vocabulary.
 *
 * The controller driver produces these; `disk_mode.c` consumes them. Keeping
 * the enum out of the half-specific header lets the mode module be built and
 * tested on the host without a controller, and lets another transport (a future
 * service-mode endpoint, or a test harness) speak the same words.
 */
#ifndef HELIKON_USB_USB_EVENTS_H
#define HELIKON_USB_USB_EVENTS_H

#include <stdint.h>

#define HELIKON_USB_SETUP_LENGTH 8u

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
    /* Bulk OUT (EP2) transfer finished; the byte count is reported. */
    HELIKON_USB_EVENT_EP2_OUT_DONE,
    /* Bus reset: address 0, endpoints disabled. */
    HELIKON_USB_EVENT_BUS_RESET,
    /* Cable removed: the host no longer owns the LUN. */
    HELIKON_USB_EVENT_DISCONNECT,
    /* An internal bound was hit (FIFO stuck, oversize OUT, queue full). */
    HELIKON_USB_EVENT_ERROR
};

#endif
