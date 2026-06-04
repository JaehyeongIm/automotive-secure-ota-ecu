#!/usr/bin/env python3
"""
Boot Metadata 빌더 — HIL 테스트 픽스처(특히 TC-03 fail-closed).

OTA_Metadata_t(ota_meta.h SSOT)를 그대로 재현하고 CRC32(zlib/ISO-HDLC)를 계산해
ST-Link로 주입 가능한 44바이트 메타 블롭을 만든다. 부트로더의 ota_meta_select는
'CRC 유효 + seq 최대' 사본을 고르므로, 위조 메타가 선택되게 하려면 seq를 높이고
양 사본(0x08008000, 0x0800C000)에 모두 써라.

레이아웃(little-endian, 11×uint32):
  magic, seq, active, a_status, b_status, a_version, b_version,
  boot_count, a_size, b_size, crc32(앞 10워드=40바이트에 대한 CRC)

사용 예:
  # TC-03: slot A는 CONFIRMED인데 size=0으로 위조 → 부트로더 fail-closed 거부 기대
  python forge_meta.py --preset size0-attack --out meta_size0.bin
  st-flash --reset write meta_size0.bin 0x08008000
  st-flash --reset write meta_size0.bin 0x0800C000   # 양 사본 모두 → 확실히 선택됨

  # 정상 CONFIRMED 상태 셋업(실제 플래시된 이미지 크기를 --a-size로)
  python forge_meta.py --preset confirmed-a --a-version 2 --a-size 0x8240 --out meta_ok.bin
"""

import argparse
import struct
import zlib

MAGIC = 0xDEADBEEF
STATUS = {
    "confirmed": 0xAAAAAAAA,
    "updated":   0xBBBBBBBB,
    "invalid":   0xCCCCCCCC,
    "updating":  0xDDDDDDDD,
    "trial":     0xEEEEEEEE,
    "erased":    0xFFFFFFFF,
    "zero":      0x00000000,
}
STATUS_NAME = {v: k for k, v in STATUS.items()}


def status_val(s):
    if s in STATUS:
        return STATUS[s]
    return int(s, 0)   # hex/dec 직접 지정 허용


def crc32(data: bytes) -> int:
    """ota_meta.c crc32_compute와 동일(zlib/ISO-HDLC). check: crc32('123456789')=0xCBF43926."""
    return zlib.crc32(data) & 0xFFFFFFFF


def build(fields: dict) -> bytes:
    body = struct.pack(
        "<10I",
        fields["magic"], fields["seq"], fields["active"],
        fields["a_status"], fields["b_status"],
        fields["a_version"], fields["b_version"],
        fields["boot_count"], fields["a_size"], fields["b_size"],
    )
    return body + struct.pack("<I", crc32(body))


PRESETS = {
    # 정상 CONFIRMED Slot A (a_size는 실제 이미지 크기로 덮어쓸 것)
    "confirmed-a": dict(active=0, a_status="confirmed", b_status="invalid",
                        a_version=2, b_version=1, a_size=0x8000, b_size=0),
    "confirmed-b": dict(active=1, a_status="invalid", b_status="confirmed",
                        a_version=1, b_version=2, a_size=0, b_size=0x8000),
    # TC-03 공격: Slot A는 CONFIRMED인데 size=0 → ECDSA 우회 시도(부트로더가 거부해야 함)
    "size0-attack": dict(active=0, a_status="confirmed", b_status="invalid",
                         a_version=2, b_version=1, a_size=0, b_size=0),
}


def main():
    ap = argparse.ArgumentParser(description="Boot Metadata 위조/생성 (HIL 픽스처)")
    ap.add_argument("--preset", choices=PRESETS.keys(), help="기본 시나리오")
    ap.add_argument("--out", default="meta.bin", help="출력 .bin (기본 meta.bin)")
    ap.add_argument("--magic", type=lambda x: int(x, 0), default=MAGIC)
    ap.add_argument("--seq", type=lambda x: int(x, 0), default=0xFFFFFFF0,
                    help="seq_counter (높을수록 선택됨; 기본 0xFFFFFFF0)")
    ap.add_argument("--active", type=lambda x: int(x, 0))
    ap.add_argument("--a-status"); ap.add_argument("--b-status")
    ap.add_argument("--a-version", type=lambda x: int(x, 0))
    ap.add_argument("--b-version", type=lambda x: int(x, 0))
    ap.add_argument("--boot-count", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--a-size", type=lambda x: int(x, 0))
    ap.add_argument("--b-size", type=lambda x: int(x, 0))
    args = ap.parse_args()

    # CRC 구현 자가검증(C check value와 일치 확인)
    assert crc32(b"123456789") == 0xCBF43926, "zlib CRC가 C 구현과 불일치!"

    base = dict(active=0, a_status="invalid", b_status="invalid",
                a_version=1, b_version=1, a_size=0, b_size=0)
    if args.preset:
        base.update(PRESETS[args.preset])

    f = dict(
        magic=args.magic, seq=args.seq, boot_count=args.boot_count,
        active=args.active if args.active is not None else base["active"],
        a_status=status_val(args.a_status or base["a_status"]),
        b_status=status_val(args.b_status or base["b_status"]),
        a_version=args.a_version if args.a_version is not None else base["a_version"],
        b_version=args.b_version if args.b_version is not None else base["b_version"],
        a_size=args.a_size if args.a_size is not None else base["a_size"],
        b_size=args.b_size if args.b_size is not None else base["b_size"],
    )

    blob = build(f)
    with open(args.out, "wb") as fp:
        fp.write(blob)

    def stat(v):
        return STATUS_NAME.get(v, f"0x{v:08X}")
    print(f"[forge_meta] {args.out}  ({len(blob)} bytes)")
    print(f"  magic=0x{f['magic']:08X}  seq={f['seq']}  active={f['active']}  boot_count={f['boot_count']}")
    print(f"  slot_a: status={stat(f['a_status'])} version={f['a_version']} size=0x{f['a_size']:X}")
    print(f"  slot_b: status={stat(f['b_status'])} version={f['b_version']} size=0x{f['b_size']:X}")
    print(f"  crc32=0x{struct.unpack('<I', blob[-4:])[0]:08X}")
    print(f"  주입: st-flash --reset write {args.out} 0x08008000  (+ 0x0800C000 권장)")


if __name__ == "__main__":
    main()
