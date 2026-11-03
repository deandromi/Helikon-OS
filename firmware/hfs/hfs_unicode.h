/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_UNICODE_H
#define HELIKON_HFS_UNICODE_H

#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Decode an HFS+ UTF-16BE name into UTF-8. This is an exact-byte decode, not a
 * case-folding or normalization policy. Invalid surrogates and NUL are rejected.
 */
int hfs_utf16be_to_utf8(const uint8_t *source, size_t units, char *dest,
                        size_t dest_size);

#endif
