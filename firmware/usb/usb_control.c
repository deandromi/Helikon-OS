/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "usb_control.h"

#include <stddef.h>
#include <stdint.h>

#include "error.h"

#define USB_REQUEST_TYPE_DIR_IN 0x80u
#define USB_REQUEST_TYPE_TYPE_MASK 0x60u
#define USB_REQUEST_TYPE_TYPE_STANDARD 0x00u
#define USB_REQUEST_TYPE_TYPE_CLASS 0x20u
#define USB_REQUEST_TYPE_RECIPIENT_MASK 0x1fu
#define USB_REQUEST_TYPE_RECIPIENT_DEVICE 0x00u
#define USB_REQUEST_TYPE_RECIPIENT_INTERFACE 0x01u
#define USB_REQUEST_TYPE_RECIPIENT_ENDPOINT 0x02u

#define USB_FEATURE_ENDPOINT_HALT 0x00u
#define USB_FEATURE_DEVICE_REMOTE_WAKEUP 0x01u

/* One LUN, so GET_MAX_LUN reports 0. */
#define HELIKON_USB_MAX_LUN 0u

static void stall(struct helikon_usb_control_result *result)
{
    result->action = HELIKON_USB_CONTROL_STALL;
    result->data = NULL;
    result->length = 0u;
}

static void no_data(struct helikon_usb_control_result *result)
{
    result->action = HELIKON_USB_CONTROL_NO_DATA;
    result->data = NULL;
    result->length = 0u;
}

static void data_in(struct helikon_usb_control_result *result,
                    const uint8_t *data, size_t length, uint16_t want)
{
    if (length > (size_t)want)
        length = (size_t)want;
    result->action = HELIKON_USB_CONTROL_IN;
    result->data = data;
    result->length = (uint16_t)length;
}

void helikon_usb_control_reset(struct helikon_usb_control *control,
                               const struct helikon_usb_identity *identity)
{
    if (control == NULL)
        return;

    control->address = 0u;
    control->configuration = 0u;
    control->self_powered = 0u;
    control->remote_wakeup = 0u;
    control->halted = 0u;
    control->bulk_only_reset = 0;
    if (identity != NULL)
        control->identity = *identity;
}

/* Endpoint number 0..6; the halt feature is tracked per number. */
static int endpoint_halted(const struct helikon_usb_control *control,
                           uint16_t index)
{
    uint8_t number = (uint8_t)(index & 0x0fu);
    if (number > 6u)
        return 0;
    return (control->halted & (uint16_t)(1u << number)) != 0u;
}

static void set_endpoint_halted(struct helikon_usb_control *control,
                                uint16_t index, int halted)
{
    uint8_t number = (uint8_t)(index & 0x0fu);
    if (number == 0u || number > 6u)
        return; /* the default control pipe is never halted by a request */
    if (halted)
        control->halted |= (uint16_t)(1u << number);
    else
        control->halted &= (uint16_t)~(uint16_t)(1u << number);
}

static void get_descriptor(struct helikon_usb_control *control,
                           const struct helikon_usb_setup *setup,
                           struct helikon_usb_control_result *result)
{
    uint8_t type = (uint8_t)(setup->value >> 8);
    uint8_t index = (uint8_t)(setup->value & 0xffu);

    switch (type) {
    case HELIKON_USB_DESCRIPTOR_DEVICE: {
        uint8_t *buffer = control->scratch;
        size_t length =
            helikon_usb_descriptor_device(buffer, sizeof(control->scratch));
        if (length == 0u)
            break;
        data_in(result, buffer, length, setup->length);
        return;
    }
    case HELIKON_USB_DESCRIPTOR_CONFIG: {
        uint8_t *buffer = control->scratch;
        size_t length =
            helikon_usb_descriptor_config(buffer, sizeof(control->scratch));
        if (length == 0u)
            break;
        data_in(result, buffer, length, setup->length);
        return;
    }
    case HELIKON_USB_DESCRIPTOR_STRING: {
        uint8_t *buffer = control->scratch;
        size_t length = helikon_usb_descriptor_string(
            index, &control->identity, buffer, sizeof(control->scratch));
        if (length == 0u)
            break;
        data_in(result, buffer, length, setup->length);
        return;
    }
    default:
        /* Device qualifier, other-speed config, BOS: not advertised. */
        break;
    }
    stall(result);
}

static void setup_device_standard(struct helikon_usb_control *control,
                                  const struct helikon_usb_setup *setup,
                                  int in,
                                  struct helikon_usb_control_result *result)
{
    if (in) {
        switch (setup->request) {
        case HELIKON_USB_REQ_GET_STATUS: {
            uint8_t *buffer = control->scratch;
            buffer[0] = (uint8_t)(control->self_powered != 0u ? 0x01u : 0x00u);
            buffer[1] = 0u;
            if (control->remote_wakeup != 0u)
                buffer[0] |= 0x02u;
            data_in(result, buffer, 2u, setup->length);
            return;
        }
        case HELIKON_USB_REQ_GET_DESCRIPTOR:
            get_descriptor(control, setup, result);
            return;
        case HELIKON_USB_REQ_GET_CONFIGURATION: {
            uint8_t *buffer = control->scratch;
            buffer[0] = control->configuration;
            data_in(result, buffer, 1u, setup->length);
            return;
        }
        case HELIKON_USB_REQ_GET_INTERFACE: {
            /* One interface, one alternate setting. */
            uint8_t *buffer = control->scratch;
            buffer[0] = 0u;
            data_in(result, buffer, 1u, setup->length);
            return;
        }
        default:
            stall(result);
            return;
        }
    }

    switch (setup->request) {
    case HELIKON_USB_REQ_SET_ADDRESS:
        if (setup->value > 127u) {
            stall(result);
            return;
        }
        control->address = (uint8_t)setup->value;
        no_data(result);
        return;
    case HELIKON_USB_REQ_SET_CONFIGURATION:
        if (setup->value > 1u) {
            stall(result);
            return;
        }
        control->configuration = (uint8_t)setup->value;
        if (control->configuration == 0u)
            control->halted = 0u;
        no_data(result);
        return;
    case HELIKON_USB_REQ_SET_INTERFACE:
        if (setup->index != 0u || setup->value != 0u) {
            stall(result);
            return;
        }
        no_data(result);
        return;
    case HELIKON_USB_REQ_SET_FEATURE:
        /* Remote wakeup is not advertised, so no device feature is accepted. */
        stall(result);
        return;
    case HELIKON_USB_REQ_CLEAR_FEATURE:
        if (setup->value == USB_FEATURE_ENDPOINT_HALT) {
            set_endpoint_halted(control, setup->index, 0);
            no_data(result);
            return;
        }
        stall(result);
        return;
    case HELIKON_USB_REQ_SET_DESCRIPTOR:
    default:
        stall(result);
        return;
    }
}

static void setup_endpoint_standard(struct helikon_usb_control *control,
                                    const struct helikon_usb_setup *setup,
                                    int in,
                                    struct helikon_usb_control_result *result)
{
    uint8_t number = (uint8_t)(setup->index & 0x0fu);
    if (number > 6u) {
        stall(result);
        return;
    }

    if (in && setup->request == HELIKON_USB_REQ_GET_STATUS) {
        uint8_t *buffer = control->scratch;
        buffer[0] = endpoint_halted(control, setup->index) ? 0x01u : 0x00u;
        buffer[1] = 0u;
        data_in(result, buffer, 2u, setup->length);
        return;
    }
    if (!in && setup->request == HELIKON_USB_REQ_SET_FEATURE &&
        setup->value == USB_FEATURE_ENDPOINT_HALT) {
        set_endpoint_halted(control, setup->index, 1);
        no_data(result);
        return;
    }
    if (!in && setup->request == HELIKON_USB_REQ_CLEAR_FEATURE &&
        setup->value == USB_FEATURE_ENDPOINT_HALT) {
        set_endpoint_halted(control, setup->index, 0);
        no_data(result);
        return;
    }
    stall(result);
}

static void setup_interface_standard(struct helikon_usb_control *control,
                                     const struct helikon_usb_setup *setup,
                                     int in,
                                     struct helikon_usb_control_result *result)
{
    if (in && setup->request == HELIKON_USB_REQ_GET_STATUS) {
        uint8_t *buffer = control->scratch;
        buffer[0] = 0u;
        buffer[1] = 0u;
        data_in(result, buffer, 2u, setup->length);
        return;
    }
    stall(result);
}

static void setup_class(struct helikon_usb_control *control,
                        const struct helikon_usb_setup *setup, int in,
                        struct helikon_usb_control_result *result)
{
    /* The class interface is interface 0; anything else is a protocol error. */
    if ((setup->request_type & USB_REQUEST_TYPE_RECIPIENT_MASK) !=
        USB_REQUEST_TYPE_RECIPIENT_INTERFACE) {
        stall(result);
        return;
    }

    if (in && setup->request == HELIKON_USB_MSC_GET_MAX_LUN) {
        uint8_t *buffer = control->scratch;
        buffer[0] = HELIKON_USB_MAX_LUN;
        data_in(result, buffer, 1u, setup->length);
        return;
    }
    if (!in && setup->request == HELIKON_USB_MSC_BULK_ONLY_RESET) {
        control->bulk_only_reset = 1;
        control->halted = 0u;
        no_data(result);
        return;
    }
    stall(result);
}

void helikon_usb_control_setup(struct helikon_usb_control *control,
                               const struct helikon_usb_setup *setup,
                               struct helikon_usb_control_result *result)
{
    if (result == NULL)
        return;
    stall(result);
    if (control == NULL || setup == NULL)
        return;

    int in = (setup->request_type & USB_REQUEST_TYPE_DIR_IN) != 0u;
    uint8_t type = setup->request_type & USB_REQUEST_TYPE_TYPE_MASK;
    uint8_t recipient =
        setup->request_type & USB_REQUEST_TYPE_RECIPIENT_MASK;

    if (type == USB_REQUEST_TYPE_TYPE_CLASS) {
        setup_class(control, setup, in, result);
        return;
    }
    if (type != USB_REQUEST_TYPE_TYPE_STANDARD) {
        stall(result); /* no vendor requests on this product */
        return;
    }

    switch (recipient) {
    case USB_REQUEST_TYPE_RECIPIENT_DEVICE:
        setup_device_standard(control, setup, in, result);
        return;
    case USB_REQUEST_TYPE_RECIPIENT_INTERFACE:
        setup_interface_standard(control, setup, in, result);
        return;
    case USB_REQUEST_TYPE_RECIPIENT_ENDPOINT:
        setup_endpoint_standard(control, setup, in, result);
        return;
    default:
        stall(result);
        return;
    }
}

int helikon_usb_control_out(struct helikon_usb_control *control,
                            const void *data, uint16_t length)
{
    (void)control;
    (void)data;
    (void)length;
    return HELIKON_ERR_UNSUPPORTED;
}
