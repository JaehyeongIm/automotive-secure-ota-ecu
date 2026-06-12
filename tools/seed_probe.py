#!/usr/bin/env python3
"""TC-ATK-005 — SecurityAccess replay/seed-freshness 실보드 검증(SR-ATK-005).

기본:    RequestSeed 1회 → Seed 출력. (재부팅 전후 두 번 돌려 Seed가 달라지면 PASS)
--replay: 캡처 → 리셋 프롬프트 → 재전송 자동 판정.
          캡처한 옛 Key가 다음 부팅에서 NRC 0x35로 거부되면 PASS(replay 차단).

실행(Pi, CAN):
  python3 tools/seed_probe.py --ecu drive  --channel can0
  python3 tools/seed_probe.py --ecu drive  --channel can0 --replay
  python3 tools/seed_probe.py --ecu sensor --channel can0 --replay
"""
import argparse
import hashlib
import hmac
import os
import sys

import can

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ota_client import OTAClient, PSK, UDSError   # noqa: E402


def request_seed(c) -> bytes:
    """ExtendedSession → RequestSeed. 4바이트 Seed 반환."""
    c.request(bytes([0x10, 0x02]))            # DiagnosticSessionControl(Extended)
    r = c.request(bytes([0x27, 0x01]))        # SecurityAccess RequestSeed
    if r[0] != 0x67 or r[1] != 0x01:
        raise SystemExit(f"unexpected RequestSeed resp: {r.hex()}")
    return r[2:6]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ecu", default="drive", choices=["drive", "sensor"])
    ap.add_argument("--channel", default="can0")
    ap.add_argument("--interface", default="socketcan")
    ap.add_argument("--replay", action="store_true",
                    help="캡처→리셋→재전송 자동 판정(TC-ATK-005)")
    args = ap.parse_args()

    bus = can.interface.Bus(interface=args.interface, channel=args.channel)
    try:
        c = OTAClient(bus, ecu=args.ecu)
        c.wait_for_idle(timeout=30)

        if not args.replay:
            print(f"SEED = {request_seed(c).hex()}")
            return

        # 1차 부팅 — (Seed, Key) 캡처 + Unlock 확인
        seed1 = request_seed(c)
        key1  = hmac.new(PSK, seed1, hashlib.sha256).digest()[:4]
        print(f"[1차] Seed1 = {seed1.hex()}   Key1 = {key1.hex()}")
        r = c.request(bytes([0x27, 0x02]) + key1)
        print(f"[1차] Unlock 응답 = {r.hex()}  (0x6702 기대)")

        input("\n>>> ECU를 리셋(전원 재인가)하고, 부팅되면 Enter <<<\n")

        # 2차 부팅 — 새 Seed 확인 + 옛 Key1 재전송(replay)
        seed2 = request_seed(c)
        print(f"[2차] Seed2 = {seed2.hex()}")
        if seed2 == seed1:
            print("✗ FAIL: Seed2 == Seed1 (재현됨 → freshness 미작동, replay 가능)")
            sys.exit(1)
        print("✓ Seed2 ≠ Seed1 (재부팅 후 seed 비반복 — freshness 작동)")

        try:
            r = c.request(bytes([0x27, 0x02]) + key1)   # 캡처한 옛 Key 재전송
            print(f"✗ FAIL: 옛 Key 통과 resp={r.hex()} (replay 성공 = 취약)")
            sys.exit(1)
        except UDSError as e:
            print(f"✓ PASS: 옛 Key 거부 ({e}) — replay 차단됨")
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()
