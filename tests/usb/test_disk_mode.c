/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host test for Helikon Disk Mode.
 *
 * The transport is a fake ops table: it records what the mode asks EP0 and EP1
 * to send, hands EP2 buffers to the test, and reports completions. Behind it
 * sits the real MSC and control modules and a sparse 4 TB block device, so the
 * whole path a laptop would drive — enumeration, a CBW, a data stage, a CSW —
 * runs on the host.
 *
 * It proves the state machine and the gate. It does not prove a controller.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "block.h"
#include "disk_mode.h"
#include "error.h"
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

#define DISK_SECTORS 8192u
#define DISK_SECTOR_SIZE 512u
#define DISK_CAPACITY 7812500000ull /* 4 TB */

struct fake_disk {
    uint8_t data[DISK_SECTORS * DISK_SECTOR_SIZE];
    unsigned int reads;
    unsigned int writes;
    unsigned int flushes;
    uint64_t last_write_lba;
};

static uint8_t *disk_slot(struct fake_disk *disk, uint64_t lba)
{
    return disk->data + (size_t)(lba % DISK_SECTORS) * DISK_SECTOR_SIZE;
}

static int disk_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    uint8_t *out = buffer;
    if (count == 0u || lba >= DISK_CAPACITY || count > DISK_CAPACITY - lba)
        return HELIKON_ERR_ARGUMENT;
    disk->reads += 1u;
    for (uint32_t index = 0u; index < count; ++index)
        memcpy(out + (size_t)index * DISK_SECTOR_SIZE,
               disk_slot(disk, lba + index), DISK_SECTOR_SIZE);
    return HELIKON_OK;
}

static int disk_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    const uint8_t *in = buffer;
    if (count == 0u || lba >= DISK_CAPACITY || count > DISK_CAPACITY - lba)
        return HELIKON_ERR_ARGUMENT;
    disk->writes += 1u;
    disk->last_write_lba = lba;
    for (uint32_t index = 0u; index < count; ++index)
        memcpy(disk_slot(disk, lba + index),
               in + (size_t)index * DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
    return HELIKON_OK;
}

static int disk_flush(void *context, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    disk->flushes += 1u;
    return HELIKON_OK;
}

static int disk_power(void *context, int on, uint32_t timeout_ms)
{
    (void)context;
    (void)on;
    (void)timeout_ms;
    return HELIKON_OK;
}

static const struct helikon_block_ops disk_ops = {
    .read = disk_read, .write = disk_write, .flush = disk_flush,
    .power = disk_power
};

/* --- fake transport ------------------------------------------------------- */

#define QUEUE 8u

struct fake_transport {
    uint8_t ep0[256];
    uint32_t ep0_length;
    unsigned int ep0_sends;
    unsigned int ep0_stalls;

    uint8_t ep1[QUEUE][600];
    uint32_t ep1_length[QUEUE];
    unsigned int ep1_count;

    uint8_t *ep2_target;
    uint32_t ep2_length;
    unsigned int ep2_arms;

    uint8_t address;
    int configured;
    unsigned int configure_calls;
};

static struct fake_transport transport;

static void reset_transport(void)
{
    memset(&transport, 0, sizeof(transport));
}

static int tr_ep0_send(void *context, const void *data, uint32_t length)
{
    (void)context;
    if (length > sizeof(transport.ep0))
        return HELIKON_ERR_BUFFER;
    transport.ep0_length = length;
    if (length != 0u && data != NULL)
        memcpy(transport.ep0, data, length);
    transport.ep0_sends += 1u;
    return HELIKON_OK;
}

static int tr_ep0_receive(void *context, void *data, uint32_t length)
{
    (void)context;
    (void)data;
    (void)length;
    return HELIKON_OK;
}

static int tr_ep0_stall(void *context)
{
    (void)context;
    transport.ep0_stalls += 1u;
    return HELIKON_OK;
}

static int tr_ep1_send(void *context, const void *data, uint32_t length)
{
    (void)context;
    if (transport.ep1_count == QUEUE || length > sizeof(transport.ep1[0]))
        return HELIKON_ERR_BUFFER;
    memcpy(transport.ep1[transport.ep1_count], data, length);
    transport.ep1_length[transport.ep1_count] = length;
    transport.ep1_count += 1u;
    return HELIKON_OK;
}

static int tr_ep2_receive(void *context, void *data, uint32_t length)
{
    (void)context;
    transport.ep2_target = data;
    transport.ep2_length = length;
    transport.ep2_arms += 1u;
    return HELIKON_OK;
}

static int tr_set_address(void *context, uint8_t address)
{
    (void)context;
    transport.address = address;
    return HELIKON_OK;
}

static int tr_configure(void *context, int configured)
{
    (void)context;
    transport.configured = configured;
    transport.configure_calls += 1u;
    return HELIKON_OK;
}

static struct helikon_usb_device_ops transport_ops(void)
{
    struct helikon_usb_device_ops ops;
    ops.context = &transport;
    ops.ep0_send = tr_ep0_send;
    ops.ep0_receive = tr_ep0_receive;
    ops.ep0_stall = tr_ep0_stall;
    ops.ep1_send = tr_ep1_send;
    ops.ep2_receive = tr_ep2_receive;
    ops.set_address = tr_set_address;
    ops.configure = tr_configure;
    return ops;
}

/* --- helpers -------------------------------------------------------------- */

static void make_cbw(uint8_t cbw[31], uint32_t tag, uint32_t transfer, int in,
                     const uint8_t *cdb, uint8_t cdb_length)
{
    memset(cbw, 0, 31);
    cbw[0] = 'U';
    cbw[1] = 'S';
    cbw[2] = 'B';
    cbw[3] = 'C';
    cbw[4] = (uint8_t)tag;
    cbw[5] = (uint8_t)(tag >> 8);
    cbw[6] = (uint8_t)(tag >> 16);
    cbw[7] = (uint8_t)(tag >> 24);
    cbw[8] = (uint8_t)transfer;
    cbw[9] = (uint8_t)(transfer >> 8);
    cbw[10] = (uint8_t)(transfer >> 16);
    cbw[11] = (uint8_t)(transfer >> 24);
    cbw[12] = in ? 0x80u : 0x00u;
    cbw[13] = 0u;
    cbw[14] = cdb_length;
    memcpy(cbw + 15, cdb, cdb_length);
}

static int feed_setup(struct helikon_disk_mode *mode, uint8_t request_type,
                      uint8_t request, uint16_t value, uint16_t index,
                      uint16_t length)
{
    uint8_t setup[8];
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8);
    return helikon_disk_mode_event(mode, HELIKON_USB_EVENT_SETUP, setup, 0u);
}

/* Runs one command whose data stage is IN: CBW, data, CSW. Returns the CSW
 * status byte through `*status`. */
static int run_in_command(struct helikon_disk_mode *mode, uint32_t tag,
                          const uint8_t *cdb, uint8_t cdb_length,
                          uint32_t transfer, uint8_t *status)
{
    unsigned int before = transport.ep1_count;
    make_cbw(mode->cbw, tag, transfer, 1, cdb, cdb_length);
    if (helikon_disk_mode_event(mode, HELIKON_USB_EVENT_EP2_OUT_DONE, NULL,
                                HELIKON_USB_CBW_SIZE) != HELIKON_OK)
        return -1;
    /* One data slice per completion, then the CSW. */
    for (unsigned int guard = 0u; guard < 64u; ++guard) {
        if (transport.ep1_count > before) {
            uint32_t last = transport.ep1_count - 1u;
            if (transport.ep1_length[last] == HELIKON_USB_CSW_SIZE) {
                *status = transport.ep1[last][12];
                /* The transport completes the CSW transfer, which closes the
                 * command and re-arms the CBW. */
                return helikon_disk_mode_event(
                    mode, HELIKON_USB_EVENT_EP1_IN_DONE, NULL, 0u);
            }
        }
        if (transport.ep1_count == before)
            return -1; /* nothing was sent at all */
        if (helikon_disk_mode_event(mode, HELIKON_USB_EVENT_EP1_IN_DONE, NULL,
                                    0u) != HELIKON_OK)
            return -1;
    }
    return -1;
}

int main(void)
{
    struct fake_disk disk;
    memset(&disk, 0, sizeof(disk));
    for (size_t index = 0u; index < sizeof(disk.data); ++index)
        disk.data[index] = (uint8_t)(index * 5u + 1u);

    /*
     * The board hands every block device to the firmware read-only; Disk Mode
     * restore mode is the one caller that must clear that flag. Starting the
     * test from a read-only device is what makes the write below meaningful.
     */
    struct helikon_block_info info = {
        .sector_size = DISK_SECTOR_SIZE,
        .sector_count = DISK_CAPACITY,
        .flags = HELIKON_BLOCK_FLAG_READ_ONLY
    };
    struct helikon_block_device device;
    if (expect(helikon_block_init(&device, &disk_ops, &disk, &info) ==
                   HELIKON_OK,
               "block device"))
        return 1;

    struct helikon_usb_identity identity;
    memset(&identity, 0, sizeof(identity));
    identity.manufacturer = "Helikon";
    identity.product = "Helikon Disk Mode";
    identity.serial = NULL;

    struct helikon_usb_device_ops ops = transport_ops();
    reset_transport();
    struct helikon_disk_mode mode;
    memset(&mode, 0, sizeof(mode));

    /* --- the opt-in gate -------------------------------------------------- */
    struct helikon_disk_mode_options options;
    memset(&options, 0, sizeof(options));
    options.timeout_ms = 1000u;
    if (expect(helikon_disk_mode_start(&mode, &ops, &device, &identity,
                                       &options) == HELIKON_ERR_UNSUPPORTED,
               "no opt-in, no Disk Mode"))
        return 1;

    options.opt_in = 1;
    options.restore_mode = 1;
    if (expect(helikon_disk_mode_start(&mode, &ops, &device, &identity,
                                       &options) == HELIKON_OK,
               "opted-in restore mode starts"))
        return 1;
    if (expect(mode.state == HELIKON_DISK_MODE_WAITING, "waiting for a host"))
        return 1;
    if (expect(mode.msc.base_lba == 0u && mode.msc.sector_count == DISK_CAPACITY,
               "the whole disk is the window"))
        return 1;
    if (expect((mode.device.info.flags & HELIKON_BLOCK_FLAG_READ_ONLY) == 0u,
               "restore mode clears the board's read-only flag"))
        return 1;

    /* --- enumeration ------------------------------------------------------ */
    if (expect(feed_setup(&mode, 0x80u, 0x06u, 0x0100u, 0u, 18u) == HELIKON_OK,
               "GET_DESCRIPTOR(device) accepted"))
        return 1;
    if (expect(transport.ep0_sends == 1u && transport.ep0_length == 18u,
               "an 18-byte device descriptor was sent"))
        return 1;
    if (expect(transport.ep0[0] == 0x12u && transport.ep0[1] == 0x01u,
               "the descriptor bytes are the device descriptor"))
        return 1;

    if (expect(feed_setup(&mode, 0x00u, 0x05u, 9u, 0u, 0u) == HELIKON_OK,
               "SET_ADDRESS accepted"))
        return 1;
    if (expect(transport.address == 9u, "the transport got address 9"))
        return 1;

    if (expect(feed_setup(&mode, 0x00u, 0x09u, 1u, 0u, 0u) == HELIKON_OK,
               "SET_CONFIGURATION(1) accepted"))
        return 1;
    if (expect(transport.configured == 1, "the transport configured the device"))
        return 1;
    if (expect(mode.state == HELIKON_DISK_MODE_ENUMERATED,
               "the mode is enumerated"))
        return 1;
    if (expect(transport.ep2_arms == 1u &&
                   transport.ep2_length == HELIKON_USB_CBW_SIZE,
               "a 31-byte CBW receive was armed"))
        return 1;

    /* An unknown request is stalled, not guessed at. */
    if (expect(feed_setup(&mode, 0xc0u, 0x77u, 0u, 0u, 4u) == HELIKON_OK,
               "unknown request handled"))
        return 1;
    if (expect(transport.ep0_stalls == 1u, "it was stalled"))
        return 1;

    /* --- INQUIRY: data stage then CSW ------------------------------------- */
    uint8_t status = 0xffu;
    uint8_t inquiry[6] = { 0x12u, 0, 0, 0, 36u, 0 };
    unsigned int ep1_before = transport.ep1_count;
    if (expect(run_in_command(&mode, 1u, inquiry, 6u, 36u, &status) == 0,
               "INQUIRY ran"))
        return 1;
    if (expect(status == HELIKON_USB_CSW_PASSED, "INQUIRY passed"))
        return 1;
    if (expect(transport.ep1_count == ep1_before + 2u, "data then CSW"))
        return 1;
    if (expect(transport.ep1_length[ep1_before] == 36u,
               "the data stage was 36 bytes"))
        return 1;
    if (expect(memcmp(transport.ep1[ep1_before] + 8, "Helikon", 7u) == 0,
               "the data carries the vendor string"))
        return 1;
    if (expect(mode.phase == HELIKON_DISK_MODE_BOT_IDLE, "back to idle"))
        return 1;
    if (expect(transport.ep2_arms == 2u, "the next CBW was armed"))
        return 1;

    /* --- a read above 128 GiB -------------------------------------------- */
    const uint64_t high_lba = 0x10000000ull; /* 64 GiB in 512-byte sectors */
    uint8_t read_cdb[10] = { 0x28u, 0, 0, 0, 0, 0, 0, 0, 1u, 0 };
    read_cdb[2] = (uint8_t)(high_lba >> 24);
    read_cdb[3] = (uint8_t)(high_lba >> 16);
    read_cdb[4] = (uint8_t)(high_lba >> 8);
    read_cdb[5] = (uint8_t)high_lba;
    ep1_before = transport.ep1_count;
    if (expect(run_in_command(&mode, 2u, read_cdb, 10u, 512u, &status) == 0,
               "READ(10) ran"))
        return 1;
    if (expect(status == HELIKON_USB_CSW_PASSED, "READ passed"))
        return 1;
    if (expect(transport.ep1_length[ep1_before] == 512u, "512 bytes came back"))
        return 1;
    if (expect(memcmp(transport.ep1[ep1_before],
                      disk_slot(&disk, high_lba), 512u) == 0,
               "the bytes are the sector at that LBA"))
        return 1;

    /* --- a write, all the way to the fake medium ------------------------- */
    uint8_t write_cdb[10] = { 0x2au, 0, 0, 0, 0, 0, 0, 0, 1u, 0 };
    memcpy(write_cdb + 2, read_cdb + 2, 4u);
    static uint8_t payload[512];
    for (size_t index = 0u; index < sizeof(payload); ++index)
        payload[index] = (uint8_t)(index ^ 0xa5u);
    make_cbw(mode.cbw, 3u, 512u, 0, write_cdb, 10u);
    if (expect(helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_EP2_OUT_DONE,
                                       NULL, 31u) == HELIKON_OK,
               "WRITE(10) CBW accepted"))
        return 1;
    if (expect(transport.ep2_length == 512u && transport.ep2_target != NULL,
               "a 512-byte data stage was armed"))
        return 1;
    memcpy(transport.ep2_target, payload, sizeof(payload));
    if (expect(helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_EP2_OUT_DONE,
                                       NULL, 512u) == HELIKON_OK,
               "the data stage completed"))
        return 1;
    if (expect(transport.ep1_length[transport.ep1_count - 1u] ==
                   HELIKON_USB_CSW_SIZE,
               "a CSW followed"))
        return 1;
    if (expect(transport.ep1[transport.ep1_count - 1u][12] ==
                   HELIKON_USB_CSW_PASSED,
               "the write passed"))
        return 1;
    if (expect(disk.last_write_lba == high_lba, "it landed at the read LBA"))
        return 1;
    if (expect(memcmp(disk_slot(&disk, high_lba), payload, sizeof(payload)) == 0,
               "the medium holds the payload"))
        return 1;
    expect(helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_EP1_IN_DONE, NULL,
                                   0u) == HELIKON_OK,
           "the CSW completion closes the command");

    /* --- eject ----------------------------------------------------------- */
    unsigned int flushes_before = disk.flushes;
    uint8_t eject_cdb[6] = { 0x1bu, 0, 0, 0, 0x02u, 0 };
    if (expect(run_in_command(&mode, 4u, eject_cdb, 6u, 0u, &status) == 0,
               "START STOP UNIT(eject) ran"))
        return 1;
    if (expect(status == HELIKON_USB_CSW_PASSED, "the eject passed"))
        return 1;
    /* The mode consumed the request when the CSW completed and let go. */
    if (expect(mode.state == HELIKON_DISK_MODE_RELEASED,
               "eject releases the medium"))
        return 1;
    if (expect(helikon_disk_mode_take_eject(&mode) == 0,
               "the request was consumed once"))
        return 1;
    if (expect(disk.flushes > flushes_before, "the medium was flushed"))
        return 1;

    /* A fresh configuration brings the host back for the next test. */
    if (expect(feed_setup(&mode, 0x00u, 0x09u, 1u, 0u, 0u) == HELIKON_OK,
               "reconfiguring after an eject"))
        return 1;

    /* --- a read-only data-volume window ---------------------------------- */
    struct helikon_disk_mode ro;
    struct fake_disk ro_disk;
    memset(&ro_disk, 0, sizeof(ro_disk));
    struct helikon_block_info ro_info = {
        .sector_size = DISK_SECTOR_SIZE,
        .sector_count = 4096u,
        .flags = 0u
    };
    struct helikon_block_device ro_device;
    expect(helikon_block_init(&ro_device, &disk_ops, &ro_disk, &ro_info) ==
               HELIKON_OK,
           "read-only block device");
    reset_transport();
    struct helikon_disk_mode_options ro_options;
    memset(&ro_options, 0, sizeof(ro_options));
    ro_options.opt_in = 1;
    ro_options.restore_mode = 0;
    ro_options.base_lba = 64u;
    ro_options.sector_count = 1024u;
    ro_options.timeout_ms = 1000u;
    if (expect(helikon_disk_mode_start(&ro, &ops, &ro_device, &identity,
                                       &ro_options) == HELIKON_OK,
               "data-volume mode starts"))
        return 1;
    if (expect((ro.device.info.flags & HELIKON_BLOCK_FLAG_READ_ONLY) != 0u,
               "the data volume is forced read-only"))
        return 1;
    if (expect(ro.msc.base_lba == 64u && ro.msc.sector_count == 1024u,
               "the window is the chosen one"))
        return 1;
    feed_setup(&ro, 0x00u, 0x09u, 1u, 0u, 0u);
    /* A WRITE(10) is an OUT command; the LBA must be inside the window so the
     * refusal under test is the write protection, not a range error. */
    uint8_t ro_write[10] = { 0x2au, 0, 0, 0, 0, 64u, 0, 0, 1u, 0 };
    make_cbw(ro.cbw, 5u, 512u, 0, ro_write, 10u);
    unsigned int ro_before = transport.ep1_count;
    if (expect(helikon_disk_mode_event(&ro, HELIKON_USB_EVENT_EP2_OUT_DONE,
                                       NULL, 31u) == HELIKON_OK,
               "a write on the read-only LUN ran"))
        return 1;
    if (expect(transport.ep1_count == ro_before + 1u &&
                   transport.ep1_length[ro_before] == HELIKON_USB_CSW_SIZE,
               "the refusal ends in a CSW"))
        return 1;
    uint8_t ro_status = transport.ep1[ro_before][12];
    if (expect(ro_status == HELIKON_USB_CSW_FAILED, "it failed"))
        return 1;
    if (expect(helikon_usb_msc_sense(&ro.msc)->key ==
                   HELIKON_USB_SENSE_DATA_PROTECT,
               "with DATA PROTECT"))
        return 1;

    /* --- reset and disconnect -------------------------------------------- */
    expect(helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_BUS_RESET, NULL,
                                   0u) == HELIKON_OK,
           "bus reset accepted");
    if (expect(mode.state == HELIKON_DISK_MODE_WAITING, "reset returns to waiting"))
        return 1;
    if (expect(mode.phase == HELIKON_DISK_MODE_BOT_IDLE, "reset clears the phase"))
        return 1;
    expect(helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_DISCONNECT, NULL,
                                   0u) == HELIKON_OK,
           "disconnect accepted");
    if (expect(mode.state == HELIKON_DISK_MODE_RELEASED,
               "disconnect releases the medium"))
        return 1;
    if (expect(strcmp(helikon_disk_mode_state_name(mode.state), "ejected") == 0,
               "the state has a name"))
        return 1;

    helikon_disk_mode_stop(&mode);
    if (expect(mode.state == HELIKON_DISK_MODE_OFF, "stop turns the mode off"))
        return 1;

    if (failures == 0u)
        printf("PASS disk_mode\n");
    return failures == 0u ? 0 : 1;
}
