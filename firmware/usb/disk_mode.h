/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helikon Disk Mode: the state machine that turns a USB device stack and a block
 * device into a LUN a laptop can format.
 *
 * It sits between the transport (the S5L8702 driver, or a host test harness)
 * and the protocol modules (`usb_control.c`, `usb_msc.c`). It owns:
 *  - the opt-in gate: Disk Mode exists only when the owner asked for it, and it
 *    refuses to start without it (D-54);
 *  - the LUN window: the whole disk for a restore/format, or a bare data volume;
 *  - the bulk-only phase machine: CBW, data stage, CSW, one command at a time;
 *  - the release path: an eject request flushes, drops the window and reports
 *    that the firmware may see the volume again.
 *
 * It does not touch registers. The transport is a small ops table, so a host
 * test can drive the exact sequence a laptop sends — including the whole-disk
 * window that formats above 128 GiB.
 *
 * **Nothing here has run on a 6G.** See `docs/disk-mode.md` §5, §6 and §9.
 */
#ifndef HELIKON_USB_DISK_MODE_H
#define HELIKON_USB_DISK_MODE_H

#include <stdint.h>

#include "block.h"
#include "usb_control.h"
#include "usb_events.h"
#include "usb_msc.h"

/* The transport's side of the conversation. Every call is synchronous. */
struct helikon_usb_device_ops {
    void *context;
    int (*ep0_send)(void *context, const void *data, uint32_t length);
    int (*ep0_receive)(void *context, void *data, uint32_t length);
    int (*ep0_stall)(void *context);
    int (*ep1_send)(void *context, const void *data, uint32_t length);
    int (*ep2_receive)(void *context, void *data, uint32_t length);
    /* Applies SET_ADDRESS. The core wants it before the status stage. */
    int (*set_address)(void *context, uint8_t address);
    /* Configures (1) or releases (0) the bulk endpoints. */
    int (*configure)(void *context, int configured);
};

/* What the owner chose, and nothing else, decides what the host may write. */
struct helikon_disk_mode_options {
    /* 1 only when the owner explicitly entered Disk Mode. */
    int opt_in;
    /*
     * 1 exposes the whole disk and accepts writes: the restore/format window
     * that lets a laptop write an APM and a fresh HFS+ volume above 128 GiB.
     * 0 is the read-only data volume.
     */
    int restore_mode;
    /* For a data-volume window: first LBA and count. 0/0 means the whole disk. */
    uint64_t base_lba;
    uint64_t sector_count;
    uint32_t timeout_ms;
};

enum helikon_disk_mode_state {
    HELIKON_DISK_MODE_OFF = 0,
    /* Opted in, waiting for the host to configure the device. */
    HELIKON_DISK_MODE_WAITING,
    /* Configured; the host may send CBWs. */
    HELIKON_DISK_MODE_ENUMERATED,
    /* An eject request flushed the medium; the firmware owns the disk again. */
    HELIKON_DISK_MODE_RELEASED,
    /* A hard error stopped the mode; the caller decides what to report. */
    HELIKON_DISK_MODE_ERROR
};

/* Bulk-only transport phase of the current command. */
enum helikon_disk_mode_phase {
    HELIKON_DISK_MODE_BOT_IDLE = 0,
    HELIKON_DISK_MODE_BOT_IN_DATA,
    HELIKON_DISK_MODE_BOT_OUT_DATA,
    HELIKON_DISK_MODE_BOT_IN_CSW
};

struct helikon_disk_mode {
    struct helikon_usb_device_ops ops;
    struct helikon_block_device device;
    struct helikon_usb_msc msc;
    struct helikon_usb_control control;
    struct helikon_usb_identity identity;

    enum helikon_disk_mode_state state;
    enum helikon_disk_mode_phase phase;
    int restore_mode;

    uint8_t csw[HELIKON_USB_CSW_SIZE];
    /* Data stage in flight: how many bytes the MSC layer still owes or wants. */
    uint32_t stage_total;
    uint32_t stage_done;
    /* CSW waiting for the host to drain the last data packet first. */
    int csw_pending;
    /* The 31-byte CBW and the 512-byte data chunk. */
    uint8_t cbw[HELIKON_USB_CBW_SIZE];
    uint8_t bounce[512];

    uint32_t commands;
    uint32_t errors;
};

/*
 * Prepares the mode. `device` must already be initialised (sectors, ops). The
 * mode copies it, so it can force the read-only flag for the data-volume LUN
 * without touching the caller's copy.
 *
 * Returns HELIKON_OK, HELIKON_ERR_ARGUMENT for a missing op or a restore window
 * on a missing device, or HELIKON_ERR_UNSUPPORTED when the owner did not opt in.
 */
int helikon_disk_mode_start(struct helikon_disk_mode *mode,
                            const struct helikon_usb_device_ops *ops,
                            struct helikon_block_device *device,
                            const struct helikon_usb_identity *identity,
                            const struct helikon_disk_mode_options *options);

/* Releases the window and forgets the host. Safe on an already stopped mode. */
void helikon_disk_mode_stop(struct helikon_disk_mode *mode);

/*
 * Feeds one transport event.
 *  - SETUP: `setup` carries the 8 bytes, `length` is ignored.
 *  - EP2_OUT_DONE: `length` is the byte count the endpoint received.
 *  - everything else: both are ignored.
 */
int helikon_disk_mode_event(struct helikon_disk_mode *mode,
                            enum helikon_usb_event event,
                            const uint8_t setup[8], uint32_t length);

/* Human-readable state, for the Disk Mode screen. Never NULL. */
const char *helikon_disk_mode_state_name(enum helikon_disk_mode_state state);

/* Bytes the host asked to eject? Consumes the request, like the MSC getter. */
int helikon_disk_mode_take_eject(struct helikon_disk_mode *mode);

#endif
