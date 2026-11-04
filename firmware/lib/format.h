/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bounded integer formatter for diagnostics and early screens.
 *
 * The functions never allocate and never write past `size`. They always
 * NUL-terminate when `size` is nonzero and return the number of characters
 * actually stored (excluding the terminator). Truncation is deliberate and
 * tested; the firmware does not use printf.
 */
#ifndef HELIKON_LIB_FORMAT_H
#define HELIKON_LIB_FORMAT_H

#include <stddef.h>
#include <stdint.h>

size_t helikon_format_u32(char *buffer, size_t size, uint32_t value);
size_t helikon_format_u64(char *buffer, size_t size, uint64_t value);
size_t helikon_format_i32(char *buffer, size_t size, int32_t value);
size_t helikon_format_hex32(char *buffer, size_t size, uint32_t value,
                            unsigned int digits);

#endif
