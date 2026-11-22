/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_TEST_CHECK_H
#define HELIKON_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

static int helikon_test_failures;
static int helikon_test_reported;

/*
 * A test that records a failure and then forgets to call
 * `helikon_test_report` used to exit 0, and the suite counted it as a pass.
 * That happened for real, so the guard stays: if the process ends with
 * failures recorded but no report, the exit status is forced to 1.
 */
static void helikon_test_guard(void)
{
    if (!helikon_test_reported && helikon_test_failures != 0) {
        fprintf(stderr, "FAIL: %d check(s) failed and the test never called "
                        "helikon_test_report\n", helikon_test_failures);
        _Exit(1);
    }
}

__attribute__((constructor)) static void helikon_test_install_guard(void)
{
    atexit(helikon_test_guard);
}

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
    helikon_test_reported = 1;
    if (helikon_test_failures == 0) {
        printf("PASS %s\n", name);
        return 0;
    }
    printf("FAIL %s (%d checks)\n", name, helikon_test_failures);
    return 1;
}

#endif
