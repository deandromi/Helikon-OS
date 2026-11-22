/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * USB mass storage: bulk-only transport and the SCSI commands Disk Mode needs.
 *
 * This module is transport-shaped, not hardware-shaped: the USB driver gives it
 * one 31-byte CBW at a time, then streams the data stage through `in`/`out`, then
 * asks for the 13-byte CSW. Nothing here touches registers, so a host test can
 * drive the exact byte sequence a laptop would send — including a format that
 * writes a partition map and an HFS+ volume over the whole disk.
 *
 * The LUN is a window over a block device: `base_lba` plus `sector_count`. The
 * Disk Mode caller decides what that window is. A bare volume window is the
 * normal case; `base = 0` and the full sector count is the whole-disk restore
 * window the owner uses to reformat beyond 128 GiB (D-54, §11 option C).
 *
 * Writes are refused when the block device is read-only, and a refused write
 * reports DATA PROTECT rather than failing silently.
 */
#ifndef HELIKON_USB_USB_MSC_H
#define HELIKON_USB_USB_MSC_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"

#define HELIKON_USB_CBW_SIZE 31u
#define HELIKON_USB_CSW_SIZE 13u
#define HELIKON_USB_CBW_SIGNATURE 0x43425355u /* "USBC", little-endian */
#define HELIKON_USB_CSW_SIGNATURE 0x53425355u /* "USBS", little-endian */

/* CSW status byte. */
#define HELIKON_USB_CSW_PASSED 0x00u
#define HELIKON_USB_CSW_FAILED 0x01u
#define HELIKON_USB_CSW_PHASE_ERROR 0x02u

/* SCSI sense keys this device uses. */
#define HELIKON_USB_SENSE_NONE 0x00u
#define HELIKON_USB_SENSE_NOT_READY 0x02u
#define HELIKON_USB_SENSE_MEDIUM_ERROR 0x03u
#define HELIKON_USB_SENSE_ILLEGAL_REQUEST 0x05u
#define HELIKON_USB_SENSE_UNIT_ATTENTION 0x06u
#define HELIKON_USB_SENSE_DATA_PROTECT 0x07u

struct helikon_usb_sense {
    uint8_t key;
    uint8_t asc;
    uint8_t ascq;
};

struct helikon_usb_msc {
    struct helikon_block_device *device;
    uint64_t base_lba;
    uint64_t sector_count;
    uint32_t sector_size;
    uint32_t timeout_ms;

    /* Current command. */
    uint32_t tag;
    uint8_t cdb[16];
    uint8_t cdb_length;
    int direction;          /* 0 none, 1 IN, -1 OUT */
    uint32_t transfer_length; /* bytes of the data stage */
    uint32_t transferred;   /* bytes moved so far in the data stage */
    uint64_t lba;           /* first LBA of a READ/WRITE */
    uint32_t blocks;        /* block count of a READ/WRITE */
    uint8_t in_buffer[192]; /* small responses (inquiry, capacity, sense) */
    uint32_t in_length;
    struct helikon_usb_sense sense;
    int failed;
    int phase_error;
    /* One sector staging buffer for unaligned transfers. */
    uint64_t staged_lba;
    int staged_valid;
    uint8_t staged[4096];
    /*
     * Set by START STOP UNIT with LoEj=1, Start=0 — the host is releasing the
     * medium. Disk Mode flushes, stops exposing the LUN and lets the firmware
     * see the volume again. `helikon_usb_msc_take_eject()` consumes it.
     */
    int eject_requested;
};

/* What the transport must do next. */
struct helikon_usb_msc_result {
    int direction;   /* 0 no data stage, 1 IN, -1 OUT */
    uint32_t length; /* bytes of data stage */
    int failed;      /* CSW status will be FAILED */
};

/*
 * `base_lba` and `sector_count` define the window. `sector_count == 0` uses the
 * whole device. The device must outlive the MSC state.
 */
int helikon_usb_msc_init(struct helikon_usb_msc *msc,
                         struct helikon_block_device *device, uint64_t base_lba,
                         uint64_t sector_count, uint32_t timeout_ms);

/* Bus reset or BULK_ONLY_RESET: drop the current command, keep the window. */
void helikon_usb_msc_reset(struct helikon_usb_msc *msc);

/*
 * Handles one CBW. Returns HELIKON_OK when the command was accepted (including a
 * command that fails at the SCSI layer, which is reported in `*result` and in the
 * sense data), or HELIKON_ERR_FORMAT for a malformed CBW the transport must
 * answer with a phase error.
 */
int helikon_usb_msc_command(struct helikon_usb_msc *msc,
                            const uint8_t cbw[HELIKON_USB_CBW_SIZE],
                            struct helikon_usb_msc_result *result);

/* Data stage. `length` reports what was actually produced/consumed. */
int helikon_usb_msc_in(struct helikon_usb_msc *msc, void *buffer, uint32_t max,
                       uint32_t *length);
int helikon_usb_msc_out(struct helikon_usb_msc *msc, const void *buffer,
                        uint32_t length, uint32_t *consumed);

/* Builds the CSW for the current command. */
/*
 * The CSW a transport sends when it refuses a CBW outright (wrong signature,
 * reserved bits set, or a CDB length outside 1..16). The tag is echoed from the
 * CBW's bytes 4..7 so the host can match it, the status is PHASE_ERROR and the
 * residue is zero because nothing transferred. Sending nothing leaves the host
 * waiting out its transfer timeout; this answers immediately.
 */
void helikon_usb_msc_phase_error_csw(const uint8_t cbw[HELIKON_USB_CBW_SIZE],
                                     uint8_t csw[HELIKON_USB_CSW_SIZE]);

/* Returns 1 once if the host asked to eject, then clears the flag. */
int helikon_usb_msc_take_eject(struct helikon_usb_msc *msc);

void helikon_usb_msc_status(struct helikon_usb_msc *msc,
                            uint8_t csw[HELIKON_USB_CSW_SIZE]);

const struct helikon_usb_sense *helikon_usb_msc_sense(
    const struct helikon_usb_msc *msc);

#endif
