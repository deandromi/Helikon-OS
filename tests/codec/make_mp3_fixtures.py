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


def mpeg2_stereo16():
    """MPEG-2 Layer III: 22.05 kHz, 576 samples per frame."""
    rate = 22050
    samples = [
        (round(9000.0 * math.sin(2.0 * math.pi * 440.0 * i / rate)),
         round(7000.0 * math.sin(2.0 * math.pi * 660.0 * i / rate + 0.5)))
        for i in range(6 * 576)
    ]
    wav = write_wav("mp3_mpeg2_stereo16", 2, rate, 16, samples)
    encode(["--cbr", "-b", "64", "--resample", "22.05"], wav,
           "mp3_mpeg2_stereo16", None)


def mpeg25_stereo16():
    """MPEG-2.5 Layer III: 11.025 kHz, 576 samples per frame."""
    rate = 11025
    samples = [
        (round(7000.0 * math.sin(2.0 * math.pi * 330.0 * i / rate)),
         round(5000.0 * math.sin(2.0 * math.pi * 495.0 * i / rate + 0.4)))
        for i in range(4 * 576)
    ]
    wav = write_wav("mp3_mpeg25_stereo16", 2, rate, 16, samples)
    encode(["--cbr", "-b", "32", "--resample", "11.025"], wav,
           "mp3_mpeg25_stereo16", None)


def layer2_stereo44():
    """MPEG-1 Layer II: 44.1 kHz, 1152 samples per frame, no Xing frame.

    LAME encodes Layer III only, so this one goes through ffmpeg's MP2 encoder.
    The fixture exists to execute the Layer I/II path the wrapper advertises,
    not to claim a second encoder.
    """
    import shutil

    rate = 44100
    samples = [
        (round(11000.0 * math.sin(2.0 * math.pi * 440.0 * i / rate)),
         round(8000.0 * math.sin(2.0 * math.pi * 554.0 * i / rate + 0.2)))
        for i in range(2 * 1152)
    ]
    wav = write_wav("mp3_layer2_stereo44", 2, rate, 16, samples)
    mp3_path = os.path.join(OUT, "mp3_layer2_stereo44.mp3")
    if shutil.which("ffmpeg") is None:
        raise SystemExit("layer 2 fixture needs ffmpeg on PATH")
    # The mp2 encoder refuses the generic mp3 muxer here, so name the format.
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", wav,
                    "-c:a", "mp2", "-b:a", "128k", "-f", "mp2", mp3_path],
                   check=True)
    print("mp3_layer2_stereo44", "mp3", os.path.getsize(mp3_path))
    os.unlink(wav)


def main():
    os.makedirs(OUT, exist_ok=True)
    cbr_stereo16()
    vbr_stereo16()
    cbr_mono16()
    with_id3v2()
    mpeg2_stereo16()
    mpeg25_stereo16()
    layer2_stereo44()


if __name__ == "__main__":
    main()
