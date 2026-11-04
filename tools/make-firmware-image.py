#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the F1 firmware manifest, bootstate and signed development image.

The private key stays under build/ and is never committed. If the loader test
build has already generated build/loader-bootloader/test-key.pem, this script
reuses it so the existing DFU loader test image can verify the firmware.
"""

import argparse
import hashlib
import os
import struct
import sys

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

MANIFEST_SIZE = 256
MANIFEST_SIGNED = 192
MANIFEST_SIGNATURE = 64
BOOTSTATE_SIZE = 512
SLOT_SIZE = 256
LOAD_ADDRESS = 0x08000000


def load_or_generate_key(path):
    if os.path.exists(path):
        with open(path, "rb") as handle:
            return serialization.load_pem_private_key(handle.read(),
                                                      password=None)
    key = Ed25519PrivateKey.generate()
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(key.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption()))
    os.chmod(path, 0o600)
    return key


def build_manifest(image, board, version, load, key_id):
    manifest = bytearray(MANIFEST_SIZE)
    manifest[0:8] = b"HELIKON1"
    struct.pack_into("<I", manifest, 8, 1)       # manifest version
    struct.pack_into("<I", manifest, 12, board)
    struct.pack_into("<I", manifest, 16, version)
    struct.pack_into("<I", manifest, 20, load)
    struct.pack_into("<I", manifest, 24, len(image))
    struct.pack_into("<I", manifest, 28, key_id)
    manifest[32:64] = hashlib.sha256(image).digest()
    struct.pack_into("<I", manifest, 64, 1)      # development-only
    return manifest


def build_bootstate(image, board, version, load):
    bootstate = bytearray(BOOTSTATE_SIZE)
    slot = bootstate[0:SLOT_SIZE]
    slot[0:4] = b"HBST"
    struct.pack_into("<I", slot, 4, 1)           # bootstate version
    struct.pack_into("<I", slot, 8, 1)           # sequence
    struct.pack_into("<I", slot, 12, board)
    struct.pack_into("<I", slot, 16, version)
    struct.pack_into("<I", slot, 20, load)
    struct.pack_into("<I", slot, 24, len(image))
    slot[28:60] = hashlib.sha256(image).digest()
    slot[60] = 2                                 # GOOD
    slot[64:96] = hashlib.sha256(slot[:64]).digest()
    bootstate[0:SLOT_SIZE] = slot
    return bootstate


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--board", required=True,
                        help="board ID or name: 6, 6g, ipod6g")
    parser.add_argument("--version", required=True, type=int)
    parser.add_argument("--load", required=True, type=lambda v: int(v, 0))
    parser.add_argument("--key-id", type=lambda v: int(v, 0),
                        default=0x48454C01)
    parser.add_argument("--key", default=None)
    args = parser.parse_args()

    if args.board not in ("6", "6g", "ipod6g"):
        print("error: F1 build supports board 6 only", file=sys.stderr)
        return 1
    board = 6
    if args.load != LOAD_ADDRESS:
        print("error: the accepted load address is 0x08000000",
              file=sys.stderr)
        return 1

    with open(args.image, "rb") as handle:
        image = handle.read()
    if not image:
        print("error: the raw image is empty", file=sys.stderr)
        return 1

    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    if args.key:
        key_path = args.key
    else:
        loader_key = os.path.join(root, "build", "loader-bootloader",
                                  "test-key.pem")
        key_path = (loader_key if os.path.exists(loader_key)
                    else os.path.join(root, "build", "firmware",
                                      "test-key.pem"))
    key = load_or_generate_key(key_path)
    public_key = key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)

    manifest = build_manifest(image, board, args.version, args.load,
                              args.key_id)
    manifest[MANIFEST_SIGNED:MANIFEST_SIZE] = key.sign(
        bytes(manifest[:MANIFEST_SIGNED]))
    bootstate = build_bootstate(image, board, args.version, args.load)

    # Verify the signature and the checksum before writing a single output.
    key.public_key().verify(bytes(manifest[MANIFEST_SIGNED:]),
                            bytes(manifest[:MANIFEST_SIGNED]))
    assert hashlib.sha256(image).digest() == manifest[32:64]
    assert hashlib.sha256(bootstate[:64]).digest() == bootstate[64:96]

    os.makedirs(args.out, exist_ok=True)
    outputs = {
        "firmware.img": image,
        "firmware.manifest": bytes(manifest),
        "bootstate": bytes(bootstate),
    }
    for name, data in outputs.items():
        with open(os.path.join(args.out, name), "wb") as handle:
            handle.write(data)

    with open(os.path.join(args.out, "test-public-key.txt"), "w",
              encoding="utf-8") as handle:
        handle.write("key id: 0x%08x\npublic key: %s\n"
                     % (args.key_id, public_key.hex()))

    lines = []
    for name in ("firmware.img", "firmware.manifest", "bootstate"):
        with open(os.path.join(args.out, name), "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        lines.append("%s  %s" % (digest, name))
    with open(os.path.join(args.out, "SHA256SUMS"), "w",
              encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")

    print("firmware:   %d bytes" % len(image))
    print("image sha:  %s" % hashlib.sha256(image).hexdigest())
    print("manifest:   %s" % hashlib.sha256(bytes(manifest)).hexdigest())
    print("bootstate:  %s" % hashlib.sha256(bytes(bootstate)).hexdigest())
    print("key id:     0x%08x" % args.key_id)
    print("key file:   %s (not committed)" % key_path)
    print("public key: %s" % public_key.hex())
    return 0


if __name__ == "__main__":
    sys.exit(main())
