#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the overflow fixture as macOS sees it: one pattern per fragment."""

import sys

BLOCK = 4096
BLOCKS = 40

path = sys.argv[1]
with open(path, "rb") as handle:
    data = handle.read()
if len(data) != BLOCK * BLOCKS:
    raise SystemExit(f"overflow size mismatch: {len(data)}")
for block in range(BLOCKS):
    chunk = data[block * BLOCK:(block + 1) * BLOCK]
    expected = 0x40 + block
    if any(byte != expected for byte in chunk):
        raise SystemExit(f"overflow content mismatch in block {block}")
print(f"macOS mount verified: {BLOCK * BLOCKS} bytes over {BLOCKS} fragments")
