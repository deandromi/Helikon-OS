#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Mint a Helikon-OS release-key certificate or revocation bundle.

The bootloader contains verification code only. This is an offline, host-side
tool: it takes a PKCS8 Ed25519 private key, signs one fixed-size structure and
writes the bytes the loader parses. Nothing here runs on the device.

Certificate layout (big-endian, 120 bytes):

    offset  size  field
    0       4     version (1)
    4       4     key_id
    8       4     issuer_id
    12      4     first_image_version
    16      4     last_image_version
    20      4     flags (bit 0: may sign development images)
    24      32    public key
    56      64    Ed25519 signature over bytes 0..55

Revocation bundle layout (big-endian, 140 bytes):

    offset  size  field
    0       4     version (1)
    4       4     serial
    8       4     count (0..16)
    12      64    revoked key ids, unused entries zero
    76      64    Ed25519 signature over bytes 0..75

Signing is deterministic for Ed25519: the same key and fields always produce
the same bytes, so a certificate can be rebuilt and compared.
"""

import argparse
import sys

try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import (
        Ed25519PrivateKey,
    )
    from cryptography.hazmat.primitives.serialization import (
        load_pem_private_key,
    )
except ImportError:  # pragma: no cover - host tool
    sys.exit("error: Python 'cryptography' is required")

CERTIFICATE_VERSION = 1
CERTIFICATE_SIGNED = 56
CERTIFICATE_SIZE = 120

REVOCATIONS_VERSION = 1
REVOCATIONS_MAX = 16
REVOCATIONS_SIGNED = 12 + REVOCATIONS_MAX * 4
REVOCATIONS_SIZE = REVOCATIONS_SIGNED + 64

FLAG_DEVELOPMENT = 0x00000001


def put32(value):
    if value < 0 or value > 0xFFFFFFFF:
        raise ValueError(f"value out of range: {value}")
    return value.to_bytes(4, "big")


def load_key(path):
    with open(path, "rb") as handle:
        data = handle.read()
    key = load_pem_private_key(data, password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise SystemExit("error: the key is not Ed25519")
    return key


def build_certificate(key, args):
    if args.first_image_version > args.last_image_version:
        raise SystemExit("error: the first image version is above the last")
    body = b"".join(
        (
            put32(CERTIFICATE_VERSION),
            put32(args.key_id),
            put32(args.issuer_id),
            put32(args.first_image_version),
            put32(args.last_image_version),
            put32(args.flags),
        )
    )
    from cryptography.hazmat.primitives import serialization

    public_key = key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )
    # The certificate carries the *release* key, so the signing key and the
    # certified key are usually different files. --key signs; --release-key
    # supplies the certified public key when they differ.
    body += public_key
    if len(body) != CERTIFICATE_SIGNED:
        raise SystemExit("internal error: certificate body length")
    return body, body + key.sign(body)


def build_revocations(key, args):
    ids = list(args.revoked or [])
    if len(ids) > REVOCATIONS_MAX:
        raise SystemExit(f"error: at most {REVOCATIONS_MAX} revoked keys")
    if len(set(ids)) != len(ids):
        raise SystemExit("error: duplicate revoked key id")
    body = put32(REVOCATIONS_VERSION) + put32(args.serial) + put32(len(ids))
    body += b"".join(put32(key_id) for key_id in ids)
    body += b"\x00" * ((REVOCATIONS_MAX - len(ids)) * 4)
    if len(body) != REVOCATIONS_SIGNED:
        raise SystemExit("internal error: revocation body length")
    return body + key.sign(body)


def emit(raw, path, as_c):
    if as_c:
        lines = []
        for offset in range(0, len(raw), 16):
            chunk = raw[offset:offset + 16]
            lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk))
        text = ",\n".join(lines)
        if path is None:
            print(text)
        else:
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text + "\n")
        return
    if path is None:
        print(raw.hex())
    else:
        with open(path, "wb") as handle:
            handle.write(raw)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--key", required=True,
                        help="PKCS8 PEM Ed25519 key that signs the output")
    parser.add_argument("--release-key",
                        help="PKCS8 PEM key to certify (default: --key)")
    parser.add_argument("--key-id", type=lambda text: int(text, 0))
    parser.add_argument("--issuer-id", type=lambda text: int(text, 0))
    parser.add_argument("--first-image-version", type=lambda text: int(text, 0))
    parser.add_argument("--last-image-version", type=lambda text: int(text, 0))
    parser.add_argument("--flags", type=lambda text: int(text, 0), default=0)
    parser.add_argument("--revoked", action="append", type=lambda text: int(text, 0),
                        help="revoked key id; repeat for each one")
    parser.add_argument("--serial", type=lambda text: int(text, 0), default=1)
    parser.add_argument("--out", help="output file (default: hex on stdout)")
    parser.add_argument("--c", action="store_true",
                        help="emit a C initialiser instead of raw bytes")
    args = parser.parse_args()

    signing_key = load_key(args.key)
    wants_certificate = args.key_id is not None

    if wants_certificate and args.revoked:
        sys.exit("error: mint one certificate or one revocation bundle")
    if not wants_certificate and not args.revoked:
        sys.exit("error: nothing to mint")

    if wants_certificate:
        for field in ("issuer_id", "first_image_version",
                      "last_image_version"):
            if getattr(args, field) is None:
                sys.exit(f"error: --{field.replace('_', '-')} is required")
        certified = load_key(args.release_key) if args.release_key else signing_key
        body, raw = build_certificate(certified, args)
        if signing_key is not certified and len(body) != CERTIFICATE_SIGNED:
            sys.exit("internal error")
        # Re-sign with the signing key when the certified key differs.
        if signing_key is not certified:
            raw = body + signing_key.sign(body)
    else:
        raw = build_revocations(signing_key, args)

    emit(raw, args.out, args.c)


if __name__ == "__main__":
    main()
