/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "usb_msc.h"

#include <stddef.h>
#include <stdint.h>

#include "helikon_error.h"

/* SCSI opcodes used by a bulk-only mass-storage host. */
#define SCSI_TEST_UNIT_READY 0x00u
#define SCSI_REQUEST_SENSE 0x03u
#define SCSI_FORMAT_UNIT 0x04u
#define SCSI_INQUIRY 0x12u
#define SCSI_MODE_SENSE_6 0x1au
#define SCSI_START_STOP_UNIT 0x1bu
#define SCSI_PREVENT_ALLOW 0x1eu
#define SCSI_READ_FORMAT_CAPACITIES 0x23u
#define SCSI_READ_CAPACITY_10 0x25u
#define SCSI_READ_10 0x28u
#define SCSI_WRITE_10 0x2au
#define SCSI_VERIFY_10 0x2fu
#define SCSI_SYNCHRONIZE_CACHE_10 0x35u
#define SCSI_MODE_SENSE_10 0x5au
#define SCSI_READ_16 0x88u
#define SCSI_WRITE_16 0x8au
#define SCSI_SYNCHRONIZE_CACHE_16 0x91u
#define SCSI_SERVICE_ACTION_IN_16 0x9eu

#define SCSI_SA_READ_CAPACITY_16 0x10u

#define SENSE_INVALID_COMMAND 0x20u
#define SENSE_LBA_OUT_OF_RANGE 0x21u
#define SENSE_INVALID_FIELD 0x24u
#define SENSE_WRITE_PROTECTED 0x27u
#define SENSE_UNRECOVERED_READ 0x11u
#define SENSE_WRITE_ERROR 0x0cu
#define SENSE_MEDIUM_NOT_PRESENT 0x3au
#define SENSE_COMMAND_SEQUENCE 0x2cu

/* The biggest single block read or write a data stage will ask for. */
#define MSC_MAX_SECTORS_PER_CALL 128u

static uint32_t get_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint64_t get_be64(const uint8_t *data)
{
    return ((uint64_t)get_be32(data) << 32) | get_be32(data + 4);
}

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void put_be64(uint8_t *data, uint64_t value)
{
    put_be32(data, (uint32_t)(value >> 32));
    put_be32(data + 4, (uint32_t)value);
}

static void put_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void sense_reset(struct helikon_usb_msc *msc)
{
    msc->sense.key = HELIKON_USB_SENSE_NONE;
    msc->sense.asc = 0u;
    msc->sense.ascq = 0u;
}

static void sense_set(struct helikon_usb_msc *msc, uint8_t key, uint8_t asc,
                      uint8_t ascq)
{
    msc->sense.key = key;
    msc->sense.asc = asc;
    msc->sense.ascq = ascq;
    msc->failed = 1;
}

/* Maps a block-layer refusal to sense data, so the host sees why. */
static void sense_from_error(struct helikon_usb_msc *msc, int rc, int writing)
{
    switch (rc) {
    case HELIKON_OK:
        return;
    case HELIKON_ERR_READ_ONLY:
        sense_set(msc, HELIKON_USB_SENSE_DATA_PROTECT, SENSE_WRITE_PROTECTED, 0u);
        return;
    case HELIKON_ERR_ARGUMENT:
        sense_set(msc, HELIKON_USB_SENSE_ILLEGAL_REQUEST,
                  SENSE_LBA_OUT_OF_RANGE, 0u);
        return;
    case HELIKON_ERR_IO:
    default:
        sense_set(msc, HELIKON_USB_SENSE_MEDIUM_ERROR,
                  writing ? SENSE_WRITE_ERROR : SENSE_UNRECOVERED_READ, 0u);
        return;
    }
}

int helikon_usb_msc_init(struct helikon_usb_msc *msc,
                         struct helikon_block_device *device, uint64_t base_lba,
                         uint64_t sector_count, uint32_t timeout_ms)
{
    if (msc == NULL || device == NULL || device->info.sector_size == 0u ||
        device->info.sector_size > sizeof(msc->staged))
        return HELIKON_ERR_ARGUMENT;
    if (base_lba >= device->info.sector_count)
        return HELIKON_ERR_ARGUMENT;
    if (sector_count == 0u)
        sector_count = device->info.sector_count - base_lba;
    if (sector_count > device->info.sector_count - base_lba)
        return HELIKON_ERR_ARGUMENT;

    msc->device = device;
    msc->base_lba = base_lba;
    msc->sector_count = sector_count;
    msc->sector_size = device->info.sector_size;
    msc->timeout_ms = timeout_ms;
    helikon_usb_msc_reset(msc);
    return HELIKON_OK;
}

void helikon_usb_msc_reset(struct helikon_usb_msc *msc)
{
    if (msc == NULL)
        return;
    msc->tag = 0u;
    msc->cdb_length = 0u;
    msc->direction = 0;
    msc->transfer_length = 0u;
    msc->transferred = 0u;
    msc->lba = 0u;
    msc->blocks = 0u;
    msc->in_length = 0u;
    msc->failed = 0;
    msc->phase_error = 0;
    msc->staged_lba = 0u;
    msc->staged_valid = 0;
    msc->eject_requested = 0;
    sense_reset(msc);
}

static int writable(const struct helikon_usb_msc *msc)
{
    return (msc->device->info.flags & HELIKON_BLOCK_FLAG_READ_ONLY) == 0u;
}

static uint64_t last_lba(const struct helikon_usb_msc *msc)
{
    return msc->sector_count - 1u;
}

/* INQUIRY: 36 bytes, SCSI-2, removable, direct-access, "Helikon". */
static void inquiry(struct helikon_usb_msc *msc)
{
    uint8_t *data = msc->in_buffer;
    for (size_t index = 0u; index < sizeof(msc->in_buffer); ++index)
        data[index] = 0u;

    data[0] = 0x00u; /* direct access block device */
    data[1] = 0x80u; /* removable medium */
    data[2] = 0x04u; /* SCSI-2, response data format 2 */
    data[3] = 0x02u; /* response data format */
    data[4] = 31u;   /* additional length */
    data[7] = 0x02u; /* CmdQue */
    const char vendor[8] = { 'H', 'e', 'l', 'i', 'k', 'o', 'n', ' ' };
    const char product[16] = { 'D', 'i', 's', 'k', ' ', 'M', 'o', 'd', 'e',
                               ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
    const char revision[4] = { '0', '.', '0', '1' };
    for (size_t index = 0u; index < sizeof(vendor); ++index)
        data[8u + index] = (uint8_t)vendor[index];
    for (size_t index = 0u; index < sizeof(product); ++index)
        data[16u + index] = (uint8_t)product[index];
    for (size_t index = 0u; index < sizeof(revision); ++index)
        data[32u + index] = (uint8_t)revision[index];
    msc->in_length = 36u;
}

static void request_sense(struct helikon_usb_msc *msc)
{
    uint8_t *data = msc->in_buffer;
    for (size_t index = 0u; index < 18u; ++index)
        data[index] = 0u;
    data[0] = 0x70u; /* current error, fixed format */
    data[2] = msc->sense.key;
    data[7] = 10u; /* additional sense length */
    data[12] = msc->sense.asc;
    data[13] = msc->sense.ascq;
    msc->in_length = 18u;
    /* The response is built; the host has now been told. A second REQUEST
     * SENSE reports NO SENSE, as SCSI requires. */
    sense_reset(msc);
}

static void read_capacity_10(struct helikon_usb_msc *msc)
{
    uint8_t *data = msc->in_buffer;
    uint64_t last = last_lba(msc);
    /*
     * A window larger than 2^32 blocks cannot be stated in READ CAPACITY(10).
     * The standard answer is the maximum value; the host then asks with
     * READ CAPACITY(16), which this device supports.
     */
    uint32_t reported = last > 0xffffffffull ? 0xffffffffu : (uint32_t)last;
    put_be32(data, reported);
    put_be32(data + 4, msc->sector_size);
    msc->in_length = 8u;
}

static void read_capacity_16(struct helikon_usb_msc *msc)
{
    uint8_t *data = msc->in_buffer;
    for (size_t index = 0u; index < 32u; ++index)
        data[index] = 0u;
    put_be64(data, last_lba(msc));
    put_be32(data + 8, msc->sector_size);
    msc->in_length = 32u;
}

static void read_format_capacities(struct helikon_usb_msc *msc)
{
    uint8_t *data = msc->in_buffer;
    for (size_t index = 0u; index < 12u; ++index)
        data[index] = 0u;
    /* One descriptor: current capacity, formatted medium. */
    uint32_t blocks = last_lba(msc) + 1u > 0xffffffffull
                          ? 0xffffffffu
                          : (uint32_t)(last_lba(msc) + 1u);
    put_be32(data + 4, blocks);
    data[4 + 3] = 0x02u; /* descriptor type: formatted media */
    put_be32(data + 8, msc->sector_size);
    msc->in_length = 12u;
}

static void mode_sense_6(struct helikon_usb_msc *msc)
{
    uint8_t *data = msc->in_buffer;
    /* Header only: no block descriptor, no pages. */
    data[0] = 3u;
    data[1] = 0u;
    data[2] = writable(msc) ? 0x00u : 0x80u; /* bit 7 write protected */
    data[3] = 0u;
    msc->in_length = 4u;
}

static void mode_sense_10(struct helikon_usb_msc *msc)
{
    uint8_t *data = msc->in_buffer;
    for (size_t index = 0u; index < 8u; ++index)
        data[index] = 0u;
    data[3] = writable(msc) ? 0x00u : 0x80u;
    msc->in_length = 8u;
}

/* READ(10)/WRITE(10)/VERIFY(10) share the CDB layout; 16-byte forms follow. */
static int parse_rw10(struct helikon_usb_msc *msc, const uint8_t *cdb)
{
    msc->lba = get_be32(cdb + 2);
    msc->blocks = ((uint32_t)cdb[7] << 8) | (uint32_t)cdb[8];
    return HELIKON_OK;
}

static int parse_rw16(struct helikon_usb_msc *msc, const uint8_t *cdb)
{
    msc->lba = get_be64(cdb + 2);
    msc->blocks = get_be32(cdb + 10);
    return HELIKON_OK;
}

/* The range check that keeps every data stage inside the LUN window. */
static int check_range(struct helikon_usb_msc *msc)
{
    if (msc->blocks == 0u)
        return HELIKON_OK;
    if (msc->lba >= msc->sector_count ||
        msc->blocks > msc->sector_count - msc->lba) {
        sense_set(msc, HELIKON_USB_SENSE_ILLEGAL_REQUEST, SENSE_LBA_OUT_OF_RANGE,
                  0u);
        return HELIKON_ERR_ARGUMENT;
    }
    return HELIKON_OK;
}

/*
 * Starts a command: the transport state of the previous one goes away, but the
 * sense data does not. A CHECK CONDITION stays pending until the host asks for
 * it with REQUEST SENSE (or issues BOT reset, which calls the public reset).
 */
static void begin_command(struct helikon_usb_msc *msc)
{
    msc->tag = 0u;
    msc->cdb_length = 0u;
    msc->direction = 0;
    msc->transfer_length = 0u;
    msc->transferred = 0u;
    msc->lba = 0u;
    msc->blocks = 0u;
    msc->in_length = 0u;
    msc->failed = 0;
    msc->phase_error = 0;
    msc->staged_lba = 0u;
    msc->staged_valid = 0;
}

/* Fills `length` bytes of the data stage from the window. */
static int read_data(struct helikon_usb_msc *msc, void *buffer, uint32_t length)
{
    uint8_t *out = buffer;
    uint32_t done = 0u;

    while (done < length) {
        uint64_t absolute = msc->transferred + done;
        uint64_t lba = msc->lba + absolute / msc->sector_size;
        uint32_t within = (uint32_t)(absolute % msc->sector_size);
        uint32_t take = length - done;
        if (take > msc->sector_size - within)
            take = msc->sector_size - within;

        int rc;
        if (within == 0u && take == msc->sector_size) {
            /* Sector-aligned: read a run at once, up to the call limit. */
            uint32_t sectors = (length - done) / msc->sector_size;
            if (sectors > MSC_MAX_SECTORS_PER_CALL)
                sectors = MSC_MAX_SECTORS_PER_CALL;
            rc = helikon_block_read(msc->device, msc->base_lba + lba, sectors,
                                    out + done, msc->timeout_ms);
            if (rc != HELIKON_OK) {
                sense_from_error(msc, rc, 0);
                return rc;
            }
            done += sectors * msc->sector_size;
            msc->staged_valid = 0;
            continue;
        }

        if (!msc->staged_valid || msc->staged_lba != lba) {
            rc = helikon_block_read(msc->device, msc->base_lba + lba, 1u,
                                    msc->staged, msc->timeout_ms);
            if (rc != HELIKON_OK) {
                sense_from_error(msc, rc, 0);
                return rc;
            }
            msc->staged_lba = lba;
            msc->staged_valid = 1;
        }
        for (uint32_t index = 0u; index < take; ++index)
            out[done + index] = msc->staged[within + index];
        done += take;
    }
    return HELIKON_OK;
}

/* Consumes `length` bytes of the data stage into the window, flushing sectors. */
static int write_data(struct helikon_usb_msc *msc, const void *buffer,
                      uint32_t length)
{
    /* A transport that reported a zero sector size would divide by zero inside
     * an interrupt path. The check is at init already; this copies the value into
     * a local so the loop cannot see it change either. */
    uint32_t sector_size = msc->sector_size;
    if (sector_size == 0u)
        return HELIKON_ERR_FORMAT;
    const uint8_t *in = buffer;
    uint32_t done = 0u;

    while (done < length) {
        uint64_t absolute = msc->transferred + done;
        uint64_t lba = msc->lba + absolute / sector_size;
        uint32_t within = (uint32_t)(absolute % sector_size);
        uint32_t take = length - done;
        if (take > sector_size - within)
            take = sector_size - within;

        int rc;
        if (within == 0u && take == sector_size) {
            uint32_t sectors = (length - done) / sector_size;
            if (sectors > MSC_MAX_SECTORS_PER_CALL)
                sectors = MSC_MAX_SECTORS_PER_CALL;
            rc = helikon_block_write(msc->device, msc->base_lba + lba, sectors,
                                     in + done, msc->timeout_ms);
            if (rc != HELIKON_OK) {
                sense_from_error(msc, rc, 1);
                return rc;
            }
            done += sectors * msc->sector_size;
            msc->staged_valid = 0;
            continue;
        }

        if (!msc->staged_valid || msc->staged_lba != lba) {
            /* A partial sector write needs the old contents first. */
            rc = helikon_block_read(msc->device, msc->base_lba + lba, 1u,
                                    msc->staged, msc->timeout_ms);
            if (rc != HELIKON_OK) {
                sense_from_error(msc, rc, 1);
                return rc;
            }
            msc->staged_lba = lba;
        }
        for (uint32_t index = 0u; index < take; ++index)
            msc->staged[within + index] = in[done + index];
        done += take;

        if (within + take == msc->sector_size) {
            rc = helikon_block_write(msc->device, msc->base_lba + lba, 1u,
                                     msc->staged, msc->timeout_ms);
            if (rc != HELIKON_OK) {
                sense_from_error(msc, rc, 1);
                return rc;
            }
            msc->staged_valid = 0;
        } else {
            msc->staged_valid = 1;
        }
    }
    return HELIKON_OK;
}

/* Ends the data stage: flush a half-written sector and answer the command. */
static int finish_command(struct helikon_usb_msc *msc)
{
    if (msc->direction < 0 && msc->staged_valid) {
        int rc = helikon_block_write(msc->device,
                                     msc->base_lba + msc->staged_lba, 1u,
                                     msc->staged, msc->timeout_ms);
        msc->staged_valid = 0;
        if (rc != HELIKON_OK) {
            sense_from_error(msc, rc, 1);
            return rc;
        }
    }
    return HELIKON_OK;
}

int helikon_usb_msc_command(struct helikon_usb_msc *msc,
                            const uint8_t cbw[HELIKON_USB_CBW_SIZE],
                            struct helikon_usb_msc_result *result)
{
    if (msc == NULL || cbw == NULL || result == NULL)
        return HELIKON_ERR_ARGUMENT;

    result->direction = 0;
    result->length = 0u;
    result->failed = 0;

    /* CBW: signature, tag, transfer length, flags, LUN, CDB length. */
    uint32_t signature = (uint32_t)cbw[0] | ((uint32_t)cbw[1] << 8) |
                         ((uint32_t)cbw[2] << 16) | ((uint32_t)cbw[3] << 24);
    if (signature != HELIKON_USB_CBW_SIGNATURE)
        return HELIKON_ERR_FORMAT;

    uint8_t cdb_length = cbw[14] & 0x1fu;
    if (cdb_length == 0u || cdb_length > 16u)
        return HELIKON_ERR_FORMAT;
    /* Byte 12 bit 7 is the direction; the rest of 12, the high LUN bits and the
     * high CBWCB-length bits are reserved and must be zero. */
    if ((cbw[12] & 0x7fu) != 0u || (cbw[13] & 0xf0u) != 0u ||
        (cbw[14] & 0xe0u) != 0u)
        return HELIKON_ERR_FORMAT;

    begin_command(msc);
    msc->tag = (uint32_t)cbw[4] | ((uint32_t)cbw[5] << 8) |
               ((uint32_t)cbw[6] << 16) | ((uint32_t)cbw[7] << 24);
    msc->transfer_length = (uint32_t)cbw[8] | ((uint32_t)cbw[9] << 8) |
                           ((uint32_t)cbw[10] << 16) |
                           ((uint32_t)cbw[11] << 24);
    int in = (cbw[12] & 0x80u) != 0u;
    uint8_t lun = cbw[13] & 0x0fu;
    if (lun != 0u) {
        sense_set(msc, HELIKON_USB_SENSE_ILLEGAL_REQUEST, 0x25u, 0u);
        result->failed = 1;
        return HELIKON_OK;
    }

    for (uint8_t index = 0u; index < cdb_length; ++index)
        msc->cdb[index] = cbw[15u + index];
    msc->cdb_length = cdb_length;

    uint8_t opcode = msc->cdb[0];
    switch (opcode) {
    case SCSI_TEST_UNIT_READY:
    case SCSI_START_STOP_UNIT:
    case SCSI_PREVENT_ALLOW:
    case SCSI_SYNCHRONIZE_CACHE_10:
    case SCSI_SYNCHRONIZE_CACHE_16: {
        if (msc->device == NULL) {
            sense_set(msc, HELIKON_USB_SENSE_NOT_READY,
                      SENSE_MEDIUM_NOT_PRESENT, 0u);
            break;
        }
        if (opcode == SCSI_SYNCHRONIZE_CACHE_10 ||
            opcode == SCSI_SYNCHRONIZE_CACHE_16) {
            int rc = helikon_block_flush(msc->device, msc->timeout_ms);
            sense_from_error(msc, rc, 1);
        }
        if (opcode == SCSI_START_STOP_UNIT) {
            /* LoEj (bit 1) with Start (bit 0) clear is an eject request. The
             * medium is flushed before Disk Mode lets go of the window. */
            int eject = (msc->cdb[4] & 0x02u) != 0u &&
                        (msc->cdb[4] & 0x01u) == 0u;
            if (eject) {
                msc->eject_requested = 1;
                int rc = helikon_block_flush(msc->device, msc->timeout_ms);
                sense_from_error(msc, rc, 1);
            }
        }
        break;
    }
    case SCSI_REQUEST_SENSE:
        request_sense(msc);
        msc->direction = 1;
        break;
    case SCSI_INQUIRY:
        inquiry(msc);
        msc->direction = 1;
        break;
    case SCSI_READ_CAPACITY_10:
        read_capacity_10(msc);
        msc->direction = 1;
        break;
    case SCSI_READ_FORMAT_CAPACITIES:
        read_format_capacities(msc);
        msc->direction = 1;
        break;
    case SCSI_MODE_SENSE_6:
        mode_sense_6(msc);
        msc->direction = 1;
        break;
    case SCSI_MODE_SENSE_10:
        mode_sense_10(msc);
        msc->direction = 1;
        break;
    case SCSI_SERVICE_ACTION_IN_16:
        if ((msc->cdb[1] & 0x1fu) != SCSI_SA_READ_CAPACITY_16) {
            sense_set(msc, HELIKON_USB_SENSE_ILLEGAL_REQUEST,
                      SENSE_INVALID_COMMAND, 0u);
            break;
        }
        read_capacity_16(msc);
        msc->direction = 1;
        break;
    case SCSI_READ_10:
    case SCSI_VERIFY_10:
    case SCSI_WRITE_10:
        if (parse_rw10(msc, msc->cdb) != HELIKON_OK || check_range(msc) !=
                HELIKON_OK)
            break;
        if (opcode == SCSI_WRITE_10 && !writable(msc)) {
            sense_set(msc, HELIKON_USB_SENSE_DATA_PROTECT,
                      SENSE_WRITE_PROTECTED, 0u);
            break;
        }
        if (opcode == SCSI_VERIFY_10)
            break; /* the bytes are on the medium already */
        msc->direction = opcode == SCSI_READ_10 ? 1 : -1;
        break;
    case SCSI_READ_16:
    case SCSI_WRITE_16:
        if (parse_rw16(msc, msc->cdb) != HELIKON_OK || check_range(msc) !=
                HELIKON_OK)
            break;
        if (opcode == SCSI_WRITE_16 && !writable(msc)) {
            sense_set(msc, HELIKON_USB_SENSE_DATA_PROTECT,
                      SENSE_WRITE_PROTECTED, 0u);
            break;
        }
        msc->direction = opcode == SCSI_READ_16 ? 1 : -1;
        break;
    case SCSI_FORMAT_UNIT:
        /*
         * The firmware never formats. A host formats by writing a partition map
         * and a filesystem through this LUN, which keeps every byte visible;
         * FORMAT UNIT would hide the decision inside the device.
         */
        sense_set(msc, HELIKON_USB_SENSE_ILLEGAL_REQUEST, SENSE_INVALID_COMMAND,
                  0u);
        break;
    default:
        sense_set(msc, HELIKON_USB_SENSE_ILLEGAL_REQUEST, SENSE_INVALID_COMMAND,
                  0u);
        break;
    }

    /*
     * The CBW direction flag must agree with the command. A mismatch is a
     * transport-level error, not a bad disk: answer PHASE ERROR so the host
     * resets the pipe instead of retrying the same broken sequence.
     */
    if (!msc->failed && msc->direction != 0 &&
        (in ? 1 : -1) != msc->direction) {
        msc->phase_error = 1;
        sense_set(msc, HELIKON_USB_SENSE_ILLEGAL_REQUEST, SENSE_COMMAND_SEQUENCE,
                  0u);
    }

    if (!msc->failed && msc->direction != 0) {
        /*
         * Fixed responses (INQUIRY, CAPACITY, SENSE, MODE SENSE) state their
         * length in `in_length`. A READ command states none: its data stage is
         * the window, `blocks` sectors of it, so it must not be capped to zero.
         */
        uint32_t available = 0u;
        if (msc->direction > 0 && msc->in_length != 0u) {
            available = msc->in_length;
        } else {
            uint64_t bytes = (uint64_t)msc->blocks * msc->sector_size;
            available = bytes > 0xffffffffull ? 0xffffffffu : (uint32_t)bytes;
        }
        uint32_t length = msc->transfer_length;
        if (length > available)
            length = available;
        msc->transfer_length = length;
        result->direction = msc->direction;
        result->length = length;
    }
    result->failed = msc->failed;
    return HELIKON_OK;
}

int helikon_usb_msc_in(struct helikon_usb_msc *msc, void *buffer, uint32_t max,
                       uint32_t *length)
{
    if (msc == NULL || length == NULL || (buffer == NULL && max != 0u))
        return HELIKON_ERR_ARGUMENT;
    *length = 0u;
    if (msc->direction <= 0 || msc->transferred >= msc->transfer_length)
        return HELIKON_OK;

    uint32_t want = msc->transfer_length - msc->transferred;
    if (want > max)
        want = max;
    if (want == 0u)
        return HELIKON_OK;

    int rc = HELIKON_OK;
    if (msc->in_length != 0u) {
        /* Small response held in the MSC state. */
        for (uint32_t index = 0u; index < want; ++index)
            ((uint8_t *)buffer)[index] = msc->in_buffer[msc->transferred + index];
        rc = HELIKON_OK;
    } else {
        rc = read_data(msc, buffer, want);
    }
    if (rc != HELIKON_OK)
        return rc;

    msc->transferred += want;
    *length = want;
    return HELIKON_OK;
}

int helikon_usb_msc_out(struct helikon_usb_msc *msc, const void *buffer,
                        uint32_t length, uint32_t *consumed)
{
    if (msc == NULL || consumed == NULL || (buffer == NULL && length != 0u))
        return HELIKON_ERR_ARGUMENT;
    *consumed = 0u;
    if (msc->direction >= 0 || msc->transferred >= msc->transfer_length)
        return HELIKON_OK;

    uint32_t want = msc->transfer_length - msc->transferred;
    if (length < want)
        want = length;
    if (want == 0u)
        return HELIKON_OK;

    int rc = write_data(msc, buffer, want);
    if (rc != HELIKON_OK)
        return rc;


    msc->transferred += want;
    *consumed = want;
    return HELIKON_OK;
}

int helikon_usb_msc_take_eject(struct helikon_usb_msc *msc)
{
    if (msc == NULL || !msc->eject_requested)
        return 0;
    msc->eject_requested = 0;
    return 1;
}

void helikon_usb_msc_status(struct helikon_usb_msc *msc,
                            uint8_t csw[HELIKON_USB_CSW_SIZE])
{
    if (msc == NULL || csw == NULL)
        return;

    (void)finish_command(msc);
    put_le32(csw, HELIKON_USB_CSW_SIGNATURE);
    put_le32(csw + 4, msc->tag);
    uint32_t residue = 0u;
    if (msc->failed) {
        residue = msc->transfer_length;
    } else if (msc->transferred < msc->transfer_length) {
        residue = msc->transfer_length - msc->transferred;
    }
    put_le32(csw + 8, residue);
    csw[12] = msc->phase_error
                  ? HELIKON_USB_CSW_PHASE_ERROR
                  : (msc->failed ? HELIKON_USB_CSW_FAILED : HELIKON_USB_CSW_PASSED);
}

void helikon_usb_msc_phase_error_csw(const uint8_t cbw[HELIKON_USB_CBW_SIZE],
                                     uint8_t csw[HELIKON_USB_CSW_SIZE])
{
    if (csw == NULL)
        return;
    uint32_t tag = 0u;
    if (cbw != NULL) {
        tag = (uint32_t)cbw[4] | ((uint32_t)cbw[5] << 8) |
              ((uint32_t)cbw[6] << 16) | ((uint32_t)cbw[7] << 24);
    }
    put_le32(csw, HELIKON_USB_CSW_SIGNATURE);
    put_le32(csw + 4, tag);
    put_le32(csw + 8, 0u);
    csw[12] = HELIKON_USB_CSW_PHASE_ERROR;
}

const struct helikon_usb_sense *helikon_usb_msc_sense(
    const struct helikon_usb_msc *msc)
{
    return msc == NULL ? NULL : &msc->sense;
}
