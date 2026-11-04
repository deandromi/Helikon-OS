/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded freestanding string helpers.
 *
 * Every function takes an explicit limit; no function scans past it. The
 * public header also provides the standard names used by the HFS+ core
 * (mem* only) so the target build can compile with -nostdinc.
 */
#ifndef HELIKON_LIB_STRING_H
#define HELIKON_LIB_STRING_H

#include <stddef.h>

#include "mem.h"

size_t helikon_strlen(const char *text, size_t limit);
int helikon_strcmp(const char *left, const char *right);
int helikon_strncmp(const char *left, const char *right, size_t limit);
size_t helikon_strlcpy(char *dest, const char *source, size_t size);

#ifndef HELIKON_HOST_TEST
/*
 * libc-compatible names for the existing HFS+ core. `strlen` has no bound;
 * new code that reads untrusted data must use helikon_strlen with an explicit
 * limit instead.
 */
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
#endif

#endif
