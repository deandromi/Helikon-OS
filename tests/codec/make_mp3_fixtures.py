#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the small MP3 host-test fixtures.

Run from the repository root (requires the `lame` encoder on PATH):

    python3 tests/codec/make_mp3_fixtures.py

The MP3 fixtures are committed so the decode test does not depend on an
encoder at test time. LAME writes a Xing/Info frame in every fixture, which
the MP3 codec wrapper skips on output.
"""

import math
import os
import struct
import subprocess
import wave

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "fixtures")


def write_wav(name, channels, rate, bits, samples):
    data = bytearray()
    for frame in samples:
        for value in frame:
            data += struct.pack("<h", value)
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as handle:
        handle.setnchannels(channels)
        handle.setsampwidth(bits // 8)
        handle.setframerate(rate)
        handle.writeframes(data)
    return path


def encode(lame_args, wav_path, mp3_name, keep_pcm):
    mp3_path = os.path.join(OUT, mp3_name + ".mp3")
    subprocess.run(["lame", "--silent", *lame_args, wav_path, mp3_path],
                   check=True)
    size = os.path.getsize(mp3_path)
    print(mp3_name, "mp3", size)
    os.unlink(wav_path)
    if keep_pcm is not None:
        pcm_path = os.path.join(OUT, keep_pcm)
        with open(pcm_path, "wb") as handle:
            pass
        print(keep_pcm, 0)


def cbr_stereo16():
    rate = 44100
    samples = [
        (round(12000.0 * math.sin(2.0 * math.pi * 440.0 * i / rate)),
         round(9000.0 * math.sin(2.0 * math.pi * 660.0 * i / rate + 0.5)))
        for i in range(4096)
    ]
    wav = write_wav("mp3_cbr_stereo16", 2, rate, 16, samples)
    encode(["--cbr", "-b", "128", "--resample", "44.1"], wav,
           "mp3_cbr_stereo16", None)


def vbr_stereo16():
    rate = 44100
    # Long enough that a mid-file seek leaves several frames after the
    # bit-reservoir rebuild drop (the first frames after a seek fail with
    # BADDATAPTR and produce no output; the chain rebuilds after them).
    samples = [
        (round(10000.0 * math.sin(2.0 * math.pi * 523.0 * i / rate)),
         round(8000.0 * math.sin(2.0 * math.pi * 784.0 * i / rate + 0.3)))
        for i in range(8 * 1152)
    ]
    wav = write_wav("mp3_vbr_stereo16", 2, rate, 16, samples)
    encode(["-V", "5", "--resample", "44.1"], wav,
           "mp3_vbr_stereo16", None)


def cbr_mono16():
    rate = 44100
    samples = [(round(11000.0 * math.sin(2.0 * math.pi * 440.0 * i / rate)),)
               for i in range(2304)]
    wav = write_wav("mp3_cbr_mono16", 1, rate, 16, samples)
    encode(["-m", "m", "--cbr", "-b", "64", "--resample", "44.1"], wav,
           "mp3_cbr_mono16", None)


def with_id3v2():
    rate = 44100
    samples = [
        (round(12000.0 * math.sin(2.0 * math.pi * 440.0 * i / rate)),
         round(9000.0 * math.sin(2.0 * math.pi * 660.0 * i / rate + 0.5)))
        for i in range(2304)
    ]
    wav = write_wav("mp3_id3_stereo16", 2, rate, 16, samples)
    encode(["--cbr", "-b", "128", "--resample", "44.1", "--add-id3v2"],
           wav, "mp3_id3_stereo16", None)


def main():
    os.makedirs(OUT, exist_ok=True)
    cbr_stereo16()
    vbr_stereo16()
    cbr_mono16()
    with_id3v2()


if __name__ == "__main__":
    main()
