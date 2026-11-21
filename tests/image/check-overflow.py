#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the overflow fixture as macOS sees it: one pattern per fragment.

With the second argument ``overwritten`` the two in-place windows that
``hfs_image_test overwrite`` writes are expected to hold the marker byte
instead, which is how the host driver's own read-back is cross-checked by an
independent reader.
"""

import sys

BLOCK = 4096
BLOCKS = 80
MARKER = 0xA5
# Must match OVERWRITE_* in hfs_image_test.c.
WINDOWS = ((8 * BLOCK - 100, 300), (20 * BLOCK + 7, 100))

overwritten = len(sys.argv) > 2 and sys.argv[2] == "overwritten"

path = sys.argv[1]
with open(path, "rb") as handle:
    data = handle.read()
if len(data) != BLOCK * BLOCKS:
    raise SystemExit(f"overflow size mismatch: {len(data)}")


def in_window(index):
    if not overwritten:
        return False
    return any(start <= index < start + length for start, length in WINDOWS)


for block in range(BLOCKS):
    chunk = data[block * BLOCK:(block + 1) * BLOCK]
    base = block * BLOCK
    pattern = 0x40 + block
    for offset, byte in enumerate(chunk):
        expected = MARKER if in_window(base + offset) else pattern
        if byte != expected:
            raise SystemExit(
                f"overflow content mismatch in block {block} at {offset}")
if overwritten:
    print(f"macOS mount verified: two in-place windows in {BLOCK * BLOCKS} bytes"
          f" over {BLOCKS} fragments")
else:
    print(f"macOS mount verified: {BLOCK * BLOCKS} bytes over {BLOCKS} fragments")
