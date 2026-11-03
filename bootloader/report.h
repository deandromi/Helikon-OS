/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_BOOTLOADER_REPORT_H
#define HELIKON_BOOTLOADER_REPORT_H

#include "diagnostics.h"

#include <stddef.h>

/*
 * Format the diagnostic screen text. Returns the number of bytes written
 * (excluding NUL), or HFS_ERR_BUFFER if the destination is too small.
 */
int boot_diag_format(const struct boot_diag_result *result, char *buffer,
                     size_t size);

#endif
