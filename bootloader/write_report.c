/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "write_report.h"

#include <stdio.h>

const char *boot_write_status_name(enum boot_write_status status)
{
    switch (status) {
    case BOOT_WRITE_PASS: return "PASS";
    case BOOT_WRITE_FAIL: return "FAIL";
    case BOOT_WRITE_UNSUPPORTED: return "UNSUPPORTED";
    case BOOT_WRITE_NOT_TESTED: return "NOT TESTED";
    default: return "UNKNOWN";
    }
}

int boot_write_diag_format(const struct boot_write_diag_result *result,
                           char *buffer, size_t size)
{
    if (result == NULL || buffer == NULL || size == 0)
        return HFS_ERR_ARGUMENT;

    int written = snprintf(
        buffer, size,
        "HELIKON-OS WRITE DIAG\n"
        "Outer HFS+: %s\n"
        "Test image: %s\n"
        "Inner HFS+: %s\n"
        "Inner catalog: %s\n"
        "Inner journal: %s\n"
        "Folder: %s\n"
        "Create: %s\n"
        "Append: %s\n"
        "Readback: %s\n"
        "Rename: %s\n"
        "Truncate: %s\n"
        "Delete: %s\n"
        "Splash: PASS\n"
        "Inner writes: %lu\n"
        "Only the test image was written.\n"
        "Outer metadata untouched.\n"
        "Copy the test image to macOS for fsck.\n",
        boot_write_status_name(result->outer_volume),
        boot_write_status_name(result->test_file),
        boot_write_status_name(result->inner_volume),
        boot_write_status_name(result->inner_catalog),
        boot_write_status_name(result->inner_journal),
        boot_write_status_name(result->create_folder),
        boot_write_status_name(result->create_file),
        boot_write_status_name(result->append),
        boot_write_status_name(result->readback),
        boot_write_status_name(result->rename),
        boot_write_status_name(result->truncate),
        boot_write_status_name(result->delete),
        (unsigned long)result->writes);

    if (written < 0)
        return HFS_ERR_IO;
    if ((size_t)written >= size)
        return HFS_ERR_BUFFER;
    return written;
}
