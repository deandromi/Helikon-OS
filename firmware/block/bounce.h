/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Byte-range reads over a sector device.
 *
 * The ATA PIO path wants sector-aligned buffers. This module hides that
 * constraint: an aligned range is read directly, an unaligned or short range
 * goes through the caller's scratch sector buffer. The caller owns the scratch
 * buffer and the output buffer; the module borrows both for the call.
 */
#ifndef HELIKON_BLOCK_BOUNCE_H
#define HELIKON_BLOCK_BOUNCE_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"

int helikon_block_read_bytes(const struct helikon_block_device *device,
                             uint64_t offset, void *buffer, size_t length,
                             uint8_t *scratch, size_t scratch_size,
                             uint32_t timeout_ms);

#endif
