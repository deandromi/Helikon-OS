/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Deterministic fuzz of the bulk-only transport and the SCSI command set.
 *
 * A laptop can send anything, including a CBW with a nonsense length, a CDB
 * longer than the CBW field that describes it, a WRITE at an LBA outside the
 * window, and a data stage that stops halfway. The transport must answer every
 * one of those without crashing, without touching a sector outside its window,
 * and without reporting success it did not achieve.
 *
 * The seed is fixed by default, so a failure reproduces in CI; `-s SEED` walks
 * other seeds and `-n COUNT` sets how many commands to send. The invariants,
 * in the order they matter:
 *
 *  1. No memory error. The sanitizer build is the check; ASan aborts on the
 *     first out-of-bounds byte in `usb_msc.c` or the fake device.
 *  2. The window never moves. `base_lba` and `sector_count` are the caller's,
 *     and no command may change them.
 *  3. No sector outside the window is read or written, and a read-only device
 *     never reaches `write()`.
 *  4. The CSW is well formed: `USBS`, the CBW's tag, a status byte in range, and
 *     a residue no larger than the declared transfer length.
 *  5. A failed command leaves a sense key behind; a phase error does not have
 *     to, because the transport, not the command, failed.
 *  6. Eject only comes from START STOP UNIT. Nothing else may release the
 *     medium.
 *
 * Run: ./fuzz_msc            (50,000 commands, seed 1)
 *      ./fuzz_msc -n 1000000 (longer run)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block.h"
#include "helikon_error.h"
#include "usb_msc.h"

#define FUZZ_SECTOR_SIZE 512u
#define FUZZ_SLOTS 256u
#define FUZZ_CAPACITY_SECTORS 7812500000ull /* 4 TB, above the LBA28 line */
#define FUZZ_WINDOW_BASE 1024ull
#define FUZZ_WINDOW_COUNT 4096ull

/* --- deterministic generator --------------------------------------------- */

static uint64_t rng_state;

static uint32_t next_u32(void)
{
    /* xorshift64*, fixed constants: the same seed is the same byte stream on
     * every host and every compiler. */
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

struct fuzz_disk {
    uint8_t data[FUZZ_SLOTS * FUZZ_SECTOR_SIZE];
    uint64_t window_base;
    uint64_t window_count;
    int read_only;
    int fault;
    unsigned long reads;
    unsigned long writes;
    unsigned long violations;
    uint64_t first_bad_lba;
};

static int inside_window(const struct fuzz_disk *disk, uint64_t lba,
                         uint32_t count)
{
    if (disk->window_count == 0ull)
        return lba < FUZZ_CAPACITY_SECTORS &&
               count <= FUZZ_CAPACITY_SECTORS - lba;
    if (lba < disk->window_base)
        return 0;
    uint64_t offset = lba - disk->window_base;
    return offset <= disk->window_count &&
           count <= disk->window_count - offset;
}

static void note_violation(struct fuzz_disk *disk, uint64_t lba)
{
    if (disk->violations == 0ul)
        disk->first_bad_lba = lba;
    disk->violations += 1ul;
}

static uint8_t *fuzz_slot(struct fuzz_disk *disk, uint64_t lba)
{
    return disk->data + (size_t)(lba % FUZZ_SLOTS) * FUZZ_SECTOR_SIZE;
}

static int fuzz_read(void *context, uint64_t lba, uint32_t count, void *buffer,
                     uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fuzz_disk *disk = context;
    if (!inside_window(disk, lba, count)) {
        note_violation(disk, lba);
        return HELIKON_ERR_ARGUMENT;
    }
    if (disk->fault)
        return HELIKON_ERR_IO;
    disk->reads += 1ul;
    uint8_t *out = buffer;
    for (uint32_t index = 0u; index < count; ++index) {
        memcpy(out + (size_t)index * FUZZ_SECTOR_SIZE,
               fuzz_slot(disk, lba + index), FUZZ_SECTOR_SIZE);
    }
    return HELIKON_OK;
}

static int fuzz_write(void *context, uint64_t lba, uint32_t count,
                      const void *buffer, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fuzz_disk *disk = context;
    if (disk->read_only) {
        /* The transport checks this itself and must never get here. */
        note_violation(disk, lba);
        return HELIKON_ERR_IO;
    }
    if (!inside_window(disk, lba, count)) {
        note_violation(disk, lba);
        return HELIKON_ERR_ARGUMENT;
    }
    if (disk->fault)
        return HELIKON_ERR_IO;
    disk->writes += 1ul;
    const uint8_t *in = buffer;
    for (uint32_t index = 0u; index < count; ++index) {
        memcpy(fuzz_slot(disk, lba + index),
               in + (size_t)index * FUZZ_SECTOR_SIZE, FUZZ_SECTOR_SIZE);
    }
    return HELIKON_OK;
}

static int fuzz_flush(void *context, uint32_t timeout_ms)
{
    (void)timeout_ms;
    struct fuzz_disk *disk = context;
    return disk->fault ? HELIKON_ERR_IO : HELIKON_OK;
}

static int fuzz_power(void *context, int on, uint32_t timeout_ms)
{
    (void)context;
    (void)on;
    (void)timeout_ms;
    return HELIKON_OK;
}

static const struct helikon_block_ops fuzz_ops = {
    .read = fuzz_read, .write = fuzz_write, .flush = fuzz_flush,
    .power = fuzz_power
};

/* --- invariants ----------------------------------------------------------- */

static unsigned long failures;
static const char *current_case = "";

static void fail(const char *message)
{
    printf("FAIL [%s] %s\n", current_case, message);
    failures += 1ul;
}

static void build_cbw(uint8_t cbw[HELIKON_USB_CBW_SIZE], uint32_t tag,
                      uint32_t transfer, uint8_t flags, uint8_t lun,
                      uint8_t cdb_length, const uint8_t *cdb)
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
    cbw[12] = flags;
    cbw[13] = lun;
    cbw[14] = cdb_length;
    memcpy(cbw + 15, cdb, HELIKON_USB_CBW_SIZE - 15u);
}

/* The interesting CDBs plus noise. Returning an opcode the device does not
 * know is a valid fuzz case too: it must be refused, not ignored. */
static uint8_t cdb_opcode(void)
{
    static const uint8_t ops[] = {
        0x00u, 0x03u, 0x12u, 0x1au, 0x1bu, 0x23u, 0x25u, 0x28u,
        0x2au, 0x2fu, 0x35u, 0x36u, 0x3bu, 0x42u, 0x43u, 0x55u,
        0x9eu, 0xa0u, 0xa1u, 0xa8u, 0xc0u, 0xc2u
    };
    if (next_below(4u) != 0u)
        return ops[next_below((uint32_t)(sizeof(ops) / sizeof(ops[0])))];
    return (uint8_t)next_u32();
}

static void check_csw(const struct helikon_usb_msc *msc, uint8_t status)
{
    if (status > HELIKON_USB_CSW_PHASE_ERROR)
        fail("CSW status out of range");
    if (status == HELIKON_USB_CSW_FAILED && msc->failed == 0)
        fail("CSW says failed but the command did not record a failure");
    if (status == HELIKON_USB_CSW_PASSED && msc->failed != 0)
        fail("CSW says passed after a recorded failure");
}

static void check_sense(const struct helikon_usb_msc *msc, uint8_t status)
{
    const struct helikon_usb_sense *sense = helikon_usb_msc_sense(msc);
    if (sense == NULL) {
        fail("sense pointer is null");
        return;
    }
    if (status == HELIKON_USB_CSW_FAILED && sense->key == HELIKON_USB_SENSE_NONE)
        fail("a failed command left no sense key");
}

/* --- the driver ----------------------------------------------------------- */

struct fuzz_result {
    unsigned long commands;
    unsigned long accepted;
    unsigned long rejected;
    unsigned long passed;
    unsigned long failed;
    unsigned long phase_errors;
    unsigned long ejects;
    unsigned long data_stage_calls;
};

static void one_command(struct helikon_usb_msc *msc, struct fuzz_disk *disk,
                        struct fuzz_result *out, unsigned long iteration)
{
    uint8_t cbw[HELIKON_USB_CBW_SIZE];
    uint8_t cdb[HELIKON_USB_CBW_SIZE - 15u];
    uint32_t tag = next_u32();
    uint32_t transfer = next_u32();
    uint8_t flags = (uint8_t)next_u32();
    uint8_t lun = (uint8_t)(next_below(4u) == 0u ? next_u32() : 0u);
    uint8_t cdb_length = (uint8_t)next_below(20u);
    uint8_t opcode = cdb_opcode();

    /* A plausible length most of the time, a hostile one sometimes. */
    if (next_below(8u) != 0u)
        transfer %= 8192u;
    /* The direction bit is the only legal one; the reserved bits are set on
     * purpose a little of the time, because that refusal has to be answered. */
    if (next_below(16u) != 0u)
        flags = (next_below(2u) != 0u ? 0x80u : 0x00u);
    if (next_below(4u) == 0u)
        cdb_length = (uint8_t)(1u + next_below(16u));

    for (uint32_t index = 0u; index < sizeof(cdb); ++index)
        cdb[index] = (uint8_t)next_u32();
    cdb[0] = opcode;
    if (next_below(2u) != 0u) {
        /* Give the length-shaped fields a chance to be sane, so the fuzz reaches
         * the transfer logic instead of only the argument checks. */
        for (uint32_t index = 2u; index < 10u; index += 2u) {
            cdb[index] = 0u;
            cdb[index + 1u] = 0u;
        }
    }

    build_cbw(cbw, tag, transfer, flags, lun, cdb_length, cdb);
    if (next_below(64u) == 0u) {
        /* Occasionally break the signature: the transport must refuse it. */
        cbw[next_below(4u)] ^= (uint8_t)(1u << next_below(8u));
    }

    uint64_t base = msc->base_lba;
    uint64_t count = msc->sector_count;

    struct helikon_usb_msc_result result;
    int rc = helikon_usb_msc_command(msc, cbw, &result);
    out->commands += 1ul;

    if (rc != HELIKON_OK && rc != HELIKON_ERR_FORMAT) {
        fail("command returned an unexpected code");
        return;
    }

    if (msc->base_lba != base || msc->sector_count != count) {
        fail("a command moved the window");
        return;
    }

    uint8_t status = HELIKON_USB_CSW_PASSED;
    if (rc == HELIKON_OK) {
        out->accepted += 1ul;
        if (result.length > transfer && transfer != 0u) {
            /* A data stage longer than the CBW asked for would overrun the
             * host's buffer. */
            fail("data stage longer than the declared transfer");
        }
        uint32_t in_length = result.length > 4096u ? 4096u : result.length;
        uint32_t done = 0u;
        int io_error = 0;
        uint8_t buffer[4096];
        memset(buffer, 0xa5, sizeof(buffer));
        while (done < in_length) {
            uint32_t want = 1u + next_below(512u);
            if (want > in_length - done)
                want = in_length - done;
            uint32_t moved = 0u;
            out->data_stage_calls += 1ul;
            int stage = result.direction >= 0
                            ? helikon_usb_msc_in(msc, buffer + done, want,
                                                 &moved)
                            : helikon_usb_msc_out(msc, buffer + done, want,
                                                  &moved);
            if (stage != HELIKON_OK) {
                /* With faults injected the device itself fails, and the
                 * transport is supposed to report that rather than pretend the
                 * bytes moved. Without faults it must not fail at all. */
                if (disk->fault) {
                    io_error = 1;
                    break;
                }
                fail("data stage refused");
                break;
            }
            if (moved == 0u)
                break; /* the command is done, not stuck */
            if (moved > want) {
                fail("data stage moved more than asked");
                break;
            }
            done += moved;
        }

        uint8_t csw[HELIKON_USB_CSW_SIZE];
        helikon_usb_msc_status(msc, csw);
        uint32_t csw_signature = (uint32_t)csw[0] | ((uint32_t)csw[1] << 8) |
                                 ((uint32_t)csw[2] << 16) |
                                 ((uint32_t)csw[3] << 24);
        uint32_t csw_tag = (uint32_t)csw[4] | ((uint32_t)csw[5] << 8) |
                           ((uint32_t)csw[6] << 16) |
                           ((uint32_t)csw[7] << 24);
        uint32_t residue = (uint32_t)csw[8] | ((uint32_t)csw[9] << 8) |
                           ((uint32_t)csw[10] << 16) |
                           ((uint32_t)csw[11] << 24);
        status = csw[12];
        if (csw_signature != HELIKON_USB_CSW_SIGNATURE)
            fail("CSW signature is wrong");
        if (csw_tag != msc->tag)
            fail("CSW does not echo the command tag");
        if (msc->failed == 0 && residue > msc->transfer_length)
            fail("CSW residue is larger than the transfer");
        check_csw(msc, status);
        check_sense(msc, status);
        if (io_error != 0 && status == HELIKON_USB_CSW_PASSED)
            fail("a device error was reported as success");
        switch (status) {
        case HELIKON_USB_CSW_PASSED:
            out->passed += 1ul;
            break;
        case HELIKON_USB_CSW_FAILED:
            out->failed += 1ul;
            break;
        default:
            out->phase_errors += 1ul;
            break;
        }
    } else {
        out->rejected += 1ul;
        out->phase_errors += 1ul;
        /* A refused CBW must not disturb the command state, and the transport
         * must have something to send back: the phase-error CSW, with the tag
         * echoed, so the host is not left waiting for its timeout. */
        if (msc->base_lba != base || msc->sector_count != count)
            fail("a refused CBW moved the window");
        uint8_t csw[HELIKON_USB_CSW_SIZE];
        helikon_usb_msc_phase_error_csw(cbw, csw);
        uint32_t signature = (uint32_t)csw[0] | ((uint32_t)csw[1] << 8) |
                             ((uint32_t)csw[2] << 16) |
                             ((uint32_t)csw[3] << 24);
        uint32_t echo = (uint32_t)csw[4] | ((uint32_t)csw[5] << 8) |
                        ((uint32_t)csw[6] << 16) | ((uint32_t)csw[7] << 24);
        uint32_t residue = (uint32_t)csw[8] | ((uint32_t)csw[9] << 8) |
                           ((uint32_t)csw[10] << 16) |
                           ((uint32_t)csw[11] << 24);
        if (signature != HELIKON_USB_CSW_SIGNATURE)
            fail("phase-error CSW signature is wrong");
        if (echo != tag)
            fail("phase-error CSW does not echo the tag");
        if (residue != 0u)
            fail("phase-error CSW carries a residue");
        if (csw[12] != HELIKON_USB_CSW_PHASE_ERROR)
            fail("phase-error CSW status is wrong");
    }

    if (helikon_usb_msc_take_eject(msc) != 0) {
        out->ejects += 1ul;
        if (cdb[0] != 0x1bu)
            fail("eject came from a command that was not START STOP UNIT");
        if (next_below(2u) == 0u) {
            /* The host may ask again; the flag must not survive being read. */
            if (helikon_usb_msc_take_eject(msc) != 0)
                fail("the eject flag was not consumed");
        }
    }

    /* Every so often, as a laptop would on a port reset. */
    if (next_below(97u) == 0u)
        helikon_usb_msc_reset(msc);

    if (disk->violations != 0ul) {
        static int reported;
        if (reported == 0) {
            printf("FAIL [%s] sector access outside the window at LBA %llu "
                   "after %lu commands\n", current_case,
                   (unsigned long long)disk->first_bad_lba, out->commands);
            reported = 1;
            failures += 1ul;
        }
    }
    (void)iteration;
}

static int run_case(const char *name, uint64_t seed, unsigned long iterations,
                    int read_only, int fault, uint64_t base, uint64_t count)
{
    static struct fuzz_disk disk;
    memset(&disk, 0, sizeof(disk));
    disk.read_only = read_only;
    disk.fault = fault;
    disk.window_base = base;
    disk.window_count = count;
    current_case = name;
    rng_state = seed == 0ull ? 0x9e3779b97f4a7c15ull : seed;

    struct helikon_block_device device;
    memset(&device, 0, sizeof(device));
    device.ops = &fuzz_ops;
    device.context = &disk;
    device.info.sector_count = FUZZ_CAPACITY_SECTORS;
    device.info.sector_size = FUZZ_SECTOR_SIZE;
    device.info.flags = read_only ? HELIKON_BLOCK_FLAG_READ_ONLY : 0u;

    struct helikon_usb_msc msc;
    if (helikon_usb_msc_init(&msc, &device, base, count, 100u) != HELIKON_OK) {
        printf("FAIL [%s] init\n", name);
        failures += 1ul;
        return 1;
    }

    struct fuzz_result stats;
    memset(&stats, 0, sizeof(stats));
    for (unsigned long index = 0ul; index < iterations; ++index) {
        if (failures > 20ul) {
            printf("stopping %s after too many failures\n", name);
            break;
        }
        one_command(&msc, &disk, &stats, index);
    }

    printf("%-22s commands %lu  accepted %lu  rejected %lu  passed %lu  "
           "failed %lu  phase %lu  ejects %lu  reads %lu  writes %lu\n",
           name, stats.commands, stats.accepted, stats.rejected, stats.passed,
           stats.failed, stats.phase_errors, stats.ejects, disk.reads,
           disk.writes);

    if (read_only && disk.writes != 0ul) {
        printf("FAIL [%s] a read-only device took %lu writes\n", name,
               disk.writes);
        failures += 1ul;
    }
    if (disk.violations != 0ul) {
        printf("FAIL [%s] %lu accesses outside the window\n", name,
               disk.violations);
        failures += 1ul;
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long iterations = 50000ul;
    uint64_t seed = 1ull;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "-n") == 0 && index + 1 < argc) {
            iterations = strtoul(argv[++index], NULL, 10);
        } else if (strcmp(argv[index], "-s") == 0 && index + 1 < argc) {
            seed = strtoull(argv[++index], NULL, 10);
        } else {
            printf("usage: %s [-n commands] [-s seed]\n", argv[0]);
            return 2;
        }
    }

    int rc = 0;
    rc |= run_case("whole-disk", seed, iterations, 0, 0, 0ull, 0ull);
    rc |= run_case("volume-window", seed + 1ull, iterations, 0, 0,
                   FUZZ_WINDOW_BASE, FUZZ_WINDOW_COUNT);
    rc |= run_case("read-only", seed + 2ull, iterations, 1, 0, 0ull, 0ull);
    rc |= run_case("io-faults", seed + 3ull, iterations / 4ul, 0, 1, 0ull, 0ull);

    if (failures != 0ul) {
        printf("FAIL fuzz_msc: %lu failures\n", failures);
        return 1;
    }
    printf("PASS fuzz_msc (%lu commands per case, seed %llu)\n", iterations,
           (unsigned long long)seed);
    return rc;
}
