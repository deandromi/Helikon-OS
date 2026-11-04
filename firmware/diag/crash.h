/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Fatal-stop record.
 *
 * The panic path stores one record in BSS before painting the screen. The
 * record contains registers, the stage and a reason code, never file contents.
 * It is not persisted: a panic must not write the filesystem.
 */
#ifndef HELIKON_DIAG_CRASH_H
#define HELIKON_DIAG_CRASH_H

#include <stdint.h>

#define HELIKON_CRASH_MAGIC 0x31485243u /* "CHR1", little-endian */

enum helikon_panic_reason {
    HELIKON_PANIC_UNDEFINED = 1,
    HELIKON_PANIC_SOFTWARE_INTERRUPT = 2,
    HELIKON_PANIC_PREFETCH_ABORT = 3,
    HELIKON_PANIC_DATA_ABORT = 4,
    HELIKON_PANIC_RESERVED = 5,
    HELIKON_PANIC_IRQ = 6,
    HELIKON_PANIC_FIQ = 7,
    HELIKON_PANIC_ASSERT = 8,
    HELIKON_PANIC_STACK_GUARD = 9,
    HELIKON_PANIC_RETURN = 10,
    HELIKON_PANIC_INVARIANT = 11
};

struct helikon_crash_record {
    uint32_t magic;
    uint32_t reason;
    uint32_t pc;
    uint32_t lr;
    uint32_t sp;
    uint32_t mode;
    int stage;
    uint64_t timestamp_us;
};

void helikon_crash_set(const struct helikon_crash_record *record);
const struct helikon_crash_record *helikon_crash_get(void);

#endif
