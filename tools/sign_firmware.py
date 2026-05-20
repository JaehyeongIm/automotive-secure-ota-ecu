#!/usr/bin/env python3
"""
Firmware signing tool for STM32 DriveECU / SensorECU OTA.

Usage:
    python sign_firmware.py <firmware.bin> <private_key.pem> [--out signed.bin]

Output:
    signed.bin = original firmware + 64-byte ECDSA-P256 signature (r || s)

The Bootloader verifies:
    SHA-256(firmware bytes) -> uECC_verify(pubkey, hash, last-64-bytes)
"""

import argparse
import hashlib
import sys
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature


def sign(firmware_path: str, key_path: str, out_path: str) -> None:
    firmware = Path(firmware_path).read_bytes()

    with open(key_path, "rb") as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None)

    digest = hashlib.sha256(firmware).digest()
    print(f"[SIGN] SHA-256: {digest.hex()}")

    der_sig = private_key.sign(firmware, ec.ECDSA(hashes.SHA256()))

    # DER → raw (r || s) each 32 bytes
    r, s = decode_dss_signature(der_sig)
    raw_sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")
    print(f"[SIGN] Signature (r): {raw_sig[:32].hex()}")
    print(f"[SIGN] Signature (s): {raw_sig[32:].hex()}")

    signed = firmware + raw_sig
    Path(out_path).write_bytes(signed)
    print(f"[SIGN] Output: {out_path}  ({len(firmware)} + 64 = {len(signed)} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Sign STM32 firmware with ECDSA-P256")
    parser.add_argument("firmware", help="Input .bin file")
    parser.add_argument("key", help="ECDSA private key .pem file")
    parser.add_argument("--out", help="Output signed .bin (default: firmware_signed.bin)")
    args = parser.parse_args()

    out = args.out or args.firmware.replace(".bin", "_signed.bin")
    sign(args.firmware, args.key, out)


if __name__ == "__main__":
    main()
