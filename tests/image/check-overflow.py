#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the overflow fixture as macOS sees it: 12 fragments, 12 patterns."""

import sys

BLOCK = 4096
BLOCKS = 12

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
print("macOS mount verified: 49152 bytes over 12 fragments")
