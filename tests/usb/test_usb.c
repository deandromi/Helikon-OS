/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host tests for the USB device core: descriptors, chapter 9 control requests and
 * the bulk-only mass-storage path, driven exactly as a laptop would drive it.
 *
 * The block device is a sparse fake with a 4 TB capacity, so one test can prove
 * that a host write at an LBA above 2^28 lands where it should — the byte-level
 * claim behind "format the card above 128 GiB" — while another proves that READ
 * CAPACITY(10) saturates past 2^32 blocks and READ CAPACITY(16) states the truth.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "block.h"
#include "helikon_error.h"
#include "usb_control.h"
#include "usb_descriptors.h"
#include "usb_msc.h"

static unsigned int failures;

static int expect(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        failures += 1u;
        return 1;
    }
    return 0;
}

/* --- fake disk ------------------------------------------------------------ */

#define FAKE_SECTORS 8192u
#define FAKE_SECTOR_SIZE 512u
#define FAKE_CAPACITY_SECTORS 7812500000ull /* 4 TB */

struct fake_disk {
    uint8_t data[FAKE_SECTORS * FAKE_SECTOR_SIZE];
    uint64_t fail_lba;
    int fail;
    unsigned int reads;
    unsigned int writes;
    unsigned int flushes;
    uint64_t last_write_lba;
};

/*
 * The staging area is small but the address space is not: sector n lives at
 * n % FAKE_SECTORS. The test only ever writes a handful of distant sectors, so
 * this keeps the fake honest for any 64-bit LBA without a 4 TB array.
 */
static uint8_t *fake_slot(struct fake_disk *disk, uint64_t lba)
{
    return disk->data + (size_t)(lba % FAKE_SECTORS) * FAKE_SECTOR_SIZE;
}

static int fake_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    uint8_t *out = buffer;
    if (disk->fail && lba >= disk->fail_lba)
        return HELIKON_ERR_IO;
    if (count == 0u || lba >= FAKE_CAPACITY_SECTORS ||
        count > FAKE_CAPACITY_SECTORS - lba)
        return HELIKON_ERR_ARGUMENT;
    disk->reads += 1u;
    for (uint32_t index = 0u; index < count; ++index)
        memcpy(out + (size_t)index * FAKE_SECTOR_SIZE,
               fake_slot(disk, lba + index), FAKE_SECTOR_SIZE);
    return HELIKON_OK;
}

static int fake_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    const uint8_t *in = buffer;
    if (disk->fail && lba >= disk->fail_lba)
        return HELIKON_ERR_IO;
    if (count == 0u || lba >= FAKE_CAPACITY_SECTORS ||
        count > FAKE_CAPACITY_SECTORS - lba)
        return HELIKON_ERR_ARGUMENT;
    disk->writes += 1u;
    disk->last_write_lba = lba;
    for (uint32_t index = 0u; index < count; ++index)
        memcpy(fake_slot(disk, lba + index),
               in + (size_t)index * FAKE_SECTOR_SIZE, FAKE_SECTOR_SIZE);
    return HELIKON_OK;
}

static int fake_flush(void *context, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    disk->flushes += 1u;
    return HELIKON_OK;
}

static int fake_power(void *context, int on, uint32_t timeout_ms)
{
    (void)context;
    (void)on;
    (void)timeout_ms;
    return HELIKON_OK;
}

static const struct helikon_block_ops fake_ops = {
    .read = fake_read, .write = fake_write, .flush = fake_flush,
    .power = fake_power
};

/* --- descriptors ---------------------------------------------------------- */

static const struct helikon_usb_identity identity = {
    "Helikon", "Helikon Disk Mode", "HLK-00000001"
};

static int check_descriptors(void)
{
    uint8_t buffer[HELIKON_USB_STRING_DESCRIPTOR_MAX];

    if (expect(helikon_usb_descriptor_device(buffer, sizeof(buffer)) ==
                   HELIKON_USB_DEVICE_DESCRIPTOR_SIZE,
               "device descriptor length"))
        return 1;
    if (expect(buffer[0] == 18u && buffer[1] == 0x01u, "device header"))
        return 1;
    if (expect(buffer[7] == 64u, "EP0 max packet"))
        return 1;
    if (expect((uint16_t)(buffer[8] | (buffer[9] << 8)) == 0x1209u,
               "vendor id"))
        return 1;
    if (expect(buffer[17] == 1u, "one configuration"))
        return 1;

    if (expect(helikon_usb_descriptor_config(buffer, sizeof(buffer)) ==
                   HELIKON_USB_CONFIG_DESCRIPTOR_SIZE,
               "config descriptor length"))
        return 1;
    if (expect(buffer[1] == 0x02u && buffer[4] == 1u, "config header"))
        return 1;
    if (expect((uint16_t)(buffer[2] | (buffer[3] << 8)) == 32u,
               "config total length"))
        return 1;
    if (expect(buffer[14] == 0x08u && buffer[15] == 0x06u &&
                   buffer[16] == 0x50u,
               "mass storage, SCSI, bulk-only"))
        return 1;
    if (expect(buffer[20] == HELIKON_USB_EP_BULK_IN &&
                   (uint16_t)(buffer[22] | (buffer[23] << 8)) == 512u,
               "bulk IN endpoint"))
        return 1;
    if (expect(buffer[27] == HELIKON_USB_EP_BULK_OUT &&
                   (uint16_t)(buffer[29] | (buffer[30] << 8)) == 512u,
               "bulk OUT endpoint"))
        return 1;

    /* A short buffer is refused, never overrun. */
    if (expect(helikon_usb_descriptor_device(buffer, 10u) == 0u,
               "short device buffer refused"))
        return 1;
    if (expect(helikon_usb_descriptor_config(buffer, 31u) == 0u,
               "short config buffer refused"))
        return 1;

    if (expect(helikon_usb_descriptor_string(0u, &identity, buffer,
                                             sizeof(buffer)) == 4u,
               "language descriptor"))
        return 1;
    if (expect(buffer[2] == 0x09u && buffer[3] == 0x04u, "US English"))
        return 1;

    size_t length = helikon_usb_descriptor_string(
        HELIKON_USB_STRING_PRODUCT, &identity, buffer, sizeof(buffer));
    if (expect(length == 2u + 2u * strlen(identity.product),
               "product string length"))
        return 1;
    if (expect(buffer[0] == (uint8_t)length && buffer[1] == 0x03u,
               "string header"))
        return 1;
    if (expect(buffer[2] == 'H' && buffer[3] == 0u, "UTF-16LE encoding"))
        return 1;
    if (expect(helikon_usb_descriptor_string(9u, &identity, buffer,
                                             sizeof(buffer)) == 0u,
               "unknown string index refused"))
        return 1;
    return 0;
}

/* --- control -------------------------------------------------------------- */

static void setup(struct helikon_usb_setup *request, uint8_t type, uint8_t code,
                  uint16_t value, uint16_t index, uint16_t length)
{
    request->request_type = type;
    request->request = code;
    request->value = value;
    request->index = index;
    request->length = length;
}

static int check_control(void)
{
    struct helikon_usb_control control;
    struct helikon_usb_setup request;
    struct helikon_usb_control_result result;

    helikon_usb_control_reset(&control, &identity);

    /* GET_DESCRIPTOR(device) returns 18 bytes. */
    setup(&request, 0x80u, HELIKON_USB_REQ_GET_DESCRIPTOR,
          (uint16_t)(HELIKON_USB_DESCRIPTOR_DEVICE << 8), 0u, 64u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_IN && result.length == 18u,
               "GET_DESCRIPTOR device"))
        return 1;
    if (expect(result.data != NULL && result.data[1] == 0x01u,
               "device descriptor data"))
        return 1;

    /* wLength truncates, it does not overrun. */
    setup(&request, 0x80u, HELIKON_USB_REQ_GET_DESCRIPTOR,
          (uint16_t)(HELIKON_USB_DESCRIPTOR_DEVICE << 8), 0u, 8u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.length == 8u, "wLength truncates the descriptor"))
        return 1;

    setup(&request, 0x80u, HELIKON_USB_REQ_GET_DESCRIPTOR,
          (uint16_t)(HELIKON_USB_DESCRIPTOR_CONFIG << 8), 0u, 9u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_IN && result.length == 9u,
               "config header first"))
        return 1;

    setup(&request, 0x80u, HELIKON_USB_REQ_GET_DESCRIPTOR,
          (uint16_t)(HELIKON_USB_DESCRIPTOR_STRING << 8) |
              HELIKON_USB_STRING_MANUFACTURER,
          0u, 255u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_IN && result.length > 2u,
               "string descriptor"))
        return 1;

    /* Unsupported descriptor types are stalled, not guessed. */
    setup(&request, 0x80u, HELIKON_USB_REQ_GET_DESCRIPTOR, 0x0600u, 0u, 10u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_STALL,
               "device qualifier stalled"))
        return 1;
    setup(&request, 0x80u, HELIKON_USB_REQ_GET_DESCRIPTOR, 0x0f00u, 0u, 10u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_STALL, "BOS stalled"))
        return 1;

    /* SET_ADDRESS, then GET_CONFIGURATION. */
    setup(&request, 0x00u, HELIKON_USB_REQ_SET_ADDRESS, 7u, 0u, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_NO_DATA && control.address == 7u,
               "SET_ADDRESS"))
        return 1;
    setup(&request, 0x00u, HELIKON_USB_REQ_SET_ADDRESS, 200u, 0u, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_STALL &&
                   control.address == 7u,
               "an address above 127 is refused"))
        return 1;

    setup(&request, 0x80u, HELIKON_USB_REQ_GET_CONFIGURATION, 0u, 0u, 1u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_IN && result.length == 1u &&
                   result.data[0] == 0u,
               "unconfigured GET_CONFIGURATION"))
        return 1;

    setup(&request, 0x00u, HELIKON_USB_REQ_SET_CONFIGURATION, 1u, 0u, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_NO_DATA &&
                   control.configuration == 1u,
               "SET_CONFIGURATION 1"))
        return 1;
    setup(&request, 0x00u, HELIKON_USB_REQ_SET_CONFIGURATION, 2u, 0u, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_STALL,
               "a second configuration is refused"))
        return 1;

    /* Endpoint halt: SET_FEATURE, GET_STATUS, CLEAR_FEATURE. */
    setup(&request, 0x02u, HELIKON_USB_REQ_SET_FEATURE, 0u,
          HELIKON_USB_EP_BULK_IN, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_NO_DATA,
               "SET_FEATURE halt"))
        return 1;
    setup(&request, 0x82u, HELIKON_USB_REQ_GET_STATUS, 0u,
          HELIKON_USB_EP_BULK_IN, 2u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_IN && result.length == 2u &&
                   result.data[0] == 0x01u,
               "halted endpoint reports it"))
        return 1;
    setup(&request, 0x02u, HELIKON_USB_REQ_CLEAR_FEATURE, 0u,
          HELIKON_USB_EP_BULK_IN, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    setup(&request, 0x82u, HELIKON_USB_REQ_GET_STATUS, 0u,
          HELIKON_USB_EP_BULK_IN, 2u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.data[0] == 0x00u, "CLEAR_FEATURE clears the halt"))
        return 1;

    /* MSC class requests. */
    setup(&request, 0xa1u, HELIKON_USB_MSC_GET_MAX_LUN, 0u, 0u, 1u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_IN && result.length == 1u &&
                   result.data[0] == 0u,
               "GET_MAX_LUN reports one LUN"))
        return 1;
    setup(&request, 0x21u, HELIKON_USB_MSC_BULK_ONLY_RESET, 0u, 0u, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_NO_DATA &&
                   control.bulk_only_reset == 1,
               "BULK_ONLY_RESET is recorded"))
        return 1;

    /* Unknown and malformed requests stall. */
    setup(&request, 0x40u, 0x01u, 0u, 0u, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_STALL,
               "vendor request stalled"))
        return 1;
    setup(&request, 0x00u, HELIKON_USB_REQ_SET_DESCRIPTOR, 0u, 0u, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_STALL,
               "SET_DESCRIPTOR stalled"))
        return 1;
    setup(&request, 0x00u, HELIKON_USB_REQ_SET_FEATURE, 1u, 0u, 0u);
    helikon_usb_control_setup(&control, &request, &result);
    if (expect(result.action == HELIKON_USB_CONTROL_STALL,
               "remote wakeup is not advertised"))
        return 1;
    return 0;
}

/* --- MSC/SCSI ------------------------------------------------------------- */

static void build_cbw(uint8_t cbw[HELIKON_USB_CBW_SIZE], uint32_t tag,
                      uint32_t transfer, int in, uint8_t lun,
                      const uint8_t *cdb, uint8_t cdb_length)
{
    memset(cbw, 0, HELIKON_USB_CBW_SIZE);
    cbw[0] = 0x55u;
    cbw[1] = 0x53u;
    cbw[2] = 0x42u;
    cbw[3] = 0x43u;
    cbw[4] = (uint8_t)tag;
    cbw[5] = (uint8_t)(tag >> 8);
    cbw[6] = (uint8_t)(tag >> 16);
    cbw[7] = (uint8_t)(tag >> 24);
    cbw[8] = (uint8_t)transfer;
    cbw[9] = (uint8_t)(transfer >> 8);
    cbw[10] = (uint8_t)(transfer >> 16);
    cbw[11] = (uint8_t)(transfer >> 24);
    cbw[12] = in ? 0x80u : 0x00u;
    cbw[13] = lun;
    cbw[14] = cdb_length;
    memcpy(cbw + 15, cdb, cdb_length);
}

/* Runs one command end to end and returns the CSW status byte. */
static int run_command(struct helikon_usb_msc *msc, uint32_t tag,
                       const uint8_t *cdb, uint8_t cdb_length, int in,
                       uint32_t transfer, const uint8_t *out_data,
                       uint8_t *in_data, uint32_t in_capacity,
                       uint32_t *in_length, uint8_t *csw_status)
{
    uint8_t cbw[HELIKON_USB_CBW_SIZE];
    struct helikon_usb_msc_result result;
    build_cbw(cbw, tag, transfer, in, 0u, cdb, cdb_length);
    if (helikon_usb_msc_command(msc, cbw, &result) != HELIKON_OK)
        return -1;

    uint32_t done = 0u;
    if (result.direction > 0) {
        while (done < result.length) {
            uint32_t got = 0u;
            uint32_t want = result.length - done;
            if (want > 64u)
                want = 64u; /* a 512-byte endpoint, exercised in small pieces */
            if (helikon_usb_msc_in(msc, in_data + done, want, &got) != HELIKON_OK)
                return -1;
            if (got == 0u)
                break;
            done += got;
        }
    } else if (result.direction < 0) {
        while (done < result.length) {
            uint32_t want = result.length - done;
            if (want > 64u)
                want = 64u;
            uint32_t used = 0u;
            if (helikon_usb_msc_out(msc, out_data + done, want, &used) !=
                HELIKON_OK)
                return -1;
            if (used == 0u)
                break;
            done += used;
        }
    }

    uint8_t csw[HELIKON_USB_CSW_SIZE];
    helikon_usb_msc_status(msc, csw);
    if (in_length != NULL)
        *in_length = done;
    *csw_status = csw[12];
    /* An IN command must not have exceeded the caller's buffer; an OUT command
     * has no input buffer at all, so `in_capacity` says nothing about it. */
    if (in && in_capacity < done)
        return -1;
    return 0;
}

static int check_msc(struct fake_disk *disk)
{
    struct helikon_block_info info = {
        .sector_size = FAKE_SECTOR_SIZE,
        .sector_count = FAKE_CAPACITY_SECTORS,
        .flags = 0u /* writable, the Disk Mode restore window */
    };
    struct helikon_block_device device;
    if (expect(helikon_block_init(&device, &fake_ops, disk, &info) == HELIKON_OK,
               "block device init"))
        return 1;

    /* The whole disk: this is the window a format writes through. */
    struct helikon_usb_msc msc;
    if (expect(helikon_usb_msc_init(&msc, &device, 0u, 0u, 100u) == HELIKON_OK,
               "msc init over the whole disk"))
        return 1;
    if (expect(msc.sector_count == FAKE_CAPACITY_SECTORS, "whole-disk window"))
        return 1;

    uint8_t sense[18];
    uint8_t csw = 0xffu;
    uint8_t in[512];
    uint32_t in_length = 0u;

    /* INQUIRY. */
    uint8_t inquiry_cdb[6] = { 0x12u, 0, 0, 0, 36u, 0 };
    if (expect(run_command(&msc, 1u, inquiry_cdb, 6u, 1, 36u, NULL, in,
                           sizeof(in), &in_length, &csw) == 0,
               "inquiry runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_PASSED && in_length == 36u,
               "inquiry passes"))
        return 1;
    if (expect(memcmp(in + 8, "Helikon", 7u) == 0, "inquiry vendor"))
        return 1;
    if (expect((in[1] & 0x80u) != 0u, "removable medium"))
        return 1;

    /* READ CAPACITY(10) must refuse to state a 4 TB window in 32 bits. */
    uint8_t capacity_cdb[10] = { 0x25u, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (expect(run_command(&msc, 2u, capacity_cdb, 10u, 1, 8u, NULL, in,
                           sizeof(in), &in_length, &csw) == 0,
               "capacity10 runs"))
        return 1;
    uint32_t last = ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
                    ((uint32_t)in[2] << 8) | in[3];
    if (expect(csw == HELIKON_USB_CSW_PASSED && last == 0xffffffffu,
               "capacity10 saturates above 2^32 blocks"))
        return 1;
    uint32_t block = ((uint32_t)in[4] << 24) | ((uint32_t)in[5] << 16) |
                     ((uint32_t)in[6] << 8) | in[7];
    if (expect(block == FAKE_SECTOR_SIZE, "capacity10 block size"))
        return 1;

    /* READ CAPACITY(16) states the real last LBA. */
    uint8_t capacity16_cdb[16] = { 0x9eu, 0x10u, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 32u, 0, 0 };
    if (expect(run_command(&msc, 3u, capacity16_cdb, 16u, 1, 32u, NULL, in,
                           sizeof(in), &in_length, &csw) == 0,
               "capacity16 runs"))
        return 1;
    uint64_t last16 = 0u;
    for (int index = 0; index < 8; ++index)
        last16 = (last16 << 8) | in[index];
    if (expect(csw == HELIKON_USB_CSW_PASSED &&
                   last16 == FAKE_CAPACITY_SECTORS - 1u,
               "capacity16 states 4 TB"))
        return 1;

    /* Write one sector above the old LBA28 limit, then read it back. */
    const uint32_t high_lba = 0x10000004u; /* 4 GiB above 128 GiB */
    uint8_t pattern[FAKE_SECTOR_SIZE];
    for (size_t index = 0u; index < sizeof(pattern); ++index)
        pattern[index] = (uint8_t)(index * 7u + 3u);

    uint8_t write_cdb[10] = { 0x2au, 0, 0, 0, 0, 0, 0, 0, 1u, 0 };
    write_cdb[2] = (uint8_t)(high_lba >> 24);
    write_cdb[3] = (uint8_t)(high_lba >> 16);
    write_cdb[4] = (uint8_t)(high_lba >> 8);
    write_cdb[5] = (uint8_t)high_lba;
    if (expect(run_command(&msc, 4u, write_cdb, 10u, 0, FAKE_SECTOR_SIZE,
                           pattern, NULL, 0u, NULL, &csw) == 0,
               "write10 runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_PASSED, "write10 passes"))
        return 1;
    if (expect(disk->last_write_lba == high_lba, "write10 reached the high LBA"))
        return 1;
    if (expect(memcmp(disk->data, pattern, sizeof(pattern)) != 0,
               "the write did not land at LBA 0"))
        return 1;

    uint8_t read_cdb[10] = { 0x28u, 0, 0, 0, 0, 0, 0, 0, 1u, 0 };
    memcpy(read_cdb + 2, write_cdb + 2, 4u);
    memset(in, 0, sizeof(in));
    if (expect(run_command(&msc, 5u, read_cdb, 10u, 1, FAKE_SECTOR_SIZE, NULL,
                           in, sizeof(in), &in_length, &csw) == 0,
               "read10 runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_PASSED && in_length == FAKE_SECTOR_SIZE,
               "read10 passes"))
        return 1;
    if (expect(memcmp(in, pattern, sizeof(pattern)) == 0,
               "read10 returns what write10 stored"))
        return 1;

    /* A write across several sectors in small pieces exercises staging. */
    uint8_t big[4u * FAKE_SECTOR_SIZE];
    for (size_t index = 0u; index < sizeof(big); ++index)
        big[index] = (uint8_t)(index ^ 0x5au);
    uint8_t multi[10] = { 0x2au, 0, 0, 0, 0, 0, 0, 0, 4u, 0 };
    multi[2] = (uint8_t)(high_lba >> 24);
    multi[3] = (uint8_t)(high_lba >> 16);
    multi[4] = (uint8_t)(high_lba >> 8);
    multi[5] = (uint8_t)(high_lba + 1u);
    if (expect(run_command(&msc, 6u, multi, 10u, 0, sizeof(big), big, NULL, 0u,
                           NULL, &csw) == 0,
               "multi-sector write runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_PASSED, "multi-sector write passes"))
        return 1;

    /*
     * Out of range: CHECK CONDITION with LBA OUT OF RANGE. The 4 TB capacity
     * leaves no 32-bit LBA outside the window, so this one needs READ(16): the
     * address is 2^33, three hundred million sectors past the last real one.
     */
    uint8_t bad[16] = { 0x88u, 0, 0, 0, 0, 2u, 0, 0, 0, 0, 0, 0, 0, 8u, 0, 0 };
    if (expect(run_command(&msc, 7u, bad, 16u, 1, 8u * FAKE_SECTOR_SIZE, NULL,
                           in, sizeof(in), &in_length, &csw) == 0,
               "out-of-range read runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_FAILED, "out-of-range read fails"))
        return 1;
    if (expect(msc.sense.key == HELIKON_USB_SENSE_ILLEGAL_REQUEST &&
                   msc.sense.asc == 0x21u,
               "sense reports LBA out of range"))
        return 1;

    /* REQUEST SENSE returns that sense data. */
    uint8_t sense_cdb[6] = { 0x03u, 0, 0, 0, 18u, 0 };
    memset(sense, 0, sizeof(sense));
    if (expect(run_command(&msc, 8u, sense_cdb, 6u, 1, 18u, NULL, sense,
                           sizeof(sense), &in_length, &csw) == 0,
               "request sense runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_PASSED && sense[2] == 0x05u &&
                   sense[12] == 0x21u,
               "request sense returns the illegal request"))
        return 1;

    /* A read-only window refuses WRITE with DATA PROTECT. */
    struct helikon_block_info ro_info = info;
    ro_info.flags = HELIKON_BLOCK_FLAG_READ_ONLY;
    struct helikon_block_device ro_device;
    if (expect(helikon_block_init(&ro_device, &fake_ops, disk, &ro_info) ==
                   HELIKON_OK,
               "read-only device init"))
        return 1;
    struct helikon_usb_msc ro;
    if (expect(helikon_usb_msc_init(&ro, &ro_device, 0u, 0u, 100u) == HELIKON_OK,
               "read-only msc"))
        return 1;
    if (expect(run_command(&ro, 9u, write_cdb, 10u, 0, FAKE_SECTOR_SIZE,
                           pattern, NULL, 0u, NULL, &csw) == 0,
               "read-only write runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_FAILED &&
                   ro.sense.key == HELIKON_USB_SENSE_DATA_PROTECT,
               "a read-only window reports DATA PROTECT"))
        return 1;

    /* FORMAT UNIT is refused: the host formats by writing, not by asking us. */
    uint8_t format_cdb[6] = { 0x04u, 0, 0, 0, 0, 0 };
    if (expect(run_command(&msc, 10u, format_cdb, 6u, 0, 0u, NULL, NULL, 0u,
                           NULL, &csw) == 0,
               "format unit runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_FAILED &&
                   msc.sense.key == HELIKON_USB_SENSE_ILLEGAL_REQUEST,
               "FORMAT UNIT is refused"))
        return 1;

    /* SYNCHRONIZE CACHE flushes the medium. */
    unsigned int flushes = disk->flushes;
    uint8_t sync_cdb[10] = { 0x35u, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (expect(run_command(&msc, 11u, sync_cdb, 10u, 0, 0u, NULL, NULL, 0u,
                           NULL, &csw) == 0,
               "synchronize cache runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_PASSED && disk->flushes == flushes + 1u,
               "synchronize cache flushes"))
        return 1;

    /* A malformed CBW is a protocol error the transport resets. */
    uint8_t cbw[HELIKON_USB_CBW_SIZE];
    struct helikon_usb_msc_result result;
    build_cbw(cbw, 12u, 0u, 0, 0u, inquiry_cdb, 6u);
    cbw[0] = 0u;
    if (expect(helikon_usb_msc_command(&msc, cbw, &result) ==
                   HELIKON_ERR_FORMAT,
               "bad CBW signature refused"))
        return 1;
    build_cbw(cbw, 12u, 0u, 0, 0u, inquiry_cdb, 6u);
    cbw[14] = 0u;
    if (expect(helikon_usb_msc_command(&msc, cbw, &result) ==
                   HELIKON_ERR_FORMAT,
               "an empty CDB is refused"))
        return 1;
    build_cbw(cbw, 12u, 0u, 0, 1u, inquiry_cdb, 6u);
    if (expect(helikon_usb_msc_command(&msc, cbw, &result) == HELIKON_OK &&
                   result.failed == 1 &&
                   msc.sense.asc == 0x25u,
               "an unsupported LUN is refused"))
        return 1;

    /* A window that starts at a partition offset maps reads through it. */
    struct helikon_usb_msc window;
    if (expect(helikon_usb_msc_init(&window, &device, 1024u, 4096u, 100u) ==
                   HELIKON_OK,
               "partition window"))
        return 1;
    uint8_t window_cdb[10] = { 0x28u, 0, 0, 0, 0, 0, 0, 0, 1u, 0 };
    if (expect(run_command(&window, 13u, window_cdb, 10u, 1, FAKE_SECTOR_SIZE,
                           NULL, in, sizeof(in), &in_length, &csw) == 0,
               "window read runs"))
        return 1;
    if (expect(csw == HELIKON_USB_CSW_PASSED &&
                   memcmp(in, disk->data + 1024u * FAKE_SECTOR_SIZE,
                          FAKE_SECTOR_SIZE) == 0,
               "window maps LBA 0 to the partition offset"))
        return 1;

    /* A window that runs past the device is refused at init. */
    if (expect(helikon_usb_msc_init(&window, &device, 100u,
                                    FAKE_CAPACITY_SECTORS, 100u) ==
                   HELIKON_ERR_ARGUMENT,
               "an overlong window is refused"))
        return 1;
    return 0;
}

int main(void)
{
    static struct fake_disk disk;
    memset(&disk, 0, sizeof(disk));
    for (size_t index = 0u; index < sizeof(disk.data); ++index)
        disk.data[index] = (uint8_t)(index & 0xffu);
    disk.fail_lba = UINT64_MAX;

    if (check_descriptors() || check_control() || check_msc(&disk))
        return 1;
    if (failures != 0u) {
        printf("FAIL usb\n");
        return 1;
    }
    printf("PASS usb\n");
    return 0;
}
