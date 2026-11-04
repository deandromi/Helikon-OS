/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "fixed.h"

int32_t helikon_fixed_from_int(int32_t value)
{
    return value * HELIKON_FIXED_ONE;
}

int32_t helikon_fixed_to_int(int32_t value)
{
    if (value >= 0)
        return value / HELIKON_FIXED_ONE;
    return -((-value) / HELIKON_FIXED_ONE);
}

int32_t helikon_fixed_mul(int32_t left, int32_t right)
{
    int64_t product = (int64_t)left * (int64_t)right;
    return (int32_t)(product / HELIKON_FIXED_ONE);
}

int32_t helikon_fixed_div(int32_t numerator, int32_t denominator)
{
    if (denominator == 0)
        return 0;
    int64_t scaled = (int64_t)numerator * HELIKON_FIXED_ONE;
    return (int32_t)(scaled / denominator);
}
