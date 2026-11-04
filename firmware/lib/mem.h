/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Freestanding memory primitives.
 *
 * These functions never fail and never block. The caller owns both buffers;
 * memcpy requires non-overlapping ranges, memmove accepts overlap. The
 * firmware links the same implementations that the host unit tests exercise.
 */
#ifndef HELIKON_LIB_MEM_H
#define HELIKON_LIB_MEM_H

#include <stddef.h>

void *helikon_memcpy(void *dest, const void *source, size_t count);
void *helikon_memmove(void *dest, const void *source, size_t count);
void *helikon_memset(void *dest, int value, size_t count);
int helikon_memcmp(const void *left, const void *right, size_t count);

#ifndef HELIKON_HOST_TEST
/*
 * The freestanding target build has no libc. The HFS+ core and compiler
 * helpers call the standard names, so the firmware image exports them here.
 * Host tests define HELIKON_HOST_TEST and call the helikon_* names directly.
 */
void *memcpy(void *dest, const void *source, size_t count);
void *memmove(void *dest, const void *source, size_t count);
void *memset(void *dest, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
#endif

#endif
