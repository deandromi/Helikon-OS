/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_OUTER_REPORT_H
#define HELIKON_OUTER_REPORT_H

#include "outer_diag.h"

#include <stddef.h>

int boot_outer_diag_format(const struct boot_outer_diag_result *result,
                           char *buffer, size_t size);

#endif
