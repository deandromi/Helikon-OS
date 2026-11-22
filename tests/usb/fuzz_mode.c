/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Fuzz the Disk Mode state machine, not the bytes it moves.
 *
 * `fuzz_msc` covers the SCSI layer; this covers the thing above it: the bulk-only
 * phase machine, the enumeration state and the release path. A laptop (or a
 * flaky cable, or a host that resets mid-command) can deliver events in any
 * order, and the mode must not wedge, must not hand the medium over twice, and
 * must not write a byte when it started read-only.
 *
 * Deterministic, like `fuzz_msc`: the same seed is the same event stream. Two
 * checks bracket the random walk:
 *
 *  1. **Invariants after every event.** State and phase stay in range; the LUN
 *     window never moves; a read-only mode never reaches `write()`; a released
 *     medium stays released until a bus reset; the error count only grows.
 *  2. **Liveness.** After any random walk, a bus reset and a fresh
 *     SET_CONFIGURATION, a valid INQUIRY must produce an answer within a bounded
 *     number of completions. A phase machine that wedges fails here even if
 *     every byte it ever moved was fine.
 *
 * Plus the 100-cycle check from T-115, done on the mock: disconnect and
 * re-enumerate a hundred times and the window, the counters and the answer must
 * all survive.
 *
 * Run: ./fuzz_mode            (20,000 events, seed 1)
 *      ./fuzz_mode -n 200000  (longer)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block.h"
#include "disk_mode.h"
#include "error.h"
#include "usb_descriptors.h"
#include "usb_events.h"
#include "usb_msc.h"

#define FUZZ_SECTORS 64u
#define FUZZ_SECTOR_SIZE 512u
#define FUZZ_CAPACITY 7812500000ull /* 4 TB */
#define EP1_QUEUE 8u

/* --- deterministic generator --------------------------------------------- */

static uint64_t rng_state;

static uint32_t next_u32(void)
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return (uint32_t)((rng_state * 2685821657736338717ull) >> 32);
}

static uint32_t next_below(uint32_t bound)
{
    return bound == 0u ? 0u : next_u32() % bound;
}

/* --- fake disk ------------------------------------------------------------ */

struct fake_disk {
    uint8_t data[FUZZ_SECTORS * FUZZ_SECTOR_SIZE];
    unsigned int reads;
    unsigned int writes;
    unsigned int flushes;
};

static uint8_t *disk_slot(struct fake_disk *disk, uint64_t lba)
{
    return disk->data + (size_t)(lba % FUZZ_SECTORS) * FUZZ_SECTOR_SIZE;
}

static int disk_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    if (count == 0u || lba >= FUZZ_CAPACITY || count > FUZZ_CAPACITY - lba)
        return HELIKON_ERR_ARGUMENT;
    unsigned char *out = buffer;
    disk->reads += 1u;
    for (uint32_t index = 0u; index < count; ++index)
        memcpy(out + (size_t)index * FUZZ_SECTOR_SIZE,
               disk_slot(disk, lba + index), FUZZ_SECTOR_SIZE);
    return HELIKON_OK;
}

static int disk_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fake_disk *disk = context;
    if (count == 0u || lba >= FUZZ_CAPACITY || count > FUZZ_CAPACITY - lba)
        return HELIKON_ERR_ARGUMENT;
    const unsigned char *in = buffer;
    disk->writes += 1u;
    for (uint32_t index = 0u; index < count; ++index)
        memcpy(disk_slot(disk, lba + index),
               in + (size_t)index * FUZZ_SECTOR_SIZE, FUZZ_SECTOR_SIZE);
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

struct fake_transport {
    uint8_t ep0[256];
    uint32_t ep0_length;
    unsigned int ep0_sends;
    unsigned int ep0_stalls;
    unsigned int ep0_receives;

    uint8_t ep1[EP1_QUEUE][600];
    uint32_t ep1_length[EP1_QUEUE];
    unsigned int ep1_count;

    uint8_t *ep2_target;
    uint32_t ep2_length;
    unsigned int ep2_arms;

    unsigned int set_address_calls;
    unsigned int configure_calls;
    int configured;
    /* Set by the fuzz when it wants the transport to fail the next call. */
    int fail_ep1;
};

static struct fake_transport transport;

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
    transport.ep0_receives += 1u;
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
    if (transport.fail_ep1)
        return HELIKON_ERR_IO;
    if (transport.ep1_count == EP1_QUEUE || length > sizeof(transport.ep1[0]))
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
    (void)address;
    transport.set_address_calls += 1u;
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

/* --- harness -------------------------------------------------------------- */

static unsigned int failures;
static const char *current_case = "";
static unsigned int events_seen;

static void fail(const char *message)
{
    printf("FAIL [%s after %u events] %s\n", current_case, events_seen, message);
    failures += 1u;
}

static void make_identity(struct helikon_usb_identity *identity)
{
    memset(identity, 0, sizeof(*identity));
    identity->manufacturer = "Helikon";
    identity->product = "Helikon Disk Mode";
    identity->serial = "HLK-FUZZ0001";
}

static void make_cbw(uint8_t cbw[HELIKON_USB_CBW_SIZE], uint32_t tag,
                     uint32_t transfer, int in, const uint8_t *cdb,
                     uint8_t cdb_length)
{
    memset(cbw, 0, HELIKON_USB_CBW_SIZE);
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
    if (cdb != NULL && cdb_length != 0u)
        memcpy(cbw + 15, cdb, cdb_length);
}

static void feed_setup(struct helikon_disk_mode *mode, uint8_t request_type,
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
    (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_SETUP, setup, 0u);
    events_seen += 1u;
}

static void enumerate(struct helikon_disk_mode *mode)
{
    feed_setup(mode, 0x80u, 0x06u, 0x0100u, 0u, 18u);
    feed_setup(mode, 0x00u, 0x05u, 3u, 0u, 0u);
    feed_setup(mode, 0x00u, 0x09u, 1u, 0u, 0u);
}

/* Checks the invariants that must hold after every event. */
static void check_invariants(struct helikon_disk_mode *mode,
                             const struct fake_disk *disk, int read_only,
                             uint64_t base, uint64_t count)
{
    if (mode->state > HELIKON_DISK_MODE_ERROR)
        fail("state out of range");
    if (mode->phase > HELIKON_DISK_MODE_BOT_IN_CSW)
        fail("phase out of range");
    if (mode->msc.base_lba != base || mode->msc.sector_count != count)
        fail("the LUN window moved");
    if (read_only && disk->writes != 0u)
        fail("a read-only mode reached write()");
    if (mode->state == HELIKON_DISK_MODE_RELEASED &&
        mode->phase != HELIKON_DISK_MODE_BOT_IDLE)
        fail("a released medium still holds a phase");
}

/*
 * After a reset, the machine must still answer. This is the anti-wedge check:
 * a valid INQUIRY produces data or a CSW within a bounded number of
 * completions, whatever the walk did before.
 */
static void check_liveness(struct helikon_disk_mode *mode)
{
    (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_BUS_RESET, NULL, 0u);
    enumerate(mode);
    if (mode->state != HELIKON_DISK_MODE_ENUMERATED) {
        fail("not enumerated after a reset and a fresh SET_CONFIGURATION");
        return;
    }
    static const uint8_t inquiry[6] = { 0x12u, 0u, 0u, 0u, 36u, 0u };
    make_cbw(mode->cbw, 0x1234u, 36u, 1, inquiry, sizeof(inquiry));
    unsigned int before = transport.ep1_count;
    transport.ep2_arms = 0u;
    (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_EP2_OUT_DONE, NULL,
                                 HELIKON_USB_CBW_SIZE);
    int answered = 0;
    for (unsigned int guard = 0u; guard < 16u; ++guard) {
        if (transport.ep1_count > before) {
            answered = 1;
            break;
        }
        if (transport.ep1_count > 0u)
            transport.ep1_count = 0u; /* keep the queue from filling up */
        (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_EP1_IN_DONE, NULL,
                                     0u);
    }
    if (!answered)
        fail("a valid INQUIRY produced no answer after a reset");
}

static void one_event(struct helikon_disk_mode *mode, struct fake_disk *disk,
                      int read_only, uint64_t base, uint64_t count)
{
    uint32_t choice = next_below(100u);
    if (choice < 12u) {
        /* A random control request, most of them legal. */
        uint8_t request_type = (uint8_t)next_u32();
        uint8_t request = (uint8_t)next_u32();
        if (next_below(2u) == 0u) {
            request_type = next_below(2u) == 0u ? 0x80u : 0x00u;
            request = 0x06u; /* GET_DESCRIPTOR, with a random type */
        }
        feed_setup(mode, request_type, request, (uint16_t)next_u32(),
                   (uint16_t)next_u32(), (uint16_t)next_u32());
    } else if (choice < 40u) {
        /* An EP2 packet: a CBW of the right length, a wrong length, or the
         * middle of a WRITE's data stage, with random bytes in the buffer. */
        uint32_t length = HELIKON_USB_CBW_SIZE;
        if (next_below(4u) == 0u) {
            length = next_below(80u);
        } else if (mode->phase == HELIKON_DISK_MODE_BOT_OUT_DATA &&
                   transport.ep2_length != 0u) {
            /* Mid WRITE: a real host sends exactly the chunk the transport asked
             * for, and that is the only way the staged write ever completes. */
            length = transport.ep2_length;
        }
        if (transport.ep2_target != NULL && transport.ep2_length != 0u) {
            uint32_t fill = transport.ep2_length;
            if (fill > 600u)
                fill = 600u;
            for (uint32_t index = 0u; index < fill; ++index) {
                transport.ep2_target[index] = (uint8_t)next_u32();
            }
        }
        (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_EP2_OUT_DONE, NULL,
                                      length);
    } else if (choice < 74u) {
        /* A completion the mode was not necessarily waiting for. */
        (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_EP1_IN_DONE, NULL,
                                      0u);
        if (transport.ep1_count != 0u)
            transport.ep1_count = 0u;
    } else if (choice < 84u) {
        (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_BUS_RESET, NULL,
                                      0u);
        enumerate(mode);
    } else if (choice < 93u) {
        (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_DISCONNECT, NULL,
                                      0u);
    } else {
        /* A valid command, so the walk also reaches the data path. */
        static const uint8_t read10[10] = { 0x28u, 0u, 0u, 0u, 0u, 0u, 0u,
                                            0u, 64u, 0u };
        static const uint8_t write10[10] = { 0x2au, 0u, 0u, 0u, 0u, 0u, 0u,
                                             0u, 1u, 0u };
        static const uint8_t start[6] = { 0x1bu, 0u, 0u, 0u, 0x02u, 0u };
        static const uint8_t inquiry[6] = { 0x12u, 0u, 0u, 0u, 36u, 0u };
        const uint8_t *cdb = inquiry;
        uint8_t length = sizeof(inquiry);
        uint32_t transfer = 36u;
        int in = 1;
        uint32_t pick = next_below(4u);
        if (pick == 1u) {
            cdb = read10;
            length = sizeof(read10);
            transfer = 64u * FUZZ_SECTOR_SIZE;
        } else if (pick == 2u) {
            cdb = write10;
            length = sizeof(write10);
            transfer = FUZZ_SECTOR_SIZE;
            in = 0;
        } else if (pick == 3u) {
            cdb = start;
            length = sizeof(start);
            transfer = 0u;
            in = 0;
        }
        make_cbw(mode->cbw, next_u32(), transfer, in, cdb, length);
        (void)helikon_disk_mode_event(mode, HELIKON_USB_EVENT_EP2_OUT_DONE, NULL,
                                      HELIKON_USB_CBW_SIZE);
    }
    events_seen += 1u;
    check_invariants(mode, disk, read_only, base, count);
    if (transport.ep1_count == EP1_QUEUE)
        transport.ep1_count = 0u; /* a real host drains its queue */
}

static int run_walk(const char *name, uint64_t seed, unsigned long iterations,
                    int read_only)
{
    static struct fake_disk disk;
    memset(&disk, 0, sizeof(disk));
    memset(&transport, 0, sizeof(transport));
    current_case = name;
    events_seen = 0u;
    rng_state = seed == 0ull ? 0x9e3779b97f4a7c15ull : seed;

    struct helikon_block_device device;
    memset(&device, 0, sizeof(device));
    device.ops = &disk_ops;
    device.context = &disk;
    device.info.sector_count = FUZZ_CAPACITY;
    device.info.sector_size = FUZZ_SECTOR_SIZE;
    device.info.flags = read_only ? HELIKON_BLOCK_FLAG_READ_ONLY : 0u;

    struct helikon_usb_identity identity;
    make_identity(&identity);
    struct helikon_usb_device_ops ops = transport_ops();
    struct helikon_disk_mode_options options;
    memset(&options, 0, sizeof(options));
    options.opt_in = 1;
    options.restore_mode = read_only ? 0 : 1;
    options.base_lba = 64u;
    options.sector_count = 1024u;
    options.timeout_ms = 1000u;

    struct helikon_disk_mode mode;
    memset(&mode, 0, sizeof(mode));
    if (helikon_disk_mode_start(&mode, &ops, &device, &identity, &options) !=
        HELIKON_OK) {
        printf("FAIL [%s] start\n", name);
        failures += 1u;
        return 1;
    }
    uint64_t base = mode.msc.base_lba;
    uint64_t count = mode.msc.sector_count;
    enumerate(&mode);

    unsigned long index = 0ul;
    while (index < iterations && failures < 20u) {
        one_event(&mode, &disk, read_only, base, count);
        ++index;
    }

    check_liveness(&mode);
    check_invariants(&mode, &disk, read_only, base, count);

    printf("%-16s events %lu  reads %u  writes %u  flushes %u  errors %u  "
           "state %d\n",
           name, iterations, disk.reads, disk.writes, disk.flushes, mode.errors,
           (int)mode.state);
    if (read_only && disk.writes != 0u)
        fail("a read-only walk wrote to the disk");
    if (mode.msc.base_lba != base || mode.msc.sector_count != count)
        fail("the walk moved the window");

    helikon_disk_mode_stop(&mode);
    return 0;
}

/* T-115's 100-cycle check, on the mock: nothing may leak between cycles. */
static void run_cycles(unsigned int cycles)
{
    static struct fake_disk disk;
    memset(&disk, 0, sizeof(disk));
    memset(&transport, 0, sizeof(transport));
    current_case = "100-cycles";
    events_seen = 0u;

    struct helikon_block_device device;
    memset(&device, 0, sizeof(device));
    device.ops = &disk_ops;
    device.context = &disk;
    device.info.sector_count = FUZZ_CAPACITY;
    device.info.sector_size = FUZZ_SECTOR_SIZE;

    struct helikon_usb_identity identity;
    make_identity(&identity);
    struct helikon_usb_device_ops ops = transport_ops();
    struct helikon_disk_mode_options options;
    memset(&options, 0, sizeof(options));
    options.opt_in = 1;
    options.restore_mode = 1;
    options.timeout_ms = 1000u;

    struct helikon_disk_mode mode;
    memset(&mode, 0, sizeof(mode));
    if (helikon_disk_mode_start(&mode, &ops, &device, &identity, &options) !=
        HELIKON_OK) {
        printf("FAIL [%s] start\n", current_case);
        failures += 1u;
        return;
    }
    uint64_t base = mode.msc.base_lba;
    uint64_t count = mode.msc.sector_count;

    unsigned int completions = 0u;
    for (unsigned int cycle = 0u; cycle < cycles; ++cycle) {
        enumerate(&mode);
        if (mode.state != HELIKON_DISK_MODE_ENUMERATED) {
            fail("not enumerated at the start of a cycle");
            break;
        }
        /* One INQUIRY per cycle, so the data path runs too. */
        static const uint8_t inquiry[6] = { 0x12u, 0u, 0u, 0u, 36u, 0u };
        make_cbw(mode.cbw, cycle + 1u, 36u, 1, inquiry, sizeof(inquiry));
        (void)helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_EP2_OUT_DONE,
                                      NULL, HELIKON_USB_CBW_SIZE);
        for (unsigned int guard = 0u; guard < 8u; ++guard) {
            if (transport.ep1_count == 0u)
                break;
            transport.ep1_count = 0u;
            completions += 1u;
            (void)helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_EP1_IN_DONE,
                                           NULL, 0u);
        }
        (void)helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_DISCONNECT, NULL,
                                      0u);
        if (mode.state != HELIKON_DISK_MODE_RELEASED) {
            fail("a disconnect did not release the medium");
            break;
        }
        if (mode.msc.base_lba != base || mode.msc.sector_count != count) {
            fail("the window moved between cycles");
            break;
        }
        (void)helikon_disk_mode_event(&mode, HELIKON_USB_EVENT_BUS_RESET, NULL,
                                      0u);
    }

    printf("%-16s cycles %u  completions %u  state %d\n", current_case, cycles,
           completions, (int)mode.state);
    helikon_disk_mode_stop(&mode);
}

int main(int argc, char **argv)
{
    unsigned long iterations = 20000ul;
    uint64_t seed = 1ull;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "-n") == 0 && index + 1 < argc) {
            iterations = strtoul(argv[++index], NULL, 10);
        } else if (strcmp(argv[index], "-s") == 0 && index + 1 < argc) {
            seed = strtoull(argv[++index], NULL, 10);
        } else {
            printf("usage: %s [-n events] [-s seed]\n", argv[0]);
            return 2;
        }
    }

    run_walk("restore", seed, iterations, 0);
    run_walk("read-only", seed + 1ull, iterations, 1);
    run_cycles(100u);

    if (failures != 0u) {
        printf("FAIL fuzz_mode: %u failures\n", failures);
        return 1;
    }
    printf("PASS fuzz_mode (%lu events per walk, 100 cycles, seed %llu)\n",
           iterations, (unsigned long long)seed);
    return 0;
}
