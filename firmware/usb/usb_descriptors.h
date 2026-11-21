/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * USB descriptor set for Helikon Disk Mode (D-54, ADR-0005, T-110).
 *
 * One configuration, one interface, two bulk endpoints: this is a mass-storage
 * device and nothing else. The descriptors are built into caller buffers so the
 * module needs no global state and can be exercised on the host.
 *
 * Identifiers: `HELIKON_USB_VID` is the pid.codes open-source vendor ID
 * (0x1209). The product ID is a placeholder until the owner registers one; two
 * projects must not ship the same 0x1209 PID. See docs/disk-mode.md §7.2 and
 * D-56 in docs/decisions.md before claiming an identity.
 */
#ifndef HELIKON_USB_USB_DESCRIPTORS_H
#define HELIKON_USB_USB_DESCRIPTORS_H

#include <stddef.h>
#include <stdint.h>

#define HELIKON_USB_VID 0x1209u
#define HELIKON_USB_PID 0x0001u

/* Bulk endpoint addresses: IN is host-readable (from the device). */
#define HELIKON_USB_EP_BULK_IN 0x81u
#define HELIKON_USB_EP_BULK_OUT 0x02u

/* bcdUSB 2.00, bcdDevice 0.01: pre-release firmware. */
#define HELIKON_USB_BCD_USB 0x0200u
#define HELIKON_USB_BCD_DEVICE 0x0001u

#define HELIKON_USB_MAX_PACKET_EP0 64u
#define HELIKON_USB_MAX_PACKET_BULK 512u

/* String descriptor indices, as referenced by the device descriptor. */
#define HELIKON_USB_STRING_MANUFACTURER 1u
#define HELIKON_USB_STRING_PRODUCT 2u
#define HELIKON_USB_STRING_SERIAL 3u
#define HELIKON_USB_STRING_CONFIGURATION 4u
#define HELIKON_USB_STRING_INTERFACE 5u

struct helikon_usb_identity {
    const char *manufacturer;
    const char *product;
    const char *serial;
};

/* Full-size descriptor lengths, so a caller can size its buffer first. */
#define HELIKON_USB_DEVICE_DESCRIPTOR_SIZE 18u
#define HELIKON_USB_CONFIG_DESCRIPTOR_SIZE 32u
/*
 * A string descriptor is 2 + 2 * characters, capped at 255 bytes by the bLength
 * field. A 63-character string is the largest that fits.
 */
#define HELIKON_USB_STRING_MAX_CHARS 63u
#define HELIKON_USB_STRING_DESCRIPTOR_MAX                                                 \
    (2u + 2u * HELIKON_USB_STRING_MAX_CHARS)

/*
 * Builders return the number of bytes written, or 0 when the buffer is too
 * small. They never write past `capacity`.
 */
size_t helikon_usb_descriptor_device(uint8_t *buffer, size_t capacity);
size_t helikon_usb_descriptor_config(uint8_t *buffer, size_t capacity);

/*
 * String descriptor `index`. Index 0 is the language list (0x0409, US English)
 * and ignores `identity`. An unknown index or a NULL text returns 0.
 */
size_t helikon_usb_descriptor_string(uint8_t index,
                                     const struct helikon_usb_identity *identity,
                                     uint8_t *buffer, size_t capacity);

#endif
