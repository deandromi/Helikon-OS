#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the AIFF/AIFF-C host-test fixtures.

The containers are written here rather than by ffmpeg on purpose: a fixture
built by the same reasoning as the parser is still worth having when the
container is this small, and it keeps the test independent of an encoder being
installed. The sample data is a deterministic ramp, one step per frame, so a
shifted or byte-swapped sample is a number, not a noise floor.

Every fixture is paired with a `.pcm` file: interleaved stereo 16-bit
little-endian, which is what the decoder promises. The 24-bit fixtures carry
16-bit values shifted left by eight, so "keep the top 16 bits" and "round to
nearest" give the same answer and the test is about the shift, not about a
dithering policy.

The 80-bit sample rate is asserted against known encodings below, because a
fixture generator that writes a wrong rate makes a correct decoder look broken.

Run from the repository root:

    python3 tests/codec/make_aiff_fixtures.py
"""

import os
import struct

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "fixtures")

FRAME_STEP = 3  # one ramp step per frame: small enough to stay in range


def extended_rate(rate):
    """Encode a sample rate as the 80-bit extended float AIFF uses."""
    if rate <= 0:
        raise ValueError("rate must be positive")
    exponent = rate.bit_length() - 1
    mantissa = rate << (63 - exponent)
    encoded = struct.pack(">HQ", 16383 + exponent, mantissa)
    if mantissa & (1 << 63) == 0:
        raise ValueError("mantissa lost its integer bit")
    return encoded


def check_rates():
    known = {
        44100: bytes.fromhex("400eac44000000000000"),
        48000: bytes.fromhex("400ebb80000000000000"),
        22050: bytes.fromhex("400dac44000000000000"),
    }
    for rate, expected in known.items():
        got = extended_rate(rate)
        if got != expected:
            raise SystemExit(
                "80-bit rate for %d is %s, expected %s"
                % (rate, got.hex(), expected.hex())
            )


def samples(count, channels, bits):
    """A ramp whose left and right channels differ, so swaps are visible."""
    frames = []
    for index in range(count):
        if bits == 24:
            left = ((index * FRAME_STEP) << 8) & 0xFFFFFF
            right = ((index * FRAME_STEP + 1) << 8) & 0xFFFFFF
        elif bits == 32:
            # 32-bit float fixtures scale down by 32768; the reference is the
            # 16-bit value itself.
            left = index * FRAME_STEP
            right = index * FRAME_STEP + 1
            if left >= 32768:
                left %= 32768
            if right >= 32768:
                right %= 32768
        else:
            left = index * FRAME_STEP
            right = index * FRAME_STEP + 1
        frames.append((left, right))
    if channels == 1:
        return [(frame[0],) for frame in frames]
    return frames


def pcm_16le(frames):
    """The reference: stereo 16-bit little-endian, mono duplicated."""
    data = bytearray()
    for frame in frames:
        left = frame[0]
        right = frame[-1]
        data += struct.pack("<hh", left, right)
    return bytes(data)


def pcm_16be(frames, bits):
    """Source bytes, big-endian, at the fixture's sample width."""
    data = bytearray()
    for frame in frames:
        for value in frame:
            if bits == 8:
                value &= 0xFF
                data += struct.pack(">B", value)
            elif bits == 16:
                data += struct.pack(">h", value)
            elif bits == 24:
                value &= 0xFFFFFF
                data += bytes(((value >> 16) & 0xFF, (value >> 8) & 0xFF,
                               value & 0xFF))
            else:
                raise ValueError("no big-endian writer for %d bits" % bits)
    return bytes(data)


def pcm_16le_source(frames):
    data = bytearray()
    for frame in frames:
        for value in frame:
            data += struct.pack("<h", value)
    return bytes(data)


def pcm_float32be(frames):
    data = bytearray()
    for frame in frames:
        for value in frame:
            data += struct.pack(">f", value / 32768.0)
    return bytes(data)


def chunk(identifier, payload):
    data = identifier + struct.pack(">I", len(payload)) + payload
    if len(payload) & 1:
        data += b"\x00"
    return data


def comm(frames, channels, bits, rate, compression=None):
    payload = struct.pack(">hIh", channels, frames, bits) + extended_rate(rate)
    if compression is not None:
        payload += compression
        name = b"Helikon fixture"
        payload += bytes((len(name),)) + name
        if len(payload) & 1:
            payload += b"\x00"
    return chunk(b"COMM", payload)


def ssnd(payload, offset=0, block_size=0):
    return chunk(
        b"SSND", struct.pack(">II", offset, block_size) + payload
    )


def write(name, chunks, form=b"AIFF"):
    body = b"".join(chunks)
    data = b"FORM" + struct.pack(">I", len(body) + 4) + form + body
    path = os.path.join(OUT, name)
    with open(path, "wb") as handle:
        handle.write(data)
    return path


def report(name, size, frames):
    print("%-28s %6d bytes  %4d frames" % (name, size, frames))


def main():
    check_rates()
    os.makedirs(OUT, exist_ok=True)

    # 1. AIFF 16-bit stereo, with a pad-length chunk before COMM so the walker
    #    has to skip an odd-sized chunk and its pad byte.
    frames = samples(1024, 2, 16)
    name = "aiff_stereo16.aiff"
    path = write(name, [
        chunk(b"NAME", b"helikon"),
        comm(len(frames), 2, 16, 44100),
        ssnd(pcm_16be(frames, 16)),
    ])
    open(os.path.join(OUT, "aiff_stereo16.pcm"), "wb").write(pcm_16le(frames))
    report(name, os.path.getsize(path), len(frames))

    # 2. AIFF 16-bit mono: the decoder must duplicate the channel.
    frames = samples(512, 1, 16)
    name = "aiff_mono16.aiff"
    path = write(name, [
        comm(len(frames), 1, 16, 44100),
        ssnd(pcm_16be(frames, 16)),
    ])
    open(os.path.join(OUT, "aiff_mono16.pcm"), "wb").write(pcm_16le(frames))
    report(name, os.path.getsize(path), len(frames))

    # 3. AIFF 24-bit stereo: the top 16 bits are the reference value.
    frames = samples(512, 2, 24)
    name = "aiff_stereo24.aiff"
    path = write(name, [
        comm(len(frames), 2, 24, 48000),
        ssnd(pcm_16be(frames, 24)),
    ])
    reference = [(frame[0] >> 8, frame[1] >> 8) for frame in frames]
    open(os.path.join(OUT, "aiff_stereo24.pcm"), "wb").write(
        pcm_16le(reference)
    )
    report(name, os.path.getsize(path), len(frames))

    # 4. AIFF-C 'sowt': the same samples little-endian, with an FVER chunk in
    #    front, which is what Apple writes.
    frames = samples(1024, 2, 16)
    name = "aiff_sowt16.aifc"
    path = write(name, [
        chunk(b"FVER", struct.pack(">I", 0xA2805140)),
        comm(len(frames), 2, 16, 44100, b"sowt"),
        ssnd(pcm_16le_source(frames)),
    ], b"AIFC")
    open(os.path.join(OUT, "aiff_sowt16.pcm"), "wb").write(pcm_16le(frames))
    report(name, os.path.getsize(path), len(frames))

    # 5. AIFF-C 'fl32': float samples that are exact multiples of 1/32768, so
    #    multiplying back is exact and the reference is the 16-bit value.
    frames = samples(256, 2, 32)
    name = "aiff_fl32.aifc"
    path = write(name, [
        chunk(b"FVER", struct.pack(">I", 0xA2805140)),
        comm(len(frames), 2, 32, 44100, b"fl32"),
        ssnd(pcm_float32be(frames)),
    ], b"AIFC")
    reference = [(frame[0] % 32768, frame[1] % 32768) for frame in frames]
    open(os.path.join(OUT, "aiff_fl32.pcm"), "wb").write(pcm_16le(reference))
    report(name, os.path.getsize(path), len(frames))

    # 6. AIFF 8-bit mono: signed samples, scaled to 16 bits by << 8.
    frames = [((index - 128),) for index in range(256)]
    name = "aiff_8bit.aiff"
    path = write(name, [
        comm(len(frames), 1, 8, 22050),
        ssnd(pcm_16be(frames, 8)),
    ])
    # The decoder scales a signed byte to 16 bits with << 8.
    reference = [(frame[0] << 8, frame[0] << 8) for frame in frames]
    open(os.path.join(OUT, "aiff_8bit.pcm"), "wb").write(pcm_16le(reference))
    report(name, os.path.getsize(path), len(frames))

    # 7. SSND before COMM: a tag writer that appends can produce this order,
    #    and the walker must not depend on which chunk comes first.
    frames = samples(256, 2, 16)
    name = "aiff_ssnd_first.aiff"
    path = write(name, [
        ssnd(pcm_16be(frames, 16)),
        comm(len(frames), 2, 16, 44100),
    ])
    open(os.path.join(OUT, "aiff_ssnd_first.pcm"), "wb").write(pcm_16le(frames))
    report(name, os.path.getsize(path), len(frames))

    # 8. A short sound chunk: COMM claims 1024 frames, SSND carries 512. The
    #    decoder reports 512 and plays 512; it must not read past the data.
    frames = samples(1024, 2, 16)
    name = "aiff_short_data.aiff"
    path = write(name, [
        comm(len(frames), 2, 16, 44100),
        ssnd(pcm_16be(frames[:512], 16)),
    ])
    open(os.path.join(OUT, "aiff_short_data.pcm"), "wb").write(
        pcm_16le(frames[:512])
    )
    report(name, os.path.getsize(path), 512)

    # 9. An AIFC compression the decoder refuses: recognized container,
    #    unsupported codec. The probe must say UNSUPPORTED, not NOT_FOUND.
    frames = samples(64, 2, 16)
    name = "aiff_alaw.aifc"
    path = write(name, [
        comm(len(frames), 2, 16, 44100, b"alaw"),
        ssnd(bytes(128)),
    ], b"AIFC")
    report(name, os.path.getsize(path), 0)


if __name__ == "__main__":
    main()
