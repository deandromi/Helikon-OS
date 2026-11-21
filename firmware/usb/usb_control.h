/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * USB chapter 9 control requests for Helikon Disk Mode.
 *
 * The transport (the S5L8702 driver) owns the endpoints and hands one SETUP
 * packet at a time to this module; the module answers with an action the
 * transport performs: send `length` bytes from `data`, receive `length` bytes,
 * acknowledge with no data, or STALL. Every branch is a pure decision over the
 * request and the device state, so the whole enumeration path is host-testable
 * without a controller.
 *
 * Only what a bulk-only mass-storage device needs is implemented. A request this
 * module does not know is stalled, which is the specified answer for an
 * unsupported request, not a guess.
 */
#ifndef HELIKON_USB_USB_CONTROL_H
#define HELIKON_USB_USB_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include "usb_descriptors.h"

struct helikon_usb_setup {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
};

enum helikon_usb_control_action {
    HELIKON_USB_CONTROL_STALL = 0,
    HELIKON_USB_CONTROL_IN = 1,
    HELIKON_USB_CONTROL_OUT = 2,
    HELIKON_USB_CONTROL_NO_DATA = 3
};

struct helikon_usb_control {
    struct helikon_usb_identity identity;
    uint8_t address;
    uint8_t configuration; /* 0 = unconfigured */
    uint8_t self_powered;
    uint8_t remote_wakeup;
    /* One bit per endpoint number: the halt feature. */
    uint16_t halted;
    /* Set by BULK_ONLY_RESET; the transport clears the MSC state. */
    int bulk_only_reset;
    /* IN data lives here, so it stays valid until the next request. */
    uint8_t scratch[HELIKON_USB_STRING_DESCRIPTOR_MAX];
};

struct helikon_usb_control_result {
    enum helikon_usb_control_action action;
    const uint8_t *data;
    uint16_t length;
};

/* Standard request codes (USB 2.0 §9.4). */
#define HELIKON_USB_REQ_GET_STATUS 0x00u
#define HELIKON_USB_REQ_CLEAR_FEATURE 0x01u
#define HELIKON_USB_REQ_SET_FEATURE 0x03u
#define HELIKON_USB_REQ_SET_ADDRESS 0x05u
#define HELIKON_USB_REQ_GET_DESCRIPTOR 0x06u
#define HELIKON_USB_REQ_SET_DESCRIPTOR 0x07u
#define HELIKON_USB_REQ_GET_CONFIGURATION 0x08u
#define HELIKON_USB_REQ_SET_CONFIGURATION 0x09u
#define HELIKON_USB_REQ_GET_INTERFACE 0x0au
#define HELIKON_USB_REQ_SET_INTERFACE 0x0bu

/* Mass-storage class requests (USB MSC 1.0 §3.2). */
#define HELIKON_USB_MSC_GET_MAX_LUN 0xfeu
#define HELIKON_USB_MSC_BULK_ONLY_RESET 0xffu

#define HELIKON_USB_DESCRIPTOR_DEVICE 0x01u
#define HELIKON_USB_DESCRIPTOR_CONFIG 0x02u
#define HELIKON_USB_DESCRIPTOR_STRING 0x03u

/* Reset to the state the bus sees after a reset: address 0, unconfigured. */
void helikon_usb_control_reset(struct helikon_usb_control *control,
                               const struct helikon_usb_identity *identity);

/* Handles one SETUP packet. `*result` is always written. */
void helikon_usb_control_setup(struct helikon_usb_control *control,
                               const struct helikon_usb_setup *setup,
                               struct helikon_usb_control_result *result);

/*
 * Data stage for an OUT request. Only SET_DESCRIPTOR would use it, and this
 * device has none, so the transport never has data to deliver; the function
 * exists so a transport can report a protocol error instead of guessing.
 */
int helikon_usb_control_out(struct helikon_usb_control *control,
                            const void *data, uint16_t length);

#endif
