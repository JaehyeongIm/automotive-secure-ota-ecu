#!/usr/bin/env python3
"""
Signed-image attack fixture generator — SIT-TC-06(변조)/07(미서명).

서명 이미지 레이아웃(sign_firmware.py, ADR-007):
    [ Image Header 0x200 ][ firmware ][ ECDSA sig 64B ]
서명은 (header+firmware) 64B(r||s)가 마지막에 붙는다.

용법:
    # SIT-TC-06: 코드 1바이트 변조(서명 유지) → 부트로더가 ECDSA FAILED로 거부해야 함
    python3 forge_image.py signed.bin --tamper            # 기본 offset=0x200(첫 앱 바이트)
    python3 forge_image.py signed.bin --tamper 0x250      # offset 지정

    # SIT-TC-07: 서명 64B를 0으로 → REQUIRED 게이트가 ECDSA 실패로 거부해야 함
    python3 forge_image.py signed.bin --unsign

출력 기본: <name>_tampered.bin / <name>_unsigned.bin
"""
import argparse
import sys
from pathlib import Path

IMG_HEADER_SIZE = 0x200
SIG_LEN = 64


def tamper(data: bytes, offset: int) -> bytes:
    if offset >= len(data) - SIG_LEN:
        raise ValueError(
            f"offset 0x{offset:X}는 서명영역(0..0x{len(data)-SIG_LEN:X}) 밖 — "
            f"서명이 덮는 부분을 변조해야 ECDSA가 실패함")
    b = bytearray(data)
    b[offset] ^= 0xFF                      # 1바이트 비트 반전
    return bytes(b)


def unsign(data: bytes) -> bytes:
    b = bytearray(data)
    b[-SIG_LEN:] = b"\x00" * SIG_LEN       # 마지막 64B 서명 = 0
    return bytes(b)


def main() -> int:
    ap = argparse.ArgumentParser(description="서명 이미지 공격 픽스처 생성 (SIT-TC-06/07)")
    ap.add_argument("image", help="입력 서명 .bin ([header][code][sig64])")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--tamper", nargs="?", type=lambda x: int(x, 0), const=IMG_HEADER_SIZE,
                   metavar="OFFSET", help=f"서명영역 1바이트 변조 (기본 0x{IMG_HEADER_SIZE:X})")
    g.add_argument("--unsign", action="store_true", help="서명 64B를 0으로")
    ap.add_argument("--out", help="출력 경로(미지정 시 자동)")
    args = ap.parse_args()

    src = Path(args.image)
    data = src.read_bytes()
    if len(data) <= IMG_HEADER_SIZE + SIG_LEN:
        print(f"[ERR] 입력이 너무 작음({len(data)}B) — [header 0x{IMG_HEADER_SIZE:X}][code][sig 64] 형식이 아님",
              file=sys.stderr)
        return 1

    if args.unsign:
        out = unsign(data)
        default = src.with_name(src.stem + "_unsigned.bin")
        print(f"[FORGE] 서명 64B → 0  (size={len(out)}B, 마지막 블록 무효 서명)")
    else:
        out = tamper(data, args.tamper)
        default = src.with_name(src.stem + "_tampered.bin")
        print(f"[FORGE] 변조 offset=0x{args.tamper:X} (byte XOR 0xFF, 서명 유지)")

    dst = Path(args.out) if args.out else default
    dst.write_bytes(out)
    print(f"[FORGE] wrote {dst} — 부트로더는 '[BL] ECDSA FAILED … refusing'로 거부해야 함")
    return 0


if __name__ == "__main__":
    sys.exit(main())
