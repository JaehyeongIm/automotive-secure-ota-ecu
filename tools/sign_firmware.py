#!/usr/bin/env python3
"""
Firmware signing tool for STM32 DriveECU / SensorECU OTA.

Usage:
    python sign_firmware.py <firmware.bin> <private_key.pem> \
        [--version N] [--ecu-id ID] [--out signed.bin]

Signed image layout (front header / MCUboot 정석, ADR-007):

    [ Image Header (IMG_HEADER_SIZE=0x200) ][ firmware (vector table+code) ][ ECDSA sig 64B ]
      magic, fw_version, ecu_id ...padding...   ← app linked at slot+0x200

    - The 64-byte ECDSA-P256 signature (r||s) covers (header + firmware).
    - fw_version sits inside the signed header → tamper-proof, read before exec
      by the bootloader for anti-rollback (FR-BL-008 / FR-AB-008).

The Bootloader verifies:
    SHA-256(header+firmware) -> uECC_verify(pubkey, hash, last-64-bytes)
then reads the header at slot+0 and refuses to boot a downgrade.
"""

import argparse
import struct
import sys
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

IMG_HEADER_MAGIC = 0x4F544148  # 'OTAH'  (ota_meta.h와 동일)
IMG_HEADER_SIZE = 0x200        # 헤더 예약영역(VTOR 0x200 정렬); 앱은 slot+0x200


def build_header(version: int, ecu_id: int, header_size: int) -> bytes:
    """16바이트 구조체(magic, fw_version, ecu_id, reserved) + 0x00 패딩."""
    hdr = struct.pack("<4I", IMG_HEADER_MAGIC, version, ecu_id, 0)
    if len(hdr) > header_size:
        raise ValueError("header struct larger than IMG_HEADER_SIZE")
    return hdr + b"\x00" * (header_size - len(hdr))


def sign(firmware_path: str, key_path: str, out_path: str,
         version: int, ecu_id: int, header_size: int) -> None:
    firmware = Path(firmware_path).read_bytes()

    with open(key_path, "rb") as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None)

    header = build_header(version, ecu_id, header_size)
    image = header + firmware          # [header][code] — 서명 대상

    der_sig = private_key.sign(image, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_sig)
    raw_sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")

    signed = image + raw_sig           # [header][code][sig]
    Path(out_path).write_bytes(signed)

    # 자가검증: 방금 만든 서명이 (header+code)에 대해 유효한지 확인
    private_key.public_key().verify(der_sig, image, ec.ECDSA(hashes.SHA256()))

    print(f"[SIGN] header: magic=OTAH version={version} ecu_id={ecu_id} "
          f"size=0x{header_size:X}")
    print(f"[SIGN] signature (r): {raw_sig[:32].hex()}")
    print(f"[SIGN] signature (s): {raw_sig[32:].hex()}")
    print(f"[SIGN] self-verify: OK")
    print(f"[SIGN] output: {out_path}  "
          f"(0x{header_size:X} + {len(firmware)} + 64 = {len(signed)} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Sign STM32 firmware with ECDSA-P256 (front header)")
    parser.add_argument("firmware", help="Input .bin file (app linked at slot+0x200)")
    parser.add_argument("key", help="ECDSA private key .pem file")
    parser.add_argument("--version", type=int, default=1,
                        help="firmware version for anti-rollback (default 1)")
    parser.add_argument("--ecu-id", type=int, default=0,
                        help="target ECU id (1=DRIVE, 2=SENSOR, 0=any)")
    parser.add_argument("--header-size", type=lambda x: int(x, 0), default=IMG_HEADER_SIZE,
                        help=f"header reserved size (default 0x{IMG_HEADER_SIZE:X})")
    parser.add_argument("--out", help="Output signed .bin (default: firmware_signed.bin)")
    args = parser.parse_args()

    out = args.out or args.firmware.replace(".bin", "_signed.bin")
    sign(args.firmware, args.key, out, args.version, args.ecu_id, args.header_size)


if __name__ == "__main__":
    main()
