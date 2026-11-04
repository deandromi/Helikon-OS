/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Compile-time assertions for the C99 firmware build.
 *
 * The project does not rely on the C11 _Static_assert extension. The typedef
 * trick below fails the build when `condition` is false (negative array size).
 */
#ifndef HELIKON_LIB_STATIC_ASSERT_H
#define HELIKON_LIB_STATIC_ASSERT_H

#define HELIKON_STATIC_ASSERT(condition, name) \
    typedef char helikon_static_assert_##name[(condition) ? 1 : -1]

#endif
