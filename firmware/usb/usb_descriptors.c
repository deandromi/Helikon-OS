/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "usb_descriptors.h"

#include <stddef.h>
#include <stdint.h>

/* Descriptor types (USB 2.0 §9.6). */
#define USB_DT_DEVICE 0x01u
#define USB_DT_CONFIG 0x02u
#define USB_DT_STRING 0x03u
#define USB_DT_INTERFACE 0x04u
#define USB_DT_ENDPOINT 0x05u

/* Class codes for mass storage, SCSI transparent command set, bulk-only. */
#define USB_CLASS_MASS_STORAGE 0x08u
#define USB_MSC_SUBCLASS_SCSI 0x06u
#define USB_MSC_PROTOCOL_BOT 0x50u

#define USB_EP_ATTR_BULK 0x02u

#define USB_CONFIG_BMATTRIBUTES_BUS_POWERED 0x80u
#define USB_CONFIG_BMAXPOWER_100MA 50u /* 100 mA, in 2 mA units */

static void put16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)(value >> 8);
}

size_t helikon_usb_descriptor_device(uint8_t *buffer, size_t capacity)
{
    if (buffer == NULL || capacity < HELIKON_USB_DEVICE_DESCRIPTOR_SIZE)
        return 0u;

    uint8_t *d = buffer;
    d[0] = HELIKON_USB_DEVICE_DESCRIPTOR_SIZE;
    d[1] = USB_DT_DEVICE;
    put16(d + 2, HELIKON_USB_BCD_USB);
    /* Class is defined per interface, not per device. */
    d[4] = 0u;
    d[5] = 0u;
    d[6] = 0u;
    d[7] = HELIKON_USB_MAX_PACKET_EP0;
    put16(d + 8, HELIKON_USB_VID);
    put16(d + 10, HELIKON_USB_PID);
    put16(d + 12, HELIKON_USB_BCD_DEVICE);
    d[14] = HELIKON_USB_STRING_MANUFACTURER;
    d[15] = HELIKON_USB_STRING_PRODUCT;
    d[16] = HELIKON_USB_STRING_SERIAL;
    d[17] = 1u; /* one configuration */
    return HELIKON_USB_DEVICE_DESCRIPTOR_SIZE;
}

size_t helikon_usb_descriptor_config(uint8_t *buffer, size_t capacity)
{
    if (buffer == NULL || capacity < HELIKON_USB_CONFIG_DESCRIPTOR_SIZE)
        return 0u;

    uint8_t *d = buffer;
    /* Configuration. */
    d[0] = 9u;
    d[1] = USB_DT_CONFIG;
    put16(d + 2, HELIKON_USB_CONFIG_DESCRIPTOR_SIZE);
    d[4] = 1u; /* interfaces */
    d[5] = 1u; /* configuration value */
    d[6] = HELIKON_USB_STRING_CONFIGURATION;
    d[7] = USB_CONFIG_BMATTRIBUTES_BUS_POWERED;
    d[8] = USB_CONFIG_BMAXPOWER_100MA;

    /* Interface 0: mass storage, SCSI transparent, bulk-only. */
    d[9] = 9u;
    d[10] = USB_DT_INTERFACE;
    d[11] = 0u;
    d[12] = 0u;
    d[13] = 2u; /* two endpoints */
    d[14] = USB_CLASS_MASS_STORAGE;
    d[15] = USB_MSC_SUBCLASS_SCSI;
    d[16] = USB_MSC_PROTOCOL_BOT;
    d[17] = HELIKON_USB_STRING_INTERFACE;

    /* Bulk IN. */
    d[18] = 7u;
    d[19] = USB_DT_ENDPOINT;
    d[20] = HELIKON_USB_EP_BULK_IN;
    d[21] = USB_EP_ATTR_BULK;
    put16(d + 22, HELIKON_USB_MAX_PACKET_BULK);
    d[24] = 0u; /* no interval for bulk */

    /* Bulk OUT. */
    d[25] = 7u;
    d[26] = USB_DT_ENDPOINT;
    d[27] = HELIKON_USB_EP_BULK_OUT;
    d[28] = USB_EP_ATTR_BULK;
    put16(d + 29, HELIKON_USB_MAX_PACKET_BULK);
    d[31] = 0u;

    return HELIKON_USB_CONFIG_DESCRIPTOR_SIZE;
}

/*
 * UTF-8 to UTF-16LE, the USB string encoding. Only ASCII is accepted: the
 * identity strings are product text under our control, and a silent '?' for
 * every non-ASCII byte would hide a mistake in the caller. A rejected character
 * makes the whole descriptor fail.
 */
static size_t string_descriptor(const char *text, uint8_t *buffer,
                                size_t capacity)
{
    if (text == NULL)
        return 0u;

    size_t characters = 0u;
    while (text[characters] != '\0') {
        uint8_t byte = (uint8_t)text[characters];
        if (byte < 0x20u || byte > 0x7eu)
            return 0u;
        characters += 1u;
        if (characters > HELIKON_USB_STRING_MAX_CHARS)
            return 0u;
    }

    size_t length = 2u + 2u * characters;
    if (buffer == NULL || capacity < length)
        return 0u;

    buffer[0] = (uint8_t)length;
    buffer[1] = USB_DT_STRING;
    for (size_t index = 0u; index < characters; ++index) {
        buffer[2u + 2u * index] = (uint8_t)text[index];
        buffer[2u + 2u * index + 1u] = 0u;
    }
    return length;
}

size_t helikon_usb_descriptor_string(uint8_t index,
                                     const struct helikon_usb_identity *identity,
                                     uint8_t *buffer, size_t capacity)
{
    if (index == 0u) {
        /* Language list: one entry, 0x0409 (US English). */
        if (buffer == NULL || capacity < 4u)
            return 0u;
        buffer[0] = 4u;
        buffer[1] = USB_DT_STRING;
        put16(buffer + 2, 0x0409u);
        return 4u;
    }

    if (identity == NULL)
        return 0u;

    switch (index) {
    case HELIKON_USB_STRING_MANUFACTURER:
        return string_descriptor(identity->manufacturer, buffer, capacity);
    case HELIKON_USB_STRING_PRODUCT:
        return string_descriptor(identity->product, buffer, capacity);
    case HELIKON_USB_STRING_SERIAL:
        return string_descriptor(identity->serial, buffer, capacity);
    case HELIKON_USB_STRING_CONFIGURATION:
        return string_descriptor("Disk Mode", buffer, capacity);
    case HELIKON_USB_STRING_INTERFACE:
        return string_descriptor("Helikon volume", buffer, capacity);
    default:
        return 0u;
    }
}
