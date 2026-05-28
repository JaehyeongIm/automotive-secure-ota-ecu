#!/usr/bin/env python3
"""
통합 테스트 실행기: 단위 테스트 + OTA 통합 테스트

Phase 1: ceedling test:all  (C 단위 테스트)
Phase 2: N회 반복 OTA       (DriveECU → SensorECU 순서)

Usage (자동 빌드+서명, 기본 3회):
    python3 ci/test_all.py --channel can0 --key ota-private-key.pem

Usage (기존 펌웨어 직접 지정):
    python3 ci/test_all.py --channel can0 \\
        --fw-drive-a  artifacts/drive_slotA_signed.bin \\
        --fw-drive-b  artifacts/drive_slotB_signed.bin \\
        --fw-sensor-a artifacts/sensor_slotA_signed.bin \\
        --fw-sensor-b artifacts/sensor_slotB_signed.bin

Usage (단위 테스트만):
    python3 ci/test_all.py --skip-ota

Usage (OTA만):
    python3 ci/test_all.py --channel can0 --key ota-private-key.pem --skip-unit
"""

import argparse
import shutil
import subprocess
import sys
import time

import can

sys.path.insert(0, 'tools')
from ota_client import ISOTPError, OTAClient, UDSError

DRIVE_HB_ID      = 0x100
SENSOR_HB_ID     = 0x201
HB_SLOT_IDX      = 1
DRIVE_DRIVE_BYTE = 2    # data[2] == 0 이면 DriveECU IDLE

REBOOT_WAIT      = 45.0
HB_SILENCE_THRESH = 0.5
HB_SILENCE_WAIT  = 3.0


# ──────────────────────────────────────────────────────────────────────────────
# Phase 1 helpers
# ──────────────────────────────────────────────────────────────────────────────

def run_unit_tests() -> str:
    """단위 테스트 실행. 반환: 요약 문자열 (최종 출력용)."""
    print(f"\n{'═'*60}")
    print("  Phase 1: 단위 테스트 (ceedling test:all)")
    print(f"{'═'*60}")
    if shutil.which("ceedling") is None:
        print("[ERROR] ceedling 을 찾을 수 없습니다.")
        print("        1) Ruby 설치:  sudo apt install -y ruby-full")
        print("        2) Ceedling:   gem install ceedling")
        print("        3) PATH 추가:  export PATH=\"$(ruby -e 'puts Gem.user_dir')/bin:$PATH\"")
        sys.exit(1)
    result = subprocess.run(["ceedling", "test:all"],
                            capture_output=True, text=True)

    # OVERALL TEST SUMMARY 섹션 추출
    summary = _parse_ceedling_summary(result.stdout)

    if result.returncode != 0:
        # 실패 시 FAILED TEST SUMMARY 섹션도 출력
        print(_extract_section(result.stdout, "FAILED TEST SUMMARY"))
        print(f"\n[FAIL] 단위 테스트 실패 — OTA 테스트를 진행하지 않습니다.")
        print(summary)
        sys.exit(result.returncode)

    print(f"[PASS] 단위 테스트 완료")
    return summary


def _parse_ceedling_summary(output: str) -> str:
    """ceedling 출력에서 OVERALL TEST SUMMARY 한 줄 추출."""
    lines = output.splitlines()
    in_summary = False
    parts = {}
    for line in lines:
        if "OVERALL TEST SUMMARY" in line:
            in_summary = True
            continue
        if in_summary:
            for key in ("TESTED", "PASSED", "FAILED", "IGNORED"):
                if line.strip().startswith(key):
                    parts[key] = line.strip().split()[-1]
            if len(parts) == 4:
                break
    if parts:
        return (f"  TESTED:{parts['TESTED']:>4}  PASSED:{parts['PASSED']:>4}"
                f"  FAILED:{parts['FAILED']:>4}  IGNORED:{parts['IGNORED']:>4}")
    return "  (요약 없음)"


def _extract_section(output: str, header: str) -> str:
    """출력에서 특정 헤더 섹션을 추출."""
    lines = output.splitlines()
    result_lines = []
    in_section = False
    for line in lines:
        if header in line:
            in_section = True
        if in_section:
            result_lines.append(line)
            if in_section and line.strip() == "" and len(result_lines) > 2:
                break
    return "\n".join(result_lines)


# ──────────────────────────────────────────────────────────────────────────────
# Phase 2 helpers
# ──────────────────────────────────────────────────────────────────────────────

def build_firmware(key_path: str) -> tuple:
    """drive+sensor A/B 빌드 및 서명. (fw_da, fw_db, fw_sa, fw_sb) 반환."""
    for ecu in ("drive", "sensor"):
        for slot in ("A", "B"):
            print(f"[BUILD] {ecu} Slot {slot} ...", end="", flush=True)
            subprocess.run(["bash", "ci/build.sh", ecu, slot], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            raw    = f"artifacts/{ecu}_slot{slot}.bin"
            signed = f"artifacts/{ecu}_slot{slot}_signed.bin"
            subprocess.run(
                ["python3", "tools/sign_firmware.py", raw, key_path, "--out", signed],
                check=True,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
            print(" done")
    return (
        _read("artifacts/drive_slotA_signed.bin"),
        _read("artifacts/drive_slotB_signed.bin"),
        _read("artifacts/sensor_slotA_signed.bin"),
        _read("artifacts/sensor_slotB_signed.bin"),
    )


def _read(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def read_slot(bus: can.BusABC, hb_id: int, timeout: float = 10.0):
    """heartbeat data[1] 에서 active_slot 읽기. 없으면 None."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if msg and msg.arbitration_id == hb_id and len(msg.data) > HB_SLOT_IDX:
            return int(msg.data[HB_SLOT_IDX])
    return None


def wait_for_drive_idle(bus: can.BusABC, timeout: float = 60.0) -> bool:
    """DriveECU 0x100 heartbeat에서 data[2]==0(IDLE) 확인."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if (msg and msg.arbitration_id == DRIVE_HB_ID
                and len(msg.data) > DRIVE_DRIVE_BYTE
                and msg.data[DRIVE_DRIVE_BYTE] == 0):
            return True
    return False


def wait_for_slot(bus: can.BusABC, hb_id: int, expected: int,
                  timeout: float = REBOOT_WAIT) -> bool:
    """재부팅 후 expected slot heartbeat 대기 (2-phase)."""
    slot_label = 'B' if expected == 1 else 'A'
    print(f"    [WAIT] HB={hb_id:#05x}  Slot={slot_label}  (최대 {timeout:.0f}초)...",
          end="", flush=True)
    t_start  = time.monotonic()
    deadline = t_start + timeout

    # Phase 1: heartbeat 소멸 대기 (ECU 리셋 시작 확인)
    last_hb      = t_start
    silence_dl   = t_start + HB_SILENCE_WAIT
    while time.monotonic() < min(deadline, silence_dl):
        msg = bus.recv(timeout=0.2)
        if msg and msg.arbitration_id == hb_id:
            last_hb = time.monotonic()
        if time.monotonic() - last_hb > HB_SILENCE_THRESH:
            break

    # Phase 2: 재부팅 완료 후 target slot 확인
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if (msg and msg.arbitration_id == hb_id
                and len(msg.data) > HB_SLOT_IDX
                and int(msg.data[HB_SLOT_IDX]) == expected):
            print(f" OK ({time.monotonic()-t_start:.1f}s)")
            return True

    print(" TIMEOUT")
    return False


def run_ota_one(bus: can.BusABC, ecu_name: str, fw_a: bytes, fw_b: bytes,
                cf_delay: float, current_slot: int) -> tuple:
    """
    ECU 1회 OTA 수행.
    반환: (success: bool, slot_after: int)
    """
    target       = 1 - current_slot
    fw           = fw_b if target == 1 else fw_a
    hb_id        = SENSOR_HB_ID if ecu_name == 'sensor' else DRIVE_HB_ID
    label        = 'B' if target == 1 else 'A'
    current_label = 'B' if current_slot == 1 else 'A'

    print(f"\n    [{ecu_name.upper()}] Slot {current_label} → Slot {label}"
          f"  ({len(fw):,} bytes)")

    client = OTAClient(bus, ecu=ecu_name, cf_delay=cf_delay)
    try:
        t0 = time.monotonic()
        client.session_extended()
        client.security_access()
        print(f"    [SA] Unlocked  ({time.monotonic()-t0:.2f}s)")

        t0 = time.monotonic()
        max_data = min(client.request_download(len(fw)), 256)
        print(f"    [RD] 0x74 수신  ({time.monotonic()-t0:.2f}s)")

        t0 = time.monotonic()
        client.transfer_data(fw, max_data)
        print(f"    [TD] 완료  ({time.monotonic()-t0:.1f}s)")

        t0 = time.monotonic()
        client.transfer_exit()
        print(f"    [TE] 0x77 수신  ({time.monotonic()-t0:.2f}s)")
    except (UDSError, ISOTPError) as e:
        print(f"    [FAIL] {e}")
        return False, current_slot

    ok = wait_for_slot(bus, hb_id, target)
    if ok:
        return True, target
    slot_now = read_slot(bus, hb_id, timeout=3.0)
    return False, slot_now if slot_now is not None else current_slot


def run_round(bus: can.BusABC, idx: int,
              drive_slot: int, sensor_slot: int,
              fw_da: bytes, fw_db: bytes, fw_sa: bytes, fw_sb: bytes,
              cf_delay: float) -> tuple:
    """
    1라운드: DriveECU OTA → SensorECU OTA
    반환: (success: bool, drive_slot_after: int, sensor_slot_after: int)
    """
    print(f"\n{'─'*60}")
    dl = 'B' if drive_slot == 1 else 'A'
    sl = 'B' if sensor_slot == 1 else 'A'
    print(f"  Round {idx}  DriveECU Slot{dl}  SensorECU Slot{sl}")
    print(f"{'─'*60}")

    # DriveECU OTA 전 IDLE 확인
    print("    [DRIVE] IDLE 확인...", end="", flush=True)
    if wait_for_drive_idle(bus, timeout=30.0):
        print(" OK")
    else:
        print(" 타임아웃 — IDLE 없이 OTA 시도")

    ok_d, drive_slot  = run_ota_one(bus, 'drive',  fw_da, fw_db, cf_delay, drive_slot)
    ok_s, sensor_slot = run_ota_one(bus, 'sensor', fw_sa, fw_sb, cf_delay, sensor_slot)

    success = ok_d and ok_s
    tag     = "PASS" if success else "FAIL"
    print(f"\n  Round {idx}: {tag}  "
          f"drive={'OK' if ok_d else 'NG'}  sensor={'OK' if ok_s else 'NG'}")
    return success, drive_slot, sensor_slot


# ──────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="단위 테스트 + OTA 통합 테스트")
    parser.add_argument("--channel",      help="CAN 채널 (예: can0, /dev/tty.usbmodemXXXX)")
    parser.add_argument("--interface",    default="socketcan",
                        choices=["slcan", "socketcan"])
    parser.add_argument("--bitrate",      type=int, default=500_000)
    parser.add_argument("--key",          help="ECDSA 개인키 .pem — 지정 시 자동 빌드+서명")
    parser.add_argument("--fw-drive-a",   help="DriveECU  SlotA 서명 펌웨어")
    parser.add_argument("--fw-drive-b",   help="DriveECU  SlotB 서명 펌웨어")
    parser.add_argument("--fw-sensor-a",  help="SensorECU SlotA 서명 펌웨어")
    parser.add_argument("--fw-sensor-b",  help="SensorECU SlotB 서명 펌웨어")
    parser.add_argument("--count",        type=int, default=3,
                        help="OTA 반복 횟수 (기본: 3)")
    parser.add_argument("--cf-delay",     type=float, default=0.005,
                        help="ISO-TP CF 간격(초) (기본: 0.005)")
    parser.add_argument("--skip-unit",    action="store_true",
                        help="단위 테스트 건너뜀")
    parser.add_argument("--skip-ota",     action="store_true",
                        help="OTA 통합 테스트 건너뜀")
    args = parser.parse_args()

    # ── Phase 1: 단위 테스트 ──────────────────────────────────────────────────
    unit_summary = None
    if not args.skip_unit:
        unit_summary = run_unit_tests()

    # ── Phase 2: OTA 통합 테스트 ─────────────────────────────────────────────
    if args.skip_ota:
        print("\n[INFO] OTA 테스트 건너뜀 (--skip-ota)")
        sys.exit(0)

    if not args.channel:
        parser.error("OTA 테스트에는 --channel 이 필요합니다.")

    # 펌웨어 준비
    if args.key:
        fw_da, fw_db, fw_sa, fw_sb = build_firmware(args.key)
        print(f"\n[FW] drive  A={len(fw_da):,}B  B={len(fw_db):,}B")
        print(f"[FW] sensor A={len(fw_sa):,}B  B={len(fw_sb):,}B")
    else:
        needed = [args.fw_drive_a, args.fw_drive_b, args.fw_sensor_a, args.fw_sensor_b]
        if not all(needed):
            parser.error(
                "--key 없이 실행할 경우 --fw-drive-a/b 와 --fw-sensor-a/b 가 모두 필요합니다."
            )
        fw_da = _read(args.fw_drive_a)
        fw_db = _read(args.fw_drive_b)
        fw_sa = _read(args.fw_sensor_a)
        fw_sb = _read(args.fw_sensor_b)

    print(f"\n{'═'*60}")
    print(f"  Phase 2: OTA 통합 테스트  count={args.count}  cf_delay={args.cf_delay}s")
    print(f"{'═'*60}")

    if args.interface == "socketcan":
        bus = can.interface.Bus(interface="socketcan", channel=args.channel)
    else:
        bus = can.interface.Bus(interface="slcan",
                                channel=args.channel, bitrate=args.bitrate)

    results = []
    try:
        # 초기 슬롯 확인
        print("\n[OTA] 초기 heartbeat 확인...", end="", flush=True)
        drive_slot  = read_slot(bus, DRIVE_HB_ID,  timeout=15.0)
        sensor_slot = read_slot(bus, SENSOR_HB_ID, timeout=15.0)
        if drive_slot is None or sensor_slot is None:
            print(f" DriveECU={drive_slot}  SensorECU={sensor_slot}")
            print("[FAIL] CAN 통신 불가 — ECU 상태 확인 필요")
            sys.exit(1)
        print(f" DriveECU=Slot{drive_slot}  SensorECU=Slot{sensor_slot}")

        for i in range(1, args.count + 1):
            ok, drive_slot, sensor_slot = run_round(
                bus, i, drive_slot, sensor_slot,
                fw_da, fw_db, fw_sa, fw_sb, args.cf_delay,
            )
            results.append((i, ok))

    finally:
        bus.shutdown()

    # ── 최종 요약 ──────────────────────────────────────────────────────────────
    passed = sum(1 for _, ok in results if ok)
    print(f"\n{'═'*60}")
    print(f"  최종 결과")
    print(f"{'═'*60}")
    if unit_summary:
        print(f"  [단위 테스트]")
        print(unit_summary)
    print(f"\n  [OTA 통합 테스트]  ({args.count}라운드 × 2 ECU)")
    for i, ok in results:
        print(f"  Round {i:2d}: {'PASS' if ok else 'FAIL'}")
    print(f"\n  OTA 합계: {passed}/{args.count} 라운드 통과")
    print(f"{'═'*60}")

    sys.exit(0 if passed == args.count else 1)


if __name__ == "__main__":
    main()
