#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the small FLAC host-test fixtures.

The fixtures are committed so the decoder test does not depend on a FLAC
encoder at test time. Run this script from the repository root to recreate
them.
"""

import math
import os
import struct
import subprocess
import wave

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "fixtures")


def pcm_bytes(samples, bits):
    data = bytearray()
    for frame in samples:
        for value in frame:
            if bits == 16:
                data += struct.pack("<h", value)
            else:
                value &= 0xFFFFFF
                data += bytes((value & 0xFF, (value >> 8) & 0xFF,
                               (value >> 16) & 0xFF))
    return data


def write_fixture(name, channels, rate, bits, samples):
    wav_path = os.path.join(OUT, name + ".wav")
    flac_path = os.path.join(OUT, name + ".flac")
    pcm_path = os.path.join(OUT, name + ".pcm")
    data = pcm_bytes(samples, bits)
    with wave.open(wav_path, "wb") as handle:
        handle.setnchannels(channels)
        handle.setsampwidth(bits // 8)
        handle.setframerate(rate)
        handle.writeframes(data)
    subprocess.run([
        "flac", "--force", "--silent", "--blocksize=1024",
        "--compression-level-5", "-o", flac_path, wav_path
    ], check=True)
    with open(pcm_path, "wb") as handle:
        handle.write(data)
    os.unlink(wav_path)
    print(name, "flac", os.path.getsize(flac_path),
          "pcm", os.path.getsize(pcm_path))


def stereo16():
    samples = [
        (round(12000.0 * math.sin(2.0 * math.pi * 440.0 * i / 44100.0)),
         round(9000.0 * math.sin(2.0 * math.pi * 660.0 * i / 44100.0 + 0.5)))
        for i in range(2048)
    ]
    write_fixture("stereo16", 2, 44100, 16, samples)


def mono16():
    samples = [(i * 7 - 3000,) for i in range(1024)]
    write_fixture("mono16", 1, 44100, 16, samples)


def stereo24():
    samples = [(i * 10000 - 5000000, -i * 5000 + 300000)
               for i in range(1200)]
    write_fixture("stereo24", 2, 48000, 24, samples)


def mono24():
    samples = [(i * 8000 - 2000000,) for i in range(800)]
    write_fixture("mono24", 1, 48000, 24, samples)


def constant16():
    samples = [(0, 0) for _ in range(512)]
    write_fixture("constant16", 2, 44100, 16, samples)


def verbatim16():
    samples = []
    value = 0x1234
    for _ in range(512):
        value = (value * 1103515245 + 12345) & 0xFFFFFFFF
        left = (value >> 8) & 0xFFFF
        value = (value * 1103515245 + 12345) & 0xFFFFFFFF
        right = (value >> 8) & 0xFFFF
        left = left - 0x10000 if left >= 0x8000 else left
        right = right - 0x10000 if right >= 0x8000 else right
        samples.append((left, right))
    write_fixture("verbatim16", 2, 44100, 16, samples)


def stereo16_48():
    samples = [
        (round(10000.0 * math.sin(2.0 * math.pi * 1000.0 * i / 48000.0)),
         round(8000.0 * math.sin(2.0 * math.pi * 1500.0 * i / 48000.0 + 0.7)))
        for i in range(1024)
    ]
    write_fixture("stereo16_48", 2, 48000, 16, samples)


def left_side16():
    samples = []
    for i in range(1024):
        right = round(10000.0 * math.sin(2.0 * math.pi * 440.0 * i / 44100.0))
        samples.append((right + 1, right))
    write_fixture("left_side16", 2, 44100, 16, samples)


def main():
    os.makedirs(OUT, exist_ok=True)
    stereo16()
    mono16()
    stereo24()
    mono24()
    constant16()
    verbatim16()
    stereo16_48()
    left_side16()


if __name__ == "__main__":
    main()
