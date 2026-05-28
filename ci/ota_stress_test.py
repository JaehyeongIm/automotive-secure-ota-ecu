#!/usr/bin/env python3
"""
OTA 반복 통합 테스트

CAN Bus-Off 자동 복구(AutoBusOff=ENABLE) 패치 검증 및
OTA 타임아웃 발생 임계값 탐색을 위한 반복 스트레스 테스트.

Usage (자동 빌드+서명, 3회 반복):
    python3 ci/ota_stress_test.py \\
        --channel can0 \\
        --key <ECDSA 비밀키 경로> \\
        --count 3

Usage (기존 펌웨어 파일 직접 지정, 5회, CF 간격 0.003s):
    python3 ci/ota_stress_test.py \\
        --channel can0 \\
        --fw-a artifacts/sensor_slotA_signed.bin \\
        --fw-b artifacts/sensor_slotB_signed.bin \\
        --count 5 --cf-delay 0.003
"""

import argparse
import subprocess
import sys
import time

import can

sys.path.insert(0, 'tools')
from ota_client import ISOTPError, OTAClient, UDSError

HB_CAN_ID         = 0x201   # SensorECU heartbeat
HB_SLOT_IDX       = 1       # heartbeat data[1] = active_slot
REBOOT_WAIT       = 45.0    # NVIC_SystemReset 후 heartbeat 복구 최대 대기(초)
SLOW_THRESH       = 2.0     # 블록 소요 시간 경고 임계값(초)
HB_SILENCE_THRESH = 0.5     # 이 시간 이상 HB 없으면 ECU 리셋 완료로 판단(초)
HB_SILENCE_WAIT   = 3.0     # Phase1 최대 대기(초)


# ──────────────────────────────────────────────────────────────────────────────

class TimedOTAClient(OTAClient):
    """블록별 소요 시간을 측정하는 OTAClient 확장."""

    def transfer_data_timed(self, fw: bytes, max_data: int) -> list:
        """
        TransferData 전송. 블록별 소요 시간을 반환한다.
        반환값: [(block_seq, elapsed_sec), ...]  — SLOW_THRESH 초과 블록만
        """
        total    = len(fw)
        offset   = 0
        bsq      = 1
        n_chunks = (total + max_data - 1) // max_data
        slow     = []

        print(f"    [TD] {n_chunks} blocks × {max_data} bytes", flush=True)

        while offset < total:
            chunk = fw[offset:offset + max_data]
            pad   = (4 - len(chunk) % 4) % 4
            chunk = chunk + bytes([0xFF] * pad)

            t0 = time.monotonic()
            r  = self.request(bytes([0x36, bsq]) + chunk, timeout=5.0)
            elapsed = time.monotonic() - t0

            if r[0] != 0x76 or r[1] != bsq:
                raise UDSError(f"Block {bsq} 응답 오류: {r.hex()}")

            if elapsed > SLOW_THRESH:
                slow.append((bsq, elapsed))
                print(f"\n    [SLOW] block={bsq}  {elapsed:.2f}s !!",
                      flush=True)

            offset += max_data
            pct = min(100, offset * 100 // total)
            print(f"\r    [TD] {pct:3d}%  block={bsq:3d}  {elapsed:.2f}s",
                  end="", flush=True)
            bsq = 0x00 if bsq == 0xFF else bsq + 1

        print()
        return slow


# ──────────────────────────────────────────────────────────────────────────────

def build_firmware(key_path: str) -> tuple:
    """ci/build.sh + sign_firmware.py로 SlotA/B 펌웨어 빌드 및 서명."""
    for slot in ("A", "B"):
        print(f"[BUILD] SensorECU Slot {slot} 빌드 중...", flush=True)
        subprocess.run(["bash", "ci/build.sh", "sensor", slot], check=True)

        raw    = f"artifacts/sensor_slot{slot}.bin"
        signed = f"artifacts/sensor_slot{slot}_signed.bin"
        print(f"[SIGN]  {raw} 서명 중...", flush=True)
        subprocess.run(
            ["python3", "tools/sign_firmware.py", raw, key_path, "--out", signed],
            check=True,
        )

    with open("artifacts/sensor_slotA_signed.bin", "rb") as f:
        fw_a = f.read()
    with open("artifacts/sensor_slotB_signed.bin", "rb") as f:
        fw_b = f.read()
    return fw_a, fw_b


# ──────────────────────────────────────────────────────────────────────────────

def read_slot(bus: can.BusABC, timeout: float = 10.0):
    """0x201 heartbeat에서 active_slot 읽기. 없으면 None."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if msg and msg.arbitration_id == HB_CAN_ID and len(msg.data) > HB_SLOT_IDX:
            return int(msg.data[HB_SLOT_IDX])
    return None


def wait_for_slot(bus: can.BusABC, expected: int, timeout: float = REBOOT_WAIT):
    """재부팅 후 expected slot의 heartbeat 대기.

    Phase 1: heartbeat가 멈출 때까지 대기(ECU 리셋 확인) — 리셋 전 버퍼 frame 무시
    Phase 2: 재부팅 완료 후 expected slot heartbeat 탐지
    """
    print(f"    [WAIT] Slot {expected} heartbeat 대기 (최대 {timeout:.0f}초)...",
          end="", flush=True)
    t_start  = time.monotonic()
    deadline = t_start + timeout

    # Phase 1: heartbeat 소멸 대기 (ECU가 실제로 리셋됐는지 확인)
    last_hb_time     = t_start
    silence_deadline = t_start + HB_SILENCE_WAIT
    while time.monotonic() < min(deadline, silence_deadline):
        msg = bus.recv(timeout=0.2)
        if msg and msg.arbitration_id == HB_CAN_ID:
            last_hb_time = time.monotonic()
        if time.monotonic() - last_hb_time > HB_SILENCE_THRESH:
            break  # heartbeat 중단 → ECU 리셋 중

    # Phase 2: 재부팅 후 target slot heartbeat 대기
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if msg and msg.arbitration_id == HB_CAN_ID and len(msg.data) > HB_SLOT_IDX:
            if int(msg.data[HB_SLOT_IDX]) == expected:
                elapsed = time.monotonic() - t_start
                print(f" OK  ({elapsed:.1f}s)")
                return True
    print(" TIMEOUT")
    return False


def check_can_alive(bus: can.BusABC, timeout: float = 5.0) -> bool:
    """CAN bus가 살아있는지(heartbeat 수신 여부) 확인."""
    slot = read_slot(bus, timeout=timeout)
    return slot is not None


# ──────────────────────────────────────────────────────────────────────────────

def run_iteration(bus, fw_a, fw_b, cf_delay, idx, current_slot):
    """
    OTA 1회 실행.
    반환: (success: bool, error_msg: str | None, slot_after: int | None)
    """
    target = 1 - current_slot
    fw     = fw_b if target == 1 else fw_a
    label  = "B" if target == 1 else "A"

    print(f"\n{'─'*60}")
    print(f"  Iteration {idx}  Slot {current_slot} → Slot {label}"
          f"  ({len(fw)} bytes  cf_delay={cf_delay}s)")
    print(f"{'─'*60}")

    client  = TimedOTAClient(bus, ecu='sensor', cf_delay=cf_delay)
    t_start = time.monotonic()

    # ── UDS 순서 ──
    try:
        t0 = time.monotonic()
        client.session_extended()
        client.security_access()
        print(f"    [SA] Unlocked  ({time.monotonic()-t0:.2f}s)")

        t0 = time.monotonic()
        max_data = client.request_download(len(fw))
        max_data = min(max_data, 256)
        print(f"    [RD] 0x74 수신  ({time.monotonic()-t0:.2f}s)")

        t0   = time.monotonic()
        slow = client.transfer_data_timed(fw, max_data)
        print(f"    [TD] 완료  ({time.monotonic()-t0:.1f}s"
              f"  slow_blocks={len(slow)})")

        t0 = time.monotonic()
        client.transfer_exit()
        print(f"    [TE] 0x77 수신  ({time.monotonic()-t0:.2f}s)")

    except (UDSError, ISOTPError) as e:
        elapsed = time.monotonic() - t_start
        print(f"\n    [FAIL] {e}  (경과 {elapsed:.1f}s)")

        # 실패 후 CAN 생존 여부 확인
        print("    [CHECK] 실패 후 CAN 상태 확인...", end="", flush=True)
        alive = check_can_alive(bus, timeout=5.0)
        print(" heartbeat 있음" if alive else " heartbeat 없음 ← Bus-Off 의심")

        slot_after = read_slot(bus, timeout=3.0) if alive else None
        return False, str(e), slot_after

    # ── 재부팅 후 slot 확인 ──
    ok = wait_for_slot(bus, target)
    elapsed = time.monotonic() - t_start
    if ok:
        print(f"    [PASS]  총 {elapsed:.1f}s")
        return True, None, target
    else:
        print(f"    [FAIL]  재부팅 후 Slot {label} heartbeat 없음")
        slot_after = read_slot(bus, timeout=3.0)
        return False, f"Slot {label} heartbeat timeout", slot_after


# ──────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="OTA 반복 통합 테스트")
    parser.add_argument("--channel",   required=True)
    parser.add_argument("--interface", default="socketcan",
                        choices=["slcan", "socketcan"])
    parser.add_argument("--bitrate",   type=int, default=500_000)
    parser.add_argument("--key",       help="ECDSA 개인키 .pem — 지정 시 자동 빌드+서명")
    parser.add_argument("--fw-a",      help="Slot A 타겟 서명 펌웨어 (--key 미사용 시 필수)")
    parser.add_argument("--fw-b",      help="Slot B 타겟 서명 펌웨어 (--key 미사용 시 필수)")
    parser.add_argument("--count",     type=int, default=3,
                        help="반복 횟수 (기본: 3)")
    parser.add_argument("--cf-delay",  type=float, default=0.005,
                        help="ISO-TP CF 간격(초) (기본: 0.005)")
    args = parser.parse_args()

    if args.key:
        fw_a, fw_b = build_firmware(args.key)
        fw_a_path, fw_b_path = "artifacts/sensor_slotA_signed.bin", "artifacts/sensor_slotB_signed.bin"
    else:
        if not args.fw_a or not args.fw_b:
            parser.error("--key 없이 실행할 경우 --fw-a 와 --fw-b 가 필요합니다.")
        with open(args.fw_a, "rb") as f: fw_a = f.read()
        with open(args.fw_b, "rb") as f: fw_b = f.read()
        fw_a_path, fw_b_path = args.fw_a, args.fw_b

    print(f"[STRESS] OTA 반복 테스트  count={args.count}  cf_delay={args.cf_delay}s")
    print(f"         fw-a: {fw_a_path}  ({len(fw_a):,} bytes)")
    print(f"         fw-b: {fw_b_path}  ({len(fw_b):,} bytes)")

    if args.interface == "socketcan":
        bus = can.interface.Bus(interface="socketcan", channel=args.channel)
    else:
        bus = can.interface.Bus(interface="slcan",
                                channel=args.channel, bitrate=args.bitrate)

    results = []
    try:
        # ── 초기 상태 확인 ──
        print("\n[STRESS] 초기 heartbeat 확인...", end="", flush=True)
        current_slot = read_slot(bus, timeout=15.0)
        if current_slot is None:
            print(" 없음\n[FAIL] CAN 통신 불가 — ECU 상태 확인 필요")
            sys.exit(1)
        print(f" Slot {current_slot}")

        # ── N회 반복 ──
        for i in range(1, args.count + 1):
            ok, err, slot_after = run_iteration(
                bus, fw_a, fw_b, args.cf_delay, i, current_slot
            )
            results.append((i, ok, err))

            if slot_after is not None:
                current_slot = slot_after
            elif ok:
                current_slot = 1 - current_slot

    finally:
        bus.shutdown()

    # ── 최종 요약 ──
    passed = sum(1 for _, ok, _ in results if ok)
    print(f"\n{'═'*60}")
    print(f"  최종 결과  ({args.count}회 실행, cf_delay={args.cf_delay}s)")
    print(f"{'═'*60}")
    for i, ok, err in results:
        tag = "PASS" if ok else "FAIL"
        note = f"  ← {err}" if err else ""
        print(f"  Iter {i:2d}: {tag}{note}")
    print(f"\n  합계: {passed}/{args.count} 통과")
    print(f"{'═'*60}")

    sys.exit(0 if passed == args.count else 1)


if __name__ == "__main__":
    main()
