#!/usr/bin/env python3
"""
DriveECU App 버전 OTA 시연 테스트

--versions 인수로 실행할 버전을 직접 지정합니다.

Usage (v1 단독):
    python3 ci/demo_ota.py --channel can0 --key ota-private-key.pem --versions 1

Usage (v1 → v2 → v3 순서로):
    python3 ci/demo_ota.py --channel can0 --key ota-private-key.pem --versions 1 2 3

Usage (v2 → v3만):
    python3 ci/demo_ota.py --channel can0 --key ota-private-key.pem --versions 2 3

버전 동작:
  v1: 직진 + 10 cm 장애물 감지 시 즉시 정지
  v2: 10–30 cm 비례 감속 + 10 cm 정지
  v3: v2 감속 + 정지 후 자동 후진 복귀
"""

import argparse
import re
import subprocess
import sys
import time

import can

sys.path.insert(0, 'tools')
from ota_client import ISOTPError, OTAClient, UDSError

DRIVE_HB_ID      = 0x100
HB_VER_IDX       = 0   # data[0] = APP_VERSION
HB_SLOT_IDX      = 1   # data[1] = active_slot
HB_DRIVE_IDX     = 2   # data[2] = driving state (0=idle)

DRIVE_H          = "DriveECU/Core/Inc/drive.h"
VERSION_DESC     = {
    1: "직진 + 10 cm 장애물 감지 시 즉시 정지",
    2: "10–30 cm 비례 감속 + 10 cm 정지",
    3: "v2 감속 + 정지 후 자동 후진 복귀",
}

REBOOT_WAIT      = 45.0
HB_SILENCE_THRESH = 0.5
HB_SILENCE_WAIT  = 3.0


# ──────────────────────────────────────────────────────────────────────────────
# 빌드
# ──────────────────────────────────────────────────────────────────────────────

def build_versions(versions: list, key_path: str) -> dict:
    """지정 버전 목록의 SlotA+B 빌드+서명. {version: (fw_a, fw_b)} 반환."""
    with open(DRIVE_H) as f:
        original = f.read()

    fw = {}
    try:
        for ver in versions:
            _patch_version(original, ver)
            fw[ver] = _build_drive(ver, key_path)
    finally:
        with open(DRIVE_H, 'w') as f:
            f.write(original)

    return fw


def _patch_version(original: str, version: int) -> None:
    patched = re.sub(r'#define\s+APP_VERSION\s+\d+',
                     f'#define APP_VERSION {version}', original)
    with open(DRIVE_H, 'w') as f:
        f.write(patched)


def _build_drive(version: int, key_path: str) -> tuple:
    for slot in ("A", "B"):
        print(f"  [BUILD] v{version} Slot {slot} ...", end="", flush=True)
        subprocess.run(["bash", "ci/build.sh", "drive", slot], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        raw    = f"artifacts/drive_slot{slot}.bin"
        signed = f"artifacts/drive_v{version}_slot{slot}_signed.bin"
        subprocess.run(
            ["python3", "tools/sign_firmware.py", raw, key_path, "--out", signed],
            check=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        print(" done")
    return (
        _read(f"artifacts/drive_v{version}_slotA_signed.bin"),
        _read(f"artifacts/drive_v{version}_slotB_signed.bin"),
    )


def _read(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


# ──────────────────────────────────────────────────────────────────────────────
# CAN 헬퍼
# ──────────────────────────────────────────────────────────────────────────────

def read_drive_hb(bus: can.BusABC, timeout: float = 10.0):
    """0x100 heartbeat에서 (version, slot) 반환. 없으면 (None, None)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if msg and msg.arbitration_id == DRIVE_HB_ID and len(msg.data) >= 2:
            return int(msg.data[HB_VER_IDX]), int(msg.data[HB_SLOT_IDX])
    return None, None


def wait_for_idle(bus: can.BusABC, timeout: float = 60.0) -> bool:
    """DriveECU data[2]==0(IDLE) 대기."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if (msg and msg.arbitration_id == DRIVE_HB_ID
                and len(msg.data) > HB_DRIVE_IDX
                and msg.data[HB_DRIVE_IDX] == 0):
            return True
    return False


def wait_for_version(bus: can.BusABC, expected_ver: int, expected_slot: int,
                     timeout: float = REBOOT_WAIT) -> bool:
    """재부팅 후 예상 버전+슬롯 heartbeat 대기 (2-phase)."""
    slot_lbl = 'B' if expected_slot == 1 else 'A'
    print(f"    [WAIT] v{expected_ver} Slot{slot_lbl}  "
          f"(최대 {timeout:.0f}초)...", end="", flush=True)
    t_start  = time.monotonic()
    deadline = t_start + timeout

    # Phase 1: heartbeat 소멸 (ECU 리셋 확인)
    last_hb    = t_start
    silence_dl = t_start + HB_SILENCE_WAIT
    while time.monotonic() < min(deadline, silence_dl):
        msg = bus.recv(timeout=0.2)
        if msg and msg.arbitration_id == DRIVE_HB_ID:
            last_hb = time.monotonic()
        if time.monotonic() - last_hb > HB_SILENCE_THRESH:
            break

    # Phase 2: 목표 버전+슬롯 확인
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=1.0)
        if (msg and msg.arbitration_id == DRIVE_HB_ID
                and len(msg.data) >= 2
                and int(msg.data[HB_VER_IDX])  == expected_ver
                and int(msg.data[HB_SLOT_IDX]) == expected_slot):
            print(f" OK ({time.monotonic()-t_start:.1f}s)")
            return True

    print(" TIMEOUT")
    return False


# ──────────────────────────────────────────────────────────────────────────────
# OTA
# ──────────────────────────────────────────────────────────────────────────────

def run_ota(bus: can.BusABC, fw_a: bytes, fw_b: bytes,
            cf_delay: float, cur_slot: int, target_ver: int) -> tuple:
    """
    1회 OTA 수행.
    반환: (success: bool, slot_after: int)
    """
    target  = 1 - cur_slot
    fw      = fw_b if target == 1 else fw_a
    cur_lbl = 'B' if cur_slot == 1 else 'A'
    tgt_lbl = 'B' if target   == 1 else 'A'

    print(f"  [OTA] Slot {cur_lbl} → Slot {tgt_lbl}  ({len(fw):,} bytes)")

    client = OTAClient(bus, ecu='drive', cf_delay=cf_delay)
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
        print(f"  [FAIL] {e}")
        return False, cur_slot

    ok = wait_for_version(bus, target_ver, target)
    return ok, (target if ok else cur_slot)


# ──────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="DriveECU 버전 OTA 시연",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="\n".join(f"  v{v}: {d}" for v, d in VERSION_DESC.items()),
    )
    parser.add_argument("--channel",   required=True,
                        help="CAN 채널 (예: can0)")
    parser.add_argument("--interface", default="socketcan",
                        choices=["slcan", "socketcan"])
    parser.add_argument("--bitrate",   type=int, default=500_000)
    parser.add_argument("--key",       required=True,
                        help="ECDSA 개인키 .pem")
    parser.add_argument("--versions",  type=int, nargs="+",
                        choices=[1, 2, 3], required=True,
                        metavar="{1,2,3}",
                        help="OTA할 버전 (예: --versions 1 2 3)")
    parser.add_argument("--cf-delay",  type=float, default=0.005,
                        help="ISO-TP CF 간격(초) (기본: 0.005)")
    args = parser.parse_args()

    print(f"\n{'═'*60}")
    vers_str = " → ".join(f"v{v}" for v in args.versions)
    print(f"  DriveECU OTA 시연  :  {vers_str}")
    print(f"{'═'*60}")

    # ── Phase 1: 빌드 ──────────────────────────────────────────────────────────
    print("\n[Phase 1] 빌드+서명")
    fw = build_versions(args.versions, args.key)

    # ── CAN 초기화 ─────────────────────────────────────────────────────────────
    if args.interface == "socketcan":
        bus = can.interface.Bus(interface="socketcan", channel=args.channel)
    else:
        bus = can.interface.Bus(interface="slcan",
                                channel=args.channel, bitrate=args.bitrate)

    results = []
    try:
        # 초기 상태
        print("\n[Phase 2] 초기 상태 확인...", end="", flush=True)
        cur_ver, cur_slot = read_drive_hb(bus, timeout=15.0)
        if cur_ver is None:
            print(" FAIL — DriveECU heartbeat 없음")
            sys.exit(1)
        print(f" v{cur_ver}  Slot{'B' if cur_slot==1 else 'A'}")

        # 지정 버전 순서로 OTA
        print(f"\n[Phase 3] OTA 시연")
        for target_ver in args.versions:
            print(f"\n{'─'*60}")
            print(f"  → v{target_ver}  :  {VERSION_DESC[target_ver]}")
            print(f"{'─'*60}")

            print("  [DRIVE] IDLE 확인...", end="", flush=True)
            if wait_for_idle(bus, timeout=30.0):
                print(" OK")
            else:
                print(" 타임아웃 (계속 진행)")

            ok, cur_slot = run_ota(bus, fw[target_ver][0], fw[target_ver][1],
                                   args.cf_delay, cur_slot, target_ver)
            if ok:
                cur_ver = target_ver

            tag = "PASS" if ok else "FAIL"
            print(f"\n  결과: {tag}  (현재 v{cur_ver}  "
                  f"Slot{'B' if cur_slot==1 else 'A'})")
            results.append((target_ver, ok))

    finally:
        bus.shutdown()

    # ── 최종 요약 ──────────────────────────────────────────────────────────────
    passed = sum(1 for _, ok in results if ok)
    print(f"\n{'═'*60}")
    print("  시연 결과")
    print(f"{'═'*60}")
    for ver, ok in results:
        tag = "PASS" if ok else "FAIL"
        print(f"  v{ver}  {tag}  |  {VERSION_DESC[ver]}")
    print(f"\n  합계: {passed}/{len(results)} 통과")
    print(f"{'═'*60}")

    sys.exit(0 if passed == len(results) else 1)


if __name__ == "__main__":
    main()
