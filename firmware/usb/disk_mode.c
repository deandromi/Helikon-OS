/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helikon Disk Mode state machine. See `disk_mode.h` for the contract and
 * `docs/disk-mode.md` §5/§6/§9 for the design it implements.
 *
 * Two rules are worth stating in code:
 *  1. No opt-in, no mode. The gate is checked here, not in the caller, so a
 *     caller that forgets cannot silently expose a writable disk.
 *  2. The data-volume LUN is read-only unless the owner chose restore mode. The
 *     flag is forced on the mode's private copy of the block device, so the
 *     MSC layer refuses a write with DATA PROTECT instead of trusting a caller.
 */
#include "disk_mode.h"

#include <stddef.h>

#include "error.h"

static uint16_t get_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static void parse_setup(const uint8_t raw[8], struct helikon_usb_setup *setup)
{
    setup->request_type = raw[0];
    setup->request = raw[1];
    setup->value = get_le16(raw + 2);
    setup->index = get_le16(raw + 4);
    setup->length = get_le16(raw + 6);
}

const char *helikon_disk_mode_state_name(enum helikon_disk_mode_state state)
{
    switch (state) {
    case HELIKON_DISK_MODE_OFF:
        return "off";
    case HELIKON_DISK_MODE_WAITING:
        return "waiting for host";
    case HELIKON_DISK_MODE_ENUMERATED:
        return "host connected";
    case HELIKON_DISK_MODE_RELEASED:
        return "ejected";
    case HELIKON_DISK_MODE_ERROR:
    default:
        return "error";
    }
}

int helikon_disk_mode_start(struct helikon_disk_mode *mode,
                            const struct helikon_usb_device_ops *ops,
                            struct helikon_block_device *device,
                            const struct helikon_usb_identity *identity,
                            const struct helikon_disk_mode_options *options)
{
    if (mode == NULL || ops == NULL || device == NULL || identity == NULL ||
        options == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (ops->ep0_send == NULL || ops->ep0_receive == NULL ||
        ops->ep0_stall == NULL || ops->ep1_send == NULL ||
        ops->ep2_receive == NULL || ops->set_address == NULL ||
        ops->configure == NULL)
        return HELIKON_ERR_ARGUMENT;

    for (uint32_t index = 0u; index < (uint32_t)sizeof(*mode); ++index)
        ((uint8_t *)mode)[index] = 0u;
    mode->state = HELIKON_DISK_MODE_OFF;

    /* The gate: D-54 says the mode is explicit. Refuse quietly. */
    if (!options->opt_in)
        return HELIKON_ERR_UNSUPPORTED;

    mode->ops = *ops;
    mode->identity = *identity;
    mode->restore_mode = options->restore_mode;
    mode->device = *device;
    /*
     * The board attaches every block device read-only. Restore mode is the one
     * caller that may write, so it clears the flag here rather than asking the
     * board for a writable device: the flag travels with the mode, and a caller
     * cannot hand in a writable device by mistake.
     */
    if (options->restore_mode)
        mode->device.info.flags &= ~HELIKON_BLOCK_FLAG_READ_ONLY;
    else
        mode->device.info.flags |= HELIKON_BLOCK_FLAG_READ_ONLY;

    uint64_t base = options->restore_mode ? 0u : options->base_lba;
    uint64_t count = options->restore_mode ? 0u : options->sector_count;
    if (options->restore_mode)
        base = 0u;

    int rc = helikon_usb_msc_init(&mode->msc, &mode->device, base, count,
                                 options->timeout_ms);
    if (rc != HELIKON_OK)
        return rc;

    helikon_usb_control_reset(&mode->control, &mode->identity);
    mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
    mode->state = HELIKON_DISK_MODE_WAITING;
    return HELIKON_OK;
}

void helikon_disk_mode_stop(struct helikon_disk_mode *mode)
{
    if (mode == NULL || mode->state == HELIKON_DISK_MODE_OFF)
        return;
    /* A host that vanished mid-write still deserves the flush attempt. */
    (void)helikon_block_flush(&mode->device, mode->msc.timeout_ms);
    mode->state = HELIKON_DISK_MODE_OFF;
    mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
    mode->stage_total = 0u;
    mode->stage_done = 0u;
    mode->csw_pending = 0;
}

/* Sends the CSW and closes the command. */
static void send_csw(struct helikon_disk_mode *mode)
{
    helikon_usb_msc_status(&mode->msc, mode->csw);
    mode->phase = HELIKON_DISK_MODE_BOT_IN_CSW;
    mode->csw_pending = 0;
    if (mode->ops.ep1_send(mode->ops.context, mode->csw,
                           HELIKON_USB_CSW_SIZE) != HELIKON_OK) {
        mode->errors += 1u;
        mode->state = HELIKON_DISK_MODE_ERROR;
    }
}

/*
 * Moves the next slice of the data stage. A READ's bytes come from the MSC
 * layer (which owns the sector staging), a WRITE's go into it. The transport
 * only carries one slice at a time, so the FIFO cannot overrun.
 */
static void push_stage(struct helikon_disk_mode *mode)
{
    if (mode->stage_done >= mode->stage_total) {
        send_csw(mode);
        return;
    }
    uint32_t chunk = mode->stage_total - mode->stage_done;
    if (chunk > sizeof(mode->bounce))
        chunk = sizeof(mode->bounce);

    if (mode->phase == HELIKON_DISK_MODE_BOT_IN_DATA) {
        uint32_t got = 0u;
        int rc = helikon_usb_msc_in(&mode->msc, mode->bounce, chunk, &got);
        if (rc != HELIKON_OK || got == 0u) {
            /* A short read means the medium refused; the CSW says FAILED. */
            if (rc != HELIKON_OK) {
                mode->errors += 1u;
                mode->stage_done = mode->stage_total;
            }
            send_csw(mode);
            return;
        }
        if (mode->ops.ep1_send(mode->ops.context, mode->bounce, got) !=
            HELIKON_OK) {
            mode->errors += 1u;
            mode->state = HELIKON_DISK_MODE_ERROR;
            return;
        }
        mode->stage_done += got;
    } else if (mode->phase == HELIKON_DISK_MODE_BOT_OUT_DATA) {
        if (mode->ops.ep2_receive(mode->ops.context, mode->bounce, chunk) !=
            HELIKON_OK) {
            mode->errors += 1u;
            mode->state = HELIKON_DISK_MODE_ERROR;
        }
    }
}

/* Starts the data stage (or the CSW when the command has none). */
static void begin_stage(struct helikon_disk_mode *mode,
                        const struct helikon_usb_msc_result *result)
{
    mode->stage_total = result->length;
    mode->stage_done = 0u;
    if (result->direction == 0 || result->length == 0u) {
        send_csw(mode);
        return;
    }
    mode->phase = result->direction > 0 ? HELIKON_DISK_MODE_BOT_IN_DATA
                                        : HELIKON_DISK_MODE_BOT_OUT_DATA;
    if (mode->phase == HELIKON_DISK_MODE_BOT_OUT_DATA) {
        uint32_t chunk = mode->stage_total;
        if (chunk > sizeof(mode->bounce))
            chunk = sizeof(mode->bounce);
        if (mode->ops.ep2_receive(mode->ops.context, mode->bounce, chunk) !=
            HELIKON_OK) {
            mode->errors += 1u;
            mode->state = HELIKON_DISK_MODE_ERROR;
        }
        return;
    }
    push_stage(mode);
}

/* Arms the 31-byte CBW receive; the host sends one only when it is idle. */
static void arm_cbw(struct helikon_disk_mode *mode)
{
    if (mode->ops.ep2_receive(mode->ops.context, mode->cbw,
                              HELIKON_USB_CBW_SIZE) != HELIKON_OK) {
        mode->errors += 1u;
        mode->state = HELIKON_DISK_MODE_ERROR;
    }
}

static void handle_cbw(struct helikon_disk_mode *mode)
{
    /*
     * Only a configured device may run a command. Once the medium is released
     * the firmware owns the volume again, and a stray CBW from a host that has
     * not noticed the eject must not reach the block device at all: a WRITE
     * there would touch a volume the rest of the firmware believes it owns.
     * A disconnected or waiting device ignores the packet the same way.
     */
    if (mode->state != HELIKON_DISK_MODE_ENUMERATED) {
        mode->errors += 1u;
        mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
        return;
    }

    struct helikon_usb_msc_result result;
    int rc = helikon_usb_msc_command(&mode->msc, mode->cbw, &result);
    mode->commands += 1u;
    if (rc != HELIKON_OK) {
        /*
         * A CBW the transport refuses still gets an answer. Going silent leaves
         * the host waiting out its transfer timeout and then resetting the pipe,
         * which turns one bad byte into seconds of stall; a 13-byte CSW with
         * PHASE_ERROR closes the command at once. The tag is echoed from the
         * CBW, even though its signature was the thing that failed.
         */
        mode->errors += 1u;
        helikon_usb_msc_phase_error_csw(mode->cbw, mode->csw);
        mode->phase = HELIKON_DISK_MODE_BOT_IN_CSW;
        mode->csw_pending = 0;
        if (mode->ops.ep1_send(mode->ops.context, mode->csw,
                               HELIKON_USB_CSW_SIZE) != HELIKON_OK) {
            mode->errors += 1u;
            mode->state = HELIKON_DISK_MODE_ERROR;
        }
        return;
    }
    begin_stage(mode, &result);
}

static void handle_setup(struct helikon_disk_mode *mode, const uint8_t raw[8])
{
    struct helikon_usb_setup setup;
    struct helikon_usb_control_result result;
    parse_setup(raw, &setup);
    helikon_usb_control_setup(&mode->control, &setup, &result);

    switch (result.action) {
    case HELIKON_USB_CONTROL_IN:
        if (mode->ops.ep0_send(mode->ops.context, result.data, result.length) !=
            HELIKON_OK)
            mode->errors += 1u;
        break;
    case HELIKON_USB_CONTROL_OUT:
        if (mode->ops.ep0_receive(mode->ops.context, NULL, result.length) !=
            HELIKON_OK)
            mode->errors += 1u;
        break;
    case HELIKON_USB_CONTROL_NO_DATA:
        /* SET_ADDRESS must reach the core before the status stage; the control
         * module has already validated 0..127. */
        if (setup.request == HELIKON_USB_REQ_SET_ADDRESS) {
            if (mode->ops.set_address(mode->ops.context,
                                      (uint8_t)setup.value) != HELIKON_OK)
                mode->errors += 1u;
        } else if (setup.request == HELIKON_USB_REQ_SET_CONFIGURATION) {
            int configured = setup.value != 0u;
            if (mode->ops.configure(mode->ops.context, configured) !=
                HELIKON_OK)
                mode->errors += 1u;
            mode->state = configured ? HELIKON_DISK_MODE_ENUMERATED
                                     : HELIKON_DISK_MODE_WAITING;
            if (configured) {
                mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
                arm_cbw(mode);
            } else {
                /* The host released the interface: drop any half command. */
                helikon_usb_msc_reset(&mode->msc);
                mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
            }
        }
        break;
    case HELIKON_USB_CONTROL_STALL:
    default:
        (void)mode->ops.ep0_stall(mode->ops.context);
        break;
    }

    if (mode->control.bulk_only_reset) {
        mode->control.bulk_only_reset = 0;
        helikon_usb_msc_reset(&mode->msc);
        mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
        mode->stage_total = 0u;
        mode->stage_done = 0u;
    }
}

int helikon_disk_mode_event(struct helikon_disk_mode *mode,
                            enum helikon_usb_event event,
                            const uint8_t setup[8], uint32_t length)
{
    if (mode == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (mode->state == HELIKON_DISK_MODE_OFF ||
        mode->state == HELIKON_DISK_MODE_ERROR)
        return HELIKON_OK;

    switch (event) {
    case HELIKON_USB_EVENT_SETUP:
        if (setup == NULL)
            return HELIKON_ERR_ARGUMENT;
        handle_setup(mode, setup);
        break;

    case HELIKON_USB_EVENT_EP2_OUT_DONE:
        if (mode->phase == HELIKON_DISK_MODE_BOT_IDLE) {
            /* A CBW: fixed 31 bytes. Anything else is a host/protocol error. */
            if (length != HELIKON_USB_CBW_SIZE) {
                mode->errors += 1u;
                arm_cbw(mode);
                break;
            }
            handle_cbw(mode);
        } else if (mode->phase == HELIKON_DISK_MODE_BOT_OUT_DATA) {
            uint32_t used = 0u;
            int rc = helikon_usb_msc_out(&mode->msc, mode->bounce, length,
                                         &used);
            if (rc != HELIKON_OK) {
                mode->errors += 1u;
                mode->stage_done = mode->stage_total;
                send_csw(mode);
                break;
            }
            mode->stage_done += used;
            if (mode->stage_done >= mode->stage_total) {
                send_csw(mode);
            } else {
                uint32_t chunk = mode->stage_total - mode->stage_done;
                if (chunk > sizeof(mode->bounce))
                    chunk = sizeof(mode->bounce);
                if (mode->ops.ep2_receive(mode->ops.context, mode->bounce,
                                          chunk) != HELIKON_OK) {
                    mode->errors += 1u;
                    mode->state = HELIKON_DISK_MODE_ERROR;
                }
            }
        } else {
            /* An OUT packet while the device expected to send: a phase error. */
            mode->errors += 1u;
        }
        break;

    case HELIKON_USB_EVENT_EP1_IN_DONE:
        if (mode->phase == HELIKON_DISK_MODE_BOT_IN_DATA) {
            push_stage(mode);
        } else if (mode->phase == HELIKON_DISK_MODE_BOT_IN_CSW) {
            mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
            if (helikon_usb_msc_take_eject(&mode->msc)) {
                (void)helikon_block_flush(&mode->device, mode->msc.timeout_ms);
                mode->state = HELIKON_DISK_MODE_RELEASED;
            } else if (mode->state == HELIKON_DISK_MODE_ENUMERATED) {
                /* Ready for the next command. */
                arm_cbw(mode);
            }
        }
        break;

    case HELIKON_USB_EVENT_BUS_RESET:
        helikon_usb_control_reset(&mode->control, &mode->identity);
        helikon_usb_msc_reset(&mode->msc);
        mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
        mode->stage_total = 0u;
        mode->stage_done = 0u;
        mode->state = HELIKON_DISK_MODE_WAITING;
        break;

    case HELIKON_USB_EVENT_DISCONNECT:
        (void)helikon_block_flush(&mode->device, mode->msc.timeout_ms);
        helikon_usb_control_reset(&mode->control, &mode->identity);
        helikon_usb_msc_reset(&mode->msc);
        mode->phase = HELIKON_DISK_MODE_BOT_IDLE;
        mode->state = HELIKON_DISK_MODE_RELEASED;
        break;

    case HELIKON_USB_EVENT_ERROR:
        mode->errors += 1u;
        break;

    case HELIKON_USB_EVENT_EP0_IN_DONE:
    case HELIKON_USB_EVENT_EP0_OUT_DONE:
    default:
        break;
    }
    return HELIKON_OK;
}

int helikon_disk_mode_take_eject(struct helikon_disk_mode *mode)
{
    if (mode == NULL)
        return 0;
    return helikon_usb_msc_take_eject(&mode->msc);
}
