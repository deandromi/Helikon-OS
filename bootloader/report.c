/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "report.h"

#include <stdio.h>

int boot_diag_format(const struct boot_diag_result *result, char *buffer,
                     size_t size)
{
    if (result == NULL || buffer == NULL || size == 0)
        return HFS_ERR_ARGUMENT;

    int written = snprintf(
        buffer, size,
        "HELIKON-OS DIAGNOSTIC\n"
        "HFS+ volume: %s\n"
        "HFS+ catalog: %s\n"
        "HFS+ read: %s (%llu bytes)\n"
        "HFS+ write: %s\n"
        "Journal: %s\n"
        "Splash: %s\n",
        boot_diag_status_name(result->volume),
        boot_diag_status_name(result->catalog),
        boot_diag_status_name(result->read),
        (unsigned long long)result->bytes_read,
        boot_diag_status_name(result->write),
        boot_diag_journal_name(result->journal),
        boot_diag_status_name(result->splash));

    if (written < 0)
        return HFS_ERR_IO;
    if ((size_t)written >= size)
        return HFS_ERR_BUFFER;
    return written;
}
