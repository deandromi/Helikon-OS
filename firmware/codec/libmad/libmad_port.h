/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helikon-OS libmad port shim.
 *
 * Replaces the two Rockbox headers the vendored libmad includes:
 * `codeclib.h` (in `global.h` and `frame.c`) and `config.h` (in
 * `mad_iram.h`). The build is freestanding without IRAM placement, so the
 * placement attributes below are empty and everything lands in .text/.bss.
 */
#ifndef HELIKON_LIBMAD_PORT_H
#define HELIKON_LIBMAD_PORT_H

#include <stdint.h> /* freestanding inttypes.h replacement (see README.md) */
#include <string.h> /* memset/memcpy come from the freestanding lib */

/* Hosted builds (host tests) use libc limits.h; the freestanding target has
 * no <limits.h>, so the .c files fall back to their CHAR_BIT define and the
 * port supplies INT_MAX for bit.c's width check. */
#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__
#define HAVE_LIMITS_H 1
#else
#define INT_MAX 0x7fffffff
#define UINT_MAX 0xffffffffu
#endif

#define ICODE_ATTR
#define ICONST_ATTR
#define IDATA_ATTR
#define IBSS_ATTR
#define MEM_ALIGN_ATTR

/*
 * Rockbox's optimized mad_bit_read (bit.c) uses betoh32 for the plain-word
 * byte-ordered read. The stream words are big-endian in memory, so this is
 * a byteswap on little-endian hosts and identity on big-endian hosts.
 */
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && \
    defined(__ORDER_LITTLE_ENDIAN__) && __ORDER_LITTLE_ENDIAN__ == __BYTE_ORDER__
#define betoh32(x) __builtin_bswap32((uint32_t)(x))
#elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __ORDER_LITTLE_ENDIAN__ == __BYTE_ORDER__
#define betoh32(x) \
    (((uint32_t)(x) << 24) | (((uint32_t)(x) & 0xff00u) << 8) | \
     (((uint32_t)(x) >> 8) & 0xff00u) | ((uint32_t)(x) >> 24))
#else
#define betoh32(x) ((uint32_t)(x))
#endif

#endif /* HELIKON_LIBMAD_PORT_H */
