/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_TEST_CHECK_H
#define HELIKON_TEST_CHECK_H

#include <stdio.h>

static int helikon_test_failures;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                             \
            ++helikon_test_failures;                                         \
        }                                                                    \
    } while (0)

#define CHECK_EQ(expected, actual)                                           \
    do {                                                                     \
        long long check_expected = (long long)(expected);                    \
        long long check_actual = (long long)(actual);                        \
        if (check_expected != check_actual) {                                \
            fprintf(stderr,                                                  \
                    "FAIL %s:%d: expected %lld, got %lld\n", __FILE__,       \
                    __LINE__, check_expected, check_actual);                 \
            ++helikon_test_failures;                                         \
        }                                                                    \
    } while (0)

static inline int helikon_test_report(const char *name)
{
    if (helikon_test_failures == 0) {
        printf("PASS %s\n", name);
        return 0;
    }
    printf("FAIL %s (%d checks)\n", name, helikon_test_failures);
    return 1;
}

#endif
