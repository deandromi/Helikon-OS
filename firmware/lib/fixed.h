/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Integer fixed-point helpers (Q16.16).
 *
 * The firmware uses integer/fixed-point math until the 6G FPU question is
 * answered by the board audit. Scale is explicit: HELIKON_FIXED_ONE is 1.0.
 */
#ifndef HELIKON_LIB_FIXED_H
#define HELIKON_LIB_FIXED_H

#include <stdint.h>

#define HELIKON_FIXED_SHIFT 16
#define HELIKON_FIXED_ONE ((int32_t)1 << HELIKON_FIXED_SHIFT)

int32_t helikon_fixed_from_int(int32_t value);
int32_t helikon_fixed_to_int(int32_t value);
int32_t helikon_fixed_mul(int32_t left, int32_t right);
int32_t helikon_fixed_div(int32_t numerator, int32_t denominator);

#endif
