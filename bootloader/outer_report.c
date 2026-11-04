/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "outer_report.h"

#include <stdio.h>

static const char *status_name(enum boot_outer_status status)
{
    switch (status) {
    case BOOT_OUTER_PASS: return "PASS";
    case BOOT_OUTER_FAIL: return "FAIL";
    case BOOT_OUTER_UNSUPPORTED: return "UNSUPPORTED";
    case BOOT_OUTER_NOT_TESTED: return "NOT TESTED";
    default: return "UNKNOWN";
    }
}

int boot_outer_diag_format(const struct boot_outer_diag_result *result,
                           char *buffer, size_t size)
{
    if (result == NULL || buffer == NULL || size == 0)
        return HFS_ERR_ARGUMENT;

    int written = snprintf(
        buffer, size,
        "HELIKON-OS OUTER DIAG\n"
        "Opt-in: %s\n"
        "Outer HFS+: %s\n"
        "Journal: %s\n"
        "Catalog: %s\n"
        "Cleanup: %s\n"
        "Folder: %s\n"
        "Create: %s\n"
        "Append: %s\n"
        "Readback: %s\n"
        "Rename: %s\n"
        "Truncate: %s\n"
        "Delete file: %s\n"
        "Delete folder: %s\n"
        "Restored: %s\n"
        "Writes: %lu\n"
        "Only /helikon-outer-test was touched.\n"
        "Check fsck_hfs and the backup.\n",
        status_name(result->opt_in),
        status_name(result->outer_volume),
        status_name(result->outer_journal),
        status_name(result->catalog),
        status_name(result->cleanup),
        status_name(result->create_folder),
        status_name(result->create_file),
        status_name(result->append),
        status_name(result->readback),
        status_name(result->rename),
        status_name(result->truncate),
        status_name(result->delete_file),
        status_name(result->delete_folder),
        status_name(result->restored),
        (unsigned long)result->writes);

    if (written < 0)
        return HFS_ERR_IO;
    if ((size_t)written >= size)
        return HFS_ERR_BUFFER;
    return written;
}
