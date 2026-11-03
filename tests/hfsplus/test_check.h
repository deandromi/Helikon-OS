/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_TEST_CHECK_H
#define HELIKON_TEST_CHECK_H

#include <stdio.h>

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                    #condition);                                               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#endif
