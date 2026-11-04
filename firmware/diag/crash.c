/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "crash.h"

#include "mem.h"

static struct helikon_crash_record crash_record;

void helikon_crash_set(const struct helikon_crash_record *record)
{
    if (record == NULL)
        return;
    helikon_memcpy(&crash_record, record, sizeof(crash_record));
    crash_record.magic = HELIKON_CRASH_MAGIC;
}

const struct helikon_crash_record *helikon_crash_get(void)
{
    if (crash_record.magic != HELIKON_CRASH_MAGIC)
        return NULL;
    return &crash_record;
}
