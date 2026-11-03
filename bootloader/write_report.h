/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_WRITE_REPORT_H
#define HELIKON_WRITE_REPORT_H

#include "write_diag.h"

#include <stddef.h>

const char *boot_write_status_name(enum boot_write_status status);

int boot_write_diag_format(const struct boot_write_diag_result *result,
                           char *buffer, size_t size);

#endif
