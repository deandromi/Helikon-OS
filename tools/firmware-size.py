#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Print the F1 firmware size report and verify the raw image length.

The ELF keeps .bss, stacks and the arena at 0x08800000. `objcopy -O binary`
is limited to the image sections, so the raw file must equal
__image_end - __image_start exactly. A mismatch usually means a loadable
section was added outside the accepted map.
"""

import argparse
import os
import re
import subprocess
import sys


def symbols(nm, elf):
    output = subprocess.check_output([nm, elf], text=True)
    found = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                found[parts[2]] = int(parts[0], 16)
            except ValueError:
                continue
    return found


def require(found, name):
    if name not in found:
        print(f"error: symbol {name} not found in the ELF", file=sys.stderr)
        sys.exit(1)
    return found[name]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--raw", required=True)
    args = parser.parse_args()

    found = symbols(args.nm, args.elf)
    image_start = require(found, "__image_start")
    image_end = require(found, "__image_end")
    bss_start = require(found, "__bss_start")
    bss_end = require(found, "__bss_end")
    kernel_end = require(found, "__kernel_end")
    image_size = image_end - image_start
    raw_size = os.path.getsize(args.raw)

    print(f"image:       0x{image_start:08x}..0x{image_end:08x} "
          f"({image_size} bytes)")
    print(f"bss:         0x{bss_start:08x}..0x{bss_end:08x} "
          f"({bss_end - bss_start} bytes)")
    print(f"kernel end:  0x{kernel_end:08x}")
    print(f"image cap:   {0x087ff000 - image_start} bytes (8 MiB - 4 KiB)")
    print(f"headroom:    {0x087ff000 - image_end} bytes")
    print(f"raw image:   {raw_size} bytes")

    if raw_size != image_size:
        print("error: raw image length does not match __image_end - "
              "__image_start", file=sys.stderr)
        return 1
    if image_end > 0x087FF000:
        print("error: image overlaps the boot-info page", file=sys.stderr)
        return 1
    if kernel_end > 0x09000000:
        print("error: BSS/stacks exceed the kernel region", file=sys.stderr)
        return 1

    undefined = subprocess.check_output(
        [args.nm, "-u", args.elf], text=True, stderr=subprocess.DEVNULL)
    banned = re.compile(
        r"\b(malloc|free|printf|sprintf|snprintf|puts|putchar|fopen|open|"
        r"read|write|exit|abort|atexit|time|errno|__libc_\w+)\b")
    hits = [line for line in undefined.splitlines() if banned.search(line)]
    if hits:
        print("error: hosted symbols found in the firmware image:",
              file=sys.stderr)
        for line in hits:
            print("  " + line, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
